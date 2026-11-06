// psbrowser v0.3.0 - visor de paginas con enlaces, barra de botones, configuracion (about:settings), marcadores
// (about:bookmarks), barra de URL, historial, teclado y raton nativos. Atajos nuevos: F10 = configuracion, Ctrl+D = marcador.
//
// Teclado USB (libSceKeyboard): F6 o Ctrl+L = barra de URL (Enter va, Esc cancela); escribir en la pagina tambien abre la barra;
//   Tab / Shift+Tab = siguiente/anterior enlace; Enter = abrir enlace; Backspace o Alt+Izq = atras; Alt+Der = adelante;
//   flechas / RePag / AvPag / Inicio / Fin / Espacio = desplazar; F5 = recargar.
// Raton USB (libSceMouse): clic izq = abrir enlace o enfocar la barra de URL; clic der = atras; rueda = desplazar.
// Mando (SDL): cruceta arriba/abajo = enlace anterior/siguiente; izq = atras; der = adelante; boton 0 = abrir, 1 = atras,
//   2 = recargar, 3 = inicio, 4/5 = pagina arriba/abajo; stick = desplazar. Los indices de boton se registran en el log.
// Todo se registra en /data/psbrowser/log.txt.
#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <vector>
#include <cmath>

#include "canvas.h"
#include "htmldoc.h"
#include "keyboard.h"  // copiado por el workflow desde samples/keyboard/keyboard/ de OpenOrbis
#include "log.h"
#include "mouse.h"
#include "netclient.h"
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

// ------------------------------------------------------------------ geometria y colores
static const int W = 1920, H = 1080, SCALE = 2;
static const int CHAR_W = 8 * SCALE, LINE_H = 8 * SCALE + 4;
static const int MARGIN = 32, TOP = 112, BOTTOM = 48;
static const int URLBAR_Y = 38, URLBAR_H = 30;
static const int BTN_W = 44, BTN_GAP = 6, NBTN = 7;
static const int URLBAR_X = MARGIN + NBTN * (BTN_W + BTN_GAP);
static const char* kBtnLabel[NBTN] = {"<", ">", "R", "H", "+", "B", "C"};  // atras, adelante, recargar, inicio, marcar, marcadores, config
static const size_t COLS = (W - 2 * MARGIN - 20) / CHAR_W;
static const int ROWS = (H - TOP - BOTTOM) / LINE_H;

static const uint32_t C_BG = 0xFF101418, C_FG = 0xFFE6E6E6, C_DIM = 0xFF8A94A0, C_ACC = 0xFF6AB0FF, C_LINK = 0xFF6AB0FF,
                      C_HEAD = 0xFFFFD479, C_BOLD = 0xFFFFFFFF, C_PRE = 0xFFA8D8A8, C_BAD = 0xFFFF5F5F, C_BOX = 0xFF1C242C,
                      C_BOXEDIT = 0xFF26343F;

// Codigos de tecla USB HID (los mismos que usa OpenOrbis en orbis/_types/keyboard.h).
enum {
    K_D = 7, K_L = 15, K_F10 = 67, K_RETURN = 40, K_ESC = 41, K_BACKSPACE = 42, K_TAB = 43, K_SPACE = 44, K_F5 = 62, K_F6 = 63, K_HOME = 74,
    K_PGUP = 75, K_END = 77, K_PGDOWN = 78, K_RIGHT = 79, K_LEFT = 80, K_DOWN = 81, K_UP = 82
};

// ------------------------------------------------------------------ estado del navegador
struct HistEntry {
    std::string req;       // URL pedida
    std::string finalUrl;  // URL final (tras redirecciones)
    Document doc;          // documento guardado al salir de la pagina (para volver sin recargar)
    int scroll = 0;
};

struct Browser {
    Document doc;
    std::string url;
    std::vector<HistEntry> hist;
    int histPos = -1;
    int scroll = 0;
    int sel = -1;    // enlace seleccionado (teclado/mando)
    int hover = -1;  // enlace bajo el raton
    std::string status;
    uint32_t statusColor = C_DIM;
    bool urlEdit = false, urlFresh = false;
    std::string urlText;
    bool dirty = true;
};
static Browser B;

// ------------------------------------------------------------------ configuracion y marcadores
struct Config {
    float mouseSens = 3.0f;   // multiplicador del movimiento crudo del raton (la lectura cruda es de +-1..2 por muestra)
    bool mouseAccel = true;   // aceleracion: movimientos rapidos van mas lejos
    bool wheelInvert = false;
    int wheelStep = 3;        // lineas por muesca de la rueda
    std::string home;         // vacio = about:home
};
static Config g_cfg;
static std::vector<std::pair<std::string, std::string>> g_bookmarks;  // (url, titulo)
static const char* kCfgPath = "/data/psbrowser/config.txt";
static const char* kBmPath = "/data/psbrowser/bookmarks.txt";

