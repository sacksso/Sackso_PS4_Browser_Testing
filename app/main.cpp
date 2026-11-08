// psbrowser v0.7.0 - "navegador de escritorio" para PS4: pestanas, barra de herramientas con omnibox, tema claro/oscuro,
// fuente TrueType proporcional, scroll suave en pixeles, imagenes (hilo aparte), formularios GET, anclas y configuracion.
//
// Teclado USB: Ctrl+T pestana nueva, Ctrl+W cerrar, Ctrl+RePag/AvPag o Ctrl+Tab cambiar; F6 o Ctrl+L = barra de direcciones;
//   Tab / Shift+Tab = siguiente/anterior enlace o campo; Enter = activar (en un campo de texto: enviar el formulario);
//   Backspace o Alt+Izq = atras; Alt+Der = adelante; F5 = recargar; F10 = configuracion; Ctrl+D = marcador;
//   Ctrl+J = descargas; Ctrl+'+' / Ctrl+'-' / Ctrl+0 = zoom del texto; flechas, RePag, AvPag, Inicio, Fin, Espacio = desplazar.
//   v0.7.0: Ctrl+C copia el enlace seleccionado o la URL; Ctrl+V pega en la barra/campos (o abre lo copiado si no editas); Ctrl+X, Ctrl+A en la
//   barra; Ctrl+R recargar; Ctrl+Mayus+T reabre pestana; Ctrl+1..9 elige pestana; Alt+Inicio = inicio; tecla Menu o Mayus+F10 = menu contextual.
// Raton USB: clic = enlaces/botones/pestanas; clic central en un enlace = pestana nueva; clic derecho = MENU CONTEXTUAL (abrir, copiar
//   direccion del enlace/imagen, guardar, atras...); Ctrl+clic = pestana nueva; Alt+clic = descargar; rueda = desplazar; botones laterales =
//   atras/adelante; la barra de desplazamiento se arrastra.
// Mando (SDL): cruceta = foco/atras/adelante; boton 0 = activar, 1 = atras, 2 = recargar, 3 = inicio, 4/5 = pagina; stick = scroll;
//   boton 11 (sin funcion hasta v0.6.0) = menu contextual del elemento con foco (cruceta mover, X elegir, O cerrar).
// Todo se registra en /data/psbrowser/log.txt; configuracion en config.txt y marcadores en bookmarks.txt.
#include <SDL2/SDL.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
#include "canvas.h"
#include "charset.h"
#include "htmldoc.h"
#include "imgworker.h"
#include "keyboard.h"  // copiado por el workflow desde samples/keyboard/keyboard/ de OpenOrbis
#include "log.h"
#include "mouse.h"
#include "osk.h"
#include "netclient.h"
#include "pagerender.h"
#include "textengine.h"
#include "theme.h"
#include "url.h"
#include "download.h"
#include "asyncwriter.h"
#include "vplayer.h"
// Lo piden los .h/.cpp de los samples de OpenOrbis (log.h): ellos lo declaran y el programa lo define.
std::stringstream debugLogStream;
static FILE* g_log = nullptr;
void psb_log(const char* fmt, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("[psbrowser] %s\n", buf);
    fflush(stdout);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}
