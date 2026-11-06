// psbrowser - fase 1a: visor de paginas con enlaces, barra de URL, historial, teclado y raton nativos.
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
static const size_t COLS = (W - 2 * MARGIN - 20) / CHAR_W;
static const int ROWS = (H - TOP - BOTTOM) / LINE_H;

static const uint32_t C_BG = 0xFF101418, C_FG = 0xFFE6E6E6, C_DIM = 0xFF8A94A0, C_ACC = 0xFF6AB0FF, C_LINK = 0xFF6AB0FF,
                      C_HEAD = 0xFFFFD479, C_BOLD = 0xFFFFFFFF, C_PRE = 0xFFA8D8A8, C_BAD = 0xFFFF5F5F, C_BOX = 0xFF1C242C,
                      C_BOXEDIT = 0xFF26343F;

// Codigos de tecla USB HID (los mismos que usa OpenOrbis en orbis/_types/keyboard.h).
enum {
    K_L = 15, K_RETURN = 40, K_ESC = 41, K_BACKSPACE = 42, K_TAB = 43, K_SPACE = 44, K_F5 = 62, K_F6 = 63, K_HOME = 74,
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

// Graficos / entrada (globales para que present() pueda usarlos desde navigate()).
static Canvas* g_cv = nullptr;
static SDL_Renderer* g_ren = nullptr;
static SDL_Texture* g_tex = nullptr;
static SDL_Texture* g_curTex = nullptr;
static const char* g_busy = nullptr;
static int g_mx = W / 2, g_my = H / 2;
static bool g_mouseOk = false;

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
    "</ul>"
    "<h2>Controles</h2>"
    "<ul>"
    "<li>Teclado: F6 o Ctrl+L = barra de URL; Tab = siguiente enlace; Enter = abrir; Backspace = atras; F5 = recargar; flechas, RePag/AvPag = desplazar.</li>"
    "<li>Raton: clic izquierdo = abrir enlace; clic derecho = atras; rueda = desplazar.</li>"
    "<li>Mando: cruceta arriba/abajo = enlace anterior/siguiente; izquierda = atras; derecha = adelante; boton 0 = abrir; 1 = atras; 2 = recargar; 3 = inicio; 4/5 = pagina; stick = desplazar.</li>"
    "</ul>"
    "<p>Solo texto: sin JavaScript, sin imagenes y sin formularios (de momento). Los caracteres no ASCII salen como ?.</p>"
    "</body></html>";

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
static void goHome() { navigate("about:home", true); }

static void followLink(int id) {
    if (id < 0 || id >= static_cast<int>(B.doc.links.size())) return;
    const std::string& href = B.doc.links[id];
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
    return urlResolve(B.url, B.doc.links[id]);
}

static void drawPage() {
    Canvas& cv = *g_cv;
    cv.clear(C_BG);
    // Fila 1: titulo
    std::string title = B.doc.title.empty() ? B.url : B.doc.title;
    int x = MARGIN + cv.text(MARGIN, 10, "psbrowser", SCALE, C_ACC);
    cv.text(x, 10, (" | " + clip(title, 100)).c_str(), SCALE, C_FG);
    // Fila 2: barra de URL
    cv.rect(MARGIN, URLBAR_Y, W - 2 * MARGIN, URLBAR_H, B.urlEdit ? C_ACC : C_DIM);
    cv.rect(MARGIN + 2, URLBAR_Y + 2, W - 2 * MARGIN - 4, URLBAR_H - 4, B.urlEdit ? C_BOXEDIT : C_BOX);
    const size_t fit = (W - 2 * MARGIN - 24) / CHAR_W;
    std::string shown = B.urlEdit ? B.urlText + "_" : B.url;
    if (B.urlEdit && shown.size() > fit) shown = shown.substr(shown.size() - fit);
    cv.text(MARGIN + 10, URLBAR_Y + 7, clip(shown, fit).c_str(), SCALE, B.urlEdit && B.urlFresh ? C_HEAD : C_FG);
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
    snprintf(foot, sizeof(foot), "F6 URL | Tab/Enter enlaces | Backspace atras | F5 recargar | raton y mando | linea %d/%d",
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

static bool inUrlBar(int x, int y) { return y >= URLBAR_Y && y < URLBAR_Y + URLBAR_H && x >= MARGIN && x < W - MARGIN; }

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
    psb_log("== psbrowser fase 1a ==");

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
    navigate("about:home", true);
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
                    g_mx = std::max(0, std::min(W - 1, g_mx + ms.dx));
                    g_my = std::max(0, std::min(H - 1, g_my + ms.dy));
                    moved = true;
                    int h = inUrlBar(g_mx, g_my) ? -1 : linkAt(g_mx, g_my);
                    if (h != B.hover) { B.hover = h; B.dirty = true; }
                }
                if (ms.wheel) {
                    static int wlog = 0;
                    if (wlog < 10) { wlog++; psb_log("rueda: %d", ms.wheel); }
                    scrollBy(ms.wheel > 0 ? -3 : 3);  // si va al reves, invertir aqui
                }
                const uint32_t pressed = ms.buttons & ~prevButtons;
                if (pressed & 1) {  // izquierdo
                    if (inUrlBar(g_mx, g_my)) startUrlEdit(B.url, true);
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