static void configSave() {
    FILE* f = fopen(kCfgPath, "w");
    if (!f) return;
    fprintf(f, "mouseSens=%.2f\nmouseAccel=%d\nwheelInvert=%d\nwheelStep=%d\nhome=%s\n", g_cfg.mouseSens, g_cfg.mouseAccel ? 1 : 0,
            g_cfg.wheelInvert ? 1 : 0, g_cfg.wheelStep, g_cfg.home.c_str());
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
    }
    fclose(f);
    g_cfg.mouseSens = std::max(0.5f, std::min(20.0f, g_cfg.mouseSens));
    g_cfg.wheelStep = std::max(1, std::min(10, g_cfg.wheelStep));
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

// Graficos / entrada (globales para que present() pueda usarlos desde navigate()).
static Canvas* g_cv = nullptr;
static SDL_Renderer* g_ren = nullptr;
static SDL_Texture* g_tex = nullptr;
static SDL_Texture* g_curTex = nullptr;
static const char* g_busy = nullptr;
static int g_mx = W / 2, g_my = H / 2;
static bool g_mouseOk = false;
static int g_hoverBtn = -1;

static std::string htmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else o += c;
    }
    return o;
}

static const char* kHomeHtml =
    "<html><head><title>psbrowser - inicio</title></head><body>"
    "<h1>psbrowser</h1>"
    "<p>Navegador de texto para PS4 (homebrew). Escribe una direccion o una busqueda con el teclado, o abre un enlace:</p>"
    "<ul>"
    "<li><a href=\"https://html.duckduckgo.com/html/\">DuckDuckGo (version HTML, para buscar)</a></li>"
    "<li><a href=\"https://en.wikipedia.org/wiki/PlayStation_4\">Wikipedia: PlayStation 4</a></li>"
    "<li><a href=\"https://www.wikipedia.org/\">Wikipedia (portada)</a></li>"
    "<li><a href=\"https://news.ycombinator.com/\">Hacker News</a></li>"
    "<li><a href=\"https://text.npr.org/\">NPR (version de texto)</a></li>"
    "<li><a href=\"https://info.cern.ch/\">info.cern.ch (la primera web)</a></li>"
    "<li><a href=\"http://example.com/\">example.com</a></li>"
    "<li><a href=\"about:bookmarks\">Marcadores</a></li>"
    "<li><a href=\"about:settings\">Configuracion (sensibilidad del raton, rueda, inicio)</a></li>"
    "</ul>"
    "<h2>Controles</h2>"
    "<ul>"
    "<li>Teclado: F6 o Ctrl+L = barra de URL; Tab = siguiente enlace; Enter = abrir; Backspace = atras; F5 = recargar; flechas, RePag/AvPag = desplazar.</li>"
    "<li>Raton: clic izquierdo = abrir enlace; clic derecho = atras; rueda = desplazar.</li>"
    "<li>Mando: cruceta arriba/abajo = enlace anterior/siguiente; izquierda = atras; derecha = adelante; boton 0 = abrir; 1 = atras; 2 = recargar; 3 = inicio; 4/5 = pagina; stick = desplazar.</li>"
    "</ul>"
    "<p>Solo texto: sin JavaScript, sin imagenes y sin formularios (de momento). Los caracteres no ASCII salen como ?.</p>"
    "</body></html>";

static std::string settingsHtml() {
    char b[3000];
    snprintf(b, sizeof(b),
             "<html><head><title>Configuracion</title></head><body><h1>Configuracion</h1>"
             "<h2>Raton</h2>"
             "<p>Sensibilidad: <a href=\"psb:sens-1\">[ -- ]</a> <a href=\"psb:sens-\">[ - ]</a> %.1fx <a href=\"psb:sens+\">[ + ]</a> <a href=\"psb:sens+1\">[ ++ ]</a> (de 0.5 a 20; por defecto 3.0)</p>"
             "<p>Aceleracion: <a href=\"psb:accel\">[ %s ]</a></p>"
             "<p>Rueda invertida: <a href=\"psb:wheelinv\">[ %s ]</a></p>"
             "<p>Velocidad de la rueda: <a href=\"psb:wheel-\">[ - ]</a> %d lineas <a href=\"psb:wheel+\">[ + ]</a></p>"
             "<h2>Pagina de inicio</h2>"
             "<p>Actual: %s</p>"
             "<p><a href=\"psb:homehere\">[ usar la pagina anterior como inicio ]</a> <a href=\"psb:homereset\">[ restablecer ]</a></p>"
             "<h2>Otros</h2>"
             "<p><a href=\"about:bookmarks\">Marcadores</a> | <a href=\"about:home\">Inicio</a></p>"
             "<p>Los cambios se guardan al momento en /data/psbrowser/config.txt. Mueve el raton para probar la sensibilidad.</p>"
             "</body></html>",
             g_cfg.mouseSens, g_cfg.mouseAccel ? "si" : "no", g_cfg.wheelInvert ? "si" : "no", g_cfg.wheelStep,
             g_cfg.home.empty() ? "about:home" : htmlEscape(g_cfg.home).c_str());
    return b;
}