static void mouseLog(const char* line) { psb_log("%s", line); }
static void openLog() {
    mkdir("/data/psbrowser", 0777);
    g_log = fopen("/data/psbrowser/log.txt", "w");
    if (!g_log) g_log = fopen("/data/psbrowser_log.txt", "w");
}
// ------------------------------------------------------------------ geometria
static const int W = 1920, H = 1080;
static const int TABBAR_H = 40, TOOLBAR_Y = 40, TOOLBAR_H = 48, VIEW_Y = 88, SBAR_W = 14;
static const int VIEW_H = H - VIEW_Y;
// Codigos de tecla USB HID (los mismos que usa OpenOrbis en orbis/_types/keyboard.h).
enum {
    K_A = 4, K_N = 17, K_Y = 28, K_H = 11, K_I = 12, K_K = 14, K_Q = 20, K_F8 = 65, K_F9 = 66, K_F = 9, K_F1 = 58, K_F3 = 60, K_C = 6, K_D = 7, K_J = 13, K_L = 15, K_R = 21, K_T = 23, K_V = 25, K_W = 26, K_X = 27, K_1 = 30, K_9 = 38, K_0 = 39, K_RETURN = 40, K_ESC = 41, K_BACKSPACE = 42, K_TAB = 43, K_SPACE = 44,
    K_MINUS = 45, K_EQUAL = 46, K_F5 = 62, K_F6 = 63, K_F10 = 67, K_HOME = 74, K_PGUP = 75, K_END = 77, K_PGDOWN = 78,
    K_RIGHT = 79, K_LEFT = 80, K_DOWN = 81, K_UP = 82, K_APP = 101  // K_APP = tecla de menu contextual
};
// ------------------------------------------------------------------ configuracion y marcadores
struct Config {
    float mouseSens = 3.5f;   // multiplicador del movimiento crudo del raton (la lectura cruda es de +-1..2 por muestra)
    bool mouseAccel = true;
    bool wheelInvert = false;
    int wheelStep = 3;        // muescas -> wheelStep*40 px
    std::string home;         // vacio = about:home
    bool dark = true;
    int zoom = 100;           // tamano del texto en %
    int pageW = 1200;         // ancho maximo del contenido en px
    bool smooth = true;
    bool images = true;
    int vdec = 0;             // decodificador de video: 0 sistema (hardware), 2 software, 3 software2
    bool vhalf = true;        // dibujar el video a media resolucion (mas rapido)
    int vscale = 0;           // tamano del video en la via rapida: 0 ajustar a pantalla, 1 nativo 1:1 (sin escalar), 2 doble (x2 si cabe)
    int search = 0;           // motor de busqueda de la barra (ver url.h)
    int ua = 0;               // User-Agent: 0 PS4, 1 Chrome, 2 Firefox, 3 Smart TV, 4 Lynx (paginas simples)
    bool vpart = false;       // EXPERIMENTAL: actualizar solo el rectangulo del video (SDL_UpdateWindowSurfaceRects) en vez de toda la pantalla
    int volume = 80;          // volumen por defecto del reproductor (0..100)
};
static Config g_cfg;
static std::vector<std::pair<std::string, std::string>> g_bookmarks;  // (url, titulo)
static const char* kCfgPath = "/data/psbrowser/config.txt";
static const char* kBmPath = "/data/psbrowser/bookmarks.txt";
static void clampCfg() {
    g_cfg.mouseSens = std::max(0.5f, std::min(20.0f, g_cfg.mouseSens));
    g_cfg.wheelStep = std::max(1, std::min(10, g_cfg.wheelStep));
    g_cfg.zoom = std::max(80, std::min(200, g_cfg.zoom));
    g_cfg.pageW = std::max(800, std::min(1700, g_cfg.pageW));
    if (g_cfg.vdec != 2 && g_cfg.vdec != 3) g_cfg.vdec = 0;
    g_cfg.volume = std::max(0, std::min(100, g_cfg.volume));
    if (g_cfg.vscale < 0 || g_cfg.vscale > 2) g_cfg.vscale = 0;
    if (g_cfg.search < 0 || g_cfg.search > 4) g_cfg.search = 0;
    if (g_cfg.ua < 0 || g_cfg.ua > 4) g_cfg.ua = 0;
    g_searchEngine = g_cfg.search;
    static const char* kUA[] = {"Mozilla/5.0 (PlayStation 4; psbrowser/0.2)",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:125.0) Gecko/20100101 Firefox/125.0",
        "Mozilla/5.0 (SMART-TV; Linux; Tizen 6.0) AppleWebKit/537.36 (KHTML, like Gecko) Version/6.0 TV Safari/537.36",
        "Lynx/2.9.0 libwww-FM/2.14 SSL-MM/1.4.1 OpenSSL/3.0.0"};
    g_netUserAgent = kUA[g_cfg.ua];
}
static void configSave() {
    FILE* f = fopen(kCfgPath, "w");
    if (!f) return;
    fprintf(f, "mouseSens=%.2f\nmouseAccel=%d\nwheelInvert=%d\nwheelStep=%d\nhome=%s\ndark=%d\nzoom=%d\npageW=%d\nsmooth=%d\nimages=%d\nvdec=%d\nvhalf=%d\nvolume=%d\nvscale=%d\nvpart=%d\nsearch=%d\nua=%d\n",
            g_cfg.mouseSens, g_cfg.mouseAccel ? 1 : 0, g_cfg.wheelInvert ? 1 : 0, g_cfg.wheelStep, g_cfg.home.c_str(), g_cfg.dark ? 1 : 0,
            g_cfg.zoom, g_cfg.pageW, g_cfg.smooth ? 1 : 0, g_cfg.images ? 1 : 0, g_cfg.vdec, g_cfg.vhalf ? 1 : 0, g_cfg.volume, g_cfg.vscale, g_cfg.vpart ? 1 : 0, g_cfg.search, g_cfg.ua);
    fclose(f);
}
static void configLoad() {
    FILE* f = fopen(kCfgPath, "r");
    if (!f) return;
    char line[600];
    while (fgets(line, sizeof(line), f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "mouseSens") g_cfg.mouseSens = static_cast<float>(atof(v.c_str()));
        else if (k == "mouseAccel") g_cfg.mouseAccel = v == "1";
        else if (k == "wheelInvert") g_cfg.wheelInvert = v == "1";
        else if (k == "wheelStep") g_cfg.wheelStep = atoi(v.c_str());
        else if (k == "home") g_cfg.home = v;
        else if (k == "dark") g_cfg.dark = v == "1";
        else if (k == "zoom") g_cfg.zoom = atoi(v.c_str());
        else if (k == "pageW") g_cfg.pageW = atoi(v.c_str());
        else if (k == "smooth") g_cfg.smooth = v == "1";
        else if (k == "images") g_cfg.images = v == "1";
        else if (k == "vdec") g_cfg.vdec = atoi(v.c_str());
        else if (k == "vhalf") g_cfg.vhalf = v == "1";
        else if (k == "volume") g_cfg.volume = atoi(v.c_str());
        else if (k == "vscale") g_cfg.vscale = atoi(v.c_str());
        else if (k == "vpart") g_cfg.vpart = v == "1";
        else if (k == "search") g_cfg.search = atoi(v.c_str());
        else if (k == "ua") g_cfg.ua = atoi(v.c_str());
    }
    fclose(f);
    clampCfg();
}
static const char* kHistPath = "/data/psbrowser/history.txt";
static std::vector<std::pair<std::string, std::string>> g_history;  // (url, titulo), la mas reciente al final
static void historySave() {
    FILE* f = fopen(kHistPath, "w");
    if (!f) return;
    for (auto& h : g_history) fprintf(f, "%s\t%s\n", h.first.c_str(), h.second.c_str());
    fclose(f);
}
static void historyLoad() {
    FILE* f = fopen(kHistPath, "r");
    if (!f) return;
    char line[1200];
    while (fgets(line, sizeof(line), f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const size_t t = l.find('\t');
        if (t != std::string::npos) g_history.push_back({l.substr(0, t), l.substr(t + 1)});
    }
    fclose(f);
}
static void historyAdd(const std::string& url, const std::string& title) {
    if (!g_history.empty() && g_history.back().first == url) { g_history.back().second = title; return; }
    g_history.push_back({url, title});
    if (g_history.size() > 300) g_history.erase(g_history.begin());
    historySave();
}
static void bookmarksSave() {
    FILE* f = fopen(kBmPath, "w");
    if (!f) return;
    for (auto& b : g_bookmarks) fprintf(f, "%s\t%s\n", b.first.c_str(), b.second.c_str());
    fclose(f);
}
static void bookmarksLoad() {
    FILE* f = fopen(kBmPath, "r");
    if (!f) return;
    char line[1200];
    while (fgets(line, sizeof(line), f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        size_t t = l.find('\t');
        if (t == std::string::npos) g_bookmarks.push_back({l, l});
        else g_bookmarks.push_back({l.substr(0, t), l.substr(t + 1)});
    }
    fclose(f);
}
static int bookmarkIndex(const std::string& url) {
    for (size_t i = 0; i < g_bookmarks.size(); i++)
        if (g_bookmarks[i].first == url) return static_cast<int>(i);
    return -1;
}
// ------------------------------------------------------------------ estado global
static Theme g_th = themeDark();
static Canvas* g_cv = nullptr;
static SDL_Renderer* g_ren = nullptr;
static SDL_Texture* g_tex = nullptr;
static SDL_Texture* g_curTex = nullptr;
static int g_mx = W / 2, g_my = H / 2;
static bool g_mouseOk = false;
static bool g_dirty = true;
static bool g_loading = false;       // carga bloqueante en curso (barra de progreso)
static std::string g_toast;
static uint32_t g_toastUntil = 0;
static bool g_toastBad = false;
static void toast(const std::string& s, bool bad = false) {
    g_toast = s;
    g_toastBad = bad;
    g_toastUntil = SDL_GetTicks() + 3500;
    g_dirty = true;
}
// ------------------------------------------------------------------ portapapeles interno y pestanas cerradas
// La PS4 no da acceso al portapapeles del sistema a un homebrew: es interno al navegador (vale para pegar en la barra de direcciones y
// en campos). Ademas se guarda en /data/psbrowser/clipboard.txt por si quieres leerlo desde un PC.
static std::string g_clip;
static void clipSet(const std::string& s) {
    g_clip = s;
    if (FILE* f = fopen("/data/psbrowser/clipboard.txt", "wb")) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
    std::string shown = s;
    if (shown.size() > 70) shown = shown.substr(0, 70) + "...";
    toast("Copiado: " + shown);
}
static std::vector<std::string> g_closedTabs;  // URLs de pestanas cerradas (Ctrl+Mayus+T las reabre)
static bool g_modCtrl = false, g_modAlt = false, g_modShift = false;  // modificadores del teclado USB en este frame
// ------------------------------------------------------------------ pestanas
struct HistEntry {
    std::string req;        // URL pedida
    std::string finalUrl;   // URL final (tras redirecciones)
    std::string html;       // fuente guardada al salir (para volver sin recargar); vacio = recargar
    int kind = 0;           // 0 html, 1 texto plano, 2 pagina interna
    int scroll = 0;
};
struct Browser {
    int id = 0;
    unsigned docGen = 0;
    Document doc;
    std::string url;
    std::string html;
    int kind = 0;
    std::vector<HistEntry> hist;
    int histPos = -1;
    float scrollY = 0;
    int scrollTarget = 0;
    int sel = -1;            // indice en doc.focus (teclado/mando)
    int hover = -1;          // indice en doc.focus (raton)
    int editField = -1;      // indice en doc.fields
    bool urlEdit = false, urlFresh = false;
    std::string urlText;
    bool needRelayout = false;
    std::vector<int> focusY; // y (px de documento) de cada elemento de doc.focus; -1 si no aparece
};
static std::vector<Browser*> g_tabs;
static int g_cur = 0;
static int g_nextTabId = 1;
#define B (*g_tabs[g_cur])
static int contentW() { return std::min(g_cfg.pageW, W - SBAR_W - 80); }
static int contentLeft() { return (W - SBAR_W - contentW()) / 2; }
static std::string htmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}
static bool isInternal(const std::string& u) { return u.compare(0, 6, "about:") == 0; }
static std::string homeUrl() { return g_cfg.home.empty() ? "about:home" : g_cfg.home; }

// ------------------------------------------------------------------ video: utilidades, buscador de videos de la pagina, salir
static std::string lowerStr(const std::string& s) {
    std::string a = s;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a;
}
static std::string urlPathLower(const std::string& url) { return lowerStr(url.substr(0, url.find_first_of("?#"))); }
static bool endsWithStr(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
// URL cuyo camino acaba en una extension de video (MP4/M4V/MOV son lo que decodifica AvPlayer; el resto es dudoso).
static bool looksVideoUrl(const std::string& url) {
    const std::string p = urlPathLower(url);
    return endsWithStr(p, ".mp4") || endsWithStr(p, ".m4v") || endsWithStr(p, ".mov") || endsWithStr(p, ".webm") || endsWithStr(p, ".mkv") || endsWithStr(p, ".m3u8");
}
static bool isVideoFileName(const std::string& name) {
    const std::string p = lowerStr(name);
    return endsWithStr(p, ".mp4") || endsWithStr(p, ".m4v") || endsWithStr(p, ".mov") || endsWithStr(p, ".webm") || endsWithStr(p, ".mkv");
}
static std::string videoNote(const std::string& urlOrName) {
    const std::string p = urlPathLower(urlOrName);
    if (endsWithStr(p, ".m3u8")) return "HLS (por segmentos): probablemente no se pueda reproducir";
    if (endsWithStr(p, ".webm") || endsWithStr(p, ".mkv")) return "formato WebM/MKV: es probable que el reproductor de la PS4 no lo decodifique";
    return std::string();
}
static std::string titleFromUrl(const std::string& url) {
    const std::string path = url.substr(0, url.find_first_of("?#"));
    const size_t sl = path.rfind('/');
    std::string n = sl == std::string::npos ? path : path.substr(sl + 1);
    return n.empty() ? url : n;
}
struct VidItem { std::string url, note; bool preview = false; int score = 0; };
static std::vector<VidItem> g_lastVids;   // videos encontrados en el HTML de la ultima pagina web
static std::string g_lastVidsPage;
static std::string g_navReferer;          // pagina desde la que se navego (Referer para videos/descargas)
static std::string g_pendRef;             // Referer de la pagina de confirmacion de descarga actual
static bool g_quitAsk = false, g_quit = false;
static void quitAsk() { g_quitAsk = true; g_dirty = true; }
// --- reproductor de video a pantalla completa y dialogo de salida: estado compartido
static bool g_vpMode = false;          // hay un video abierto y la interfaz de video ocupa toda la pantalla
static uint32_t g_vpLastAct = 0;       // ultima actividad del usuario (el overlay se oculta a los 3 s)
static bool g_vpStats = false;         // panel de estadisticas (boton 3 / tecla I)
struct VpBtn { int x, y, w, h, id; };
static std::vector<VpBtn> g_vpBtns;    // botones del overlay (del ultimo fotograma dibujado)
static int g_vpBar[4] = {0, 0, 0, 0};  // zona clicable de la barra de progreso: x, y, w, h
static int g_quitBtn[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};  // [Salir] y [Cancelar]: x, y, w, h
// Abre un video (URL http(s) o ruta de archivo). El reproductor abre en un hilo: no bloquea.
static void playVideo(const std::string& src, const std::string& title, const std::string& referer) {
    if (vp_active()) vp_close();
    vp_set_decoder(g_cfg.vdec);
    vp_set_volume_default(g_cfg.volume);
    imgw_cancel();
    imgw_pause(true);  // las imagenes no deben competir por la red con el video
    psb_log("[video] reproducir '%s' (%s) referer='%s'", title.c_str(), src.c_str(), referer.c_str());
    if (!vp_open(src, title, referer)) {
        toast("No se pudo iniciar el reproductor", true);
        imgw_pause(false);
        return;
    }
    g_vpMode = true;
    g_vpStats = false;
    g_vpLastAct = SDL_GetTicks();
    g_dirty = true;
}
static void closeVideo() {
    if (vp_active()) vp_close();
    g_vpMode = false;
    imgw_pause(false);
    g_dirty = true;
}

// Clasifica una URL de video: las vistas previas (clips de 10 s que salen al pasar el raton por las miniaturas) se mandan al final y
// se avisa; se prefieren las que indican calidad (1080/720/480...). Mayor puntuacion = mejor candidato al video completo.
static int vidScore(const std::string& url, bool& preview) {
    const std::string path = lowerStr(url.substr(0, url.find_first_of("?#")));
    static const char* pv[] = {"preview", "thumb", "trailer", "teaser", "hover", "/td.", "_td.", "-td.", "/tn.", "_tn."};
    preview = false;
    for (const char* t : pv) if (path.find(t) != std::string::npos) { preview = true; break; }
    int sc = 0;
    if (preview) sc -= 20;
    static const struct { const char* t; int v; } q[] = {{"2160", 7}, {"1440", 6}, {"1080", 5}, {"720", 4}, {"480", 3}, {"360", 2}, {"240", 1}};
    for (const auto& e : q) if (path.find(e.t) != std::string::npos) { sc += e.v; break; }
    if (endsWithStr(path, ".m3u8") || endsWithStr(path, ".webm")) sc -= 1;  // el reproductor de la PS4 probablemente no los lea
    return sc;
}
// Busca a mano URLs de video en el HTML CRUDO (muchos sitios las dejan dentro de JavaScript/JSON, no en <video>).
static void scanVideos(const std::string& html, const std::string& base, std::vector<VidItem>& out) {
    out.clear();
    const std::string low = lowerStr(html);
    static const char* exts[] = {".mp4", ".m3u8", ".webm", ".mov", ".m4v"};
    std::map<std::string, bool> seen;
    auto isDelim = [](char c) { return c == '"' || c == '\'' || c == '(' || c == ')' || c == '<' || c == '>' || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',' || c == ';'; };
    for (const char* ext : exts) {
        const size_t en = std::strlen(ext);
        size_t pos = 0;
        while ((pos = low.find(ext, pos)) != std::string::npos && out.size() < 60) {
            const size_t p = pos;
            pos += en;
            if (p + en < low.size()) {
                const char nc = low[p + en];
                if (std::isalnum(static_cast<unsigned char>(nc)) || nc == '_') continue;  // p. ej. ".mp4a"
            }
            // ';' separa, salvo que sea el final de "&amp;" (muy comun en URLs dentro de HTML)
            auto isDelimAt = [&](size_t i) { return html[i] == ';' && i >= 4 && html.compare(i - 4, 4, "&amp") == 0 ? false : isDelim(html[i]); };
            size_t st = p;
            while (st > 0 && !isDelimAt(st - 1) && p - st < 1400) st--;
            size_t ed = p + en;
            while (ed < html.size() && !isDelimAt(ed) && ed - p < 1400) ed++;
            std::string cand = html.substr(st, ed - st);
            if (cand.size() < 5) continue;
            const bool quoted = st > 0 && ed < html.size() && (html[st - 1] == '"' || html[st - 1] == '\'') && html[ed] == html[st - 1];
            // des-escapar JSON/HTML
            std::string c2;
            for (size_t i = 0; i < cand.size(); i++) {
                if (cand.compare(i, 2, "\\/") == 0) { c2 += '/'; i++; }
                else if (cand.compare(i, 6, "\\u0026") == 0) { c2 += '&'; i += 5; }
                else if (cand.compare(i, 6, "\\u002F") == 0 || cand.compare(i, 6, "\\u002f") == 0) { c2 += '/'; i += 5; }
                else if (cand.compare(i, 5, "&amp;") == 0) { c2 += '&'; i += 4; }
                else c2 += cand[i];
            }
            if (c2.find('/') == std::string::npos && !quoted) continue;  // "video.mp4" suelto en un texto no es una direccion
            const std::string full = urlResolve(base, c2);
            if (!urlIsHttp(full) || seen.count(full)) continue;
            seen[full] = true;
            VidItem it;
            it.url = full;
            it.note = videoNote(full);
            it.score = vidScore(full, it.preview);
            if (it.preview) it.note = (it.note.empty() ? std::string() : it.note + "; ") + "parece una VISTA PREVIA (clip corto), no el video completo";
            out.push_back(it);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const VidItem& a, const VidItem& b) { return a.score > b.score; });  // mejores primero
}
static void captureVideos(const std::string& html, const std::string& base) {
    std::vector<VidItem> v;
    scanVideos(html, base, v);
    if (v.empty()) return;
    g_lastVids.swap(v);
    g_lastVidsPage = base;
    bool allPrev = true;
    for (const VidItem& it : g_lastVids) if (!it.preview) { allPrev = false; break; }
    if (allPrev) toast(std::to_string(g_lastVids.size()) + " clips de vista previa (parece un listado): abre la pagina del video y pulsa F8");
    else toast(std::to_string(g_lastVids.size()) + (g_lastVids.size() == 1 ? " video detectado (F8)" : " videos detectados (F8)"));
}
static std::string genHome() {
    std::string h =
        "<html><head><title>Inicio</title></head><body><h1>psbrowser</h1>"
        "<form action=\"https://html.duckduckgo.com/html/\" method=\"get\">"
        "<input type=\"text\" name=\"q\" placeholder=\"Buscar con DuckDuckGo\"> <input type=\"submit\" value=\"Buscar\"></form>"
        "<p><a href=\"about:downloads\">Descargas</a> | <a href=\"about:images\">Imagenes de la ultima pagina</a> | <a href=\"about:videos\">Videos de la pagina</a> | <a href=\"about:history\">Historial</a> | <a href=\"about:shortcuts\">Atajos</a> | <a href=\"psb:quit\">Salir</a></p>"
        "<h2>Marcadores</h2>";
    if (g_bookmarks.empty()) h += "<p>Aun no hay marcadores: pulsa la estrella de la barra (o Ctrl+D) en una pagina.</p>";
    else {
        h += "<ul>";
        for (size_t i = 0; i < g_bookmarks.size() && i < 12; i++)
            h += "<li><a href=\"" + htmlEscape(g_bookmarks[i].first) + "\">" + htmlEscape(g_bookmarks[i].second.empty() ? g_bookmarks[i].first : g_bookmarks[i].second) + "</a></li>";
        h += "</ul>";
        if (g_bookmarks.size() > 12) h += "<p><a href=\"about:bookmarks\">Ver todos los marcadores</a></p>";
    }
    h += "<h2>Sitios sin JavaScript</h2><ul>"
         "<li><a href=\"https://en.wikipedia.org/wiki/PlayStation_4\">Wikipedia: PlayStation 4</a></li>"
         "<li><a href=\"https://news.ycombinator.com/\">Hacker News</a></li>"
         "<li><a href=\"https://text.npr.org/\">NPR (version de texto)</a></li>"
         "<li><a href=\"https://info.cern.ch/\">info.cern.ch (la primera web)</a></li>"
         "<li><a href=\"http://example.com/\">example.com</a></li>"
         "</ul><h2>Controles</h2><ul>"
         "<li>Teclado: Ctrl+T nueva pestana, Ctrl+W cerrar, F6 o Ctrl+L direccion, Tab/Enter enlaces y campos, Backspace atras, F5 recargar, F10 configuracion, Ctrl+D marcador, Ctrl +/- zoom, Ctrl+C copiar enlace o direccion, Ctrl+V pegar, Ctrl+Mayus+T reabrir pestana, tecla Menu = menu contextual.</li>"
         "<li>Raton: clic en enlaces, botones y pestanas; clic central o Ctrl+clic abre en pestana nueva; Alt+clic descarga; clic derecho = menu contextual (copiar enlace o imagen, guardar, atras...); rueda = desplazar.</li>"
         "<li>Mando: cruceta = foco / atras / adelante; boton 0 activar, 1 atras, 2 recargar, 3 inicio, 4/5 pagina; boton 11 menu contextual; stick = desplazar.</li>"
         "</ul><p>Sin JavaScript y con CSS limitado. Los formularios POST se envian de verdad; no hay cookies. Video: MP4 (H.264/AAC) con el reproductor de la consola (experimental): abre un enlace a un .mp4 y elige Reproducir; F8 = videos de la pagina; Ctrl+Q = salir.</p></body></html>";
    return h;
}
static std::string genSettings() {
    char b[7600];
    static const char* kUaName[] = {"PS4 (por defecto)", "Chrome (Windows)", "Firefox (Windows)", "Smart TV (Tizen)", "Lynx (paginas simples)"};
    const char* vd = g_cfg.vdec == 2 ? "software" : (g_cfg.vdec == 3 ? "software 2" : "sistema (hardware)");
    snprintf(b, sizeof(b),
             "<html><head><title>Configuracion</title></head><body><h1>Configuracion</h1>"
             "<h2>Aspecto</h2>"
             "<p>Tema: <a href=\"psb:theme\">[ %s ]</a> (pulsa para cambiar)</p>"
             "<p>Tamano del texto: <a href=\"psb:zoom-\">[ - ]</a> %d%% <a href=\"psb:zoom+\">[ + ]</a> (80 a 200)</p>"
             "<p>Ancho de pagina: <a href=\"psb:pw-\">[ - ]</a> %d px <a href=\"psb:pw+\">[ + ]</a> (800 a 1700)</p>"
             "<p>Scroll suave: <a href=\"psb:smooth\">[ %s ]</a></p>"
             "<p>Cargar imagenes: <a href=\"psb:images\">[ %s ]</a></p>"
             "<h2>Raton</h2>"
             "<p>Sensibilidad: <a href=\"psb:sens-1\">[ -- ]</a> <a href=\"psb:sens-\">[ - ]</a> %.1fx <a href=\"psb:sens+\">[ + ]</a> <a href=\"psb:sens+1\">[ ++ ]</a> (de 0.5 a 20; por defecto 3.5)</p>"
             "<p>Aceleracion: <a href=\"psb:accel\">[ %s ]</a></p>"
             "<p>Rueda invertida: <a href=\"psb:wheelinv\">[ %s ]</a></p>"
             "<p>Velocidad de la rueda: <a href=\"psb:wheel-\">[ - ]</a> %d muescas <a href=\"psb:wheel+\">[ + ]</a></p>"
             "<h2>Pagina de inicio</h2>"
             "<p>Actual: %s</p>"
             "<p><a href=\"psb:homehere\">[ usar la pagina anterior como inicio ]</a> <a href=\"psb:homereset\">[ restablecer ]</a></p>"
             "<h2>Video</h2>"
             "<p>Decodificador: <a href=\"psb:vdec\">[ %s ]</a> (si el video no muestra imagen, prueba otro)</p>"
             "<p>Dibujar a media resolucion (mas rapido): <a href=\"psb:vhalf\">[ %s ]</a></p>"
             "<p>Volumen inicial: <a href=\"psb:vol-\">[ - ]</a> %d%% <a href=\"psb:vol+\">[ + ]</a></p>"
             "<p>Tamano del video (via rapida): <a href=\"psb:vscale\">[ %s ]</a> (1:1 y x2 evitan escalar por CPU)</p>"
             "<p>Actualizacion parcial de pantalla (experimental): <a href=\"psb:vpart\">[ %s ]</a> (solo sin raton conectado; si el video parpadea, desactivala)</p>"
             "<p><a href=\"psb:bench\">[ Medir velocidad de presentacion (F9, ~20 s) ]</a> <a href=\"about:bench\">[ ver ultimo resultado ]</a></p>"
             "<h2>Otros</h2><p><a href=\"about:bookmarks\">Marcadores</a> | <a href=\"about:home\">Inicio</a> | <a href=\"about:videos\">Videos de la pagina</a></p>"
             "<p><a href=\"psb:quit\">[ Salir del navegador ]</a></p>"
             "<p>Los cambios se guardan al momento en /data/psbrowser/config.txt. Mueve el raton para probar la sensibilidad.</p>"
             "</body></html>",
             g_cfg.dark ? "oscuro" : "claro", g_cfg.zoom, g_cfg.pageW, g_cfg.smooth ? "si" : "no", g_cfg.images ? "si" : "no", g_cfg.mouseSens,
             g_cfg.mouseAccel ? "si" : "no", g_cfg.wheelInvert ? "si" : "no", g_cfg.wheelStep, htmlEscape(homeUrl()).c_str(), vd, g_cfg.vhalf ? "si" : "no", g_cfg.volume,
             g_cfg.vscale == 1 ? "nativo 1:1" : (g_cfg.vscale == 2 ? "doble x2" : "ajustar a pantalla"), g_cfg.vpart ? "si" : "no");
    std::string out = b;
    const std::string extra =
        "<h2>Navegador</h2>"
        "<p>Buscador de la barra: <a href=\"psb:search\">[ " + std::string(urlSearchEngineName(g_cfg.search)) + " ]</a></p>"
        "<p>Identificarse como (User-Agent): <a href=\"psb:ua\">[ " + kUaName[g_cfg.ua] + " ]</a> (se aplica a la siguiente pagina; "
        "algunas paginas de streaming y de video sirven mas contenido a Chrome/Smart TV, otras dan HTML mas simple a Lynx)</p>"
        "<p><a href=\"about:shortcuts\">[ Atajos de teclado (F1) ]</a> <a href=\"about:history\">[ Historial (Ctrl+H) ]</a></p>";
    const size_t e = out.rfind("</body>");
    out.insert(e == std::string::npos ? out.size() : e, extra);
    return out;
}
static std::string genBookmarks() {
    std::string h = "<html><head><title>Marcadores</title></head><body><h1>Marcadores</h1>";
    if (g_bookmarks.empty()) h += "<p>No hay marcadores. Pulsa la estrella de la barra o Ctrl+D en una pagina para guardarla.</p>";
    h += "<ul>";
    for (size_t i = 0; i < g_bookmarks.size(); i++)
        h += "<li><a href=\"" + htmlEscape(g_bookmarks[i].first) + "\">" + htmlEscape(g_bookmarks[i].second.empty() ? g_bookmarks[i].first : g_bookmarks[i].second) +
             "</a> <a href=\"psb:delbm:" + std::to_string(i) + "\">[ borrar ]</a></li>";
    h += "</ul><p><a href=\"about:home\">Inicio</a> | <a href=\"about:settings\">Configuracion</a></p></body></html>";
    return h;
}
// ------------------------------------------------------------------ descargas: datos y paginas internas
static const char* kDlDir = "/data/psbrowser/downloads";
static const char* kDlList = "/data/psbrowser/downloads.txt";  // una linea por archivo: nombre \t bytes \t url
struct DlItem {
    std::string name;
    long long bytes = 0;
    std::string url;
};
static std::vector<DlItem> g_dls;
struct DlPending {  // lo que vio la puerta de cabeceras de la ultima pagina de confirmacion
    std::string url, name, type;
    long long len = -1;
};
static DlPending g_pend;
struct DlProgress {  // descarga en primer plano
    bool active = false, cancel = false;
    std::string name;
    long long done = 0, total = -1;
    float speed = 0;  // bytes/s (media movil)
    uint32_t lastDraw = 0, lastSpeedT = 0, lastLog = 0, startT = 0;
    long long lastSpeedB = 0;
    long long queueKb = 0;  // datos en cola esperando al disco
};
static DlProgress g_dlp;
static std::vector<std::pair<std::string, std::string>> g_lastImgs;  // (URL absoluta, alt) de la ultima pagina web con imagenes
static std::string g_lastImgsPage;
static bool g_skipCapture = false;  // la pagina de confirmacion de descarga no sustituye a la lista de imagenes
static void dlListSave() {
    FILE* f = fopen(kDlList, "w");
    if (!f) return;
    for (const DlItem& d : g_dls) fprintf(f, "%s\t%lld\t%s\n", d.name.c_str(), d.bytes, d.url.c_str());
    fclose(f);
}
static void dlListLoad() {
    FILE* f = fopen(kDlList, "r");
    if (!f) return;
    char line[2200];
    while (fgets(line, sizeof(line), f)) {
        std::string t(line);
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
        const size_t t1 = t.find('\t');
        if (t1 == std::string::npos) continue;
        const size_t t2 = t.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        DlItem d;
        d.name = t.substr(0, t1);
        d.bytes = atoll(t.substr(t1 + 1, t2 - t1 - 1).c_str());
        d.url = t.substr(t2 + 1);
        if (!d.name.empty()) g_dls.push_back(d);
    }
    fclose(f);
}
static std::string genDownloads() {
    std::string h = "<html><head><title>Descargas</title></head><body><h1>Descargas</h1><p>Los archivos se guardan en " + std::string(kDlDir) +
                    "/ de la consola.</p>";
    if (g_dls.empty()) {
        h += "<p>Aun no hay descargas. Abre un enlace a un archivo (zip, pkg, pdf, mp4...) o a una imagen y elige Descargar.</p>";
    } else {
        h += "<ul>";
        for (size_t i = g_dls.size(); i-- > 0;)  // la mas reciente arriba
            h += "<li><b>" + htmlEscape(g_dls[i].name) + "</b> - " + dl_format_size(g_dls[i].bytes) +
                 (isVideoFileName(g_dls[i].name) ? " <a href=\"psb:playfile:" + std::to_string(i) + "\">[ Reproducir ]</a>" : std::string()) +
                 " <a href=\"psb:deldl:" + std::to_string(i) + "\">[ borrar ]</a><br>" + htmlEscape(g_dls[i].url) + "</li>";
        h += "</ul>";
    }
    h += "<p><a href=\"about:images\">Imagenes de la ultima pagina</a> | <a href=\"about:home\">Inicio</a> | <a href=\"about:settings\">Configuracion</a></p></body></html>";
    return h;
}
static std::string genImages() {
    std::string h = "<html><head><title>Imagenes de la pagina</title></head><body><h1>Imagenes de la ultima pagina</h1>";
    if (g_lastImgs.empty()) {
        h += "<p>Todavia no hay imagenes: abre una pagina con imagenes y vuelve aqui.</p>";
    } else {
        h += "<p>" + htmlEscape(g_lastImgsPage) + "</p><p>Abre una para verla y descargarla.</p><ul>";
        for (const auto& im : g_lastImgs) {
            std::string label = im.second;
            if (label.empty()) {
                const std::string path = im.first.substr(0, im.first.find_first_of("?#"));
                const size_t sl = path.rfind('/');
                label = sl == std::string::npos ? im.first : path.substr(sl + 1);
                if (label.empty()) label = im.first;
            }
            if (label.size() > 90) label.resize(90);
            h += "<li><a href=\"" + htmlEscape(im.first) + "\">" + htmlEscape(label) + "</a></li>";
        }
        h += "</ul>";
    }
    h += "<p><a href=\"about:downloads\">Descargas</a> | <a href=\"about:home\">Inicio</a></p></body></html>";
    return h;
}
static std::string genVideos() {
    std::string h = "<html><head><title>Videos de la pagina</title></head><body><h1>Videos de la ultima pagina</h1>";
    if (g_lastVids.empty()) {
        h += "<p>No se detectaron videos: abre una pagina con videos y vuelve aqui (F8). Se buscan direcciones .mp4, .m3u8 y .webm en el codigo de la pagina, "
             "incluso las que estan dentro de JavaScript.</p>";
    } else {
        h += "<p>" + htmlEscape(g_lastVidsPage) + "</p><p>Reproducir ve el video sin descargarlo (solo MP4 con H.264/AAC es seguro); Descargar lo guarda.</p>";
        bool allPrev = true;
        for (const VidItem& it : g_lastVids) if (!it.preview) { allPrev = false; break; }
        if (allPrev) h += "<p><b>Esta pagina parece un listado:</b> todo lo encontrado son vistas previas de unos 10 s. Abre la pagina del video que quieres ver y pulsa F8 alli.</p>";
        h += "<ul>";
        for (size_t i = 0; i < g_lastVids.size(); i++) {
            std::string label = g_lastVids[i].url;
            if (label.size() > 110) label = label.substr(0, 107) + "...";
            h += "<li>" + htmlEscape(label) + "<br><a href=\"psb:play:" + htmlEscape(g_lastVids[i].url) + "\">[ Reproducir ]</a> <a href=\"psb:dlgo:" +
                 htmlEscape(g_lastVids[i].url) + "\">[ Descargar ]</a>";
            if (!g_lastVids[i].note.empty()) h += " - " + htmlEscape(g_lastVids[i].note);
            h += "</li>";
        }
        h += "</ul>";
    }
    h += "<p><a href=\"about:downloads\">Descargas</a> | <a href=\"about:images\">Imagenes de la pagina</a> | <a href=\"about:home\">Inicio</a></p></body></html>";
    return h;
}
static std::string genShortcuts() {
    return "<html><head><title>Atajos de teclado</title></head><body><h1>Atajos de teclado</h1>"
           "<h2>Navegacion</h2><ul>"
           "<li>Ctrl+L / F6: barra de direcciones. Alt+Izquierda / Atras: pagina anterior. Alt+Derecha: siguiente. Alt+Inicio: pagina de inicio.</li>"
           "<li>F5 / Ctrl+R: recargar. Ctrl+V: pegar y ir. Ctrl+C: copiar enlace seleccionado o la direccion.</li>"
           "<li>Tab / Mayus+Tab: siguiente / anterior enlace o campo. Intro: abrir. Flechas, RePag, AvPag, Espacio, Inicio, Fin: desplazarse.</li></ul>"
           "<h2>Pestanas</h2><ul>"
           "<li>Ctrl+T: nueva. Ctrl+W: cerrar. Ctrl+Mayus+T: reabrir la cerrada. Ctrl+Tab / Ctrl+RePag: cambiar. Ctrl+1..8 / Ctrl+9: ir a la pestana / a la ultima.</li></ul>"
           "<h2>Pagina</h2><ul>"
           "<li>Ctrl+F: buscar en la pagina (Intro / F3: siguiente, Mayus+F3: anterior, Esc: cerrar). Ctrl++ / Ctrl+-: tamano del texto, Ctrl+0: restablecer.</li>"
           "<li>Ctrl+D: marcador. Ctrl+J: descargas. Ctrl+H: historial. F8: videos de la pagina. F10: configuracion. F1: esta ayuda.</li>"
           "<li>Clic derecho / Mayus+F10 / tecla Menu: menu contextual (copiar enlace, abrir en pestana nueva, descargar...).</li></ul>"
           "<h2>Video</h2><ul>"
           "<li>Espacio / K: pausa. Izquierda / Derecha: -10 / +10 s. J / L: -30 / +30 s. Arriba / Abajo: volumen. Inicio: al principio. H: calidad. I: estadisticas. Esc / Q: cerrar.</li></ul>"
           "<h2>Diagnostico</h2><ul><li>F9: medir la velocidad de presentacion (benchmark).</li></ul>"
           "<p><a href=\"about:home\">Inicio</a> | <a href=\"about:settings\">Configuracion</a></p></body></html>";
}
static std::string genHistory() {
    std::string h = "<html><head><title>Historial</title></head><body><h1>Historial</h1>";
    if (g_history.empty()) h += "<p>Todavia no hay paginas visitadas.</p>";
    else {
        h += "<p><a href=\"psb:clearhist\">[ borrar historial ]</a></p><ul>";
        int shown = 0;
        for (size_t i = g_history.size(); i-- > 0 && shown < 150; shown++)
            h += "<li><a href=\"" + htmlEscape(g_history[i].first) + "\">" + htmlEscape(g_history[i].second.empty() ? g_history[i].first : g_history[i].second) + "</a></li>";
        h += "</ul>";
    }
    h += "<p><a href=\"about:home\">Inicio</a> | <a href=\"about:bookmarks\">Marcadores</a></p></body></html>";
    return h;
}
static std::vector<std::string> g_bench;  // resultados de la ultima medicion de presentacion
static std::string genBench() {
    std::string h = "<html><head><title>Velocidad de presentacion</title></head><body><h1>Velocidad de presentacion</h1>"
                    "<p>Mide cuanto tarda SDL en poner un fotograma en pantalla segun lo que se copie. <a href=\"psb:bench\">[ ejecutar la medicion (~20 s) ]</a></p>";
    if (g_bench.empty()) h += "<p>Aun no se ha medido.</p>";
    else { h += "<ul>"; for (const std::string& l : g_bench) h += "<li>" + htmlEscape(l) + "</li>"; h += "</ul>"; }
    h += "<p>Devuelve /data/psbrowser/log.txt (lineas [bench]) para decidir la mejora. <a href=\"about:settings\">Configuracion</a></p></body></html>";
    return h;
}
static std::string genInternal(const std::string& url) {
    if (url == "about:shortcuts") return genShortcuts();
    if (url == "about:history") return genHistory();
    if (url == "about:bench") return genBench();
    if (url == "about:videos") return genVideos();
    if (url == "about:settings") return genSettings();
    if (url == "about:bookmarks") return genBookmarks();
    if (url == "about:downloads") return genDownloads();
    if (url == "about:images") return genImages();
    if (url == "about:blank") return "<html><head><title>Pestana nueva</title></head><body></body></html>";
    return genHome();
}
static std::string infoHtml(const std::string& title, const std::string& msg, const std::string& url) {
    return "<html><head><title>" + htmlEscape(title) + "</title></head><body><h1>" + htmlEscape(title) + "</h1><p>" + htmlEscape(msg) +
           "</p><p>" + htmlEscape(url) + "</p></body></html>";
}
static bool containsCI(const std::string& s, const char* sub) {
    std::string a = s;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.find(sub) != std::string::npos;
}
// ------------------------------------------------------------------ documento de una pestana
static int maxScroll(const Browser& b) { return std::max(0, b.doc.height - VIEW_H + 24); }
static void buildFocusIndex(Browser& b) {
    b.focusY.assign(b.doc.focus.size(), -1);
    for (const DocLine& L : b.doc.lines)
        for (const Span& s : L.spans)
            if (s.focus >= 0 && s.focus < static_cast<int>(b.focusY.size()) && b.focusY[s.focus] < 0) b.focusY[s.focus] = L.y;
}
static void layoutDoc(Browser& b) {
    const int cw = contentW();
    b.doc = (b.kind == 1) ? parsePlain(b.html, cw, g_cfg.zoom) : parseHtml(b.html, cw, g_cfg.zoom);
    b.needRelayout = false;
    b.docGen++;
    b.sel = -1;
    b.hover = -1;
    b.editField = -1;
    buildFocusIndex(b);
}
// Guarda las URLs de las imagenes de la pagina para "Imagenes de la ultima pagina" (descargarlas una a una).
static void captureImages(const Browser& b) {
    std::vector<std::pair<std::string, std::string>> v;
    std::map<std::string, bool> seen;
    for (const ImageRef& im : b.doc.images) {
        const std::string u = urlResolve(b.url, im.src);
        if (!urlIsHttp(u) || seen.count(u)) continue;
        seen[u] = true;
        v.push_back({u, im.alt});
        if (v.size() >= 400) break;
    }
    if (v.empty()) return;
    g_lastImgs.swap(v);
    g_lastImgsPage = b.url;
}
static void setPage(Browser& b, const std::string& url, std::string html, int kind) {
    b.url = url;
    b.html = std::move(html);
    b.kind = kind;
    layoutDoc(b);
    if (kind == 0 && !isInternal(url) && urlIsHttp(url)) historyAdd(url, b.doc.title);
    if (kind == 0 && !g_skipCapture && !isInternal(url)) { captureImages(b); captureVideos(b.html, url); }
    b.scrollY = 0;
    b.scrollTarget = 0;
    g_dirty = true;
}
// Re-maqueta conservando la proporcion de scroll (zoom, ancho de pagina...).
static void relayout(Browser& b) {
    const float ratio = b.doc.height > 0 ? static_cast<float>(b.scrollTarget) / b.doc.height : 0.0f;
    layoutDoc(b);
    b.scrollTarget = std::max(0, std::min(maxScroll(b), static_cast<int>(ratio * b.doc.height)));
    b.scrollY = static_cast<float>(b.scrollTarget);
    g_dirty = true;
}
// Regenera una pagina interna (ajustes, marcadores) conservando scroll y seleccion.
static void refreshInternal(Browser& b) {
    if (!isInternal(b.url)) return;
    const int sc = b.scrollTarget, se = b.sel;
    b.html = genInternal(b.url);
    layoutDoc(b);
    b.scrollTarget = std::min(sc, maxScroll(b));
    b.scrollY = static_cast<float>(b.scrollTarget);
    b.sel = se < static_cast<int>(b.doc.focus.size()) ? se : -1;
    g_dirty = true;
}
static void scrollTo(Browser& b, int target, bool instant) {
    target = std::max(0, std::min(maxScroll(b), target));
    if (target == b.scrollTarget && std::fabs(b.scrollY - target) < 0.5f) return;
    b.scrollTarget = target;
    if (instant || !g_cfg.smooth) b.scrollY = static_cast<float>(target);
    g_dirty = true;
}
static void scrollBy(Browser& b, int px) { scrollTo(b, b.scrollTarget + px, false); }
static bool jumpAnchor(Browser& b, const std::string& fragRaw) {
    const std::string frag = urlPercentDecode(fragRaw);
    if (frag.empty() || frag == "top") { scrollTo(b, 0, false); return true; }
    for (const auto& a : b.doc.anchors)
        if (a.first == frag && a.second >= 0 && a.second < static_cast<int>(b.doc.lines.size())) {
            scrollTo(b, b.doc.lines[a.second].y - 12, false);
            return true;
        }
    return false;
}
static void ensureFocusVisible(Browser& b) {
    if (b.sel < 0 || b.sel >= static_cast<int>(b.focusY.size()) || b.focusY[b.sel] < 0) return;
    const int y = b.focusY[b.sel];
    if (y < b.scrollTarget + 30) scrollTo(b, y - 100, false);
    else if (y > b.scrollTarget + VIEW_H - 100) scrollTo(b, y - VIEW_H + 200, false);
}
static bool focusVisible(const Browser& b, int fi) {
    if (fi < 0 || fi >= static_cast<int>(b.focusY.size()) || b.focusY[fi] < 0) return false;
    return b.focusY[fi] >= b.scrollTarget && b.focusY[fi] < b.scrollTarget + VIEW_H - 40;
}
// Mueve el foco al elemento siguiente (+1) o anterior (-1); sin elementos, desplaza.
static void moveFocus(Browser& b, int dir) {
    const int n = static_cast<int>(b.doc.focus.size());
    if (n == 0) { scrollBy(b, dir * 160); return; }
    int cand = -1;
    if (b.sel < 0 || !focusVisible(b, b.sel)) {
        for (int i = (dir > 0 ? 0 : n - 1); i >= 0 && i < n; i += dir)
            if (focusVisible(b, i)) { cand = i; break; }
        if (cand < 0) { scrollBy(b, dir * 300); return; }
    } else {
        cand = b.sel + dir;
        while (cand >= 0 && cand < n && b.focusY[cand] < 0) cand += dir;
        if (cand < 0 || cand >= n) { scrollBy(b, dir * 300); return; }
    }
    b.sel = cand;
    ensureFocusVisible(b);
    g_dirty = true;
}
// ------------------------------------------------------------------ imagenes (cache global + hilo)
struct CImg {
    int pw = 0, ph = 0;
    std::vector<uint32_t> pix;
};
static std::map<std::string, CImg> g_imgCache;
static size_t g_imgBytes = 0;
static std::string imgKey(const std::string& u, int w, int h) { return u + "|" + std::to_string(w) + "x" + std::to_string(h); }
static Browser* findTab(int id) {
    for (Browser* t : g_tabs)
        if (t->id == id) return t;
    return nullptr;
}
static void pumpImages() {
    ImgResult r;
    while (imgw_poll(r)) {
        Browser* t = findTab(r.tabId);
        if (r.ok) {
            if (g_imgBytes > 40u * 1024 * 1024) { g_imgCache.clear(); g_imgBytes = 0; }
            CImg c;
            c.pw = r.pw; c.ph = r.ph; c.pix = r.pix;
            g_imgBytes += c.pix.size() * 4;
            g_imgCache[imgKey(r.url, r.boxW, r.boxH)] = std::move(c);
        }
        if (t && t->docGen == r.gen && r.idx >= 0 && r.idx < static_cast<int>(t->doc.images.size())) {
            ImageRef& im = t->doc.images[r.idx];
            if (r.ok) { im.pw = r.pw; im.ph = r.ph; im.pix = std::move(r.pix); im.state = 2; }
            else im.state = 3;
            if (t == &B) g_dirty = true;
        }
    }
    if (!g_cfg.images) return;
    Browser& b = B;
    const int y0 = b.scrollTarget - VIEW_H, y1 = b.scrollTarget + 2 * VIEW_H;
    for (int li = firstVisibleLine(b.doc, std::max(0, y0)); li < static_cast<int>(b.doc.lines.size()); li++) {
        const DocLine& L = b.doc.lines[li];
        if (L.y > y1) break;
        for (const Span& s : L.spans) {
            if (s.kind != SK_IMAGE || s.ref < 0 || s.ref >= static_cast<int>(b.doc.images.size())) continue;
            ImageRef& im = b.doc.images[s.ref];
            if (im.state != 0) continue;
            const std::string u = urlResolve(b.url, im.src);
            if (!urlIsHttp(u)) { im.state = 3; continue; }
            {   // GIF: de momento no se descarga (el decodificador esta desactivado, ver imgworker.cpp)
                std::string lu = u;
                for (char& ch : lu) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                const size_t q = lu.find_first_of("?#");
                const std::string path = lu.substr(0, q);
                if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".gif") == 0) { im.state = 3; continue; }
            }
            auto it = g_imgCache.find(imgKey(u, im.w, im.h));
            if (it != g_imgCache.end()) {
                im.pw = it->second.pw; im.ph = it->second.ph; im.pix = it->second.pix; im.state = 2;
                g_dirty = true;
                continue;
            }
            if (imgw_pending() >= 3) return;
            ImgJob j;
            j.tabId = b.id; j.gen = b.docGen; j.idx = s.ref; j.url = u; j.boxW = im.w; j.boxH = im.h;
            imgw_submit(j);
            im.state = 1;
        }
    }
}
// ------------------------------------------------------------------ navegacion
static void present();  // dibuja y muestra un frame ya (para el indicador de carga)
static void saveLeaving(Browser& b) {
    if (b.histPos < 0 || b.histPos >= static_cast<int>(b.hist.size())) return;
    HistEntry& e = b.hist[b.histPos];
    e.finalUrl = b.url;
    e.scroll = b.scrollTarget;
    e.kind = b.kind;
    e.html = std::move(b.html);  // b.html se vuelve a rellenar al cargar la pagina siguiente
    b.html.clear();
}
// ------------------------------------------------------------------ descargas: red, disco y progreso (primer plano)
static std::string fmtTime(long long secs) {
    char t[32];
    if (secs < 0) secs = 0;
    if (secs >= 3600) snprintf(t, sizeof(t), "%lld:%02lld:%02lld", secs / 3600, (secs / 60) % 60, secs % 60);
    else snprintf(t, sizeof(t), "%lld:%02lld", secs / 60, secs % 60);
    return t;
}
// Se llama con cada trozo recibido: cada ~300 ms recalcula la velocidad, atiende el mando (O = cancelar) y redibuja.
static void dlTick() {
    const uint32_t now = SDL_GetTicks();
    if (now - g_dlp.lastDraw < 600) return;  // subir el lienzo cuesta ~60 ms y la red no se lee mientras: no hacerlo con cada trozo
    const uint32_t dt = now - g_dlp.lastSpeedT;
    if (dt > 0) {
        const float inst = static_cast<float>(g_dlp.done - g_dlp.lastSpeedB) * 1000.0f / static_cast<float>(dt);
        g_dlp.speed = g_dlp.speed <= 0 ? inst : g_dlp.speed * 0.7f + inst * 0.3f;
    }
    g_dlp.lastSpeedT = now;
    g_dlp.lastSpeedB = g_dlp.done;
    if (now - g_dlp.lastLog >= 5000) {  // diagnostico de velocidad en log.txt cada 5 s
        g_dlp.lastLog = now;
        psb_log("[dl] %s de %s, %.2f MB/s (media %.2f MB/s), cola de disco %lld KB", dl_format_size(g_dlp.done).c_str(),
                g_dlp.total > 0 ? dl_format_size(g_dlp.total).c_str() : "?", g_dlp.speed / 1048576.0,
                (now - g_dlp.startT) > 0 ? g_dlp.done * 1000.0 / (now - g_dlp.startT) / 1048576.0 : 0.0, g_dlp.queueKb);
    }
    bool quit = false;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) quit = true;
        else if (e.type == SDL_JOYBUTTONDOWN && e.jbutton.button == 1) g_dlp.cancel = true;  // O
    }
    if (quit) {  // cerrar la aplicacion: cancela y devuelve el evento al bucle principal
        g_dlp.cancel = true;
        SDL_Event q;
        memset(&q, 0, sizeof(q));
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
    }
    g_dlp.lastDraw = SDL_GetTicks();
    present();
}
struct FileSink : HttpSink {
    AsyncWriter w;  // el disco se escribe en otro hilo: red y disco avanzan a la vez
    bool opened = false;
    std::string partPath, finalPath, name;
    bool diskError = false, htmlInstead = false, badStatus = false;
    int status = 0;
    bool begin(const HttpResult& r) override {
        status = r.status;
        if (r.status < 200 || r.status >= 300) { badStatus = true; return false; }
        // Se esperaba un archivo y llega una pagina web: enlace temporal, sesion caducada, verificacion (captcha/Cloudflare)...
        if (!g_pend.type.empty() && !dl_is_page_type(g_pend.type) && dl_is_page_type(r.contentType)) { htmlInstead = true; return false; }
        mkdir(kDlDir, 0777);
        name = dl_filename(r.finalUrl, r.contentDisposition, r.contentType);
        finalPath = dl_unique_path(kDlDir, name);
        name = finalPath.substr(strlen(kDlDir) + 1);
        partPath = finalPath + ".part";
        if (!w.open(partPath)) { diskError = true; return false; }
        opened = true;
        g_dlp.name = name;
        g_dlp.total = r.contentLength;
        g_dlp.startT = SDL_GetTicks();
        psb_log("[dl] %s -> %s (%s)", r.finalUrl.c_str(), finalPath.c_str(),
                r.contentLength >= 0 ? dl_format_size(r.contentLength).c_str() : "tamano desconocido");
        return true;
    }
    bool write(const void* d, size_t n) override {
        if (!w.write(d, n)) { diskError = true; return false; }
        g_dlp.done += static_cast<long long>(n);
        g_dlp.queueKb = w.queued() / 1024;
        dlTick();
        return !g_dlp.cancel;
    }
};
// Descarga 'url' a /data/psbrowser/downloads/ (bloquea la interfaz con una barra de progreso). true = guardado.
static bool runDownload(const std::string& url, std::string& err) {
    imgw_cancel();
    imgw_pause(true);  // el hilo de imagenes no debe competir por la red
    g_dlp = DlProgress();
    g_dlp.active = true;
    const bool same = g_pend.url == url;
    g_dlp.name = same && !g_pend.name.empty() ? g_pend.name : "archivo";
    g_dlp.total = same ? g_pend.len : -1;
    g_dlp.lastSpeedT = SDL_GetTicks();
    g_loading = true;
    present();
    FileSink sink;
    HttpResult res;
    const uint32_t t0 = SDL_GetTicks();
    const bool ok = http_request(url, nullptr, res, 0, 8, &sink);
    const uint32_t tNet = SDL_GetTicks();
    const bool flushed = sink.opened ? (g_dlp.cancel || !ok ? (sink.w.abort(), true) : sink.w.finish()) : true;  // espera a que el disco termine
    const uint32_t tEnd = SDL_GetTicks();
    if (sink.opened && !flushed) sink.diskError = true;
    if (sink.opened && res.received > 0)
        psb_log("[dl] red: %.1f s, %.2f MB/s | disco tras la red: %u ms | total %.1f s", (tNet - t0) / 1000.0,
                res.received * 1000.0 / std::max<uint32_t>(1, tNet - t0) / 1048576.0, tEnd - tNet, (tEnd - t0) / 1000.0);
    g_dlp.active = false;
    g_loading = false;
    imgw_pause(false);
    g_dirty = true;
    bool good = ok && res.status >= 200 && res.status < 300;
    if (good && sink.diskError) {
        good = false;
        err = "No se pudo escribir en el disco (sin espacio o sin permisos).";
    }
    if (good && res.contentLength >= 0 && res.received < res.contentLength) {
        good = false;
        err = "La descarga quedo incompleta (" + dl_format_size(res.received) + " de " + dl_format_size(res.contentLength) + "): se cerro la conexion.";
    }
    if (!good && err.empty()) {
        char code[64];
        snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(res.sceError));
        if (g_dlp.cancel) err = "Descarga cancelada.";
        else if (sink.diskError) err = "No se pudo escribir en el disco (sin espacio o sin permisos).";
        else if (sink.htmlInstead)
            err = "El servidor devolvio una pagina web en vez del archivo: el enlace puede ser temporal, exigir iniciar sesion o una verificacion "
                  "(captcha/Cloudflare) que este navegador no puede resolver.";
        else if (sink.badStatus) {
            err = "El servidor respondio HTTP " + std::to_string(sink.status) + ".";
            if (sink.status == 403 || sink.status == 429 || sink.status == 503)
                err += " Puede exigir verificacion (Cloudflare/captcha) o iniciar sesion; este navegador no ejecuta JavaScript y no puede resolverla.";
        } else err = "Error de red en '" + res.stage + "' (codigo " + code + ").";
    }
    if (!good) {
        if (!sink.partPath.empty()) remove(sink.partPath.c_str());
        psb_log("[dl] fallo: %s", err.c_str());
        return false;
    }
    if (rename(sink.partPath.c_str(), sink.finalPath.c_str()) != 0) {
        remove(sink.partPath.c_str());
        err = "No se pudo renombrar el archivo descargado.";
        psb_log("[dl] fallo: %s", err.c_str());
        return false;
    }
    DlItem it;
    it.name = sink.name;
    it.bytes = res.received;
    it.url = res.finalUrl;
    g_dls.push_back(it);
    dlListSave();
    psb_log("[dl] ok: %s (%lld bytes)", sink.finalPath.c_str(), res.received);
    toast("Descargado: " + it.name + " (" + dl_format_size(it.bytes) + ")");
    return true;
}
// Puerta de cabeceras de las paginas: lo que el visor no sabe mostrar (o que pide descargarse) no se lee, se ofrece guardar.
static bool pageGate(const HttpResult& r) {
    if (r.status < 200 || r.status >= 300) return true;  // errores HTTP: se muestran como pagina
    if (dl_is_attachment(r.contentDisposition)) return false;
    return dl_is_page_type(r.contentType);
}
// Pagina de confirmacion: nombre, tipo, tamano y [Descargar] (con vista previa si es PNG/JPEG/BMP).
static void showDownloadPrompt(Browser& b, const HttpResult& r) {
    const std::string url = r.finalUrl;
    if (dl_is_streaming(r.contentType, url)) {
        setPage(b, url, infoHtml("Video por segmentos (streaming)", "Esta lista reparte el video en muchos trozos y no se puede guardar como un solo archivo.", url), 0);
        return;
    }
    g_pendRef = g_navReferer;
    g_pend.url = url;
    g_pend.name = dl_filename(url, r.contentDisposition, r.contentType);
    g_pend.type = r.contentType.empty() ? std::string("application/octet-stream") : r.contentType;
    g_pend.len = r.contentLength;
    std::string h = "<html><head><title>Descargar " + htmlEscape(g_pend.name) + "</title></head><body><h1>Descargar archivo</h1>";
    const bool isVid = r.contentType.compare(0, 6, "video/") == 0 || (looksVideoUrl(url) && urlPathLower(url).find(".m3u8") == std::string::npos);
    if (isVid) {  // video directo: primero [Reproducir] (queda seleccionado) y luego las opciones de descarga
        h += "<p><a href=\"psb:play:" + htmlEscape(url) + "\">[ Reproducir sin descargar ]</a> <a href=\"psb:dlplay:" + htmlEscape(url) +
             "\">[ Descargar y reproducir ]</a></p>";
        std::string note = videoNote(url);
        if (note.empty() && (containsCI(r.contentType, "webm") || containsCI(r.contentType, "matroska"))) note = "formato WebM/MKV: es probable que el reproductor de la PS4 no lo decodifique";
        h += "<p>El reproductor de la consola decodifica MP4 (H.264/AAC). Es experimental" + (note.empty() ? std::string() : "; " + note) + ".</p>";
    }
    if (dl_is_previewable_image(r.contentType)) h += "<p><img src=\"" + htmlEscape(url) + "\" width=\"640\" height=\"420\"></p>";
    h += "<p>Nombre: <b>" + htmlEscape(g_pend.name) + "</b></p><p>Tipo: " + htmlEscape(dl_type_label(r.contentType)) + "</p><p>Tamano: " +
         (r.contentLength >= 0 ? dl_format_size(r.contentLength) : std::string("desconocido")) + "</p><p>Se guardara en " + kDlDir + "/</p>" +
         "<p><a href=\"psb:dlgo:" + htmlEscape(url) + "\">[ Descargar ]</a> <a href=\"psb:dlback\">[ Cancelar ]</a> <a href=\"about:downloads\">[ Ver descargas ]</a></p>" +
         "<p>" + htmlEscape(url) + "</p></body></html>";
    g_skipCapture = true;
    setPage(b, url, h, 0);
    g_skipCapture = false;
    if (!b.doc.focus.empty()) b.sel = 0;  // el primer boton ([Reproducir] o [Descargar]) ya queda seleccionado: X lo activa
}
// Cuerpo POST pendiente: lo deja submitForm y lo consume load() una sola vez (recargar o volver usan GET).
static HttpPost g_post;
static bool g_hasPost = false;
static void load(Browser& b, const std::string& urlIn, int depth = 0) {
    HttpPost post;
    const bool isPost = g_hasPost;
    if (isPost) { post = std::move(g_post); g_post = HttpPost(); g_hasPost = false; }
    std::string url = urlIn, frag;
    size_t h = url.find('#');
    if (h != std::string::npos) { frag = url.substr(h + 1); url = url.substr(0, h); }
    psb_log("[nav] %s", urlIn.c_str());
    if (isInternal(url)) {
        setPage(b, url, genInternal(url), 2);
        if (!frag.empty()) jumpAnchor(b, frag);
        return;
    }
    if (dl_is_streaming("", url)) {  // .m3u8 / .mpd
        setPage(b, url, infoHtml("Video por segmentos (streaming)", "Esta lista reparte el video en muchos trozos y no se puede guardar como un solo archivo. Los .mp4 y .webm directos si se pueden descargar.", url), 0);
        return;
    }
    if (urlIsHttp(b.url) && b.url != url) g_navReferer = b.url;  // pagina desde la que se llega (Referer de videos/descargas)
    g_loading = true;
    imgw_cancel();      // las imagenes pendientes de la pagina anterior ya no importan
    imgw_pause(true);   // y el hilo de imagenes no debe quitarle la red a la navegacion
    present();
    HttpResult res;
    const bool ok = http_request(url, isPost ? &post : nullptr, res, 2 * 1024 * 1024, 5, nullptr, pageGate);
    imgw_pause(false);
    g_loading = false;
    if (!ok) {
        char code[64];
        snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(res.sceError));
        setPage(b, url, infoHtml("No se pudo cargar la pagina", std::string("Paso: ") + res.stage + " - codigo " + code, url), 0);
        toast("Error al cargar la pagina", true);
        return;
    }
    if (res.stopped) {  // archivo, imagen o video directo: pagina de confirmacion de descarga
        showDownloadPrompt(b, res);
        return;
    }
    const std::string ct = res.contentType;
    const std::string finalUrl = res.finalUrl;
    const size_t bytes = res.body.size();
    const bool truncated = res.truncated;
    const int status = res.status;
    if (ct.empty() || containsCI(ct, "html") || containsCI(ct, "text/") || containsCI(ct, "xml")) {
        const std::string cs = charset_detect(ct, res.body.substr(0, 2048));
        if (charset_to_utf8(res.body, cs)) psb_log("[charset] convertido a UTF-8 (declarado: '%s')", cs.c_str());
        else if (!cs.empty() && cs != "utf-8" && cs != "utf8") psb_log("[charset] '%s' sin soporte: se muestra tal cual", cs.c_str());
    }
    if (ct.empty() || containsCI(ct, "html")) setPage(b, finalUrl, std::move(res.body), 0);
    else if (containsCI(ct, "text/") || containsCI(ct, "json") || containsCI(ct, "xml")) setPage(b, finalUrl, std::move(res.body), 1);
    else {
        char info[220];
        snprintf(info, sizeof(info), "Tipo de contenido no soportado: %s (%zu bytes).", ct.c_str(), bytes);
        setPage(b, finalUrl, infoHtml("Contenido no soportado", info, finalUrl), 0);
    }
    if (b.kind == 0 && !b.doc.refreshUrl.empty() && b.doc.lines.size() < 6 && depth < 3) {  // <meta refresh> en paginas casi vacias
        const std::string target = urlStripFragment(urlResolve(finalUrl, b.doc.refreshUrl));
        if (urlIsHttp(target) && target != finalUrl) {
            psb_log("[refresh] -> %s", target.c_str());
            load(b, target, depth + 1);
            return;
        }
    }
    psb_log("[page] HTTP %d - %zu bytes%s - %zu lineas, %zu enlaces, %zu imagenes, %zu campos, alto %d px", status, bytes,
            truncated ? " (truncado a 2 MB)" : "", b.doc.lines.size(), b.doc.links.size(), b.doc.images.size(), b.doc.fields.size(), b.doc.height);
    if (status != 200) toast("HTTP " + std::to_string(status), true);
    else if (truncated) toast("Pagina truncada a 2 MB", true);
    if (!frag.empty()) jumpAnchor(b, frag);
}
static void navigate(Browser& b, const std::string& url, bool push) {
    if (push) {
        saveLeaving(b);
        if (b.histPos >= 0) b.hist.resize(b.histPos + 1);
        HistEntry e;
        e.req = url;
        b.hist.push_back(std::move(e));
        b.histPos = static_cast<int>(b.hist.size()) - 1;
        if (b.hist.size() > 50) { b.hist.erase(b.hist.begin()); b.histPos--; }
        for (int i = 0; i + 9 < static_cast<int>(b.hist.size()); i++) b.hist[i].html.clear();  // solo las ultimas 9 conservan su fuente
    } else if (b.histPos >= 0) {
        b.hist[b.histPos].req = url;
    }
    load(b, url);
}
static void goHistory(Browser& b, int idx) {
    if (idx < 0 || idx >= static_cast<int>(b.hist.size())) return;
    saveLeaving(b);
    b.histPos = idx;
    HistEntry& e = b.hist[idx];
    if (!e.html.empty() || e.kind == 2) {
        const int sc = e.scroll;
        const std::string fu = e.finalUrl.empty() ? e.req : e.finalUrl;
        setPage(b, fu, e.kind == 2 ? genInternal(fu) : std::move(e.html), e.kind);
        e.html.clear();
        b.scrollTarget = std::min(sc, maxScroll(b));
        b.scrollY = static_cast<float>(b.scrollTarget);
    } else {
        const int sc = e.scroll;
        load(b, e.req);
        b.scrollTarget = std::min(sc, maxScroll(b));
        b.scrollY = static_cast<float>(b.scrollTarget);
    }
    g_dirty = true;
}
static void goBack(Browser& b) { if (b.histPos > 0) goHistory(b, b.histPos - 1); else toast("No hay pagina anterior"); }
static void goForward(Browser& b) { if (b.histPos >= 0 && b.histPos + 1 < static_cast<int>(b.hist.size())) goHistory(b, b.histPos + 1); else toast("No hay pagina siguiente"); }
static void reload(Browser& b) { if (b.histPos >= 0) { const int sc = b.scrollTarget; load(b, b.hist[b.histPos].req); scrollTo(b, sc, true); } }
static Browser* newTab(const std::string& url, bool activate) {
    Browser* t = new Browser();
    t->id = g_nextTabId++;
    g_tabs.push_back(t);
    if (activate) g_cur = static_cast<int>(g_tabs.size()) - 1;
    navigate(*t, url, true);
    g_dirty = true;
    return t;
}
static void closeTab(int i) {
    if (i < 0 || i >= static_cast<int>(g_tabs.size())) return;
    if (!g_tabs[i]->url.empty() && g_tabs[i]->url != "about:blank") {
        g_closedTabs.push_back(g_tabs[i]->url);
        if (g_closedTabs.size() > 15) g_closedTabs.erase(g_closedTabs.begin());
    }
    delete g_tabs[i];
    g_tabs.erase(g_tabs.begin() + i);
    if (g_tabs.empty()) { newTab(homeUrl(), true); return; }
    if (i < g_cur) g_cur--;
    if (g_cur >= static_cast<int>(g_tabs.size())) g_cur = static_cast<int>(g_tabs.size()) - 1;
    g_dirty = true;
}
static void activateTab(int i) {
    if (i < 0 || i >= static_cast<int>(g_tabs.size()) || i == g_cur) return;
    g_cur = i;
    if (B.needRelayout) relayout(B);
    g_dirty = true;
}
static void relayoutAll() {
    for (Browser* t : g_tabs) t->needRelayout = true;
    if (B.needRelayout) relayout(B);
}
static void toggleBookmark(Browser& b) {
    if (b.url.empty() || isInternal(b.url)) { toast("Esta pagina no se puede guardar", true); return; }
    const int i = bookmarkIndex(b.url);
    if (i >= 0) { g_bookmarks.erase(g_bookmarks.begin() + i); bookmarksSave(); toast("Marcador eliminado"); }
    else { g_bookmarks.push_back({b.url, b.doc.title}); bookmarksSave(); toast("Marcador guardado (" + std::to_string(g_bookmarks.size()) + ")"); }
    g_dirty = true;
}
// Acciones internas (enlaces psb:...). Devuelve true si la accion se reconocio.
// Descarga en primer plano; con thenPlay, al terminar abre el video descargado en el reproductor.
static bool startDownload(Browser& b, const std::string& url, bool thenPlay) {
    if (g_pend.url != url) { g_pend = DlPending(); g_pend.url = url; }
    std::string err;
    if (runDownload(url, err)) {
        const std::string saved = g_dls.empty() ? std::string() : g_dls.back().name;
        navigate(b, "about:downloads", true);
        if (thenPlay && !saved.empty()) playVideo(std::string(kDlDir) + "/" + saved, saved, std::string());
        return true;
    }
    std::string h = "<html><head><title>Descarga fallida</title></head><body><h1>Descarga fallida</h1><p>" + htmlEscape(err) + "</p><p>" +
                    htmlEscape(url) + "</p><p><a href=\"psb:" + (thenPlay ? "dlplay:" : "dlgo:") + htmlEscape(url) +
                    "\">[ Reintentar ]</a> <a href=\"psb:dlback\">[ Volver ]</a></p></body></html>";
    g_skipCapture = true;
    setPage(b, b.url, h, 0);
    g_skipCapture = false;
    if (!b.doc.focus.empty()) b.sel = 0;
    toast(err.size() > 90 ? err.substr(0, 90) : err, true);
    return true;
}
static void runBench();
static bool g_vidFastPrev0 = false;  // pedir un repintado completo (bandas negras) en el siguiente fotograma de video
static bool handleAction(Browser& b, const std::string& a) {
    bool cfg = true, relay = false;
    if (a == "sens-1") g_cfg.mouseSens -= 1.0f;
    else if (a == "sens-") g_cfg.mouseSens -= 0.5f;
    else if (a == "sens+") g_cfg.mouseSens += 0.5f;
    else if (a == "sens+1") g_cfg.mouseSens += 1.0f;
    else if (a == "accel") g_cfg.mouseAccel = !g_cfg.mouseAccel;
    else if (a == "wheelinv") g_cfg.wheelInvert = !g_cfg.wheelInvert;
    else if (a == "wheel-") g_cfg.wheelStep--;
    else if (a == "wheel+") g_cfg.wheelStep++;
    else if (a == "theme") { g_cfg.dark = !g_cfg.dark; g_th = g_cfg.dark ? themeDark() : themeLight(); }
    else if (a == "zoom-") { g_cfg.zoom -= 10; relay = true; }
    else if (a == "zoom+") { g_cfg.zoom += 10; relay = true; }
    else if (a == "pw-") { g_cfg.pageW -= 100; relay = true; }
    else if (a == "pw+") { g_cfg.pageW += 100; relay = true; }
    else if (a == "smooth") g_cfg.smooth = !g_cfg.smooth;
    else if (a == "images") g_cfg.images = !g_cfg.images;
    else if (a == "homehere") {
        const int prev = b.histPos - 1;
        if (prev >= 0) g_cfg.home = b.hist[prev].req; else g_cfg.home.clear();
    } else if (a == "homereset") g_cfg.home.clear();
    else if (a == "quit") { quitAsk(); return true; }
    else if (a == "vdec") g_cfg.vdec = g_cfg.vdec == 0 ? 2 : (g_cfg.vdec == 2 ? 3 : 0);
    else if (a == "vhalf") g_cfg.vhalf = !g_cfg.vhalf;
    else if (a == "search") g_cfg.search = (g_cfg.search + 1) % 5;
    else if (a == "ua") g_cfg.ua = (g_cfg.ua + 1) % 5;
    else if (a == "clearhist") { g_history.clear(); historySave(); toast("Historial borrado"); }
    else if (a == "vscale") g_cfg.vscale = (g_cfg.vscale + 1) % 3;
    else if (a == "vpart") { g_cfg.vpart = !g_cfg.vpart; g_vidFastPrev0 = true; }
    else if (a == "bench") { runBench(); navigate(b, "about:bench", true); return true; }
    else if (a == "vol-") g_cfg.volume -= 10;
    else if (a == "vol+") g_cfg.volume += 10;
    else if (a.compare(0, 5, "play:") == 0) {  // psb:play:<url> = ver sin descargar (streaming por rangos)
        const std::string url = a.substr(5);
        const std::string ref = g_pend.url == url ? g_pendRef : (isInternal(b.url) ? g_lastVidsPage : b.url);
        playVideo(url, titleFromUrl(url), urlIsHttp(ref) ? ref : std::string());
        return true;
    } else if (a.compare(0, 9, "playfile:") == 0) {  // psb:playfile:N = reproducir una descarga
        const size_t i = static_cast<size_t>(atoi(a.c_str() + 9));
        if (i < g_dls.size()) playVideo(std::string(kDlDir) + "/" + g_dls[i].name, g_dls[i].name, std::string());
        return true;
    }
    else if (a.compare(0, 5, "dlgo:") == 0) return startDownload(b, a.substr(5), false);  // psb:dlgo:<url> = descargar (lo crea showDownloadPrompt)
    else if (a.compare(0, 7, "dlplay:") == 0) return startDownload(b, a.substr(7), true);   // descargar y reproducir
    else if (a == "dlback") {
        if (b.histPos > 0) goBack(b); else navigate(b, homeUrl(), true);
        return true;
    } else if (a.compare(0, 6, "deldl:") == 0) {
        const size_t i = static_cast<size_t>(atoi(a.c_str() + 6));
        if (i < g_dls.size()) {
            remove((std::string(kDlDir) + "/" + g_dls[i].name).c_str());
            g_dls.erase(g_dls.begin() + i);
            dlListSave();
        }
        cfg = false;
    }
    else if (a.compare(0, 6, "delbm:") == 0) {
        size_t i = static_cast<size_t>(atoi(a.c_str() + 6));
        if (i < g_bookmarks.size()) { g_bookmarks.erase(g_bookmarks.begin() + i); bookmarksSave(); }
        cfg = false;
    } else return false;
    clampCfg();
    if (cfg) configSave();
    if (relay) { relayoutAll(); if (isInternal(b.url)) refreshInternal(b); }
    else refreshInternal(b);
    g_dirty = true;
    return true;
}
static std::string focusTarget(const Browser& b, int fi) {
    if (fi < 0 || fi >= static_cast<int>(b.doc.focus.size())) return std::string();
    const Focusable& F = b.doc.focus[fi];
    if (F.kind != 0 || F.ref < 0 || F.ref >= static_cast<int>(b.doc.links.size())) return std::string();
    const std::string& h = b.doc.links[F.ref];
    if (h.compare(0, 4, "psb:") == 0) return std::string();
    if (h.compare(0, 6, "about:") == 0) return h;
    const std::string full = urlResolve(b.url, h);
    const std::string real = urlUnwrapSearchRedirect(full);
    return real.empty() ? full : real;
}
static void followLink(Browser& b, int id, bool newTabFlag) {
    if (id < 0 || id >= static_cast<int>(b.doc.links.size())) return;
    const std::string href = b.doc.links[id];
    if (href.compare(0, 4, "psb:") == 0) { handleAction(b, href.substr(4)); return; }
    if (href.compare(0, 6, "about:") == 0) { if (newTabFlag) newTab(href, true); else navigate(b, href, true); return; }
    std::string lower = href.substr(0, 11);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.compare(0, 11, "javascript:") == 0 || lower.compare(0, 7, "mailto:") == 0 || lower.compare(0, 4, "tel:") == 0) {
        toast("Enlace no soportado: " + href.substr(0, 80), true);
        return;
    }
    std::string full = urlResolve(b.url, href);
    const std::string real = urlUnwrapSearchRedirect(full);
    if (!real.empty()) full = real;
    if (!urlIsHttp(full)) { toast("Esquema no soportado: " + full.substr(0, 80), true); return; }
    // Ancla del mismo documento: solo saltar.
    if (!newTabFlag && urlStripFragment(full) == urlStripFragment(b.url)) {
        const size_t h = full.find('#');
        if (h != std::string::npos) { if (!jumpAnchor(b, full.substr(h + 1))) toast("No se encontro el ancla", true); }
        return;
    }
    if (newTabFlag) newTab(full, true); else navigate(b, full, true);
}
// ---- formularios
static void submitForm(Browser& b, int formIdx, int pressedField) {
    if (formIdx < 0 || formIdx >= static_cast<int>(b.doc.forms.size())) return;
    const Form& f = b.doc.forms[formIdx];
    std::string action = f.action.empty() ? b.url : urlResolve(b.url, f.action);
    action = urlStripFragment(action);
    if (!f.post) {  // con GET los campos sustituyen a la query de la accion; con POST la query de la accion se conserva
        const size_t q = action.find('?');
        if (q != std::string::npos) action = action.substr(0, q);
    }
    std::string query;
    for (size_t i = 0; i < b.doc.fields.size(); i++) {
        const FormField& ff = b.doc.fields[i];
        if (ff.form != formIdx || ff.name.empty()) continue;
        bool include = false;
        if (ff.type == "hidden" || ff.type == "text" || ff.type == "password") include = true;
        else if (ff.type == "checkbox" || ff.type == "radio") include = ff.checked;
        else if (ff.type == "submit" || ff.type == "button") include = static_cast<int>(i) == pressedField;
        if (!include) continue;
        std::string v = ff.value;
        if ((ff.type == "checkbox" || ff.type == "radio") && v.empty()) v = "on";
        if (!query.empty()) query += "&";
        query += urlPercentEncode(ff.name) + "=" + urlPercentEncode(v);
    }
    if (!urlIsHttp(action)) { toast("Accion de formulario no soportada", true); return; }
    if (f.post) {
        g_post.body = query;
        g_post.contentType = "application/x-www-form-urlencoded";
        g_hasPost = true;
        psb_log("[form] POST %s (%zu bytes)", action.c_str(), query.size());
        navigate(b, action, true);
        g_hasPost = false;  // por si navigate no llego a cargar (p. ej. URL interna)
        return;
    }
    navigate(b, query.empty() ? action : action + "?" + query, true);
}
// ---- teclado en pantalla (mando) ----
static Osk g_osk;
static int g_oskTarget = 0;  // 0 ninguno, 1 barra de direcciones, 2 campo de formulario
static void oskOpenField(Browser& b, int fieldIdx);
static void activateFocus(Browser& b, int fi) {
    if (fi < 0 || fi >= static_cast<int>(b.doc.focus.size())) return;
    const Focusable F = b.doc.focus[fi];
    b.sel = fi;
    if (F.kind == 0) { b.editField = -1; followLink(b, F.ref, false); return; }
    if (F.ref < 0 || F.ref >= static_cast<int>(b.doc.fields.size())) return;
    FormField& f = b.doc.fields[F.ref];
    if (f.type == "submit" || f.type == "button" || f.type == "reset") {
        b.editField = -1;
        if (f.type == "reset") { toast("Restablecer no esta soportado", true); return; }
        submitForm(b, f.form, F.ref);
    } else if (f.type == "checkbox") {
        f.checked = !f.checked;
        g_dirty = true;
    } else if (f.type == "radio") {
        for (auto& o : b.doc.fields)
            if (o.type == "radio" && o.form == f.form && o.name == f.name) o.checked = false;
        f.checked = true;
        g_dirty = true;
    } else {  // text, password, textarea
        b.editField = F.ref;
        g_dirty = true;
    }
}
// Campo de texto siguiente al indicado (para Tab dentro de un formulario); -1 si no hay.
static int nextTextField(const Browser& b, int from) {
    for (size_t k = from + 1; k < b.doc.fields.size(); k++) {
        const std::string& t = b.doc.fields[k].type;
        if (t == "text" || t == "password") return static_cast<int>(k);
    }
    return -1;
}
// ------------------------------------------------------------------ interfaz: pestanas, barra, omnibox
enum UiKind { UI_TAB = 1, UI_TABCLOSE, UI_NEWTAB, UI_BACK, UI_FWD, UI_RELOAD, UI_HOME, UI_OMNI, UI_STAR, UI_BMARKS, UI_MENU, UI_SBAR, UI_POWER };
struct UiRect { int x, y, w, h, kind, arg; };
static std::vector<UiRect> g_ui;
static int g_hoverKey = -1;
static bool g_caret = true;
static int g_thumbY = 0, g_thumbH = 0;
static void addUi(int x, int y, int w, int h, int kind, int arg = 0) { g_ui.push_back({x, y, w, h, kind, arg}); }
static int uiAt(int x, int y) {
    for (int i = static_cast<int>(g_ui.size()) - 1; i >= 0; i--) {
        const UiRect& r = g_ui[i];
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
    }
    return -1;
}
static int uiKey(int idx) { return idx < 0 ? -1 : g_ui[idx].kind * 1000 + g_ui[idx].arg; }
static const char* uiTip(int kind) {
    switch (kind) {
        case UI_BACK: return "Atras (Backspace)";
        case UI_FWD: return "Adelante (Alt+Der)";
        case UI_RELOAD: return "Recargar (F5)";
        case UI_HOME: return "Inicio";
        case UI_STAR: return "Anadir o quitar marcador (Ctrl+D)";
        case UI_BMARKS: return "Marcadores";
        case UI_MENU: return "Configuracion (F10)";
        case UI_POWER: return "Salir del navegador (Ctrl+Q)";
        case UI_NEWTAB: return "Pestana nueva (Ctrl+T)";
        default: return "";
    }
}
static int baseFor(int y, int h, int font, int px) { return y + h / 2 + (te_ascent(font, px) - te_descent(font, px)) / 2; }
static std::string hostOf(const std::string& u) {
    size_t s = u.find("://");
    s = (s == std::string::npos) ? 0 : s + 3;
    size_t e = u.find_first_of("/?#", s);
    return u.substr(s, e == std::string::npos ? std::string::npos : e - s);
}
static std::string tabTitle(const Browser& b) {
    if (!b.doc.title.empty()) return b.doc.title;
    if (b.url.empty() || b.url == "about:blank") return "Pestana nueva";
    if (isInternal(b.url)) return b.url == "about:settings" ? "Configuracion" : (b.url == "about:bookmarks" ? "Marcadores" : (b.url == "about:history" ? "Historial" : (b.url == "about:shortcuts" ? "Atajos" : "Inicio")));
    return hostOf(b.url);
}
static void startUrlEdit(Browser& b, const std::string& initial, bool fresh) {
    b.urlEdit = true;
    b.urlFresh = fresh;
    b.urlText = initial;
    b.editField = -1;
    g_dirty = true;
}
static void oskOpenField(Browser& b, int fieldIdx) {
    if (fieldIdx < 0 || fieldIdx >= static_cast<int>(b.doc.fields.size())) return;
    const FormField& f = b.doc.fields[fieldIdx];
    osk_open(g_osk, f.placeholder.empty() ? std::string("Escribir en el campo") : f.placeholder, f.value, f.type == "password");
    g_oskTarget = 2;
    g_dirty = true;
}
static void oskOpenUrl(Browser& b) {
    startUrlEdit(b, "", true);
    osk_open(g_osk, "Direccion o busqueda", "", false);
    g_oskTarget = 1;
    g_dirty = true;
}
static void oskClose() {
    g_osk.open = false;
    g_oskTarget = 0;
    g_dirty = true;
}
// Aplica el resultado del teclado al destino (barra o campo).
static void oskApply(Browser& b, OskResult r) {
    if (r == OSK_NONE) return;
    if (r == OSK_CHANGED || r == OSK_SUBMITTED) {
        if (g_oskTarget == 1) { b.urlText = g_osk.text; b.urlFresh = false; }
        else if (g_oskTarget == 2 && b.editField >= 0 && b.editField < static_cast<int>(b.doc.fields.size()))
            b.doc.fields[b.editField].value = g_osk.text;
    }
    g_dirty = true;
    if (r == OSK_SUBMITTED) {
        const int target = g_oskTarget;
        oskClose();
        if (target == 1) {
            const std::string u = urlFromUserInput(b.urlText);
            b.urlEdit = false;
            if (!u.empty()) navigate(b, u, true);
        } else if (target == 2) {
            const int form = (b.editField >= 0 && b.editField < static_cast<int>(b.doc.fields.size())) ? b.doc.fields[b.editField].form : -1;
            b.editField = -1;
            if (form >= 0) submitForm(b, form, -1);
        }
    } else if (r == OSK_CANCELLED) {
        oskClose();
        b.urlEdit = false;
        b.editField = -1;
    }
}
// ---- buscar en la pagina (Ctrl+F)
struct FindState {
    bool active = false;
    std::string q, qLower;
    std::vector<int> hits;  // indices de linea con coincidencia
    int cur = -1;
    unsigned gen = ~0u;     // docGen con el que se calcularon los hits
    int genLines = -1;
};
static FindState g_find;
static std::string asciiLower(const std::string& t) {
    std::string o = t;
    for (char& c : o) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return o;
}
static void findRecompute(Browser& b, bool keepCur) {
    g_find.qLower = asciiLower(g_find.q);
    const int old = g_find.cur >= 0 && g_find.cur < static_cast<int>(g_find.hits.size()) ? g_find.hits[g_find.cur] : -1;
    g_find.hits.clear();
    g_find.cur = -1;
    g_find.gen = b.docGen;
    g_find.genLines = static_cast<int>(b.doc.lines.size());
    if (g_find.qLower.empty()) return;
    for (size_t li = 0; li < b.doc.lines.size(); li++) {
        std::string t;
        for (const Span& sp : b.doc.lines[li].spans) if (sp.kind == SK_TEXT) { t += sp.text; t += ' '; }
        if (asciiLower(t).find(g_find.qLower) != std::string::npos) g_find.hits.push_back(static_cast<int>(li));
    }
    if (!g_find.hits.empty()) {
        g_find.cur = 0;
        if (keepCur && old >= 0)
            for (size_t i = 0; i < g_find.hits.size(); i++) if (g_find.hits[i] >= old) { g_find.cur = static_cast<int>(i); break; }
    }
}
static void findGoto(Browser& b, int idx) {
    if (g_find.hits.empty()) return;
    const int n = static_cast<int>(g_find.hits.size());
    g_find.cur = ((idx % n) + n) % n;
    const DocLine& L = b.doc.lines[g_find.hits[g_find.cur]];
    scrollTo(b, std::max(0, std::min(maxScroll(b), L.y - VIEW_H / 3)), false);
    g_dirty = true;
}
static void findStep(Browser& b, int dir) {
    if (g_find.gen != b.docGen || g_find.genLines != static_cast<int>(b.doc.lines.size())) findRecompute(b, true);
    findGoto(b, g_find.cur + dir);
}
static void findClose() { g_find.active = false; g_dirty = true; }
static void drawFindBar(Canvas& cv, Browser& b) {
    if (!g_find.active) return;
    if (g_find.gen != b.docGen || g_find.genLines != static_cast<int>(b.doc.lines.size())) findRecompute(b, true);
    const Theme& th = g_th;
    const int bh = 44, by = H - bh;
    cv.rect(0, by, W, bh, th.imgBg);
    cv.rect(0, by, W, 1, th.rule);
    std::string cnt = g_find.q.empty() ? "" : (g_find.hits.empty() ? "sin resultados" : std::to_string(g_find.cur + 1) + " de " + std::to_string(g_find.hits.size()));
    te_draw(cv, 20, by + 29, "Buscar:", FONT_SANS, 18, true, th.text);
    te_draw(cv, 100, by + 29, g_find.q + (g_caret ? "|" : ""), FONT_SANS, 18, false, th.text);
    te_draw(cv, 700, by + 29, cnt, FONT_SANS, 16, false, g_find.hits.empty() && !g_find.q.empty() ? 0xFFE06060u : th.dim);
    te_draw(cv, 1000, by + 29, "Intro/F3 siguiente - Mayus+F3 anterior - Esc cerrar", FONT_SANS, 14, false, th.dim);
}
static RenderCtx makeRc(Browser& b) {
    RenderCtx rc{&g_th, 0, VIEW_Y, W - SBAR_W, VIEW_H, contentLeft(), static_cast<int>(b.scrollY + 0.5f)};
    rc.focus = b.sel;
    rc.hover = b.hover;
    rc.editField = b.editField;
    rc.caretOn = g_caret;
    if (g_find.active && !g_find.qLower.empty()) {
        rc.find = &g_find.qLower;
        rc.findLine = g_find.cur >= 0 && g_find.cur < static_cast<int>(g_find.hits.size()) ? g_find.hits[g_find.cur] : -1;
    }
    return rc;
}
// ------------------------------------------------------------------ menu contextual (clic derecho / tecla Menu / boton 11 del mando)
enum CtxAct {
    CA_SEP = 0, CA_OPEN_LINK, CA_OPEN_LINK_TAB, CA_COPY_LINK, CA_DL_LINK, CA_OPEN_IMG, CA_OPEN_IMG_TAB, CA_COPY_IMG, CA_DL_IMG,
    CA_BACK, CA_FWD, CA_RELOAD, CA_COPY_URL, CA_BOOKMARK, CA_PASTE_GO, CA_EDIT_URL, CA_NEW_TAB, CA_CLOSE_TAB, CA_DUP_TAB,
    CA_CLOSE_OTHERS, CA_REOPEN_TAB, CA_DOWNLOADS, CA_HOME, CA_BOOKMARKS, CA_VIDEOS, CA_PLAY_LINK, CA_QUIT
};
struct CtxItem { std::string label; int act; std::string arg; int tab; bool enabled; };
struct CtxMenu {
    bool open = false;
    int x = 0, y = 0, w = 0, h = 0, sel = -1;
    std::vector<CtxItem> items;
};
static CtxMenu g_ctx;
static const int CTX_ITEM_H = 34, CTX_SEP_H = 11, CTX_PAD = 6, CTX_PX = 17;
static void ctxAdd(const std::string& label, int act, const std::string& arg = std::string(), bool en = true, int tab = -1) {
    g_ctx.items.push_back({label, act, arg, tab, en});
}
static void ctxSep() {
    if (!g_ctx.items.empty() && g_ctx.items.back().act != CA_SEP) ctxAdd("", CA_SEP, "", false);
}
static int ctxItemY(int i) {  // y del borde superior del elemento i (en pantalla)
    int y = g_ctx.y + CTX_PAD;
    for (int k = 0; k < i; k++) y += g_ctx.items[k].act == CA_SEP ? CTX_SEP_H : CTX_ITEM_H;
    return y;
}
static void ctxLayout() {
    while (!g_ctx.items.empty() && g_ctx.items.back().act == CA_SEP) g_ctx.items.pop_back();
    int w = 220, h = CTX_PAD * 2;
    for (const CtxItem& it : g_ctx.items) {
        if (it.act == CA_SEP) { h += CTX_SEP_H; continue; }
        h += CTX_ITEM_H;
        w = std::max(w, te_width(it.label, FONT_SANS, CTX_PX, false) + 44);
    }
    g_ctx.w = w;
    g_ctx.h = h;
    g_ctx.x = std::max(4, std::min(g_ctx.x, W - w - 4));  // que quepa en pantalla
    g_ctx.y = std::max(4, std::min(g_ctx.y, H - h - 4));
}
// Elemento bajo (x,y): indice, -2 si esta en el menu pero no sobre un elemento (separador/borde), -1 fuera del menu.
static int ctxHit(int x, int y) {
    if (!g_ctx.open || x < g_ctx.x || x >= g_ctx.x + g_ctx.w || y < g_ctx.y || y >= g_ctx.y + g_ctx.h) return -1;
    for (size_t i = 0; i < g_ctx.items.size(); i++) {
        if (g_ctx.items[i].act == CA_SEP) continue;
        const int iy = ctxItemY(static_cast<int>(i));
        if (y >= iy && y < iy + CTX_ITEM_H) return static_cast<int>(i);
    }
    return -2;
}
static void ctxMove(int dir) {  // salta separadores y elementos desactivados
    const int n = static_cast<int>(g_ctx.items.size());
    if (n == 0) return;
    int i = g_ctx.sel;
    for (int k = 0; k < n + 1; k++) {
        i = i < 0 ? (dir > 0 ? 0 : n - 1) : (i + dir + n) % n;
        if (g_ctx.items[i].act != CA_SEP && g_ctx.items[i].enabled) { g_ctx.sel = i; g_dirty = true; return; }
    }
}
// 'forceFocus' >= 0: usar ese elemento de doc.focus (teclado/mando) en vez de lo que haya bajo el puntero.
static void ctxOpen(int x, int y, int forceFocus = -1) {
    Browser& b = B;
    g_ctx = CtxMenu();
    g_ctx.x = x;
    g_ctx.y = y;
    const int ui = forceFocus >= 0 ? -1 : uiAt(x, y);
    const int kind = ui >= 0 ? g_ui[ui].kind : 0;
    const bool canBack = b.histPos > 0, canFwd = b.histPos >= 0 && b.histPos + 1 < static_cast<int>(b.hist.size());
    const bool real = !b.url.empty() && !isInternal(b.url);
    if (kind == UI_TAB || kind == UI_TABCLOSE) {
        const int ti = g_ui[ui].arg;
        ctxAdd("Pestana nueva", CA_NEW_TAB);
        ctxAdd("Duplicar pestana", CA_DUP_TAB, "", true, ti);
        ctxSep();
        ctxAdd("Cerrar pestana", CA_CLOSE_TAB, "", true, ti);
        ctxAdd("Cerrar las demas", CA_CLOSE_OTHERS, "", g_tabs.size() > 1, ti);
        ctxAdd("Reabrir pestana cerrada", CA_REOPEN_TAB, "", !g_closedTabs.empty());
    } else if (kind == UI_OMNI) {
        const bool have = !b.url.empty() && !isInternal(b.url);
        ctxAdd("Copiar direccion", CA_COPY_URL, b.url, have);
        ctxAdd("Pegar y buscar", CA_PASTE_GO, "", !g_clip.empty());
        ctxAdd("Editar direccion", CA_EDIT_URL);
    } else {
        std::string linkUrl, imgUrl;
        const RenderCtx rc = makeRc(b);
        int fi = forceFocus >= 0 ? forceFocus : (ui < 0 ? hitFocus(b.doc, rc, x, y) : -1);
        if (fi >= 0 && fi < static_cast<int>(b.doc.focus.size()) && b.doc.focus[fi].kind == 0) linkUrl = focusTarget(b, fi);
        const int ii = (forceFocus >= 0 || ui >= 0) ? -1 : hitImage(b.doc, rc, x, y);
        if (ii >= 0) {
            const std::string full = urlResolve(b.url, b.doc.images[ii].src);
            if (urlIsHttp(full)) imgUrl = full;
        }
        if (!linkUrl.empty() && (urlIsHttp(linkUrl) || isInternal(linkUrl))) {
            ctxAdd("Abrir enlace", CA_OPEN_LINK, linkUrl);
            ctxAdd("Abrir en pestana nueva", CA_OPEN_LINK_TAB, linkUrl);
            ctxAdd("Copiar direccion del enlace", CA_COPY_LINK, linkUrl);
            if (urlIsHttp(linkUrl) && looksVideoUrl(linkUrl)) ctxAdd("Reproducir video", CA_PLAY_LINK, linkUrl);
            if (urlIsHttp(linkUrl)) ctxAdd("Descargar enlace", CA_DL_LINK, linkUrl);
            ctxSep();
        }
        if (!imgUrl.empty()) {
            ctxAdd("Abrir imagen", CA_OPEN_IMG, imgUrl);
            ctxAdd("Abrir imagen en pestana nueva", CA_OPEN_IMG_TAB, imgUrl);
            ctxAdd("Copiar direccion de la imagen", CA_COPY_IMG, imgUrl);
            ctxAdd("Guardar imagen", CA_DL_IMG, imgUrl);
            ctxSep();
        }
        ctxAdd("Atras", CA_BACK, "", canBack);
        ctxAdd("Adelante", CA_FWD, "", canFwd);
        ctxAdd("Recargar", CA_RELOAD);
        ctxSep();
        ctxAdd("Copiar direccion de la pagina", CA_COPY_URL, b.url, real);
        ctxAdd(bookmarkIndex(b.url) >= 0 ? "Quitar marcador" : "Anadir marcador", CA_BOOKMARK, "", real);
        ctxAdd("Pegar y buscar", CA_PASTE_GO, "", !g_clip.empty());
        ctxSep();
        ctxAdd("Pestana nueva", CA_NEW_TAB);
        ctxAdd("Reabrir pestana cerrada", CA_REOPEN_TAB, "", !g_closedTabs.empty());
        ctxAdd("Marcadores", CA_BOOKMARKS);
        ctxAdd("Descargas", CA_DOWNLOADS);
        ctxAdd("Videos de la pagina (F8)", CA_VIDEOS, "", !g_lastVids.empty());
        ctxAdd("Inicio", CA_HOME);
        ctxSep();
        ctxAdd("Salir del navegador", CA_QUIT);
    }
    g_ctx.open = true;
    ctxLayout();
    g_ctx.sel = forceFocus >= 0 ? -1 : ctxHit(g_mx, g_my);
    if (g_ctx.sel == -2) g_ctx.sel = -1;
    if (forceFocus >= 0) ctxMove(1);
    g_dirty = true;
}
static void reopenClosedTab() {
    if (g_closedTabs.empty()) { toast("No hay pestanas cerradas", true); return; }
    const std::string u = g_closedTabs.back();
    g_closedTabs.pop_back();
    newTab(u, true);
}
static void ctxRun(int i) {
    if (i < 0 || i >= static_cast<int>(g_ctx.items.size())) return;
    const CtxItem it = g_ctx.items[i];
    if (!it.enabled || it.act == CA_SEP) return;
    g_ctx.open = false;
    g_dirty = true;
    Browser& b = B;
    switch (it.act) {
        case CA_OPEN_LINK: case CA_OPEN_IMG: navigate(b, it.arg, true); break;
        case CA_OPEN_LINK_TAB: case CA_OPEN_IMG_TAB: newTab(it.arg, true); break;
        case CA_COPY_LINK: case CA_COPY_IMG: case CA_COPY_URL: clipSet(it.arg); break;
        case CA_DL_LINK: case CA_DL_IMG: handleAction(b, "dlgo:" + it.arg); break;
        case CA_BACK: goBack(b); break;
        case CA_FWD: goForward(b); break;
        case CA_RELOAD: reload(b); break;
        case CA_BOOKMARK: toggleBookmark(b); break;
        case CA_PASTE_GO: { const std::string u = urlFromUserInput(g_clip); if (!u.empty()) navigate(b, u, true); break; }
        case CA_EDIT_URL: startUrlEdit(b, isInternal(b.url) ? "" : b.url, true); break;
        case CA_NEW_TAB: { Browser* t = newTab("about:blank", true); startUrlEdit(*t, "", false); break; }
        case CA_CLOSE_TAB: closeTab(it.tab); break;
        case CA_DUP_TAB: if (it.tab >= 0 && it.tab < static_cast<int>(g_tabs.size())) newTab(g_tabs[it.tab]->url, true); break;
        case CA_CLOSE_OTHERS:
            if (it.tab >= 0 && it.tab < static_cast<int>(g_tabs.size())) {
                Browser* keep = g_tabs[it.tab];
                for (size_t k = 0; k < g_tabs.size();) {
                    if (g_tabs[k] == keep) { k++; continue; }
                    delete g_tabs[k];
                    g_tabs.erase(g_tabs.begin() + k);
                }
                g_cur = 0;
                g_dirty = true;
            }
            break;
        case CA_REOPEN_TAB: reopenClosedTab(); break;
        case CA_DOWNLOADS: navigate(b, "about:downloads", true); break;
        case CA_BOOKMARKS: navigate(b, "about:bookmarks", true); break;
        case CA_HOME: navigate(b, homeUrl(), true); break;
        case CA_VIDEOS: navigate(b, "about:videos", true); break;
        case CA_PLAY_LINK: handleAction(b, "play:" + it.arg); break;
        case CA_QUIT: quitAsk(); break;
        default: break;
    }
}
// Abre el menu para el elemento con foco (teclado/mando), cerca de ese elemento.
static void ctxOpenAtFocus() {
    Browser& b = B;
    int x = contentLeft() + 60, y = VIEW_Y + 80;
    if (b.sel >= 0 && b.sel < static_cast<int>(b.focusY.size()) && b.focusY[b.sel] >= 0) {
        y = VIEW_Y + b.focusY[b.sel] - static_cast<int>(b.scrollY) + 24;
        y = std::max(VIEW_Y + 20, std::min(H - 60, y));
    }
    if (g_mouseOk) { x = g_mx; y = g_my; }
    ctxOpen(x, y, b.sel >= 0 ? b.sel : -1);
}
static void drawCtx(Canvas& cv) {
    if (!g_ctx.open) return;
    const Theme& th = g_th;
    cv.rrect(g_ctx.x + 3, g_ctx.y + 4, g_ctx.w, g_ctx.h, 10, 0x55000000);  // sombra
    cv.rrect(g_ctx.x - 1, g_ctx.y - 1, g_ctx.w + 2, g_ctx.h + 2, 10, th.rule);
    cv.rrect(g_ctx.x, g_ctx.y, g_ctx.w, g_ctx.h, 9, th.toolbar);
    for (size_t i = 0; i < g_ctx.items.size(); i++) {
        const CtxItem& it = g_ctx.items[i];
        const int iy = ctxItemY(static_cast<int>(i));
        if (it.act == CA_SEP) { cv.rect(g_ctx.x + 10, iy + CTX_SEP_H / 2, g_ctx.w - 20, 1, th.rule); continue; }
        if (static_cast<int>(i) == g_ctx.sel && it.enabled) cv.rrect(g_ctx.x + 4, iy, g_ctx.w - 8, CTX_ITEM_H, 6, th.focusBg);
        te_draw(cv, g_ctx.x + 18, baseFor(iy, CTX_ITEM_H, FONT_SANS, CTX_PX), it.label, FONT_SANS, CTX_PX, false, it.enabled ? th.text : th.dim);
    }
}
static void drawTabbar(Canvas& cv, Browser& cur) {
    const Theme& th = g_th;
    cv.rect(0, 0, W, TABBAR_H, th.tabbar);
    const int n = static_cast<int>(g_tabs.size());
    const int tw = std::min(260, (W - 80) / std::max(1, n));
    int x = 8;
    (void)cur;
    for (int i = 0; i < n; i++) {
        const bool act = i == g_cur;
        const bool hov = g_hoverKey == UI_TAB * 1000 + i || g_hoverKey == UI_TABCLOSE * 1000 + i;
        if (act) cv.rrect(x, 6, tw, TABBAR_H - 6 + 10, 9, th.tabActive);
        else if (hov) cv.rrect(x, 6, tw, TABBAR_H - 6, 9, th.tabHover);
        std::string title = te_fit(tabTitle(*g_tabs[i]), tw - 56, FONT_SANS, 15, false);
        te_draw(cv, x + 14, baseFor(6, TABBAR_H - 6, FONT_SANS, 15), title, FONT_SANS, 15, false, act ? th.text : th.dim);
        const float cx = static_cast<float>(x + tw - 18), cy = static_cast<float>(6 + (TABBAR_H - 6) / 2);
        if (g_hoverKey == UI_TABCLOSE * 1000 + i) cv.circle(cx, cy, 10, th.tabHover == th.tabActive ? th.rule : th.rule);
        cv.line(cx - 4, cy - 4, cx + 4, cy + 4, 1.8f, act ? th.text : th.dim);
        cv.line(cx - 4, cy + 4, cx + 4, cy - 4, 1.8f, act ? th.text : th.dim);
        addUi(x, 6, tw, TABBAR_H - 6, UI_TAB, i);
        addUi(x + tw - 32, 6, 28, TABBAR_H - 6, UI_TABCLOSE, i);
        x += tw + 2;
    }
    const float px = static_cast<float>(x + 18), py = static_cast<float>(TABBAR_H / 2 + 2);
    if (g_hoverKey == UI_NEWTAB * 1000) cv.circle(px, py, 14, th.tabHover);
    cv.line(px - 6, py, px + 6, py, 2.0f, th.text);
    cv.line(px, py - 6, px, py + 6, 2.0f, th.text);
    addUi(x + 4, 6, 30, TABBAR_H - 6, UI_NEWTAB, 0);
}
static void drawToolbar(Canvas& cv, Browser& b) {
    const Theme& th = g_th;
    cv.rect(0, TOOLBAR_Y, W, TOOLBAR_H, th.toolbar);
    cv.rect(0, TOOLBAR_Y + TOOLBAR_H - 1, W, 1, th.rule);
    const float cyb = static_cast<float>(TOOLBAR_Y + TOOLBAR_H / 2);
    const bool canBack = b.histPos > 0, canFwd = b.histPos >= 0 && b.histPos + 1 < static_cast<int>(b.hist.size());
    for (int k = 0; k < 4; k++) {
        const int kind = UI_BACK + k;
        const float cx = 34.0f + k * 44;
        const bool hov = g_hoverKey == kind * 1000;
        const bool enabled = (k == 0 && canBack) || (k == 1 && canFwd) || k >= 2;
        if (hov && enabled) cv.circle(cx, cyb, 19, th.tabHover);
        const uint32_t col = enabled ? th.ink : th.dim;
        if (k == 0) {
            cv.line(cx + 7, cyb, cx - 7, cyb, 2.4f, col); cv.line(cx - 7, cyb, cx, cyb - 7, 2.4f, col); cv.line(cx - 7, cyb, cx, cyb + 7, 2.4f, col);
        } else if (k == 1) {
            cv.line(cx - 7, cyb, cx + 7, cyb, 2.4f, col); cv.line(cx + 7, cyb, cx, cyb - 7, 2.4f, col); cv.line(cx + 7, cyb, cx, cyb + 7, 2.4f, col);
        } else if (k == 2) {
            cv.circle(cx, cyb, 9.5f, col); cv.circle(cx, cyb, 7.3f, hov ? th.tabHover : th.toolbar);
            cv.rect(static_cast<int>(cx) + 1, static_cast<int>(cyb) - 12, 12, 9, hov ? th.tabHover : th.toolbar);
            cv.line(cx + 3, cyb - 12, cx + 3, cyb - 5, 2.2f, col); cv.line(cx + 3, cyb - 5, cx + 10, cyb - 5, 2.2f, col);
        } else {
            cv.line(cx - 10, cyb, cx, cyb - 9, 2.2f, col); cv.line(cx, cyb - 9, cx + 10, cyb, 2.2f, col);
            cv.line(cx - 6, cyb - 1, cx - 6, cyb + 8, 2.2f, col); cv.line(cx + 6, cyb - 1, cx + 6, cyb + 8, 2.2f, col);
            cv.line(cx - 6, cyb + 8, cx + 6, cyb + 8, 2.2f, col);
        }
        addUi(static_cast<int>(cx) - 20, TOOLBAR_Y + 4, 40, 40, kind, 0);
    }
    // botones de la derecha: marcadores y menu
    const float cxPow = static_cast<float>(W - 36), cxMenu = static_cast<float>(W - 82), cxBm = static_cast<float>(W - 128);
    {  // boton de apagado (salir)
        const bool hp = g_hoverKey == UI_POWER * 1000;
        const uint32_t bgc = hp ? th.tabHover : th.toolbar;
        if (hp) cv.circle(cxPow, cyb, 19, th.tabHover);
        cv.circle(cxPow, cyb + 1, 9.5f, th.ink);
        cv.circle(cxPow, cyb + 1, 7.4f, bgc);
        cv.rect(static_cast<int>(cxPow) - 6, static_cast<int>(cyb) - 12, 12, 10, bgc);
        cv.line(cxPow, cyb - 11, cxPow, cyb, 2.4f, th.ink);
        addUi(static_cast<int>(cxPow) - 20, TOOLBAR_Y + 4, 40, 40, UI_POWER, 0);
    }
    if (g_hoverKey == UI_MENU * 1000) cv.circle(cxMenu, cyb, 19, th.tabHover);
    for (int i = -1; i <= 1; i++) cv.rect(static_cast<int>(cxMenu) - 8, static_cast<int>(cyb) + i * 6 - 1, 16, 2, th.ink);
    addUi(static_cast<int>(cxMenu) - 20, TOOLBAR_Y + 4, 40, 40, UI_MENU, 0);
    if (g_hoverKey == UI_BMARKS * 1000) cv.circle(cxBm, cyb, 19, th.tabHover);
    cv.line(cxBm - 6, cyb - 9, cxBm + 6, cyb - 9, 2.0f, th.ink); cv.line(cxBm + 6, cyb - 9, cxBm + 6, cyb + 9, 2.0f, th.ink);
    cv.line(cxBm + 6, cyb + 9, cxBm, cyb + 4, 2.0f, th.ink); cv.line(cxBm, cyb + 4, cxBm - 6, cyb + 9, 2.0f, th.ink); cv.line(cxBm - 6, cyb + 9, cxBm - 6, cyb - 9, 2.0f, th.ink);
    addUi(static_cast<int>(cxBm) - 20, TOOLBAR_Y + 4, 40, 40, UI_BMARKS, 0);
    // omnibox
    const int ox = 14 + 4 * 44 + 12, ow = W - 166 - ox, oy = TOOLBAR_Y + 6, oh = 36;
    if (b.urlEdit) cv.rrectBorder(ox, oy, ow, oh, 18, 2, th.accent, th.omni);
    else cv.rrectBorder(ox, oy, ow, oh, 18, 1, th.omniBorder, th.omni);
    addUi(ox, oy, ow, oh, UI_OMNI, 0);
    const bool secure = b.url.compare(0, 8, "https://") == 0;
    const float lx = static_cast<float>(ox + 24), ly = static_cast<float>(oy + oh / 2);
    if (secure) {
        cv.circle(lx, ly - 4, 5.0f, th.dim); cv.circle(lx, ly - 4, 3.2f, th.omni);
        cv.rrect(static_cast<int>(lx) - 6, static_cast<int>(ly) - 2, 12, 9, 2, th.dim);
    } else {
        cv.circle(lx, ly, 6.5f, th.dim); cv.circle(lx, ly, 4.8f, th.omni); cv.rect(static_cast<int>(lx) - 1, static_cast<int>(ly) - 1, 2, 5, th.dim); cv.rect(static_cast<int>(lx) - 1, static_cast<int>(ly) - 4, 2, 2, th.dim);
    }
    const int tx = ox + 48, tmax = ow - 48 - 48, tpx = 18, base = baseFor(oy, oh, FONT_SANS, tpx);
    if (b.urlEdit) {
        std::string shown = b.urlText;
        int tw = te_width(shown, FONT_SANS, tpx, false);
        while (tw > tmax && !shown.empty()) { shown.erase(0, 1); tw = te_width(shown, FONT_SANS, tpx, false); }  // muestra el final
        if (b.urlFresh && !shown.empty()) cv.rrect(tx - 3, oy + 6, tw + 6, oh - 12, 4, th.focusBg);
        te_draw(cv, tx, base, shown, FONT_SANS, tpx, false, th.text);
        if (g_caret && !b.urlFresh) cv.rect(tx + tw + 1, oy + 8, 2, oh - 16, th.text);
    } else if (b.url.empty() || b.url == "about:blank" || b.url == "about:home") {
        te_draw(cv, tx, base, "Buscar o escribir una direccion", FONT_SANS, tpx, false, th.dim);
    } else if (isInternal(b.url)) {
        te_draw(cv, tx, base, b.url, FONT_SANS, tpx, false, th.dim);
    } else {
        std::string u = b.url;
        size_t s = u.find("://");
        s = (s == std::string::npos) ? 0 : s + 3;
        const size_t e = u.find_first_of("/?#", s);
        const std::string host = u.substr(s, e == std::string::npos ? std::string::npos : e - s);
        const std::string rest = e == std::string::npos ? std::string() : u.substr(e);
        const int hw = te_draw(cv, tx, base, te_fit(host, tmax, FONT_SANS, tpx, false), FONT_SANS, tpx, false, th.text);
        if (!rest.empty() && hw < tmax - 30) te_draw(cv, tx + hw, base, te_fit(rest, tmax - hw, FONT_SANS, tpx, false), FONT_SANS, tpx, false, th.dim);
    }
    // estrella dentro del omnibox
    const float sx = static_cast<float>(ox + ow - 28), sy = static_cast<float>(oy + oh / 2);
    const bool marked = bookmarkIndex(b.url) >= 0;
    if (g_hoverKey == UI_STAR * 1000) cv.circle(sx, sy, 15, th.tabHover);
    if (marked) cv.circle(sx, sy, 6.5f, th.accent);
    const float PI = 3.14159265f;
    float px0 = 0, py0 = 0;
    for (int i = 0; i <= 10; i++) {
        const float ang = -PI / 2 + i * PI / 5, r = (i % 2 == 0) ? 10.0f : 4.6f;
        const float px1 = sx + std::cos(ang) * r, py1 = sy + std::sin(ang) * r;
        if (i > 0) cv.line(px0, py0, px1, py1, 1.8f, marked ? th.accent : th.dim);
        px0 = px1; py0 = py1;
    }
    addUi(ox + ow - 48, oy, 48, oh, UI_STAR, 0);
    if (g_loading) cv.rect(0, VIEW_Y - 3, W * 2 / 5, 3, th.accent);
}
static void drawBubble(Canvas& cv, Browser& b, uint32_t now) {
    const Theme& th = g_th;
    std::string txt;
    bool bad = false;
    const int focus = b.hover >= 0 ? b.hover : (b.sel >= 0 ? b.sel : -1);
    if (g_hoverKey >= 0) txt = uiTip(g_hoverKey / 1000);
    if (txt.empty()) txt = focusTarget(b, focus);
    if (txt.empty() && now < g_toastUntil) { txt = g_toast; bad = g_toastBad; }
    if (txt.empty()) return;
    const int px = 15;
    const std::string t = te_fit(txt, 1100, FONT_SANS, px, false);
    const int tw = te_width(t, FONT_SANS, px, false);
    const int bw = tw + 28, bh = 32, bx = 0, by = H - bh;
    cv.rrect(bx, by - 1, bw + 1, bh + 1, 8, th.rule);
    cv.rrect(bx, by, bw, bh, 8, th.statusBg);
    te_draw(cv, bx + 14, baseFor(by, bh, FONT_SANS, px), t, FONT_SANS, px, false, bad ? th.bad : th.statusText);
}
static void drawDownload(Canvas& cv) {
    const Theme& th = g_th;
    const int bw = 820, bh = 230, bx = (W - bw) / 2, by = (H - bh) / 2;
    cv.rrect(bx - 2, by - 2, bw + 4, bh + 4, 14, th.rule);
    cv.rrect(bx, by, bw, bh, 12, th.toolbar);
    te_draw(cv, bx + 30, by + 46, "Descargando", FONT_SANS, 24, true, th.text);
    te_draw(cv, bx + 30, by + 84, te_fit(g_dlp.name, bw - 60, FONT_SANS, 18, false), FONT_SANS, 18, false, th.dim);
    const int px = bx + 30, py = by + 110, pw = bw - 60, ph = 18;
    cv.rrect(px, py, pw, ph, 9, th.scrollTrack);
    if (g_dlp.total > 0) {
        const float fr = std::min(1.0f, static_cast<float>(g_dlp.done) / static_cast<float>(g_dlp.total));
        cv.rrect(px, py, std::max(ph, static_cast<int>(pw * fr)), ph, 9, th.accent);
    } else {  // tamano desconocido: barra que se desliza
        const int seg = 160;
        const int off = static_cast<int>((SDL_GetTicks() / 6) % static_cast<uint32_t>(pw + seg)) - seg;
        const int x0 = std::max(0, off), x1 = std::min(pw, off + seg);
        if (x1 > x0) cv.rrect(px + x0, py, x1 - x0, ph, 9, th.accent);
    }
    std::string st = dl_format_size(g_dlp.done);
    if (g_dlp.total > 0) st += " de " + dl_format_size(g_dlp.total) + " (" + std::to_string(static_cast<int>(100.0 * g_dlp.done / g_dlp.total)) + "%)";
    if (g_dlp.speed > 1) {
        st += "   -   " + dl_format_size(static_cast<long long>(g_dlp.speed)) + "/s";
        if (g_dlp.total > g_dlp.done) st += "   -   faltan " + fmtTime(static_cast<long long>((g_dlp.total - g_dlp.done) / g_dlp.speed));
    }
    te_draw(cv, px, py + ph + 40, te_fit(st, pw, FONT_SANS, 18, false), FONT_SANS, 18, false, th.text);
    te_draw(cv, px, by + bh - 26, "Cancelar: boton O (circulo) del mando", FONT_SANS, 16, false, th.dim);
}
// ------------------------------------------------------------------ reproductor de video: interfaz y entradas
static bool vpOverlayVisible(uint32_t now, const VpInfo& vi) {
    if (vi.state != VP_PLAYING) return true;  // abriendo, en pausa, almacenando, fin o error: siempre visible
    return g_vpStats || now - g_vpLastAct < 3000;
}
static void vpVolToast() { toast("Volumen " + std::to_string(vp_info().volume) + "%"); }
static void drawVideoUi(Canvas& cv) {
    const Theme& th = g_th;
    const VpInfo vi = vp_info();
    const uint32_t now = SDL_GetTicks();
    cv.resetClip();
    cv.rect(0, 0, W, H, 0xFF000000);
    if (vi.frames > 0 && vi.vw > 0 && vi.vh > 0) {  // fotograma con la proporcion original (bandas negras)
        int vw = W, vh = H;
        if (static_cast<int64_t>(W) * vi.vh > static_cast<int64_t>(H) * vi.vw) vw = static_cast<int>(static_cast<int64_t>(H) * vi.vw / vi.vh);
        else vh = static_cast<int>(static_cast<int64_t>(W) * vi.vh / vi.vw);
        vp_draw_video(cv, (W - vw) / 2, (H - vh) / 2, vw, vh, g_cfg.vhalf);
    }
    g_vpBtns.clear();
    g_vpBar[0] = g_vpBar[1] = g_vpBar[2] = g_vpBar[3] = 0;
    // texto de estado en el centro
    std::string msg;
    bool bad = false;
    if (vi.state == VP_ERROR) { msg = vi.error.empty() ? std::string("Error de reproduccion") : vi.error; bad = true; }
    else if (vi.state == VP_OPENING) msg = vi.stage.empty() ? std::string("Abriendo video...") : vi.stage;
    else if (vi.netWait || vi.state == VP_BUFFERING) msg = "Almacenando en bufer " + std::to_string(vi.bufPct) + "%";
    else if (vi.state == VP_ENDED) msg = "Fin del video";
    else if (vi.noFrameSecs >= 8) msg = "Sin imagen: el decodificador no entrega fotogramas. Prueba otro decodificador en Configuracion.";
    else if (vi.frames == 0 && vi.state == VP_PLAYING) msg = vi.stage.empty() ? std::string("Preparando imagen...") : vi.stage;
    if (!msg.empty()) {
        const std::string t = te_fit(msg, W - 240, FONT_SANS, 26, false);
        const int tw = te_width(t, FONT_SANS, 26, false);
        cv.rrect((W - tw) / 2 - 28, H / 2 - 36, tw + 56, 72, 12, 0xCC000000);
        te_draw(cv, (W - tw) / 2, baseFor(H / 2 - 36, 72, FONT_SANS, 26), t, FONT_SANS, 26, false, bad ? th.bad : 0xFFFFFFFF);
    }
    if (vpOverlayVisible(now, vi)) {
        cv.rect(0, 0, W, 72, 0xB0000000);  // titulo
        te_draw(cv, 40, baseFor(0, 72, FONT_SANS, 24), te_fit(vi.title, W - 640, FONT_SANS, 24, true), FONT_SANS, 24, true, 0xFFFFFFFF);
        const std::string hint = "Esc cierra   H calidad   I informacion";
        te_draw(cv, W - 40 - te_width(hint, FONT_SANS, 18, false), baseFor(0, 72, FONT_SANS, 18), hint, FONT_SANS, 18, false, 0xFFCCCCCC);
        const int barTop = H - 176;
        cv.rect(0, barTop, W, 176, 0xB0000000);  // barra inferior
        const int bx = 60, bw = W - 120, by = barTop + 26, bh = 8;
        cv.rrect(bx, by, bw, bh, 4, 0x55FFFFFF);
        const float fr = vi.durMs > 0 ? std::min(1.0f, static_cast<float>(vi.posMs) / static_cast<float>(vi.durMs)) : 0.0f;
        const int fw = static_cast<int>(bw * fr);
        if (fw > 0) cv.rrect(bx, by, std::max(8, fw), bh, 4, th.accent);
        cv.circle(static_cast<float>(bx + fw), by + bh / 2.0f, 10.0f, 0xFFFFFFFF);
        g_vpBar[0] = bx - 12; g_vpBar[1] = by - 16; g_vpBar[2] = bw + 24; g_vpBar[3] = bh + 32;
        const std::string tm = fmtTime(static_cast<long long>(vi.posMs / 1000)) + " / " + (vi.durMs > 0 ? fmtTime(static_cast<long long>(vi.durMs / 1000)) : std::string("--:--"));
        te_draw(cv, bx, by + 50, tm, FONT_SANS, 20, false, 0xFFFFFFFF);
        const std::string vol = "Volumen " + std::to_string(vi.volume) + "%";
        te_draw(cv, W - 60 - te_width(vol, FONT_SANS, 20, false), by + 50, vol, FONT_SANS, 20, false, 0xFFFFFFFF);
        const char* labels[6] = {"-10 s", vi.state == VP_PAUSED ? "Seguir" : "Pausa", "+10 s", "Vol -", "Vol +", "Cerrar"};
        const int bwid = 132, bhei = 52, gap = 14, total = 6 * bwid + 5 * gap;
        int x = (W - total) / 2;
        const int y = barTop + 100;
        for (int i = 0; i < 6; i++) {
            const bool hov = g_mouseOk && g_mx >= x && g_mx < x + bwid && g_my >= y && g_my < y + bhei;
            cv.rrectBorder(x, y, bwid, bhei, 10, 1, 0x66FFFFFF, hov ? th.accent : 0xCC2B2C2F);
            const int tw = te_width(labels[i], FONT_SANS, 22, false);
            te_draw(cv, x + (bwid - tw) / 2, baseFor(y, bhei, FONT_SANS, 22), labels[i], FONT_SANS, 22, false, hov && !th.dark ? 0xFFFFFFFF : (hov ? th.bg : 0xFFFFFFFF));
            g_vpBtns.push_back({x, y, bwid, bhei, i + 1});
            x += bwid + gap;
        }
    }
    if (g_vpStats) {
        char l1[240], l2[200];
        snprintf(l1, sizeof(l1), "Video %dx%d   %.1f fps   velocidad %.2fx   fotogramas: %lu (descartados %lu)   conv. %.1f ms   reloj: %s", vi.vw, vi.vh, vi.fps,
                 vi.speed, vi.frames, vi.dropped, vi.convMs, vi.clockSync ? "pared" : "audio");
        snprintf(l2, sizeof(l2), "Decodificador: %s   audio: %s   bufer: %d%%", vi.vdec == 0 ? "sistema" : (vi.vdec == 2 ? "software" : "software 2"), vi.hasAudio ? "si" : "no", vi.bufPct);
        cv.rrect(40, 90, 1300, 120, 10, 0xB0000000);
        te_draw(cv, 60, 126, l1, FONT_SANS, 19, false, 0xFFFFFFFF);
        te_draw(cv, 60, 156, l2, FONT_SANS, 19, false, 0xFFFFFFFF);
        te_draw(cv, 60, 186, te_fit("Estado: " + vi.stage, 860, FONT_SANS, 19, false), FONT_SANS, 19, false, 0xFFCCCCCC);
    }
    if (!g_toast.empty() && now < g_toastUntil) {
        const std::string t = te_fit(g_toast, 900, FONT_SANS, 22, false);
        const int tw = te_width(t, FONT_SANS, 22, false);
        cv.rrect(W - tw - 80, 90, tw + 40, 48, 10, 0xCC000000);
        te_draw(cv, W - tw - 60, baseFor(90, 48, FONT_SANS, 22), t, FONT_SANS, 22, false, g_toastBad ? th.bad : 0xFFFFFFFF);
    }
}
static void videoAction(int id) {
    switch (id) {
        case 1: vp_seek_rel(-10000); break;
        case 2: vp_pause_toggle(); break;
        case 3: vp_seek_rel(10000); break;
        case 4: vp_volume_rel(-10); vpVolToast(); break;
        case 5: vp_volume_rel(10); vpVolToast(); break;
        case 6: closeVideo(); break;
        default: break;
    }
}
static void videoKey(int code) {
    g_vpLastAct = SDL_GetTicks();
    g_dirty = true;
    switch (code) {
        case K_SPACE: case K_K: vp_pause_toggle(); break;
        case K_LEFT: vp_seek_rel(-10000); break;
        case K_RIGHT: vp_seek_rel(10000); break;
        case K_J: vp_seek_rel(-30000); break;
        case K_L: vp_seek_rel(30000); break;
        case K_UP: vp_volume_rel(5); vpVolToast(); break;
        case K_DOWN: vp_volume_rel(-5); vpVolToast(); break;
        case K_HOME: vp_seek_abs(0); break;
        case K_H: g_cfg.vhalf = !g_cfg.vhalf; configSave(); toast(g_cfg.vhalf ? "Calidad: media resolucion" : "Calidad: completa"); break;
        case K_I: g_vpStats = !g_vpStats; break;
        case K_ESC: case K_BACKSPACE: case K_Q: closeVideo(); break;
        default: break;
    }
}
static void videoPad(int button) {  // 0 pausa, 1 cierra, 2 calidad, 3 informacion, 4/5 -+30 s
    g_vpLastAct = SDL_GetTicks();
    g_dirty = true;
    switch (button) {
        case 0: vp_pause_toggle(); break;
        case 1: closeVideo(); break;
        case 2: g_cfg.vhalf = !g_cfg.vhalf; configSave(); toast(g_cfg.vhalf ? "Calidad: media resolucion" : "Calidad: completa"); break;
        case 3: g_vpStats = !g_vpStats; break;
        case 4: vp_seek_rel(-30000); break;
        case 5: vp_seek_rel(30000); break;
        default: break;
    }
}
static void videoHat(int v) {  // cruceta: izquierda/derecha = -+10 s, arriba/abajo = volumen
    g_vpLastAct = SDL_GetTicks();
    g_dirty = true;
    if (v & SDL_HAT_LEFT) vp_seek_rel(-10000);
    if (v & SDL_HAT_RIGHT) vp_seek_rel(10000);
    if (v & SDL_HAT_UP) { vp_volume_rel(5); vpVolToast(); }
    if (v & SDL_HAT_DOWN) { vp_volume_rel(-5); vpVolToast(); }
}
static void videoClick() {
    g_vpLastAct = SDL_GetTicks();
    g_dirty = true;
    for (const VpBtn& vb : g_vpBtns)
        if (g_mx >= vb.x && g_mx < vb.x + vb.w && g_my >= vb.y && g_my < vb.y + vb.h) { videoAction(vb.id); return; }
    if (g_vpBar[2] > 0 && g_mx >= g_vpBar[0] && g_mx < g_vpBar[0] + g_vpBar[2] && g_my >= g_vpBar[1] && g_my < g_vpBar[1] + g_vpBar[3]) {
        const VpInfo vi = vp_info();
        if (vi.durMs > 0) {
            const float fr = std::max(0.0f, std::min(1.0f, static_cast<float>(g_mx - (g_vpBar[0] + 12)) / static_cast<float>(g_vpBar[2] - 24)));
            vp_seek_abs(static_cast<uint64_t>(fr * static_cast<float>(vi.durMs)));
        }
        return;
    }
    vp_pause_toggle();  // clic en el video = pausa / seguir
}
// ------------------------------------------------------------------ dialogo de salida
static void drawQuit(Canvas& cv) {
    const Theme& th = g_th;
    const int bw = 720, bh = 260, bx = (W - bw) / 2, by = (H - bh) / 2;
    cv.rrect(bx + 4, by + 6, bw, bh, 16, 0x66000000);
    cv.rrect(bx - 2, by - 2, bw + 4, bh + 4, 16, th.rule);
    cv.rrect(bx, by, bw, bh, 14, th.toolbar);
    te_draw(cv, bx + 40, by + 70, "Salir de PS Browser?", FONT_SANS, 30, true, th.text);
    te_draw(cv, bx + 40, by + 108, "Se cerrara el navegador y volveras a la pantalla de inicio de la consola.", FONT_SANS, 18, false, th.dim);
    const int y = by + bh - 100, h = 56, w = 300;
    const char* lab[2] = {"Salir", "Cancelar"};
    for (int i = 0; i < 2; i++) {
        const int x = bx + 40 + i * (w + 40);
        g_quitBtn[i][0] = x; g_quitBtn[i][1] = y; g_quitBtn[i][2] = w; g_quitBtn[i][3] = h;
        const bool hov = g_mouseOk && g_mx >= x && g_mx < x + w && g_my >= y && g_my < y + h;
        cv.rrectBorder(x, y, w, h, 10, hov ? 3 : 1, hov ? th.accent : th.btnBorder, i == 0 ? th.accent : th.btnBg);
        const int tw = te_width(lab[i], FONT_SANS, 24, true);
        te_draw(cv, x + (w - tw) / 2, baseFor(y, h, FONT_SANS, 24), lab[i], FONT_SANS, 24, true, i == 0 ? (th.dark ? th.bg : 0xFFFFFFFF) : th.text);
    }
    te_draw(cv, bx + 40, by + bh - 20, "Mando: X salir, O cancelar   Teclado: Intro salir, Esc cancelar", FONT_SANS, 16, false, th.dim);
}
static void quitClick() {
    for (int i = 0; i < 2; i++) {
        if (g_mx >= g_quitBtn[i][0] && g_mx < g_quitBtn[i][0] + g_quitBtn[i][2] && g_my >= g_quitBtn[i][1] && g_my < g_quitBtn[i][1] + g_quitBtn[i][3]) {
            if (i == 0) g_quit = true;
            else { g_quitAsk = false; g_dirty = true; }
            return;
        }
    }
}
// Salida: cierra el video y termina el proceso con _exit (evita destructores estaticos con hilos vivos). Un hilo vigilante fuerza la salida
// si algo se atasca al cerrar (p. ej. el reproductor).
static void hardExit() {
    psb_log("[quit] saliendo");
    std::thread([] { SDL_Delay(2500); psb_log("[quit] salida forzada por el vigilante"); _exit(0); }).detach();
    vp_shutdown();  // cierra video (si lo hay), puerto de audio y texturas, y lo deja registrado
    imgw_pause(true);
    psb_log("[quit] adios");
    _exit(0);
}
static void drawFrame() {
    Canvas& cv = *g_cv;
    if (g_vpMode) {  // el video ocupa toda la pantalla
        g_ui.clear();
        drawVideoUi(cv);
        if (g_quitAsk) drawQuit(cv);
        return;
    }
    Browser& b = B;
    const Theme& th = g_th;
    g_ui.clear();
    cv.resetClip();
    cv.clear(th.bg);
    drawDocument(cv, b.doc, makeRc(b));
    cv.resetClip();
    // barra de desplazamiento
    cv.rect(W - SBAR_W, VIEW_Y, SBAR_W, VIEW_H, th.scrollTrack);
    const int ms = maxScroll(b);
    if (b.doc.height > VIEW_H && ms > 0) {
        g_thumbH = std::max(40, VIEW_H * VIEW_H / b.doc.height);
        g_thumbY = VIEW_Y + static_cast<int>((VIEW_H - g_thumbH) * (b.scrollY / ms));
        g_thumbY = std::max(VIEW_Y, std::min(VIEW_Y + VIEW_H - g_thumbH, g_thumbY));
        cv.rrect(W - SBAR_W + 2, g_thumbY, SBAR_W - 4, g_thumbH, 5, th.scrollThumb);
    } else {
        g_thumbH = 0;
    }
    addUi(W - SBAR_W, VIEW_Y, SBAR_W, VIEW_H, UI_SBAR, 0);
    drawTabbar(cv, b);
    drawToolbar(cv, b);
    drawBubble(cv, b, SDL_GetTicks());
    drawFindBar(cv, b);
    drawCtx(cv);
    if (g_dlp.active) drawDownload(cv);
    if (g_osk.open) osk_draw(g_osk, cv, th, W, H, g_caret);
    if (g_quitAsk) drawQuit(cv);
}
static void renderFrame() {
    // sin SDL_RenderClear: la textura cubre toda la pantalla y sin mezcla alfa, asi que limpiar antes es un relleno de 8 MB de mas
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    if (g_mouseOk && g_curTex) {
        SDL_Rect dst = {g_mx, g_my, 12 * 2, 17 * 2};
        SDL_RenderCopy(g_ren, g_curTex, nullptr, &dst);
    }
    SDL_RenderPresent(g_ren);
}
// Via rapida de video (solo cuando no hay nada que dibujar encima: overlay oculto, sin mensajes ni avisos): el fotograma se sube como textura
// a su tamano NATIVO (p. ej. 864x480 = 1,6 MB en vez de 8 MB) y SDL lo escala con RenderCopy; se evita componer 1920x1080 en CPU y subirlos.
static SDL_Window* g_win = nullptr;
static SDL_Texture* g_vidTex = nullptr;
static int g_vidTexW = 0, g_vidTexH = 0;
static unsigned long g_vidSerial = 0;
static SDL_Rect g_vidDst = {0, 0, 0, 0};
static bool g_partFailed = false;  // SDL_UpdateWindowSurfaceRects no funciona en este sistema
static bool g_vidFastPrev = false;  // el ultimo frame presentado fue por la via rapida (las bandas negras ya estan pintadas)
static void vidTexFree() {
    if (g_vidTex) { SDL_DestroyTexture(g_vidTex); g_vidTex = nullptr; }
    g_vidTexW = g_vidTexH = 0; g_vidSerial = 0; g_vidFastPrev = false;
}
static bool vpCleanPlayback(uint32_t now) {
    if (!g_vpMode || g_quitAsk || !g_toast.empty()) return false;
    const VpInfo vi = vp_info();
    if (vi.state != VP_PLAYING || vi.netWait || vi.frames == 0 || vi.noFrameSecs >= 8) return false;
    return !vpOverlayVisible(now, vi);
}
// true = presentado por la via rapida; false = usar la via normal (componer en el lienzo)
static bool presentVideoFast(uint32_t* tUp, uint32_t* tPres) {
    int w = 0, h = 0;
    unsigned long serial = 0;
    const uint32_t* px = vp_frame(w, h, serial);
    if (!px) return false;
    if (!g_vidTex || g_vidTexW != w || g_vidTexH != h) {
        vidTexFree();
        g_vidTex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!g_vidTex) { psb_log("[perfv] no se pudo crear la textura de video %dx%d: via normal", w, h); return false; }
        g_vidTexW = w; g_vidTexH = h; g_vidSerial = 0;
        psb_log("[perfv] textura de video nativa %dx%d (via rapida)", w, h);
    }
    const uint32_t a = SDL_GetTicks();
    if (serial != g_vidSerial) { SDL_UpdateTexture(g_vidTex, nullptr, px, w * 4); g_vidSerial = serial; }
    const uint32_t b = SDL_GetTicks();
    int dw = W, dh = H;
    if (static_cast<int64_t>(W) * h > static_cast<int64_t>(H) * w) dw = static_cast<int>(static_cast<int64_t>(H) * w / h);
    else dh = static_cast<int>(static_cast<int64_t>(W) * h / w);
    if (g_cfg.vscale == 1) { dw = w; dh = h; }                            // 1:1: copia directa, sin escalar por CPU
    else if (g_cfg.vscale == 2 && w * 2 <= W && h * 2 <= H) { dw = w * 2; dh = h * 2; }  // x2 exacto si cabe
    const SDL_Rect dst = {(W - dw) / 2, (H - dh) / 2, dw, dh};
    // Actualizacion parcial: solo con la opcion activa, sin raton (el cursor se pinta fuera del rectangulo) y si el sistema la acepta.
    const bool part = g_cfg.vpart && !g_mouseOk && g_win && !g_partFailed;
    const bool fullRepaint = !g_vidFastPrev || g_mouseOk || g_vidFastPrev0 || g_vidDst.x != dst.x || g_vidDst.y != dst.y || g_vidDst.w != dst.w || g_vidDst.h != dst.h;
    if (fullRepaint) { SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255); SDL_RenderClear(g_ren); }  // bandas negras (y borra el cursor)
    SDL_RenderCopy(g_ren, g_vidTex, nullptr, &dst);
    if (g_mouseOk && g_curTex) {
        SDL_Rect cd = {g_mx, g_my, 12 * 2, 17 * 2};
        SDL_RenderCopy(g_ren, g_curTex, nullptr, &cd);
    }
    if (part && !fullRepaint) {
        if (SDL_UpdateWindowSurfaceRects(g_win, &dst, 1) != 0) {  // no soportado: volver a la presentacion normal para siempre
            psb_log("[perfv] SDL_UpdateWindowSurfaceRects fallo (%s): se desactiva la actualizacion parcial", SDL_GetError());
            g_partFailed = true;
            SDL_RenderPresent(g_ren);
        }
    } else {
        SDL_RenderPresent(g_ren);
    }
    g_vidFastPrev0 = false;
    g_vidDst = dst;
    g_vidFastPrev = true;
    *tUp = b - a;
    *tPres = SDL_GetTicks() - b;
    return true;
}
static void present() {
    drawFrame();
    SDL_UpdateTexture(g_tex, nullptr, g_cv->px.data(), W * 4);
    renderFrame();
}

