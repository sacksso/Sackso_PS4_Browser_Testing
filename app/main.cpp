// psbrowser v0.4.0 - "navegador de escritorio" para PS4: pestanas, barra de herramientas con omnibox, tema claro/oscuro,
// fuente TrueType proporcional, scroll suave en pixeles, imagenes (hilo aparte), formularios GET, anclas y configuracion.
//
// Teclado USB: Ctrl+T pestana nueva, Ctrl+W cerrar, Ctrl+RePag/AvPag o Ctrl+Tab cambiar; F6 o Ctrl+L = barra de direcciones;
//   Tab / Shift+Tab = siguiente/anterior enlace o campo; Enter = activar (en un campo de texto: enviar el formulario);
//   Backspace o Alt+Izq = atras; Alt+Der = adelante; F5 = recargar; F10 = configuracion; Ctrl+D = marcador;
//   Ctrl+'+' / Ctrl+'-' / Ctrl+0 = zoom del texto; flechas, RePag, AvPag, Inicio, Fin, Espacio = desplazar.
// Raton USB: clic = enlaces/botones/pestanas; clic central en un enlace = pestana nueva; clic derecho = atras; rueda = desplazar;
//   la barra de desplazamiento se arrastra.
// Mando (SDL): cruceta = foco/atras/adelante; boton 0 = activar, 1 = atras, 2 = recargar, 3 = inicio, 4/5 = pagina; stick = scroll.
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
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>
#include "canvas.h"
#include "htmldoc.h"
#include "imgworker.h"
#include "keyboard.h"  // copiado por el workflow desde samples/keyboard/keyboard/ de OpenOrbis
#include "log.h"
#include "mouse.h"
#include "netclient.h"
#include "pagerender.h"
#include "textengine.h"
#include "theme.h"
#include "url.h"
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
    K_D = 7, K_L = 15, K_T = 23, K_W = 26, K_0 = 39, K_RETURN = 40, K_ESC = 41, K_BACKSPACE = 42, K_TAB = 43, K_SPACE = 44,
    K_MINUS = 45, K_EQUAL = 46, K_F5 = 62, K_F6 = 63, K_F10 = 67, K_HOME = 74, K_PGUP = 75, K_END = 77, K_PGDOWN = 78,
    K_RIGHT = 79, K_LEFT = 80, K_DOWN = 81, K_UP = 82
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
}
static void configSave() {
    FILE* f = fopen(kCfgPath, "w");
    if (!f) return;
    fprintf(f, "mouseSens=%.2f\nmouseAccel=%d\nwheelInvert=%d\nwheelStep=%d\nhome=%s\ndark=%d\nzoom=%d\npageW=%d\nsmooth=%d\nimages=%d\n",
            g_cfg.mouseSens, g_cfg.mouseAccel ? 1 : 0, g_cfg.wheelInvert ? 1 : 0, g_cfg.wheelStep, g_cfg.home.c_str(), g_cfg.dark ? 1 : 0,
            g_cfg.zoom, g_cfg.pageW, g_cfg.smooth ? 1 : 0, g_cfg.images ? 1 : 0);
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
    }
    fclose(f);
    clampCfg();
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
static std::string genHome() {
    std::string h =
        "<html><head><title>Inicio</title></head><body><h1>psbrowser</h1>"
        "<form action=\"https://html.duckduckgo.com/html/\" method=\"get\">"
        "<input type=\"text\" name=\"q\" placeholder=\"Buscar con DuckDuckGo\"> <input type=\"submit\" value=\"Buscar\"></form>"
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
         "<li>Teclado: Ctrl+T nueva pestana, Ctrl+W cerrar, F6 o Ctrl+L direccion, Tab/Enter enlaces y campos, Backspace atras, F5 recargar, F10 configuracion, Ctrl+D marcador, Ctrl +/- zoom.</li>"
         "<li>Raton: clic en enlaces, botones y pestanas; clic central abre en pestana nueva; clic derecho = atras; rueda = desplazar.</li>"
         "<li>Mando: cruceta = foco / atras / adelante; boton 0 activar, 1 atras, 2 recargar, 3 inicio, 4/5 pagina; stick = desplazar.</li>"
         "</ul><p>Sin JavaScript, sin video/audio y con CSS limitado. Los formularios con POST se envian como GET.</p></body></html>";
    return h;
}
static std::string genSettings() {
    char b[4200];
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
             "<h2>Otros</h2><p><a href=\"about:bookmarks\">Marcadores</a> | <a href=\"about:home\">Inicio</a></p>"
             "<p>Los cambios se guardan al momento en /data/psbrowser/config.txt. Mueve el raton para probar la sensibilidad.</p>"
             "</body></html>",
             g_cfg.dark ? "oscuro" : "claro", g_cfg.zoom, g_cfg.pageW, g_cfg.smooth ? "si" : "no", g_cfg.images ? "si" : "no", g_cfg.mouseSens,
             g_cfg.mouseAccel ? "si" : "no", g_cfg.wheelInvert ? "si" : "no", g_cfg.wheelStep, htmlEscape(homeUrl()).c_str());
    return b;
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
static std::string genInternal(const std::string& url) {
    if (url == "about:settings") return genSettings();
    if (url == "about:bookmarks") return genBookmarks();
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
static bool isMediaUrl(const std::string& url) {
    std::string p = url.substr(0, url.find_first_of("?#"));
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* ext[] = {".mp4", ".m3u8", ".webm", ".mkv", ".mov", ".avi", ".mp3", ".m4a", ".ogg", ".flv"};
    for (const char* e : ext) {
        size_t n = std::strlen(e);
        if (p.size() >= n && p.compare(p.size() - n, n, e) == 0) return true;
    }
    return false;
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
static void setPage(Browser& b, const std::string& url, std::string html, int kind) {
    b.url = url;
    b.html = std::move(html);
    b.kind = kind;
    layoutDoc(b);
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
static void load(Browser& b, const std::string& urlIn, int depth = 0) {
    std::string url = urlIn, frag;
    size_t h = url.find('#');
    if (h != std::string::npos) { frag = url.substr(h + 1); url = url.substr(0, h); }
    psb_log("[nav] %s", urlIn.c_str());
    if (isInternal(url)) {
        setPage(b, url, genInternal(url), 2);
        if (!frag.empty()) jumpAnchor(b, frag);
        return;
    }
    if (isMediaUrl(url)) {
        setPage(b, url, infoHtml("Video/audio aun no soportado", "Este enlace apunta a un archivo multimedia. La reproduccion todavia no esta implementada.", url), 0);
        return;
    }
    g_loading = true;
    present();
    HttpResult res;
    const bool ok = http_get(url, res, 2 * 1024 * 1024);
    g_loading = false;
    if (!ok) {
        char code[64];
        snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(res.sceError));
        setPage(b, url, infoHtml("No se pudo cargar la pagina", std::string("Paso: ") + res.stage + " - codigo " + code, url), 0);
        toast("Error al cargar la pagina", true);
        return;
    }
    const std::string ct = res.contentType;
    const std::string finalUrl = res.finalUrl;
    const size_t bytes = res.body.size();
    const bool truncated = res.truncated;
    const int status = res.status;
    if (ct.empty() || containsCI(ct, "html")) setPage(b, finalUrl, std::move(res.body), 0);
    else if (containsCI(ct, "text/") || containsCI(ct, "json") || containsCI(ct, "xml")) setPage(b, finalUrl, std::move(res.body), 1);
    else {
        char info[220];
        snprintf(info, sizeof(info), "Tipo no soportado: %s (%zu bytes). Aun no hay descargas ni este tipo de contenido.", ct.c_str(), bytes);
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
    const size_t q = action.find('?');
    if (q != std::string::npos) action = action.substr(0, q);
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
    if (f.post) toast("Formulario POST: se envia como GET (aun no hay POST)", true);
    navigate(b, query.empty() ? action : action + "?" + query, true);
}
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
enum UiKind { UI_TAB = 1, UI_TABCLOSE, UI_NEWTAB, UI_BACK, UI_FWD, UI_RELOAD, UI_HOME, UI_OMNI, UI_STAR, UI_BMARKS, UI_MENU, UI_SBAR };
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
    if (isInternal(b.url)) return b.url == "about:settings" ? "Configuracion" : (b.url == "about:bookmarks" ? "Marcadores" : "Inicio");
    return hostOf(b.url);
}
static void startUrlEdit(Browser& b, const std::string& initial, bool fresh) {
    b.urlEdit = true;
    b.urlFresh = fresh;
    b.urlText = initial;
    b.editField = -1;
    g_dirty = true;
}
static RenderCtx makeRc(Browser& b) {
    RenderCtx rc{&g_th, 0, VIEW_Y, W - SBAR_W, VIEW_H, contentLeft(), static_cast<int>(b.scrollY + 0.5f)};
    rc.focus = b.sel;
    rc.hover = b.hover;
    rc.editField = b.editField;
    rc.caretOn = g_caret;
    return rc;
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
    const float cxMenu = static_cast<float>(W - 36), cxBm = static_cast<float>(W - 82);
    if (g_hoverKey == UI_MENU * 1000) cv.circle(cxMenu, cyb, 19, th.tabHover);
    for (int i = -1; i <= 1; i++) cv.rect(static_cast<int>(cxMenu) - 8, static_cast<int>(cyb) + i * 6 - 1, 16, 2, th.ink);
    addUi(static_cast<int>(cxMenu) - 20, TOOLBAR_Y + 4, 40, 40, UI_MENU, 0);
    if (g_hoverKey == UI_BMARKS * 1000) cv.circle(cxBm, cyb, 19, th.tabHover);
    cv.line(cxBm - 6, cyb - 9, cxBm + 6, cyb - 9, 2.0f, th.ink); cv.line(cxBm + 6, cyb - 9, cxBm + 6, cyb + 9, 2.0f, th.ink);
    cv.line(cxBm + 6, cyb + 9, cxBm, cyb + 4, 2.0f, th.ink); cv.line(cxBm, cyb + 4, cxBm - 6, cyb + 9, 2.0f, th.ink); cv.line(cxBm - 6, cyb + 9, cxBm - 6, cyb - 9, 2.0f, th.ink);
    addUi(static_cast<int>(cxBm) - 20, TOOLBAR_Y + 4, 40, 40, UI_BMARKS, 0);
    // omnibox
    const int ox = 14 + 4 * 44 + 12, ow = W - 120 - ox, oy = TOOLBAR_Y + 6, oh = 36;
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
static void drawFrame() {
    Canvas& cv = *g_cv;
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
}
static void renderFrame() {
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, nullptr, nullptr);
    if (g_mouseOk && g_curTex) {
        SDL_Rect dst = {g_mx, g_my, 12 * 2, 17 * 2};
        SDL_RenderCopy(g_ren, g_curTex, nullptr, &dst);
    }
    SDL_RenderPresent(g_ren);
}
static void present() {
    drawFrame();
    SDL_UpdateTexture(g_tex, nullptr, g_cv->px.data(), W * 4);
    renderFrame();
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
            else if (!b.urlText.empty()) b.urlText.pop_back();
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
            if (!f.value.empty()) f.value.pop_back();
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
        switch (code) {
            case K_T: { Browser* t = newTab("about:blank", true); startUrlEdit(*t, "", false); return; }
            case K_W: closeTab(g_cur); return;
            case K_PGUP: activateTab((g_cur + n - 1) % n); return;
            case K_PGDOWN: activateTab((g_cur + 1) % n); return;
            case K_TAB: activateTab(shift ? (g_cur + n - 1) % n : (g_cur + 1) % n); return;
            case K_L: startUrlEdit(b, isInternal(b.url) ? "" : b.url, true); return;
            case K_D: toggleBookmark(b); return;
            case K_EQUAL: changeZoom(10); return;
            case K_MINUS: changeZoom(-10); return;
            case K_0: changeZoom(0, true); return;
            default: break;
        }
    }
    switch (code) {
        case K_F6: startUrlEdit(b, isInternal(b.url) ? "" : b.url, true); return;
        case K_F5: reload(b); return;
        case K_F10: navigate(b, "about:settings", true); return;
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
        case K_HOME: scrollTo(b, 0, false); return;
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
    if (fi >= 0) activateFocus(b, fi);
    else if (b.editField >= 0) { b.editField = -1; g_dirty = true; }
}
int main(int, char**) {
    openLog();
    psb_log("== psbrowser v0.4.0 ==");
    configLoad();
    bookmarksLoad();
    g_th = g_cfg.dark ? themeDark() : themeLight();
    if (!te_init()) { psb_log("te_init FALLO (fuentes)"); return 1; }
    SDL_Init(0);
    const struct { Uint32 f; const char* n; } subs[] = {{SDL_INIT_VIDEO, "VIDEO"}, {SDL_INIT_JOYSTICK, "JOYSTICK"}, {SDL_INIT_EVENTS, "EVENTS"}};
    for (const auto& s : subs) {
        int rc = SDL_InitSubSystem(s.f);
        psb_log("SDL_InitSubSystem(%s) = %d %s", s.n, rc, rc ? SDL_GetError() : "ok");
    }
    SDL_Window* win = SDL_CreateWindow("psbrowser", 0, 0, W, H, 0);
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
                    switch (e.jbutton.button) {
                        case 0: if (B.sel >= 0) activateFocus(B, B.sel); break;
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
                    if (e.jhat.value & SDL_HAT_UP) moveFocus(B, -1);
                    if (e.jhat.value & SDL_HAT_DOWN) moveFocus(B, 1);
                    if (e.jhat.value & SDL_HAT_LEFT) goBack(B);
                    if (e.jhat.value & SDL_HAT_RIGHT) goForward(B);
                    break;
                case SDL_JOYAXISMOTION:
                    if (e.jaxis.axis == 1 || e.jaxis.axis == 3) padAxis = std::abs(e.jaxis.value) > 6000 ? e.jaxis.value : 0;
                    break;
                default: break;
            }
        }
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
                    if (dragging && (ms.buttons & 1) && g_thumbH > 0) {
                        const int track = VIEW_H - g_thumbH;
                        const float r = track > 0 ? static_cast<float>(g_my - dragOff - VIEW_Y) / track : 0.0f;
                        scrollTo(B, static_cast<int>(std::max(0.0f, std::min(1.0f, r)) * maxScroll(B)), true);
                    } else {
                        updateHover(g_mx, g_my);
                    }
                }
                if (ms.wheel) {
                    static int wlog = 0;
                    if (wlog < 10) { wlog++; psb_log("rueda: %d", ms.wheel); }
                    scrollBy(B, ((ms.wheel > 0) != g_cfg.wheelInvert ? -1 : 1) * g_cfg.wheelStep * 40);
                }
                const uint32_t pressed = ms.buttons & ~prevButtons;
                if (pressed & 1) mouseLeftPress(dragging, dragOff);
                if (!(ms.buttons & 1)) dragging = false;
                if (pressed & 2) goBack(B);
                if (pressed & 4) {  // central: enlace en pestana nueva / cerrar pestana
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
            if (B.urlEdit || B.editField >= 0) g_dirty = true;
        }
        if (g_toastUntil && now > g_toastUntil) { g_toastUntil = 0; g_toast.clear(); g_dirty = true; }
        if (now - lastPump > 120) { lastPump = now; pumpImages(); }
        if (g_dirty) {
            const uint32_t t0 = SDL_GetTicks();
            drawFrame();
            const uint32_t t1 = SDL_GetTicks();
            SDL_UpdateTexture(g_tex, nullptr, cv.px.data(), W * 4);
            renderFrame();
            const uint32_t t2 = SDL_GetTicks();
            g_dirty = false;
            static int perf = 0;
            if (perf < 40) { perf++; psb_log("[perf] dibujo=%u ms, subida+presentacion=%u ms (frame %d)", t1 - t0, t2 - t1, perf); }
        } else if (moved) {
            renderFrame();
        }
        SDL_Delay(4);
    }
    net_shutdown();
    return 0;
}