static std::string bookmarksHtml() {
    std::string h = "<html><head><title>Marcadores</title></head><body><h1>Marcadores</h1>";
    if (g_bookmarks.empty()) h += "<p>No hay marcadores. Pulsa [+] en la barra o Ctrl+D en una pagina para guardarla.</p>";
    h += "<ul>";
    for (size_t i = 0; i < g_bookmarks.size(); i++) {
        h += "<li><a href=\"" + htmlEscape(g_bookmarks[i].first) + "\">" + htmlEscape(g_bookmarks[i].second.empty() ? g_bookmarks[i].first : g_bookmarks[i].second) +
             "</a> <a href=\"psb:delbm:" + std::to_string(i) + "\">[ borrar ]</a></li>";
    }
    h += "</ul><p><a href=\"about:home\">Inicio</a> | <a href=\"about:settings\">Configuracion</a></p></body></html>";
    return h;
}

static void setStatus(const std::string& s, uint32_t color = C_DIM) {
    B.status = s;
    B.statusColor = color;
    B.dirty = true;
}

static int maxScroll() {
    int m = static_cast<int>(B.doc.lines.size()) - ROWS + 2;
    return m < 0 ? 0 : m;
}
static void clampScroll() { B.scroll = std::max(0, std::min(B.scroll, maxScroll())); }
static void scrollBy(int n) {
    int old = B.scroll;
    B.scroll += n;
    clampScroll();
    if (B.scroll != old) B.dirty = true;
}

static bool linkVisible(int link) {
    if (link < 0 || link >= static_cast<int>(B.doc.linkLine.size())) return false;
    int line = B.doc.linkLine[link];
    return line >= B.scroll && line < B.scroll + ROWS;
}

static void ensureVisible(int link) {
    if (link < 0 || link >= static_cast<int>(B.doc.linkLine.size())) return;
    int line = B.doc.linkLine[link];
    if (line < 0) return;
    if (line < B.scroll) B.scroll = line - 2;
    else if (line >= B.scroll + ROWS) B.scroll = line - ROWS + 3;
    clampScroll();
}

// Mueve la seleccion al enlace siguiente (dir=1) o anterior (dir=-1); sin enlaces, desplaza.
static void moveSelection(int dir) {
    const int n = static_cast<int>(B.doc.links.size());
    if (n == 0) { scrollBy(dir * 3); return; }
    int cand = -1;
    if (B.sel < 0 || !linkVisible(B.sel)) {
        // primer (o ultimo) enlace de los que se ven en pantalla
        const int lines = static_cast<int>(B.doc.lines.size());
        for (int r = 0; r < ROWS && cand < 0; r++) {
            int li = dir > 0 ? B.scroll + r : B.scroll + ROWS - 1 - r;
            if (li < 0 || li >= lines) continue;
            const auto& sp = B.doc.lines[li].spans;
            for (size_t k = 0; k < sp.size(); k++) {
                const Span& s = dir > 0 ? sp[k] : sp[sp.size() - 1 - k];
                if (s.link >= 0 && B.doc.linkLine[s.link] == li) { cand = s.link; break; }
            }
        }
        if (cand < 0) { scrollBy(dir * 3); return; }
    } else {
        cand = B.sel + dir;
        while (cand >= 0 && cand < n && B.doc.linkLine[cand] < 0) cand += dir;
        if (cand < 0 || cand >= n) { scrollBy(dir * 3); return; }
    }
    B.sel = cand;
    ensureVisible(cand);
    B.dirty = true;
}

static void setDocument(Document&& d, const std::string& url) {
    B.doc = std::move(d);
    B.url = url;
    B.sel = -1;
    B.hover = -1;
    B.scroll = 0;
    B.dirty = true;
}

