#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "url.h"
#include "log.h"
void psb_log(const char*, ...) {}

#include "scan_extract.inc"  // lo genera run_scanvideos.sh a partir de main.cpp
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static std::vector<VidItem> scan(const std::string& h, const std::string& base = "https://site.com/page/x.html") { std::vector<VidItem> v; scanVideos(h, base, v); return v; }
int main() {
    auto v = scan("<video src=\"/media/a.mp4\"></video>");
    CHECK(v.size() == 1 && v[0].url == "https://site.com/media/a.mp4" && v[0].note.empty());
    v = scan("{\"file\":\"https:\\/\\/cdn.x.com\\/v\\/abc.mp4?token=1\\u0026e=2\"}");
    CHECK(v.size() == 1 && v[0].url == "https://cdn.x.com/v/abc.mp4?token=1&e=2");
    v = scan("<a href=\"https://c.com/v.mp4?a=1&amp;b=2\">x</a>");
    CHECK(v.size() == 1 && v[0].url == "https://c.com/v.mp4?a=1&b=2");
    v = scan("<a href=\"https://c.com/audio.mp4a\">x</a> <a href=\"https://c.com/f.mp4s\">y</a>");
    CHECK(v.empty());
    v = scan("<source src='h.m3u8'> <source src=\"x/y.webm\">");
    CHECK(v.size() == 2);
    bool hls = false, webm = false;
    for (auto& i : v) { if (i.url.find(".m3u8") != std::string::npos) hls = i.note.find("HLS") != std::string::npos; if (i.url.find(".webm") != std::string::npos) webm = i.note.find("WebM") != std::string::npos; }
    CHECK(hls); CHECK(webm);
    v = scan("<a href=\"/a.mp4\"></a><a href=\"/a.mp4\"></a><b data=\"/a.mp4\"></b>");
    CHECK(v.size() == 1);
    v = scan("body{background:url(/img/v.mp4)} x");
    CHECK(v.size() == 1 && v[0].url == "https://site.com/img/v.mp4");
    v = scan("<video src=\"./rel/clip.MP4\">");   // mayusculas
    CHECK(v.size() == 1);
    v = scan("<a href=\"javascript:void(0)\">.mp4</a>");   // texto suelto
    CHECK(v.empty());
    v = scan("<p>archivo.mp4</p>");    // sin barra: no es una URL
    CHECK(v.empty());
    // clasificacion: vistas previas al final y avisadas; calidad mayor primero (caso real: td.mp4 de 10 s en un listado)
    v = scan("<a href=\"https://tbv.sb-cd.com/t/1/320x180/td.mp4\"></a><a href=\"https://cdn.x.com/a-480p.mp4\"></a><a href=\"https://cdn.x.com/a-1080p.mp4\"></a>");
    CHECK(v.size() == 3);
    CHECK(v.size() == 3 && v[0].url.find("1080p") != std::string::npos && v[1].url.find("480p") != std::string::npos && v[2].preview);
    CHECK(v.size() == 3 && !v[0].preview && v[2].note.find("VISTA PREVIA") != std::string::npos);
    v = scan("<a href=\"/x/preview/a.mp4\"></a><a href=\"/x/trailer.mp4\"></a>");
    CHECK(v.size() == 2 && v[0].preview && v[1].preview);
    v = scan("<a href=\"/a.m3u8\"></a><a href=\"/b.mp4\"></a>");   // HLS/WebM puntuan menos que un MP4 normal
    CHECK(v.size() == 2 && v[0].url.find("b.mp4") != std::string::npos);
    // limite de 60 y rendimiento en una pagina de 2 MB
    std::string many; for (int i = 0; i < 200; i++) many += "<a href=\"/v" + std::to_string(i) + ".mp4\"></a>";
    v = scan(many); CHECK(v.size() == 60);
    std::string big; while (big.size() < 2 * 1024 * 1024) big += "<div class=\"c\">Lorem ipsum dolor sit amet, <a href=\"/x\">enlace</a></div>\n";
    auto t0 = std::chrono::steady_clock::now(); v = scan(big); auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(v.empty()); printf("pagina de 2 MB sin videos: %lld ms\n", (long long)ms); CHECK(ms < 300);
    // JSON enorme con una sola cadena larga sin delimitadores (no debe colgarse ni leer fuera)
    std::string longc(5000, 'a'); v = scan("\"" + longc + ".mp4\""); CHECK(v.size() <= 1);
    // HTML truncado en el borde
    v = scan("<a href=\"/a.mp4"); CHECK(v.size() == 1);
    v = scan(".mp4"); CHECK(v.empty());
    printf(fails ? "HAY FALLOS: %d\n" : "scanVideos: todas las pruebas OK\n", fails);
    return fails ? 1 : 0;
}
