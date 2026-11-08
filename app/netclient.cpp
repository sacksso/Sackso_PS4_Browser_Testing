#include "netclient.h"
#include "inflate.h"

#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "log.h"

std::string g_netUserAgent = "Mozilla/5.0 (PlayStation 4; psbrowser/0.2)";

// Tamanos de pools habituales en homebrew de PS4.
static const int NET_POOL_SIZE = 16 * 1024;
static const size_t SSL_POOL_SIZE = 300 * 1024;
static const size_t HTTP_POOL_SIZE = 1024 * 1024;
static const size_t kHeaderMax = 128 * 1024;  // limite de cabeceras de respuesta que pedimos a libSceHttp

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

// compat=true: conexion sin keep-alive y con cabeceras Accept / Accept-Encoding explicitas (reintento tras un fallo en sendRequest).
static bool singleGet(const std::string& url, const HttpPost* post, HttpResult& out, size_t maxBody, std::string& redirectTo, bool compat,
                      HttpSink* sink, HttpGate gate, const HttpExtra* extra) {
    redirectTo.clear();
    out.stage.clear();
    out.contentDisposition.clear();
    out.contentLength = -1;
    out.rangeTotal = -1;
    out.stopped = false;
    out.sceError = 0;
    int tpl = 0, conn = 0, req = 0;
    bool ok = false;
    std::string encoding;  // Content-Encoding de la respuesta
    auto fail = [&](const char* stage, int code) {
        out.stage = stage;
        out.sceError = code;
        psb_log("[http] %s fallo 0x%08X (%s)", stage, static_cast<unsigned>(code), url.c_str());
    };

    tpl = sceHttpCreateTemplate(g_httpCtxId, g_netUserAgent.c_str(), ORBIS_HTTP_VERSION_1_1, 1);
    if (tpl < 0) { fail("createTemplate", tpl); tpl = 0; goto done; }
    sceHttpSetResolveTimeOut(tpl, 10 * 1000 * 1000);
    sceHttpSetConnectTimeOut(tpl, 10 * 1000 * 1000);
    sceHttpsSetSslCallback(tpl, sslCallback, nullptr);
    // Hipotesis de la prueba 2: las paginas /wiki/ de Wikipedia mandan cabeceras de respuesta grandes y libSceHttp tiene un
    // limite por defecto pequeno (www.wikipedia.org y action=raw, con menos cabeceras, si funcionan). La cabecera de OpenOrbis
    // declara esta funcion sin parametros; la firma real es int sceHttpSetResponseHeaderMaxSize(int id, size_t size).
    {
        typedef int (*SetHdrMaxFn)(int, size_t);
        int rc = reinterpret_cast<SetHdrMaxFn>(sceHttpSetResponseHeaderMaxSize)(tpl, kHeaderMax);
        static bool logged = false;
        if (!logged) { psb_log("[http] sceHttpSetResponseHeaderMaxSize(%zu) = 0x%08X", kHeaderMax, static_cast<unsigned>(rc)); logged = true; }
    }

    conn = sceHttpCreateConnectionWithURL(tpl, url.c_str(), !compat);
    if (conn < 0) { fail("createConnection", conn); conn = 0; goto done; }
    // POST = 1 en el SDK de Sony (SCE_HTTP_METHOD_POST); el cast sirve tanto si ORBIS_METHOD_GET es un entero como un enum.
    req = sceHttpCreateRequestWithURL(conn, post ? static_cast<decltype(ORBIS_METHOD_GET)>(1) : ORBIS_METHOD_GET, url.c_str(),
                                      post ? static_cast<uint64_t>(post->body.size()) : 0);
    if (req < 0) { fail("createRequest", req); req = 0; goto done; }

    if (compat) {
        sceHttpAddRequestHeader(req, "Accept", "text/html,*/*;q=0.8", 0);
        sceHttpAddRequestHeader(req, "Accept-Encoding", "identity", 0);
    } else if (sink || (extra && extra->rangeStart >= 0)) {  // descarga/lectura por rangos: tal cual, sin recompresion
        sceHttpAddRequestHeader(req, "Accept", "*/*", 0);
        sceHttpAddRequestHeader(req, "Accept-Language", "es-ES,es;q=0.9,en;q=0.7", 0);
        sceHttpAddRequestHeader(req, "Accept-Encoding", "identity", 0);
    } else {
        sceHttpAddRequestHeader(req, "Accept", "text/html,application/xhtml+xml,image/png,image/jpeg,image/*;q=0.8,*/*;q=0.5", 0);
        sceHttpAddRequestHeader(req, "Accept-Language", "es-ES,es;q=0.9,en;q=0.7", 0);
        sceHttpAddRequestHeader(req, "Accept-Encoding", "gzip, deflate", 0);
    }
    if (post) sceHttpAddRequestHeader(req, "Content-Type", post->contentType.c_str(), 0);
    if (extra) {
        if (extra->rangeStart >= 0) {
            char rg[80];
            if (extra->rangeEnd >= extra->rangeStart) snprintf(rg, sizeof(rg), "bytes=%lld-%lld", extra->rangeStart, extra->rangeEnd);
            else snprintf(rg, sizeof(rg), "bytes=%lld-", extra->rangeStart);
            sceHttpAddRequestHeader(req, "Range", rg, 0);
        }
        if (!extra->referer.empty()) sceHttpAddRequestHeader(req, "Referer", extra->referer.c_str(), 0);
    }
    {
        int rc = post ? sceHttpSendRequest(req, post->body.data(), post->body.size()) : sceHttpSendRequest(req, nullptr, 0);
        if (rc < 0) { fail("sendRequest", rc); goto done; }
        int32_t status = 0;
        rc = sceHttpGetStatusCode(req, &status);
        if (rc < 0) { fail("statusCode", rc); goto done; }
        out.status = status;

        char* hdrs = nullptr;
        size_t hdrLen = 0;
        if (sceHttpGetAllResponseHeaders(req, &hdrs, &hdrLen) >= 0 && hdrs) {
            out.contentType = headerValue(hdrs, hdrLen, "Content-Type");
            encoding = headerValue(hdrs, hdrLen, "Content-Encoding");
            out.contentDisposition = headerValue(hdrs, hdrLen, "Content-Disposition");
            {   // Content-Range: bytes 0-1048575/33496987
                const std::string cr = headerValue(hdrs, hdrLen, "Content-Range");
                const size_t sl = cr.rfind('/');
                if (sl != std::string::npos && sl + 1 < cr.size() && cr[sl + 1] != '*') out.rangeTotal = atoll(cr.c_str() + sl + 1);
            }
            // Registro de las cabeceras (hasta ~1500 bytes, en trozos) para diagnosticar.
            std::string h(hdrs, hdrLen > 1500 ? 1500 : hdrLen);
            for (auto& c : h) if (c == '\r') c = ' '; else if (c == '\n') c = '|';
            psb_log("[http] cabeceras (%zu bytes en total):", hdrLen);
            for (size_t o = 0; o < h.size(); o += 480) psb_log("[hdr] %s", h.substr(o, 480).c_str());
            if (status >= 300 && status < 400) redirectTo = headerValue(hdrs, hdrLen, "Location");
        }
        int32_t lenType = 0;
        size_t clen = 0;
        if (sceHttpGetResponseContentLength(req, &lenType, &clen) >= 0 && lenType == ORBIS_HTTP_CONTENTLEN_EXIST)
            out.contentLength = static_cast<long long>(clen);

        if (!redirectTo.empty()) { ok = true; goto done; }

        out.body.clear();
        out.received = 0;
        if (gate && !gate(out)) { out.stopped = true; ok = true; goto done; }  // solo cabeceras
        if (sink && !sink->begin(out)) { out.stage = "cancelado"; out.sceError = -2; goto done; }
        std::vector<uint8_t> buf(sink ? 256 * 1024 : 32 * 1024);  // descargas: trozos grandes = menos llamadas a sceHttpReadData
        for (;;) {
            int n = sceHttpReadData(req, buf.data(), static_cast<uint32_t>(buf.size()));
            if (n < 0) { fail("readData", n); goto done; }
            if (n == 0) break;
            if (sink) {
                out.received += n;
                if (!sink->write(buf.data(), static_cast<size_t>(n))) { out.stage = "cancelado"; out.sceError = -2; goto done; }
                continue;
            }
            if (out.body.size() + n > maxBody) {
                out.body.append(reinterpret_cast<char*>(buf.data()), maxBody - out.body.size());
                out.truncated = true;
                break;
            }
            out.body.append(reinterpret_cast<char*>(buf.data()), n);
        }
        // Cuerpo comprimido: se descomprime aqui. Si el sistema ya lo descomprimio (sin la marca gzip), se deja tal cual.
        if (!sink && !encoding.empty() && out.body.size() > 2) {
            const bool gz = static_cast<unsigned char>(out.body[0]) == 0x1f && static_cast<unsigned char>(out.body[1]) == 0x8b;
            std::string e;
            for (char c : encoding) e += static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            const bool isGz = e.find("gzip") != std::string::npos, isDef = e.find("deflate") != std::string::npos;
            if ((isGz && gz) || (isDef && !isGz)) {
                std::string dec;
                const int rc = content_decode(isGz ? "gzip" : "deflate", out.body, dec, maxBody);
                psb_log("[http] Content-Encoding=%s: %zu -> %zu bytes (rc=%d)", e.c_str(), out.body.size(), dec.size(), rc);
                if (rc == 1) { fail("decode", -1); goto done; }
                if (rc == 2) out.truncated = true;
                if (rc == 0 || rc == 2) out.body.swap(dec);
            } else {
                psb_log("[http] Content-Encoding=%s pero el cuerpo ya viene sin comprimir", e.c_str());
            }
        }
        ok = true;
    }
done:
    if (req > 0) sceHttpDeleteRequest(req);
    if (conn > 0) sceHttpDeleteConnection(conn);
    if (tpl > 0) sceHttpDeleteTemplate(tpl);
    return ok;
}

