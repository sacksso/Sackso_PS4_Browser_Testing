#include "netclient.h"

#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "log.h"

// Tamanos de pools habituales en homebrew de PS4.
static const int NET_POOL_SIZE = 16 * 1024;
static const size_t SSL_POOL_SIZE = 300 * 1024;
static const size_t HTTP_POOL_SIZE = 1024 * 1024;

// Por defecto aceptamos cualquier certificado y solo lo registramos (hello de diagnostico).
// Compilar con -DPSB_STRICT_TLS para aceptar unicamente cuando verifyErr == 0.
#ifdef PSB_STRICT_TLS
static const bool kAcceptBadCerts = false;
#else
static const bool kAcceptBadCerts = true;
#endif

static int g_netMemId = 0, g_sslCtxId = 0, g_httpCtxId = 0;
static bool g_ready = false;

static int32_t sslCallback(int32_t libsslId, uint32_t verifyErr, void* const /*sslCert*/[], int32_t certNum, void* /*userArg*/) {
    psb_log("[tls] ctx=%d verifyErr=0x%08X certs=%d -> %s", libsslId, verifyErr, certNum,
            (verifyErr == 0 || kAcceptBadCerts) ? "acepto" : "RECHAZO");
    return (verifyErr == 0 || kAcceptBadCerts) ? 1 : 0;  // 1 = continuar (como en el sample net_http de OpenOrbis)
}

bool net_init() {
    if (g_ready) return true;
    uint32_t r;
    r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    psb_log("[net] load NET = 0x%08X", r);
    r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP);
    psb_log("[net] load HTTP = 0x%08X", r);
    r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL);
    psb_log("[net] load SSL = 0x%08X", r);

    int rc = sceNetInit();
    psb_log("[net] sceNetInit = 0x%08X", rc);
    rc = sceNetPoolCreate("psbrowser", NET_POOL_SIZE, 0);
    if (rc < 0) { psb_log("[net] sceNetPoolCreate fallo 0x%08X", rc); return false; }
    g_netMemId = rc;
    rc = sceSslInit(SSL_POOL_SIZE);
    if (rc < 0) { psb_log("[net] sceSslInit fallo 0x%08X", rc); return false; }
    g_sslCtxId = rc;
    rc = sceHttpInit(g_netMemId, g_sslCtxId, HTTP_POOL_SIZE);
    if (rc < 0) { psb_log("[net] sceHttpInit fallo 0x%08X", rc); return false; }
    g_httpCtxId = rc;
    psb_log("[net] listo: netMem=%d ssl=%d http=%d (TLS %s)", g_netMemId, g_sslCtxId, g_httpCtxId,
            kAcceptBadCerts ? "PERMISIVO" : "estricto");
    g_ready = true;
    return true;
}

void net_shutdown() {
    if (!g_ready) return;
    sceHttpTerm(g_httpCtxId);
    // La cabecera de OpenOrbis declara sceSslTerm() sin parametros; la firma real es int sceSslTerm(int libsslCtxId).
    reinterpret_cast<int (*)(int)>(sceSslTerm)(g_sslCtxId);
    sceNetPoolDestroy(g_netMemId);
    g_ready = false;
}

// --- utilidades ---
static bool ieq_prefix(const char* s, const char* prefix) {
    while (*prefix) {
        if (std::tolower(static_cast<unsigned char>(*s)) != std::tolower(static_cast<unsigned char>(*prefix))) return false;
        ++s; ++prefix;
    }
    return true;
}

// Valor de una cabecera dentro del bloque completo de cabeceras de la respuesta.
static std::string headerValue(const char* hdrs, size_t len, const char* name) {
    std::string n = std::string(name) + ":";
    const char* p = hdrs;
    const char* end = hdrs + len;
    while (p < end) {
        const char* eol = static_cast<const char*>(memchr(p, '\n', end - p));
        const char* lineEnd = eol ? eol : end;
        if (static_cast<size_t>(lineEnd - p) > n.size() && ieq_prefix(p, n.c_str())) {
            const char* v = p + n.size();
            while (v < lineEnd && (*v == ' ' || *v == '\t')) ++v;
            const char* e = lineEnd;
            while (e > v && (e[-1] == '\r' || e[-1] == ' ')) --e;
            return std::string(v, e);
        }
        if (!eol) break;
        p = eol + 1;
    }
    return std::string();
}

