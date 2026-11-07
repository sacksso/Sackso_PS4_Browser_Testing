// Prueba en PC de netclient.cpp (sink + puerta de cabeceras) con un sceHttp simulado.
#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include "netclient.h"

void psb_log(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); }

struct Resp { int status; std::string headers; std::string body; };
static std::map<std::string, Resp> g_site;
struct Req { std::string url; size_t pos = 0; std::string acceptEnc; };
static std::map<int, Req> g_reqs;
static int g_next = 10;
static std::string g_hdrBuf;

extern "C" {
int sceSysmoduleLoadModuleInternal(uint32_t) { return 0; }
int sceSysmoduleLoadModule(uint16_t) { return 0; }
int sceNetInit() { return 0; }
int sceNetPoolCreate(const char*, int, int) { return 1; }
int sceNetPoolDestroy(int) { return 0; }
int sceSslInit(size_t) { return 2; }
void sceSslTerm() {}
int sceHttpInit(int, int, int) { return 3; }
int sceHttpTerm(int) { return 0; }
int sceHttpCreateTemplate(int, const char*, int, int) { return 20; }
int sceHttpSetResolveTimeOut(int, uint32_t) { return 0; }
int sceHttpSetConnectTimeOut(int, uint32_t) { return 0; }
void sceHttpSetResponseHeaderMaxSize() {}
int sceHttpsSetSslCallback(int, int (*)(int, unsigned, void* const*, int, void*), void*) { return 0; }
int sceHttpCreateConnectionWithURL(int, const char*, int) { return 30; }
int32_t sceHttpCreateRequestWithURL(int32_t, int32_t, const char* url, uint64_t) { int id = g_next++; g_reqs[id].url = url; return id; }
int sceHttpAddRequestHeader(int r, const char* k, const char* v, uint32_t) { if (!strcmp(k, "Accept-Encoding")) g_reqs[r].acceptEnc = v; return 0; }
int32_t sceHttpSendRequest(int32_t r, const void*, size_t) { return g_site.count(g_reqs[r].url) ? 0 : -1; }
int sceHttpGetStatusCode(int r, int32_t* s) { *s = g_site[g_reqs[r].url].status; return 0; }
int sceHttpGetAllResponseHeaders(int r, char** h, size_t* n) { g_hdrBuf = g_site[g_reqs[r].url].headers; *h = &g_hdrBuf[0]; *n = g_hdrBuf.size(); return 0; }
int sceHttpGetResponseContentLength(int r, int32_t* t, size_t* l) {
    const Resp& R = g_site[g_reqs[r].url];
    if (R.headers.find("Content-Length:") == std::string::npos) { *t = 1; return 0; }
    *t = ORBIS_HTTP_CONTENTLEN_EXIST; *l = R.body.size(); return 0; }
int sceHttpReadData(int r, void* buf, uint32_t n) {
    Req& q = g_reqs[r]; const Resp& R = g_site[q.url];
    size_t left = R.body.size() - q.pos; size_t k = left < n ? left : n; if (k > 7000) k = 7000;
    memcpy(buf, R.body.data() + q.pos, k); q.pos += k; return static_cast<int>(k); }
int sceHttpDeleteRequest(int) { return 0; }
int sceHttpDeleteConnection(int) { return 0; }
int sceHttpDeleteTemplate(int) { return 0; }
}

#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); return 1; } } while (0)

struct MemSink : HttpSink {
    std::string data; int chunks = 0; size_t cancelAfter = 0; bool began = false; std::string cd, ct; long long cl = -2;
    bool begin(const HttpResult& r) override { began = true; cd = r.contentDisposition; ct = r.contentType; cl = r.contentLength; return true; }
    bool write(const void* d, size_t n) override { data.append(static_cast<const char*>(d), n); chunks++; return !(cancelAfter && data.size() >= cancelAfter); }
};
static bool gateZip(const HttpResult& r) { return r.contentType.find("html") != std::string::npos || r.contentType.empty(); }

int main() {
    CHECK(net_init());
    std::string big; for (int i = 0; i < 300000; i++) big += static_cast<char>('a' + i % 26);
    g_site["https://a/file.zip"] = {200, "HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Length: 300000\r\nContent-Disposition: attachment; filename=\"x y.zip\"\r\n\r\n", big};
    g_site["https://a/page"] = {200, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 11\r\n\r\n", "<p>hola</p>\n"};
    g_site["https://a/redir"] = {302, "HTTP/1.1 302 Found\r\nLocation: /file.zip\r\nContent-Length: 0\r\n\r\n", ""};
    g_site["https://a/chunked"] = {200, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n", big.substr(0, 100000)};
    g_site["https://a/404"] = {404, "HTTP/1.1 404 Not Found\r\nContent-Type: application/zip\r\n\r\n", "no"};

    { // 1) puerta: un zip no se lee
        HttpResult r; CHECK(http_request("https://a/file.zip", nullptr, r, 2 << 20, 5, nullptr, gateZip));
        CHECK(r.stopped); CHECK(r.body.empty()); CHECK(r.contentType == "application/zip"); CHECK(r.contentLength == 300000);
        CHECK(r.contentDisposition.find("x y.zip") != std::string::npos); CHECK(r.status == 200);
        CHECK(g_reqs[g_next - 1].acceptEnc == "gzip, deflate"); }
    { // 2) puerta: html se lee entero (ruta normal, sin sink)
        HttpResult r; CHECK(http_request("https://a/page", nullptr, r, 2 << 20, 5, nullptr, gateZip));
        CHECK(!r.stopped); CHECK(r.body == "<p>hola</p>\n"); }
    { // 3) sink: 300000 bytes en varios trozos, sin acumular en body, sin compresion
        HttpResult r; MemSink s; CHECK(http_request("https://a/file.zip", nullptr, r, 0, 5, &s));
        CHECK(s.began); CHECK(s.data == big); CHECK(s.chunks > 40); CHECK(r.body.empty()); CHECK(r.received == 300000);
        CHECK(s.cl == 300000); CHECK(s.cd.find("attachment") != std::string::npos); CHECK(g_reqs[g_next - 1].acceptEnc == "identity"); }
    { // 4) redireccion + sink: begin solo con la respuesta final
        HttpResult r; MemSink s; CHECK(http_request("https://a/redir", nullptr, r, 0, 5, &s));
        CHECK(s.data == big); CHECK(r.finalUrl == "https://a/file.zip"); CHECK(r.redirects == 1); }
    { // 5) sin Content-Length
        HttpResult r; MemSink s; CHECK(http_request("https://a/chunked", nullptr, r, 0, 5, &s));
        CHECK(s.data.size() == 100000); CHECK(s.cl == -1); }
    { // 6) cancelar desde el sink
        HttpResult r; MemSink s; s.cancelAfter = 50000;
        CHECK(!http_request("https://a/file.zip", nullptr, r, 0, 5, &s)); CHECK(r.stage == "cancelado"); CHECK(s.data.size() >= 50000 && s.data.size() < 300000); }
    { // 7) la puerta no frena errores HTTP si el gate los deja pasar; con sink el begin ve el 404
        HttpResult r; MemSink s; CHECK(http_request("https://a/404", nullptr, r, 0, 5, &s)); CHECK(r.status == 404); }
    { // 8) pagina grande sin sink sigue respetando maxBody
        HttpResult r; CHECK(http_request("https://a/file.zip", nullptr, r, 1000, 5)); CHECK(r.truncated); CHECK(r.body.size() == 1000); }
    puts("netclient: todo ok"); return 0;
}