static void showError(const std::string& title, const std::string& msg, const std::string& url) {
    std::string h = "<html><head><title>" + htmlEscape(title) + "</title></head><body><h1>" + htmlEscape(title) + "</h1><p>" +
                    htmlEscape(msg) + "</p><p>" + htmlEscape(url) + "</p></body></html>";
    setDocument(parseHtml(h, COLS), url);
}

static bool containsCI(const std::string& s, const char* sub) {
    std::string a = s;
    for (auto& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return a.find(sub) != std::string::npos;
}

static void present(const char* busy);

// Carga 'url' en B.doc (red o about:home). No toca el historial.
static void loadInto(const std::string& url) {
    if (url == "about:home") {
        setDocument(parseHtml(kHomeHtml, COLS), url);
        setStatus("");
        return;
    }
    if (url == "about:settings") { setDocument(parseHtml(settingsHtml(), COLS), url); setStatus(""); return; }
    if (url == "about:bookmarks") { setDocument(parseHtml(bookmarksHtml(), COLS), url); setStatus(""); return; }
    present((std::string("Cargando ") + url).c_str());
    HttpResult res;
    bool ok = http_get(url, res, 2 * 1024 * 1024);
    if (!ok) {
        char code[64];
        snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(res.sceError));
        showError("No se pudo cargar la pagina", std::string("Paso: ") + res.stage + " - codigo " + code, url);
        setStatus("Error al cargar", C_BAD);
        return;
    }
    const std::string& ct = res.contentType;
    if (ct.empty() || containsCI(ct, "html")) {
        setDocument(parseHtml(res.body, COLS), res.finalUrl);
    } else if (containsCI(ct, "text/") || containsCI(ct, "json") || containsCI(ct, "xml")) {
        setDocument(parsePlain(res.body, COLS), res.finalUrl);
    } else {
        char info[200];
        snprintf(info, sizeof(info), "Tipo no soportado: %s (%zu bytes). Aun no hay descargas ni imagenes.", ct.c_str(), res.body.size());
        showError("Contenido no soportado", info, res.finalUrl);
    }
    if (!B.doc.refreshUrl.empty() && B.doc.lines.size() < 6) {  // <meta refresh> en paginas casi vacias: seguirlo una vez
        static int refreshDepth = 0;
        std::string target = urlStripFragment(urlResolve(B.url, B.doc.refreshUrl));
        if (refreshDepth < 3 && urlIsHttp(target) && target != B.url) {
            refreshDepth++;
            psb_log("[refresh] -> %s", target.c_str());
            loadInto(target);
            refreshDepth--;
            return;
        }
    }
    char st[160];
    snprintf(st, sizeof(st), "HTTP %d - %zu bytes%s - %zu lineas, %zu enlaces", res.status, res.body.size(),
             res.truncated ? " (truncado a 2 MB)" : "", B.doc.lines.size(), B.doc.links.size());
    setStatus(st, res.status == 200 ? C_DIM : C_BAD);
    psb_log("[page] %s", st);
}

static void stash() {
    if (B.histPos < 0 || B.histPos >= static_cast<int>(B.hist.size())) return;
    HistEntry& e = B.hist[B.histPos];
    e.doc = std::move(B.doc);
    e.scroll = B.scroll;
    e.finalUrl = B.url;
    B.doc = Document();
}

static void restore(int idx) {
    B.histPos = idx;
    HistEntry& e = B.hist[idx];
    B.doc = std::move(e.doc);
    e.doc = Document();
    B.url = e.finalUrl;
    B.scroll = e.scroll;
    B.sel = -1;
    B.hover = -1;
    clampScroll();
    setStatus("");
    B.dirty = true;
}

static void navigate(const std::string& url, bool push) {
    psb_log("[nav] %s", url.c_str());
    if (push) {
        stash();
        if (B.histPos >= 0) B.hist.resize(B.histPos + 1);
        HistEntry e;
        e.req = url;
        B.hist.push_back(std::move(e));
        B.histPos = static_cast<int>(B.hist.size()) - 1;
        if (B.hist.size() > 50) { B.hist.erase(B.hist.begin()); B.histPos--; }
    } else if (B.histPos >= 0) {
        B.hist[B.histPos].req = url;
    }
    loadInto(url);
}