// Benchmark de presentacion: mide ms/fotograma de SDL segun lo que se copie y que parte de la pantalla se actualice. Bloquea ~20 s.
static void benchOne(const std::string& name, int n, const std::function<void()>& fn) {
    fn();  // calentar
    const uint32_t t0 = SDL_GetTicks();
    for (int i = 0; i < n; i++) { fn(); SDL_PumpEvents(); }
    const uint32_t dt = std::max<uint32_t>(1, SDL_GetTicks() - t0);
    char l[260];
    snprintf(l, sizeof(l), "%s: %.1f ms por fotograma (%.1f fps)", name.c_str(), dt / static_cast<double>(n), n * 1000.0 / dt);
    g_bench.push_back(l);
    psb_log("[bench] %s", l);
}
static void runBench() {
    g_bench.clear();
    toast("Midiendo presentacion (~20 s)...");
    present();  // que se vea el aviso antes de bloquear
    psb_log("[bench] inicio (ventana %dx%d, renderer software)", W, H);
    const int N = 24;
    const int vw = 864, vh = 480;
    std::vector<uint32_t> px(static_cast<size_t>(vw) * vh);
    for (int y = 0; y < vh; y++) for (int x = 0; x < vw; x++) px[static_cast<size_t>(y) * vw + x] = 0xFF000000u | ((x * 255 / vw) << 16) | ((y * 255 / vh) << 8) | 0x40;
    SDL_Texture* t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, vw, vh);
    if (!t) { g_bench.push_back("no se pudo crear la textura de prueba"); return; }
    SDL_UpdateTexture(t, nullptr, px.data(), vw * 4);
    const SDL_Rect c11 = {(W - vw) / 2, (H - vh) / 2, vw, vh};
    SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255);
    benchOne("1. Solo Present (sin copiar nada)", N, [&] { SDL_RenderPresent(g_ren); });
    benchOne("2. Limpiar a negro + Present", N, [&] { SDL_RenderClear(g_ren); SDL_RenderPresent(g_ren); });
    benchOne("3. Copiar textura 1920x1080 + Present (lo que hace la interfaz)", N, [&] { SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr); SDL_RenderPresent(g_ren); });
    benchOne("4. Copiar 864x480 a 1:1 + Present", N, [&] { SDL_RenderCopy(g_ren, t, nullptr, &c11); SDL_RenderPresent(g_ren); });
    {
        int dw = W, dh = H;
        if (static_cast<int64_t>(W) * vh > static_cast<int64_t>(H) * vw) dw = static_cast<int>(static_cast<int64_t>(H) * vw / vh); else dh = static_cast<int>(static_cast<int64_t>(W) * vh / vw);
        const SDL_Rect fit = {(W - dw) / 2, (H - dh) / 2, dw, dh};
        benchOne("5. Copiar 864x480 escalado a pantalla + Present (via rapida actual)", N, [&] { SDL_RenderCopy(g_ren, t, nullptr, &fit); SDL_RenderPresent(g_ren); });
    }
    SDL_Surface* surf = g_win ? SDL_GetWindowSurface(g_win) : nullptr;
    if (!surf) {
        const std::string m = std::string("SDL_GetWindowSurface no disponible: ") + SDL_GetError() + " (no se puede probar la actualizacion parcial)";
        g_bench.push_back(m);
        psb_log("[bench] %s", m.c_str());
    } else {
        const SDL_Rect rects[] = {{0, 0, 1, 1}, {0, 0, 480, 270}, {0, 0, 960, 540}, c11, {0, 0, W, H}};
        for (const SDL_Rect& r : rects) {
            int rc = 0;
            char nm[120];
            snprintf(nm, sizeof(nm), "6. Copia 1:1 + UpdateWindowSurfaceRects %dx%d", r.w, r.h);
            benchOne(nm, N, [&] { SDL_RenderCopy(g_ren, t, nullptr, &c11); rc = SDL_UpdateWindowSurfaceRects(g_win, &r, 1); });
            if (rc != 0) { const std::string m = std::string("   (devolvio error: ") + SDL_GetError() + ")"; g_bench.push_back(m); psb_log("[bench] %s", m.c_str()); }
        }
        benchOne("7. Copia 1:1 + SDL_UpdateWindowSurface (completa)", N, [&] { SDL_RenderCopy(g_ren, t, nullptr, &c11); SDL_UpdateWindowSurface(g_win); });
    }
    SDL_DestroyTexture(t);
    g_bench.push_back("Fin. Si 6 con rectangulos pequenos es mucho mas rapido que 3, la actualizacion parcial merece la pena (Configuracion > Video).");
    toast("Medicion terminada");
    g_dirty = true;
}
static SDL_Texture* makeCursor(SDL_Renderer* ren) {
    static const char* bmp[17] = {"X           ", "XX          ", "XOX         ", "XOOX        ", "XOOOX       ", "XOOOOX      ",
                                  "XOOOOOX     ", "XOOOOOOX    ", "XOOOOOOOX   ", "XOOOOOOOOX  ", "XOOOOOXXXXX ", "XOOXOOX     ",
                                  "XOX XOOX    ", "XX  XOOX    ", "X    XOOX   ", "     XOOX   ", "      XX    "};
    uint32_t px[17 * 12];
    for (int y = 0; y < 17; y++)
        for (int x = 0; x < 12; x++) px[y * 12 + x] = bmp[y][x] == 'X' ? 0xFF000000 : (bmp[y][x] == 'O' ? 0xFFFFFFFF : 0x00000000);
    SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 12, 17);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px, 12 * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    return t;
}
// ------------------------------------------------------------------ entrada
static void changeZoom(int delta, bool reset = false) {
    g_cfg.zoom = reset ? 100 : g_cfg.zoom + delta;
    clampCfg();
    configSave();
    relayoutAll();
    if (isInternal(B.url)) refreshInternal(B);
    toast("Texto: " + std::to_string(g_cfg.zoom) + "%");
}
static void onKeyDown(int code, int ch, uint32_t mods) {
    const bool shift = (mods & (ORBIS_KEYBOARD_MOD_LEFT_SHIFT | ORBIS_KEYBOARD_MOD_RIGHT_SHIFT)) != 0;
    const bool ctrl = (mods & (ORBIS_KEYBOARD_MOD_LEFT_CTRL | ORBIS_KEYBOARD_MOD_RIGHT_CTRL)) != 0;
    const bool alt = (mods & (ORBIS_KEYBOARD_MOD_LEFT_ALT | ORBIS_KEYBOARD_MOD_RIGHT_ALT)) != 0;
    static int logged = 0;
    if (logged < 60) { logged++; psb_log("tecla: codigo=%d char=%d mods=0x%x", code, ch, mods); }
    Browser& b = B;
    if (g_quitAsk) {  // dialogo de salida: Intro o Y salen, Esc o N cancelan
        if (code == K_RETURN || code == K_Y) g_quit = true;
        else if (code == K_ESC || code == K_N) { g_quitAsk = false; g_dirty = true; }
        return;
    }
    if (ctrl && code == K_Q) { quitAsk(); return; }
    if (g_vpMode) { videoKey(code); return; }  // el video captura todo el teclado
    if (g_ctx.open) {  // el menu contextual captura el teclado
        if (code == K_ESC || code == K_APP) { g_ctx.open = false; g_dirty = true; }
        else if (code == K_UP) ctxMove(-1);
        else if (code == K_DOWN || code == K_TAB) ctxMove(1);
        else if (code == K_RETURN && g_ctx.sel >= 0) ctxRun(g_ctx.sel);
        return;
    }
    if (g_osk.open) {
        if (code == K_ESC) oskApply(b, OSK_CANCELLED);
        return;
    }
    // 0) buscar en la pagina: la barra de busqueda se queda con el teclado
    if (g_find.active) {
        const bool shiftF = (mods & (ORBIS_KEYBOARD_MOD_LEFT_SHIFT | ORBIS_KEYBOARD_MOD_RIGHT_SHIFT)) != 0;
        if (code == K_ESC) findClose();
        else if (code == K_RETURN || code == K_F3) findStep(b, shiftF ? -1 : 1);
        else if (code == K_BACKSPACE) { utf8_pop_back(g_find.q); findRecompute(b, false); findGoto(b, 0); }
        else if (ctrl && code == K_V) { for (char x : g_clip) if (x != '\r' && x != '\n') g_find.q += x; findRecompute(b, false); findGoto(b, 0); }
        else if (ctrl && code == K_F) g_dirty = true;
        else if (ctrl) { findClose(); onKeyDown(code, ch, mods); }  // otros atajos (Ctrl+T, Ctrl+L...) cierran la busqueda y funcionan
        else if (ch >= 32 && ch < 127) { g_find.q += static_cast<char>(ch); findRecompute(b, false); findGoto(b, 0); }
        else if (code == K_UP) scrollBy(b, -64);
        else if (code == K_DOWN) scrollBy(b, 64);
        else if (code == K_PGUP) scrollBy(b, -VIEW_H * 9 / 10);
        else if (code == K_PGDOWN) scrollBy(b, VIEW_H * 9 / 10);
        return;
    }
    // 1) edicion de la barra de direcciones
    if (b.urlEdit) {
        if (code == K_RETURN) {
            const std::string u = urlFromUserInput(b.urlText);
            b.urlEdit = false;
            g_dirty = true;
            if (!u.empty()) navigate(b, u, true);
        } else if (code == K_ESC) {
            b.urlEdit = false;
            g_dirty = true;
        } else if (code == K_BACKSPACE) {
            if (b.urlFresh) { b.urlText.clear(); b.urlFresh = false; }
            else utf8_pop_back(b.urlText);
            g_dirty = true;
        } else if (ctrl && code == K_V) {  // pegar (una sola linea)
            std::string c;
            for (char x : g_clip) if (x != '\r' && x != '\n') c += x;
            if (b.urlFresh) { b.urlText.clear(); b.urlFresh = false; }
            b.urlText += c;
            g_dirty = true;
        } else if (ctrl && (code == K_C || code == K_X)) {
            clipSet(b.urlText);
            if (code == K_X) { b.urlText.clear(); b.urlFresh = false; }
            g_dirty = true;
        } else if (ctrl && code == K_A) {  // seleccionar todo: lo siguiente que se escriba lo sustituye
            b.urlFresh = true;
            g_dirty = true;
        } else if (ch >= 32 && ch < 127 && !ctrl) {
            if (b.urlFresh) { b.urlText.clear(); b.urlFresh = false; }
            b.urlText += static_cast<char>(ch);
            g_dirty = true;
        }
        return;
    }
    // 2) edicion de un campo de formulario
    if (b.editField >= 0 && b.editField < static_cast<int>(b.doc.fields.size())) {
        FormField& f = b.doc.fields[b.editField];
        if (code == K_RETURN) {
            const int form = f.form;
            b.editField = -1;
            g_dirty = true;
            if (form >= 0) submitForm(b, form, -1);
        } else if (code == K_ESC) {
            b.editField = -1;
            g_dirty = true;
        } else if (code == K_TAB) {
            const int nx = nextTextField(b, b.editField);
            b.editField = nx;
            if (nx >= 0)
                for (size_t i = 0; i < b.doc.focus.size(); i++)
                    if (b.doc.focus[i].kind == 1 && b.doc.focus[i].ref == nx) { b.sel = static_cast<int>(i); ensureFocusVisible(b); break; }
            g_dirty = true;
        } else if (code == K_BACKSPACE) {
            utf8_pop_back(f.value);
            g_dirty = true;
        } else if (ctrl && code == K_V) {
            for (char x : g_clip) if (x != '\r' && x != '\n') f.value += x;
            g_dirty = true;
        } else if (ctrl && (code == K_C || code == K_X)) {
            clipSet(f.value);
            if (code == K_X) f.value.clear();
            g_dirty = true;
        } else if (ch >= 32 && ch < 127 && !ctrl) {
            f.value += static_cast<char>(ch);
            g_dirty = true;
        }
        return;
    }
    // 3) atajos con Ctrl
    if (ctrl) {
        const int n = static_cast<int>(g_tabs.size());
        if (code >= K_1 && code <= K_9) {  // Ctrl+1..8 = esa pestana, Ctrl+9 = la ultima
            activateTab(code == K_9 ? n - 1 : std::min(code - K_1, n - 1));
            return;
        }
        switch (code) {
            case K_T:
                if (shift) reopenClosedTab();
                else { Browser* t = newTab("about:blank", true); startUrlEdit(*t, "", false); }
                return;
            case K_R: reload(b); return;
            case K_F: g_find.active = true; g_find.gen = ~0u; g_find.q.clear(); g_find.qLower.clear(); g_find.hits.clear(); g_find.cur = -1; g_dirty = true; return;
            case K_H: navigate(b, "about:history", true); return;
            case K_C: {  // copiar: el enlace seleccionado o, si no hay, la direccion de la pagina
                std::string u = b.sel >= 0 ? focusTarget(b, b.sel) : std::string();
                if (u.empty() && !isInternal(b.url)) u = b.url;
                if (u.empty()) toast("Nada que copiar", true); else clipSet(u);
                return;
            }
            case K_V: { const std::string u = urlFromUserInput(g_clip); if (!u.empty()) navigate(b, u, true); return; }  // pegar y buscar/ir
            case K_W: closeTab(g_cur); return;
            case K_PGUP: activateTab((g_cur + n - 1) % n); return;
            case K_PGDOWN: activateTab((g_cur + 1) % n); return;
            case K_TAB: activateTab(shift ? (g_cur + n - 1) % n : (g_cur + 1) % n); return;
            case K_L: startUrlEdit(b, isInternal(b.url) ? "" : b.url, true); return;
            case K_D: toggleBookmark(b); return;
            case K_J: navigate(b, "about:downloads", true); return;
            case K_EQUAL: changeZoom(10); return;
            case K_MINUS: changeZoom(-10); return;
            case K_0: changeZoom(0, true); return;
            default: break;
        }
    }
    switch (code) {
        case K_F6: startUrlEdit(b, isInternal(b.url) ? "" : b.url, true); return;
        case K_F5: reload(b); return;
        case K_F10: if (shift) ctxOpenAtFocus(); else navigate(b, "about:settings", true); return;
        case K_F8: navigate(b, "about:videos", true); return;
        case K_F1: navigate(b, "about:shortcuts", true); return;
        case K_F3: if (!g_find.q.empty()) { g_find.active = true; findStep(b, shift ? -1 : 1); } return;
        case K_F9: runBench(); navigate(b, "about:bench", true); return;
        case K_APP: ctxOpenAtFocus(); return;
        case K_BACKSPACE: goBack(b); return;
        case K_LEFT: if (alt) { goBack(b); return; } break;
        case K_RIGHT: if (alt) { goForward(b); return; } break;
        case K_TAB: moveFocus(b, shift ? -1 : 1); return;
        case K_RETURN: if (b.sel >= 0) activateFocus(b, b.sel); return;
        case K_UP: scrollBy(b, -64); return;
        case K_DOWN: scrollBy(b, 64); return;
        case K_PGUP: scrollBy(b, -VIEW_H * 9 / 10); return;
        case K_PGDOWN: scrollBy(b, VIEW_H * 9 / 10); return;
        case K_SPACE: scrollBy(b, shift ? -VIEW_H * 9 / 10 : VIEW_H * 9 / 10); return;
        case K_HOME: if (alt) navigate(b, homeUrl(), true); else scrollTo(b, 0, false); return;
        case K_END: scrollTo(b, maxScroll(b), false); return;
        default: break;
    }
    // Escribir cualquier caracter imprimible abre la barra de direcciones con ese caracter.
    if (!ctrl && !alt && ch > 32 && ch < 127) startUrlEdit(b, std::string(1, static_cast<char>(ch)), false);
}
struct Held {
    uint16_t code;
    uint32_t downAt, lastRep;
};
static bool repeatable(uint16_t code) {
    if (g_vpMode) return code == K_LEFT || code == K_RIGHT || code == K_UP || code == K_DOWN;
    if (code == K_UP || code == K_DOWN || code == K_PGUP || code == K_PGDOWN) return true;
    if (B.urlEdit || B.editField >= 0) return code != K_RETURN && code != K_ESC && code != K_TAB;
    return false;
}
static void updateHover(int x, int y) {
    const int ui = uiAt(x, y);
    const int key = uiKey(ui);
    if (key != g_hoverKey) { g_hoverKey = key; g_dirty = true; }
    int h = -1;
    if (ui < 0 || g_ui[ui].kind == UI_SBAR) h = -1;
    if (ui < 0) h = hitFocus(B.doc, makeRc(B), x, y);
    if (h != B.hover) { B.hover = h; g_dirty = true; }
}
static void mouseLeftPress(bool& dragging, int& dragOff) {
    Browser& b = B;
    if (g_quitAsk) { quitClick(); return; }
    if (g_vpMode) { videoClick(); return; }
    if (g_ctx.open) {  // menu contextual: clic en un elemento lo ejecuta; fuera lo cierra
        const int hi = ctxHit(g_mx, g_my);
        if (hi >= 0) ctxRun(hi);
        else if (hi == -1) { g_ctx.open = false; g_dirty = true; }
        return;
    }
    const int ui = uiAt(g_mx, g_my);
    if (ui >= 0) {
        const UiRect r = g_ui[ui];
        switch (r.kind) {
            case UI_TAB: activateTab(r.arg); break;
            case UI_TABCLOSE: closeTab(r.arg); break;
            case UI_NEWTAB: { Browser* t = newTab("about:blank", true); startUrlEdit(*t, "", false); break; }
            case UI_BACK: goBack(b); break;
            case UI_FWD: goForward(b); break;
            case UI_RELOAD: reload(b); break;
            case UI_HOME: navigate(b, homeUrl(), true); break;
            case UI_OMNI: startUrlEdit(b, isInternal(b.url) ? "" : b.url, true); break;
            case UI_STAR: toggleBookmark(b); break;
            case UI_BMARKS: navigate(b, "about:bookmarks", true); break;
            case UI_MENU: navigate(b, "about:settings", true); break;
            case UI_POWER: quitAsk(); break;
            case UI_SBAR:
                if (g_thumbH > 0 && g_my >= g_thumbY && g_my < g_thumbY + g_thumbH) { dragging = true; dragOff = g_my - g_thumbY; }
                else scrollBy(b, g_my < g_thumbY ? -VIEW_H * 9 / 10 : VIEW_H * 9 / 10);
                break;
            default: break;
        }
        return;
    }
    if (b.urlEdit) { b.urlEdit = false; g_dirty = true; }
    const int fi = hitFocus(b.doc, makeRc(b), g_mx, g_my);
    if (fi >= 0 && b.doc.focus[fi].kind == 0 && (g_modCtrl || g_modAlt)) {  // Ctrl+clic = pestana nueva; Alt+clic = descargar
        const std::string t = focusTarget(b, fi);
        if (urlIsHttp(t)) {
            if (g_modAlt) handleAction(b, "dlgo:" + t);
            else followLink(b, b.doc.focus[fi].ref, true);
            return;
        }
    }
    if (fi >= 0) activateFocus(b, fi);
    else if (b.editField >= 0) { b.editField = -1; g_dirty = true; }
}
int main(int, char**) {
    openLog();
    psb_log("== psbrowser v0.8.3 ==");
    configLoad();
    bookmarksLoad();
    historyLoad();
    clampCfg();  // aplica el buscador y el User-Agent guardados
    dlListLoad();
    g_th = g_cfg.dark ? themeDark() : themeLight();
    if (!te_init()) { psb_log("te_init FALLO (fuentes)"); return 1; }
    SDL_Init(0);
    const struct { Uint32 f; const char* n; } subs[] = {{SDL_INIT_VIDEO, "VIDEO"}, {SDL_INIT_JOYSTICK, "JOYSTICK"}, {SDL_INIT_EVENTS, "EVENTS"}};
    for (const auto& s : subs) {
        int rc = SDL_InitSubSystem(s.f);
        psb_log("SDL_InitSubSystem(%s) = %d %s", s.n, rc, rc ? SDL_GetError() : "ok");
    }
    SDL_Window* win = SDL_CreateWindow("psbrowser", 0, 0, W, H, 0);
    g_win = win;
    g_ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!g_ren) { psb_log("sin renderer: %s", SDL_GetError()); return 1; }
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!g_tex) { psb_log("sin textura: %s", SDL_GetError()); return 1; }
    g_curTex = makeCursor(g_ren);
    Canvas cv(W, H);
    g_cv = &cv;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_Joystick* j = SDL_JoystickOpen(i);
        psb_log("joystick %d: %s", i, j && SDL_JoystickName(j) ? SDL_JoystickName(j) : "?");
    }
    Keyboard* kb = new Keyboard();
    const bool kbOk = kb->Init(-1);
    psb_log("Keyboard::Init(-1) = %s", kbOk ? "ok" : "FALLO");
    const bool netOk = net_init();
    imgw_start();
    psb_log("imgw_start() lanzado (hilo de imagenes)");
    newTab(homeUrl(), true);
    if (!netOk) toast("Red no disponible (mira log.txt)", true);
    present();
    bool mouseTried = false, dragging = false;
    int dragOff = 0;
    uint32_t prevButtons = 0;
    std::vector<Held> held;
    int padAxis = 0;
    float padAcc = 0;
    unsigned long frames = 0;
    uint32_t lastNow = SDL_GetTicks(), lastPump = 0, lastToggle = 0;
    bool running = true;
    while (running) {
        frames++;
        const uint32_t now = SDL_GetTicks();
        const float dt = static_cast<float>(std::max<uint32_t>(1, std::min<uint32_t>(100, now - lastNow)));
        lastNow = now;
        bool moved = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_JOYBUTTONDOWN:
                    psb_log("boton %d", e.jbutton.button);
                    if (g_quitAsk) {  // X = salir, O = cancelar
                        if (e.jbutton.button == 0) g_quit = true;
                        else if (e.jbutton.button == 1) { g_quitAsk = false; g_dirty = true; }
                        break;
                    }
                    if (g_vpMode) { videoPad(e.jbutton.button); break; }
                    if (g_ctx.open) {  // menu contextual con el mando: cruceta = mover, X = elegir, O = cerrar
                        if (e.jbutton.button == 0) { if (g_ctx.sel >= 0) ctxRun(g_ctx.sel); }
                        else if (e.jbutton.button == 1 || e.jbutton.button == 11) { g_ctx.open = false; g_dirty = true; }
                        break;
                    }
                    if (e.jbutton.button == 11 && !g_osk.open) { ctxOpenAtFocus(); break; }  // boton 11 (sin funcion hasta ahora) = menu contextual
                    if (g_osk.open) {  // el teclado en pantalla captura el mando
                        OskResult r = OSK_NONE;
                        switch (e.jbutton.button) {
                            case 0: r = osk_input(g_osk, OSK_SELECT); break;
                            case 1: r = osk_input(g_osk, OSK_BACKSPACE); break;
                            case 2: r = osk_input(g_osk, OSK_SPACE); break;
                            case 3: r = osk_input(g_osk, OSK_SHIFT); break;
                            case 4: r = osk_input(g_osk, OSK_CURSOR_L); break;
                            case 5: r = osk_input(g_osk, OSK_CURSOR_R); break;
                            case 9: r = OSK_SUBMITTED; break;  // Options = Enter (indice supuesto)
                            default: break;
                        }
                        oskApply(B, r);
                        break;
                    }
                    if (e.jbutton.button == 8 || e.jbutton.button == 9) { oskOpenUrl(B); break; }  // Share/Options (supuesto)
                    switch (e.jbutton.button) {
                        case 0:
                            if (B.sel >= 0) {
                                activateFocus(B, B.sel);
                                if (B.editField >= 0) oskOpenField(B, B.editField);
                            }
                            break;
                        case 1: goBack(B); break;
                        case 2: reload(B); break;
                        case 3: navigate(B, homeUrl(), true); break;
                        case 4: scrollBy(B, -VIEW_H * 9 / 10); break;
                        case 5: scrollBy(B, VIEW_H * 9 / 10); break;
                        default: break;
                    }
                    break;
                case SDL_JOYHATMOTION:
                    psb_log("hat %d", e.jhat.value);
                    if (g_quitAsk) break;
                    if (g_vpMode) { videoHat(e.jhat.value); break; }
                    if (g_ctx.open) {
                        if (e.jhat.value & SDL_HAT_UP) ctxMove(-1);
                        if (e.jhat.value & SDL_HAT_DOWN) ctxMove(1);
                        break;
                    }
                    if (g_osk.open) {
                        OskResult r = OSK_NONE;
                        if (e.jhat.value & SDL_HAT_UP) r = osk_input(g_osk, OSK_UP);
                        if (e.jhat.value & SDL_HAT_DOWN) r = osk_input(g_osk, OSK_DOWN);
                        if (e.jhat.value & SDL_HAT_LEFT) r = osk_input(g_osk, OSK_LEFT);
                        if (e.jhat.value & SDL_HAT_RIGHT) r = osk_input(g_osk, OSK_RIGHT);
                        oskApply(B, r);
                        g_dirty = true;
                        break;
                    }
                    if (e.jhat.value & SDL_HAT_UP) moveFocus(B, -1);
                    if (e.jhat.value & SDL_HAT_DOWN) moveFocus(B, 1);
                    if (e.jhat.value & SDL_HAT_LEFT) goBack(B);
                    if (e.jhat.value & SDL_HAT_RIGHT) goForward(B);
                    break;
                case SDL_JOYAXISMOTION:
                    if (e.jaxis.axis == 1 || e.jaxis.axis == 3) padAxis = (!g_osk.open && !g_vpMode && !g_quitAsk && std::abs(e.jaxis.value) > 6000) ? e.jaxis.value : 0;
                    break;
                default: break;
            }
        }
        if (g_quit) running = false;
        if (padAxis != 0) {  // stick izquierdo: scroll continuo proporcional (hasta ~1500 px/s)
            const float v = (std::abs(padAxis) - 6000) / 26767.0f;
            padAcc += (padAxis < 0 ? -1.0f : 1.0f) * v * v * 1.5f * dt + (padAxis < 0 ? -1.0f : 1.0f) * v * 0.2f * dt;
            const int step = static_cast<int>(padAcc);
            if (step != 0) { padAcc -= step; scrollTo(B, B.scrollTarget + step, true); }
        } else padAcc = 0;
        // Teclado nativo: flancos de subida + repeticion.
        if (kbOk) {
            std::vector<uint16_t> cur;
            for (uint16_t k : kb->Keys()) if (k != 0) cur.push_back(k);
            const uint32_t mods = (kb->CheckModifierKey(Keyboard::SHIFT) ? (ORBIS_KEYBOARD_MOD_LEFT_SHIFT) : 0) |
                                  (kb->CheckModifierKey(Keyboard::CTRL) ? (ORBIS_KEYBOARD_MOD_LEFT_CTRL) : 0) |
                                  (kb->CheckModifierKey(Keyboard::ALT) ? (ORBIS_KEYBOARD_MOD_LEFT_ALT) : 0);
            g_modCtrl = (mods & ORBIS_KEYBOARD_MOD_LEFT_CTRL) != 0;
            g_modAlt = (mods & ORBIS_KEYBOARD_MOD_LEFT_ALT) != 0;
            g_modShift = (mods & ORBIS_KEYBOARD_MOD_LEFT_SHIFT) != 0;
            for (uint16_t k : cur) {
                auto it = std::find_if(held.begin(), held.end(), [k](const Held& h) { return h.code == k; });
                if (it == held.end()) {
                    held.push_back({k, now, now});
                    onKeyDown(k, kb->Key2Char(k), mods);
                } else if (repeatable(k) && now - it->downAt > 400 && now - it->lastRep > 55) {
                    it->lastRep = now;
                    onKeyDown(k, kb->Key2Char(k), mods);
                }
            }
            held.erase(std::remove_if(held.begin(), held.end(),
                                      [&cur](const Held& h) { return std::find(cur.begin(), cur.end(), h.code) == cur.end(); }),
                       held.end());
        }
        // Raton nativo (se inicializa tras unos frames para que la pantalla arranque aunque falle).
        if (!mouseTried && frames > 30) {
            mouseTried = true;
            g_mouseOk = psmouse_init(mouseLog);
            psb_log("psmouse_init = %s", g_mouseOk ? "ok" : "FALLO");
        }
        if (g_mouseOk) {
            PsMouseState ms;
            if (psmouse_poll(&ms)) {
                if (ms.dx || ms.dy) {
                    float gain = g_cfg.mouseSens;
                    if (g_cfg.mouseAccel) {  // movimientos rapidos (muchas cuentas por lectura) avanzan mas
                        const float mag = std::sqrt(static_cast<float>(ms.dx * ms.dx + ms.dy * ms.dy));
                        gain *= 1.0f + std::min(mag, 12.0f) * 0.12f;
                    }
                    static float fx = 0, fy = 0;  // resto fraccionario
                    fx += ms.dx * gain;
                    fy += ms.dy * gain;
                    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
                    fx -= ix;
                    fy -= iy;
                    g_mx = std::max(0, std::min(W - 1, g_mx + ix));
                    g_my = std::max(0, std::min(H - 1, g_my + iy));
                    moved = true;
                    if (g_vpMode) g_vpLastAct = now;  // mover el raton muestra los controles del video
                    if (g_vpMode || g_quitAsk) g_dirty = true;
                    if (dragging && (ms.buttons & 1) && g_thumbH > 0) {
                        const int track = VIEW_H - g_thumbH;
                        const float r = track > 0 ? static_cast<float>(g_my - dragOff - VIEW_Y) / track : 0.0f;
                        scrollTo(B, static_cast<int>(std::max(0.0f, std::min(1.0f, r)) * maxScroll(B)), true);
                    } else if (g_ctx.open) {
                        const int hi = ctxHit(g_mx, g_my);
                        const int ns = (hi >= 0 && g_ctx.items[hi].enabled) ? hi : -1;
                        if (ns != g_ctx.sel) { g_ctx.sel = ns; g_dirty = true; }
                    } else {
                        updateHover(g_mx, g_my);
                    }
                }
                if (ms.wheel && g_vpMode && !g_quitAsk) { vp_volume_rel(ms.wheel > 0 ? 5 : -5); vpVolToast(); g_vpLastAct = now; }
                if (ms.wheel && !g_osk.open && !g_ctx.open && !g_vpMode && !g_quitAsk) {
                    static int wlog = 0;
                    if (wlog < 10) { wlog++; psb_log("rueda: %d", ms.wheel); }
                    scrollBy(B, ((ms.wheel > 0) != g_cfg.wheelInvert ? -1 : 1) * g_cfg.wheelStep * 40);
                }
                const uint32_t pressed = ms.buttons & ~prevButtons;
                if ((pressed & 1) && !g_osk.open) mouseLeftPress(dragging, dragOff);
                if (!(ms.buttons & 1)) dragging = false;
                if ((pressed & 2) && !g_osk.open && !g_vpMode && !g_quitAsk) ctxOpen(g_mx, g_my);  // clic derecho = menu contextual (Atras esta dentro del menu)
                if ((pressed & 8) && !g_osk.open && !g_ctx.open && !g_vpMode && !g_quitAsk) goBack(B);     // botones laterales (si el raton los tiene)
                if ((pressed & 16) && !g_osk.open && !g_ctx.open && !g_vpMode && !g_quitAsk) goForward(B);
                if ((pressed & 4) && !g_ctx.open && !g_vpMode && !g_quitAsk) {  // central: enlace en pestana nueva / cerrar pestana
                    const int ui = uiAt(g_mx, g_my);
                    if (ui >= 0 && g_ui[ui].kind == UI_TAB) closeTab(g_ui[ui].arg);
                    else {
                        const int fi = hitFocus(B.doc, makeRc(B), g_mx, g_my);
                        if (fi >= 0 && B.doc.focus[fi].kind == 0) followLink(B, B.doc.focus[fi].ref, true);
                    }
                }
                prevButtons = ms.buttons;
            }
        }
        // Scroll suave: la posicion mostrada persigue al objetivo (constante de tiempo ~60 ms).
        {
            Browser& b = B;
            const float diff = static_cast<float>(b.scrollTarget) - b.scrollY;
            if (std::fabs(diff) > 0.01f) {
                if (g_cfg.smooth && std::fabs(diff) > 0.6f) b.scrollY += diff * (1.0f - std::exp(-dt / 60.0f));
                else b.scrollY = static_cast<float>(b.scrollTarget);
                g_dirty = true;
            }
        }
        // Cursor de texto parpadeante y caducidad de avisos.
        if (now - lastToggle > 500) {
            lastToggle = now;
            g_caret = !g_caret;
            if (B.urlEdit || B.editField >= 0 || g_osk.open) g_dirty = true;
        }
        if (g_toastUntil && now > g_toastUntil) { g_toastUntil = 0; g_toast.clear(); g_dirty = true; }
        if (now - lastPump > 120) { lastPump = now; if (!g_vpMode) pumpImages(); }
        if (g_vpMode) {  // fotogramas nuevos, cambios de estado y reloj del overlay
            if (!vp_active()) { g_vpMode = false; vidTexFree(); imgw_pause(false); g_dirty = true; }
            else {
                if (vp_update()) g_dirty = true;
                const VpInfo vi = vp_info();
                static VpState lastSt = VP_CLOSED;
                static bool lastOv = false;
                static uint32_t lastClk = 0;
                const bool ov = vpOverlayVisible(now, vi);
                if (vi.state != lastSt) { lastSt = vi.state; g_dirty = true; }
                if (ov != lastOv) { lastOv = ov; g_dirty = true; }
                if (ov && now - lastClk > 250) { lastClk = now; g_dirty = true; }
            }
        }
        uint32_t fUp = 0, fPres = 0;
        const uint32_t fT0 = SDL_GetTicks();
        if (g_dirty && vpCleanPlayback(now) && presentVideoFast(&fUp, &fPres)) {
            g_dirty = false;
            static int fperf = 0;
            if (fperf < 90 && (fperf < 30 || fperf % 5 == 0)) psb_log("[perfv] RAPIDO t=%u ms subida=%u presentacion=%u (total %u ms)", SDL_GetTicks(), fUp, fPres, SDL_GetTicks() - fT0);
            fperf++;
        } else if (g_dirty) {
            g_vidFastPrev = false;
            const uint32_t t0 = SDL_GetTicks();
            drawFrame();
            const uint32_t t1 = SDL_GetTicks();
            SDL_UpdateTexture(g_tex, nullptr, cv.px.data(), W * 4);
            const uint32_t t1b = SDL_GetTicks();
            renderFrame();
            const uint32_t t2 = SDL_GetTicks();
            g_dirty = false;
            static int perf = 0, vperf = 0;
            if (perf < 40) { perf++; psb_log("[perf] dibujo=%u ms, subida+presentacion=%u ms (frame %d)", t1 - t0, t2 - t1, perf); }
            if (g_vpMode && vperf < 90 && (vperf < 30 || vperf % 5 == 0)) {  // en video: dibujo / UpdateTexture / RenderCopy+Present por separado (reloj SDL_GetTicks)
                psb_log("[perfv] t=%u ms dibujo=%u subida=%u presentacion=%u (total %u ms)", t2, t1 - t0, t1b - t1, t2 - t1b, t2 - t0);
            }
            if (g_vpMode) vperf++;
        } else if (moved) {
            renderFrame();
        }
        SDL_Delay(4);
    }
    hardExit();  // no vuelve: _exit(0)
    return 0;
}