// Resuelve 'loc' (cabecera Location) respecto a 'base'.
static std::string resolveUrl(const std::string& base, const std::string& loc) {
    if (loc.compare(0, 7, "http://") == 0 || loc.compare(0, 8, "https://") == 0) return loc;
    size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos) return loc;
    size_t hostEnd = base.find('/', schemeEnd + 3);
    std::string origin = (hostEnd == std::string::npos) ? base : base.substr(0, hostEnd);
    if (loc.compare(0, 2, "//") == 0) return base.substr(0, schemeEnd + 1) + loc;
    if (!loc.empty() && loc[0] == '/') return origin + loc;
    std::string dir = (hostEnd == std::string::npos) ? origin + "/" : base.substr(0, base.rfind('/') + 1);
    return dir + loc;
}

static bool singleGet(const std::string& url, HttpResult& out, size_t maxBody, std::string& redirectTo) {
    redirectTo.clear();
    int tpl = 0, conn = 0, req = 0;
    bool ok = false;
    auto fail = [&](const char* stage, int code) {
        out.stage = stage;
        out.sceError = code;
        psb_log("[http] %s fallo 0x%08X (%s)", stage, static_cast<unsigned>(code), url.c_str());
    };

    tpl = sceHttpCreateTemplate(g_httpCtxId, "Mozilla/5.0 (PlayStation 4; psbrowser/0.1)", ORBIS_HTTP_VERSION_1_1, 1);
    if (tpl < 0) { fail("createTemplate", tpl); tpl = 0; goto done; }
    sceHttpSetResolveTimeOut(tpl, 10 * 1000 * 1000);
    sceHttpSetConnectTimeOut(tpl, 10 * 1000 * 1000);
    sceHttpsSetSslCallback(tpl, sslCallback, nullptr);

    conn = sceHttpCreateConnectionWithURL(tpl, url.c_str(), true);
    if (conn < 0) { fail("createConnection", conn); conn = 0; goto done; }
    req = sceHttpCreateRequestWithURL(conn, ORBIS_METHOD_GET, url.c_str(), 0);
    if (req < 0) { fail("createRequest", req); req = 0; goto done; }

    {
        int rc = sceHttpSendRequest(req, nullptr, 0);
        if (rc < 0) { fail("sendRequest", rc); goto done; }
        int32_t status = 0;
        rc = sceHttpGetStatusCode(req, &status);
        if (rc < 0) { fail("statusCode", rc); goto done; }
        out.status = status;

        char* hdrs = nullptr;
        size_t hdrLen = 0;
        if (sceHttpGetAllResponseHeaders(req, &hdrs, &hdrLen) >= 0 && hdrs) {
            out.contentType = headerValue(hdrs, hdrLen, "Content-Type");
            if (status >= 300 && status < 400) redirectTo = headerValue(hdrs, hdrLen, "Location");
        }
        int32_t lenType = 0;
        size_t clen = 0;
        if (sceHttpGetResponseContentLength(req, &lenType, &clen) >= 0 && lenType == ORBIS_HTTP_CONTENTLEN_EXIST)
            out.contentLength = static_cast<long long>(clen);

        if (!redirectTo.empty()) { ok = true; goto done; }

        out.body.clear();
        std::vector<uint8_t> buf(32 * 1024);
        for (;;) {
            int n = sceHttpReadData(req, buf.data(), static_cast<uint32_t>(buf.size()));
            if (n < 0) { fail("readData", n); goto done; }
            if (n == 0) break;
            if (out.body.size() + n > maxBody) {
                out.body.append(reinterpret_cast<char*>(buf.data()), maxBody - out.body.size());
                out.truncated = true;
                break;
            }
            out.body.append(reinterpret_cast<char*>(buf.data()), n);
        }
        ok = true;
    }
done:
    if (req > 0) sceHttpDeleteRequest(req);
    if (conn > 0) sceHttpDeleteConnection(conn);
    if (tpl > 0) sceHttpDeleteTemplate(tpl);
    return ok;
}

bool http_get(const std::string& url, HttpResult& out, size_t maxBody, int maxRedirects) {
    out = HttpResult();
    if (!g_ready) { out.stage = "net_init"; return false; }
    std::string cur = url;
    for (int i = 0; i <= maxRedirects; i++) {
        out.finalUrl = cur;
        std::string next;
        psb_log("[http] GET %s", cur.c_str());
        if (!singleGet(cur, out, maxBody, next)) return false;
        psb_log("[http] -> %d tipo='%s' longitud=%lld cuerpo=%zu bytes", out.status, out.contentType.c_str(),
                out.contentLength, out.body.size());
        if (next.empty()) return true;
        cur = resolveUrl(cur, next);
        out.redirects++;
    }
    out.stage = "demasiadas redirecciones";
    return false;
}