static void goBack() {
    if (B.histPos <= 0) { setStatus("No hay pagina anterior"); return; }
    stash();
    restore(B.histPos - 1);
}
static void goForward() {
    if (B.histPos < 0 || B.histPos + 1 >= static_cast<int>(B.hist.size())) { setStatus("No hay pagina siguiente"); return; }
    stash();
    restore(B.histPos + 1);
}
static void reload() {
    if (B.histPos >= 0) navigate(B.hist[B.histPos].req, false);
}
static void goHome() { navigate(g_cfg.home.empty() ? "about:home" : g_cfg.home, true); }

static void addBookmark() {
    if (B.url.empty() || B.url.compare(0, 6, "about:") == 0) { setStatus("Esta pagina no se puede guardar", C_BAD); return; }
    for (auto& b : g_bookmarks) if (b.first == B.url) { setStatus("Ya estaba en marcadores"); return; }
    g_bookmarks.push_back({B.url, B.doc.title});
    bookmarksSave();
    setStatus("Marcador guardado (" + std::to_string(g_bookmarks.size()) + ")", C_HEAD);
}

// Acciones internas (enlaces psb:...). Devuelve true si la accion se reconocio.
static bool handleAction(const std::string& a) {
    bool cfg = true, refresh = true;
    if (a == "sens-1") g_cfg.mouseSens -= 1.0f;
    else if (a == "sens-") g_cfg.mouseSens -= 0.5f;
    else if (a == "sens+") g_cfg.mouseSens += 0.5f;
    else if (a == "sens+1") g_cfg.mouseSens += 1.0f;
    else if (a == "accel") g_cfg.mouseAccel = !g_cfg.mouseAccel;
    else if (a == "wheelinv") g_cfg.wheelInvert = !g_cfg.wheelInvert;
    else if (a == "wheel-") g_cfg.wheelStep--;
    else if (a == "wheel+") g_cfg.wheelStep++;
    else if (a == "homehere") {
        const int prev = B.histPos - 1;
        if (prev >= 0) g_cfg.home = B.hist[prev].req;
        else g_cfg.home.clear();
    } else if (a == "homereset") g_cfg.home.clear();
    else if (a.compare(0, 6, "delbm:") == 0) {
        size_t i = static_cast<size_t>(atoi(a.c_str() + 6));
        if (i < g_bookmarks.size()) { g_bookmarks.erase(g_bookmarks.begin() + i); bookmarksSave(); }
        cfg = false;
    } else return false;
    g_cfg.mouseSens = std::max(0.5f, std::min(20.0f, g_cfg.mouseSens));
    g_cfg.wheelStep = std::max(1, std::min(10, g_cfg.wheelStep));
    if (cfg) configSave();
    if (refresh) {
        const int keep = B.scroll;
        loadInto(B.url);
        B.scroll = keep;
        clampScroll();
        B.dirty = true;
    }
    return true;
}

static void followLink(int id) {
    if (id < 0 || id >= static_cast<int>(B.doc.links.size())) return;
    const std::string& href = B.doc.links[id];
    if (href.compare(0, 4, "psb:") == 0) { handleAction(href.substr(4)); return; }
    if (href == "about:home" || href == "about:settings" || href == "about:bookmarks") { navigate(href, true); return; }
    std::string lower = href.substr(0, 11);
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.compare(0, 11, "javascript:") == 0 || lower.compare(0, 7, "mailto:") == 0 || lower.compare(0, 4, "tel:") == 0) {
        setStatus("Enlace no soportado: " + href.substr(0, 80), C_BAD);
        return;
    }
    if (!href.empty() && href[0] == '#') { setStatus("Enlace interno (#...): aun no soportado", C_BAD); return; }
    std::string full = urlStripFragment(urlResolve(B.url, href));
    if (!urlIsHttp(full)) { setStatus("Esquema no soportado: " + full.substr(0, 80), C_BAD); return; }
    navigate(full, true);
}

static void startUrlEdit(const std::string& initial, bool fresh) {
    B.urlEdit = true;
    B.urlFresh = fresh;
    B.urlText = initial;
    B.dirty = true;
}

// ------------------------------------------------------------------ dibujo
static std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n); }

static uint32_t styleColor(uint8_t st) {
    switch (st) {
        case ST_HEADING: return C_HEAD;
        case ST_LINK: return C_LINK;
        case ST_BOLD: return C_BOLD;
        case ST_PRE: return C_PRE;
        case ST_DIM: return C_DIM;
        default: return C_FG;
    }
}

static std::string linkTarget(int id) {
    if (id < 0 || id >= static_cast<int>(B.doc.links.size())) return std::string();
    if (B.doc.links[id].compare(0, 4, "psb:") == 0 || B.doc.links[id].compare(0, 6, "about:") == 0) return B.doc.links[id];
    return urlResolve(B.url, B.doc.links[id]);
}