bool http_request(const std::string& url, const HttpPost* post, HttpResult& out, size_t maxBody, int maxRedirects, HttpSink* sink,
                  HttpGate gate, const HttpExtra* extra) {
    static std::mutex httpMutex;  // libSceHttp se usa desde el hilo principal y el de imagenes: una peticion a la vez
    std::lock_guard<std::mutex> httpLock(httpMutex);
    out = HttpResult();
    if (!g_ready) { out.stage = "net_init"; return false; }
    std::string cur = url;
    for (int i = 0; i <= maxRedirects; i++) {
        out.finalUrl = cur;
        std::string next;
        psb_log("[http] %s %s", post ? "POST" : "GET", cur.c_str());
        bool good = singleGet(cur, post, out, maxBody, next, false, sink, gate, extra);
        if (!good && (out.stage == "sendRequest" || out.stage == "decode")) {
            psb_log("[http] reintento en modo compat (sin keep-alive, Accept-Encoding: identity)");
            good = singleGet(cur, post, out, maxBody, next, true, sink, gate, extra);
            psb_log("[http] reintento: %s", good ? "OK" : "FALLO tambien");
        }
        if (!good) return false;
        psb_log("[http] -> %d tipo='%s' longitud=%lld cuerpo=%zu bytes%s", out.status, out.contentType.c_str(),
                out.contentLength, sink ? static_cast<size_t>(out.received) : out.body.size(), out.stopped ? " (solo cabeceras)" : "");
        if (next.empty()) return true;
        cur = resolveUrl(cur, next);
        out.redirects++;
        post = nullptr;  // tras una redireccion se sigue con GET (comportamiento habitual de 301/302/303)
    }
    out.stage = "demasiadas redirecciones";
    return false;
}

bool http_get(const std::string& url, HttpResult& out, size_t maxBody, int maxRedirects) {
    return http_request(url, nullptr, out, maxBody, maxRedirects);
}

bool http_range(const std::string& url, long long start, long long len, const std::string& referer, HttpResult& out, int maxRedirects) {
    HttpExtra ex;
    ex.rangeStart = start;
    ex.rangeEnd = start + len - 1;
    ex.referer = referer;
    // maxBody = len: si el servidor ignora Range y manda el archivo entero (200), el cuerpo se corta y 'truncated' avisa.
    if (!http_request(url, nullptr, out, static_cast<size_t>(len), maxRedirects, nullptr, nullptr, &ex)) return false;
    if (out.status == 206) return true;
    if (out.status == 200 && !out.truncated) {  // el archivo entero cabia en el trozo pedido
        out.rangeTotal = static_cast<long long>(out.body.size());
        return start == 0;
    }
    return false;
}