static void drawPage() {
    Canvas& cv = *g_cv;
    cv.clear(C_BG);
    // Fila 1: titulo
    std::string title = B.doc.title.empty() ? B.url : B.doc.title;
    int x = MARGIN + cv.text(MARGIN, 10, "psbrowser", SCALE, C_ACC);
    cv.text(x, 10, (" v0.3.0 | " + clip(title, 100)).c_str(), SCALE, C_FG);
    // Fila 2: barra de URL
    for (int i = 0; i < NBTN; i++) {
        const int bx = MARGIN + i * (BTN_W + BTN_GAP);
        const bool hot = g_hoverBtn == i;
        cv.rect(bx, URLBAR_Y, BTN_W, URLBAR_H, hot ? C_ACC : C_DIM);
        cv.rect(bx + 2, URLBAR_Y + 2, BTN_W - 4, URLBAR_H - 4, hot ? C_BOXEDIT : C_BOX);
        cv.text(bx + (BTN_W - CHAR_W) / 2, URLBAR_Y + 7, kBtnLabel[i], SCALE, hot ? C_BOLD : C_FG);
    }
    cv.rect(URLBAR_X, URLBAR_Y, W - MARGIN - URLBAR_X, URLBAR_H, B.urlEdit ? C_ACC : C_DIM);
    cv.rect(URLBAR_X + 2, URLBAR_Y + 2, W - MARGIN - URLBAR_X - 4, URLBAR_H - 4, B.urlEdit ? C_BOXEDIT : C_BOX);
    const size_t fit = (W - MARGIN - URLBAR_X - 24) / CHAR_W;
    std::string shown = B.urlEdit ? B.urlText + "_" : B.url;
    if (B.urlEdit && shown.size() > fit) shown = shown.substr(shown.size() - fit);
    cv.text(URLBAR_X + 10, URLBAR_Y + 7, clip(shown, fit).c_str(), SCALE, B.urlEdit && B.urlFresh ? C_HEAD : C_FG);
    // Fila 3: estado / destino del enlace
    std::string line3;
    uint32_t c3 = C_DIM;
    int focus = B.hover >= 0 ? B.hover : B.sel;
    if (g_busy) { line3 = g_busy; c3 = C_HEAD; }
    else if (focus >= 0) { line3 = linkTarget(focus); c3 = C_LINK; }
    else { line3 = B.status; c3 = B.statusColor; }
    cv.text(MARGIN, 80, clip(line3, 116).c_str(), SCALE, c3);
    cv.rect(MARGIN, TOP - 8, W - 2 * MARGIN, 2, C_DIM);
    // Cuerpo
    for (int r = 0; r < ROWS; r++) {
        int li = B.scroll + r;
        if (li >= static_cast<int>(B.doc.lines.size())) break;
        int y = TOP + r * LINE_H;
        for (const Span& sp : B.doc.lines[li].spans) {
            int sx = MARGIN + sp.col * CHAR_W;
            int sw = static_cast<int>(sp.text.size()) * CHAR_W;
            bool hi = sp.link >= 0 && (sp.link == B.sel || sp.link == B.hover);
            uint32_t col = styleColor(sp.style);
            if (hi) { cv.rect(sx - 2, y - 2, sw + 4, LINE_H, C_LINK); col = C_BG; }
            cv.text(sx, y, sp.text.c_str(), SCALE, col);
            if (sp.style == ST_BOLD || sp.style == ST_HEADING) cv.text(sx + 1, y, sp.text.c_str(), SCALE, col);
            if (sp.style == ST_LINK && !hi) cv.rect(sx, y + 17, sw, 2, col);
        }
    }
    // Barra de desplazamiento
    const int trackY = TOP, trackH = ROWS * LINE_H;
    cv.rect(W - 18, trackY, 8, trackH, C_BOX);
    const int total = static_cast<int>(B.doc.lines.size());
    if (total > ROWS) {
        int th = std::max(24, trackH * ROWS / total);
        int ty = trackY + (trackH - th) * B.scroll / std::max(1, maxScroll());
        cv.rect(W - 18, ty, 8, th, C_DIM);
    }
    // Pie
    char foot[256];
    snprintf(foot, sizeof(foot), "F6 URL | F10 config | Ctrl+D marcador | Tab/Enter enlaces | Backspace atras | linea %d/%d",
             total ? std::min(B.scroll + 1, total) : 0, total);
    cv.text(MARGIN, H - BOTTOM + 14, clip(foot, 116).c_str(), SCALE, C_DIM);
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

static void redrawIfDirty() {
    if (!B.dirty) return;
    drawPage();
    SDL_UpdateTexture(g_tex, nullptr, g_cv->px.data(), W * 4);
    B.dirty = false;
    renderFrame();
}

static void present(const char* busy) {
    g_busy = busy;
    B.dirty = true;
    redrawIfDirty();
    g_busy = nullptr;
    B.dirty = true;  // al volver al bucle se redibuja ya sin el mensaje de "Cargando"
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
static int linkAt(int x, int y) {
    if (y < TOP || y >= TOP + ROWS * LINE_H) return -1;
    int li = B.scroll + (y - TOP) / LINE_H;
    int col = (x - MARGIN) / CHAR_W;
    if (x < MARGIN || li >= static_cast<int>(B.doc.lines.size())) return -1;
    for (const Span& sp : B.doc.lines[li].spans)
        if (sp.link >= 0 && col >= sp.col && col < sp.col + static_cast<int>(sp.text.size())) return sp.link;
    return -1;
}

static bool inUrlBar(int x, int y) { return y >= URLBAR_Y && y < URLBAR_Y + URLBAR_H && x >= URLBAR_X && x < W - MARGIN; }
static int buttonAt(int x, int y) {
    if (y < URLBAR_Y || y >= URLBAR_Y + URLBAR_H || x < MARGIN) return -1;
    int i = (x - MARGIN) / (BTN_W + BTN_GAP);
    if (i >= NBTN || (x - MARGIN) % (BTN_W + BTN_GAP) >= BTN_W) return -1;
    return i;
}
static void pressButton(int i) {
    switch (i) {
        case 0: goBack(); break;
        case 1: goForward(); break;
        case 2: reload(); break;
        case 3: goHome(); break;
        case 4: addBookmark(); break;
        case 5: navigate("about:bookmarks", true); break;
        case 6: navigate("about:settings", true); break;
        default: break;
    }
}

static void onKeyDown(int code, int ch, uint32_t mods) {
    const bool shift = (mods & (ORBIS_KEYBOARD_MOD_LEFT_SHIFT | ORBIS_KEYBOARD_MOD_RIGHT_SHIFT)) != 0;
    const bool ctrl = (mods & (ORBIS_KEYBOARD_MOD_LEFT_CTRL | ORBIS_KEYBOARD_MOD_RIGHT_CTRL)) != 0;
    const bool alt = (mods & (ORBIS_KEYBOARD_MOD_LEFT_ALT | ORBIS_KEYBOARD_MOD_RIGHT_ALT)) != 0;
    static int logged = 0;
    if (logged < 60) { logged++; psb_log("tecla: codigo=%d char=%d mods=0x%x", code, ch, mods); }

    if (B.urlEdit) {
        if (code == K_RETURN) {
            std::string u = urlFromUserInput(B.urlText);
            B.urlEdit = false;
            B.dirty = true;
            if (!u.empty()) navigate(u, true);
        } else if (code == K_ESC) {
            B.urlEdit = false;
            B.dirty = true;
        } else if (code == K_BACKSPACE) {
            if (B.urlFresh) { B.urlText.clear(); B.urlFresh = false; }
            else if (!B.urlText.empty()) B.urlText.pop_back();
            B.dirty = true;
        } else if (ch >= 32 && ch < 127) {
            if (B.urlFresh) { B.urlText.clear(); B.urlFresh = false; }
            B.urlText += static_cast<char>(ch);
            B.dirty = true;
        }
        return;
    }
    switch (code) {
        case K_F6: startUrlEdit(B.url, true); return;
        case K_L: if (ctrl) { startUrlEdit(B.url, true); return; } break;
        case K_F5: reload(); return;
        case K_F10: navigate("about:settings", true); return;
        case K_D: if (ctrl) { addBookmark(); return; } break;
        case K_BACKSPACE: goBack(); return;
        case K_LEFT: if (alt) { goBack(); return; } break;
        case K_RIGHT: if (alt) { goForward(); return; } break;
        case K_TAB: moveSelection(shift ? -1 : 1); return;
        case K_RETURN: if (B.sel >= 0) followLink(B.sel); return;
        case K_UP: scrollBy(-1); return;
        case K_DOWN: scrollBy(1); return;
        case K_PGUP: scrollBy(-(ROWS - 2)); return;
        case K_PGDOWN: scrollBy(ROWS - 2); return;
        case K_SPACE: scrollBy(shift ? -(ROWS - 2) : (ROWS - 2)); return;
        case K_HOME: B.scroll = 0; B.dirty = true; return;
        case K_END: B.scroll = maxScroll(); B.dirty = true; return;
        default: break;
    }
    // Escribir cualquier caracter imprimible abre la barra de URL con ese caracter.
    if (!ctrl && !alt && ch > 32 && ch < 127) startUrlEdit(std::string(1, static_cast<char>(ch)), false);
}

struct Held {
    uint16_t code;
    uint32_t downAt, lastRep;
};

static bool repeatable(uint16_t code) {
    if (code == K_UP || code == K_DOWN || code == K_PGUP || code == K_PGDOWN) return true;
    if (B.urlEdit) return code != K_RETURN && code != K_ESC && code != K_TAB;
    return false;
}

int main(int, char**) {
    openLog();
    psb_log("== psbrowser v0.3.0 ==");
    configLoad();
    bookmarksLoad();

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

    present("Iniciando...");
    const bool netOk = net_init();
    navigate(g_cfg.home.empty() ? "about:home" : g_cfg.home, true);
    if (!netOk) setStatus("Red no disponible (mira log.txt)", C_BAD);
    redrawIfDirty();

    bool mouseTried = false;
    uint32_t prevButtons = 0;
    std::vector<Held> held;
    int padAxis = 0;
    uint32_t lastAxis = 0;
    unsigned long frames = 0;
    bool running = true;

    while (running) {
        frames++;
        bool moved = false;
        const uint32_t now = SDL_GetTicks();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_JOYBUTTONDOWN:
                    psb_log("boton %d", e.jbutton.button);
                    switch (e.jbutton.button) {
                        case 0: if (B.sel >= 0) followLink(B.sel); break;
                        case 1: goBack(); break;
                        case 2: reload(); break;
                        case 3: goHome(); break;
                        case 4: scrollBy(-(ROWS - 2)); break;
                        case 5: scrollBy(ROWS - 2); break;
                        default: break;
                    }
                    break;
                case SDL_JOYHATMOTION:
                    psb_log("hat %d", e.jhat.value);
                    if (e.jhat.value & SDL_HAT_UP) moveSelection(-1);
                    if (e.jhat.value & SDL_HAT_DOWN) moveSelection(1);
                    if (e.jhat.value & SDL_HAT_LEFT) goBack();
                    if (e.jhat.value & SDL_HAT_RIGHT) goForward();
                    break;
                case SDL_JOYAXISMOTION:
                    if (e.jaxis.axis == 1 || e.jaxis.axis == 3) padAxis = std::abs(e.jaxis.value) > 16000 ? e.jaxis.value : 0;
                    break;
                default: break;
            }
        }
        if (padAxis != 0 && now - lastAxis > 70) { scrollBy(padAxis < 0 ? -3 : 3); lastAxis = now; }

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
                    static float fx = 0, fy = 0;  // resto fraccionario: asi sensibilidades <1x o entre cuentas no se pierden
                    fx += ms.dx * gain;
                    fy += ms.dy * gain;
                    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
                    fx -= ix;
                    fy -= iy;
                    g_mx = std::max(0, std::min(W - 1, g_mx + ix));
                    g_my = std::max(0, std::min(H - 1, g_my + iy));
                    moved = true;
                    int h = inUrlBar(g_mx, g_my) ? -1 : linkAt(g_mx, g_my);
                    if (h != B.hover) { B.hover = h; B.dirty = true; }
                    int hb = buttonAt(g_mx, g_my);
                    if (hb != g_hoverBtn) { g_hoverBtn = hb; B.dirty = true; }
                }
                if (ms.wheel) {
                    static int wlog = 0;
                    if (wlog < 10) { wlog++; psb_log("rueda: %d", ms.wheel); }
                    scrollBy(((ms.wheel > 0) != g_cfg.wheelInvert ? -1 : 1) * g_cfg.wheelStep);
                }
                const uint32_t pressed = ms.buttons & ~prevButtons;
                if (pressed & 1) {  // izquierdo
                    int bi = buttonAt(g_mx, g_my);
                    if (bi >= 0) pressButton(bi);
                    else if (inUrlBar(g_mx, g_my)) startUrlEdit(B.url, true);
                    else {
                        if (B.urlEdit) { B.urlEdit = false; B.dirty = true; }
                        int l = linkAt(g_mx, g_my);
                        if (l >= 0) followLink(l);
                    }
                }
                if (pressed & 2) goBack();  // derecho
                prevButtons = ms.buttons;
            }
        }

        if (B.dirty) redrawIfDirty();
        else if (moved) renderFrame();
        SDL_Delay(8);
    }
    net_shutdown();
    return 0;
}
