// psbrowser - fase 0: hello de red + visor de texto.
//
// Objetivo: comprobar en la PS4 real que la pila de red del sistema (DNS + HTTP + TLS via libSceHttp/libSceSsl)
// funciona desde un homebrew de OpenOrbis, y mostrar el resultado en pantalla.
//  - Al arrancar descarga una lista de URLs de prueba (primero http, luego https) y muestra estado + texto de la pagina.
//  - Mando: cualquier boton = siguiente URL; cruceta arriba/abajo (o stick izquierdo) = desplazar.
//  - Todo queda registrado en /data/psbrowser/log.txt (subelo si algo falla). Ultima pagina en /data/psbrowser/last.html.
#include <SDL2/SDL.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "canvas.h"
#include "htmltext.h"
#include "log.h"
#include "netclient.h"

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

static void openLog() {
    mkdir("/data/psbrowser", 0777);
    g_log = fopen("/data/psbrowser/log.txt", "w");
    if (!g_log) g_log = fopen("/data/psbrowser_log.txt", "w");
}

static const char* kUrls[] = {
    "http://example.com/",
    "https://example.com/",
    "https://info.cern.ch/",
    "https://en.wikipedia.org/wiki/PlayStation_4",
    "https://www.wikipedia.org/",
};
static const int kNumUrls = sizeof(kUrls) / sizeof(kUrls[0]);

static const int W = 1920, H = 1080, SCALE = 2;
static const int CHAR_W = 8 * SCALE, LINE_H = 8 * SCALE + 4;
static const int MARGIN = 32, TOP = 110, BOTTOM = 60;
static const size_t COLS = (W - 2 * MARGIN) / CHAR_W;

struct Page {
    int urlIndex = 0;
    HttpResult res;
    bool ok = false;
    std::vector<std::string> lines;
    int scroll = 0;
};

static void loadPage(Page& pg, int idx) {
    pg.urlIndex = idx;
    pg.scroll = 0;
    pg.lines.clear();
    pg.ok = http_get(kUrls[idx], pg.res, 512 * 1024);
    if (pg.ok) {
        FILE* f = fopen("/data/psbrowser/last.html", "wb");
        if (f) { fwrite(pg.res.body.data(), 1, pg.res.body.size(), f); fclose(f); }
        pg.lines = htmlToLines(pg.res.body, COLS);
        psb_log("[page] %zu lineas de texto", pg.lines.size());
    }
}

static void draw(Canvas& cv, const Page& pg, const char* busyMsg) {
    const uint32_t BG = 0xFF101418, FG = 0xFFE6E6E6, DIM = 0xFF8A94A0, OK = 0xFF5FD35F, BAD = 0xFFFF5F5F, ACC = 0xFF6AB0FF;
    cv.clear(BG);
    cv.text(MARGIN, 16, "psbrowser 0.1 - fase 0 (red)", 2, ACC);
    char hdr[512];
    snprintf(hdr, sizeof(hdr), "[%d/%d] %s", pg.urlIndex + 1, kNumUrls, kUrls[pg.urlIndex]);
    cv.text(MARGIN, 44, hdr, 2, FG);

    if (busyMsg) {
        cv.text(MARGIN, 76, busyMsg, 2, DIM);
    } else if (pg.ok) {
        snprintf(hdr, sizeof(hdr), "HTTP %d | %s | %zu bytes%s | redirecciones: %d | final: %s", pg.res.status,
                 pg.res.contentType.c_str(), pg.res.body.size(), pg.res.truncated ? " (truncado)" : "", pg.res.redirects,
                 pg.res.finalUrl.c_str());
        cv.text(MARGIN, 76, hdr, 1 * SCALE, pg.res.status == 200 ? OK : BAD);
    } else {
        snprintf(hdr, sizeof(hdr), "ERROR en '%s': codigo 0x%08X", pg.res.stage.c_str(), static_cast<unsigned>(pg.res.sceError));
        cv.text(MARGIN, 76, hdr, 2, BAD);
    }

    cv.rect(MARGIN, TOP - 8, W - 2 * MARGIN, 2, DIM);
    const int rows = (H - TOP - BOTTOM) / LINE_H;
    for (int r = 0; r < rows; r++) {
        int li = pg.scroll + r;
        if (li < 0 || li >= static_cast<int>(pg.lines.size())) break;
        cv.text(MARGIN, TOP + r * LINE_H, pg.lines[li].c_str(), SCALE, FG);
    }
    cv.rect(MARGIN, H - BOTTOM + 6, W - 2 * MARGIN, 2, DIM);
    snprintf(hdr, sizeof(hdr), "Cualquier boton: siguiente URL | Cruceta arriba/abajo: desplazar | linea %d/%zu", pg.scroll + 1,
             pg.lines.size());
    cv.text(MARGIN, H - BOTTOM + 18, hdr, SCALE, DIM);
}

int main(int, char**) {
    openLog();
    psb_log("== psbrowser fase 0 ==");

    SDL_Init(0);
    const struct { Uint32 f; const char* n; } subs[] = {
        {SDL_INIT_VIDEO, "VIDEO"}, {SDL_INIT_JOYSTICK, "JOYSTICK"}, {SDL_INIT_EVENTS, "EVENTS"}};
    for (const auto& s : subs) {
        int rc = SDL_InitSubSystem(s.f);
        psb_log("SDL_InitSubSystem(%s) = %d %s", s.n, rc, rc ? SDL_GetError() : "ok");
    }
    SDL_Window* win = SDL_CreateWindow("psbrowser", 0, 0, W, H, 0);
    SDL_Renderer* ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : nullptr;
    if (!ren) { psb_log("sin renderer: %s", SDL_GetError()); return 1; }
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!tex) { psb_log("sin textura: %s", SDL_GetError()); return 1; }

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_Joystick* j = SDL_JoystickOpen(i);
        psb_log("joystick %d: %s", i, j && SDL_JoystickName(j) ? SDL_JoystickName(j) : "?");
    }

    Canvas cv(W, H);
    Page pg;
    auto present = [&](const char* busy) {
        draw(cv, pg, busy);
        SDL_UpdateTexture(tex, nullptr, cv.px.data(), W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
    };

    present("Iniciando red...");
    bool netOk = net_init();
    if (!netOk) {
        pg.ok = false;
        pg.res.stage = "net_init (mira log.txt)";
    } else {
        present("Descargando...");
        loadPage(pg, 0);
    }
    present(nullptr);

    bool running = true;
    Uint32 lastAxis = 0;
    while (running) {
        bool dirty = false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_JOYBUTTONDOWN:
                    psb_log("boton %d", e.jbutton.button);
                    if (netOk) {
                        present("Descargando...");
                        loadPage(pg, (pg.urlIndex + 1) % kNumUrls);
                        dirty = true;
                    }
                    break;
                case SDL_JOYHATMOTION:
                    if (e.jhat.value & SDL_HAT_UP) { pg.scroll -= 5; dirty = true; }
                    if (e.jhat.value & SDL_HAT_DOWN) { pg.scroll += 5; dirty = true; }
                    break;
                case SDL_JOYAXISMOTION:
                    if (e.jaxis.axis == 1 && SDL_GetTicks() - lastAxis > 80) {
                        if (e.jaxis.value < -16000) { pg.scroll -= 3; dirty = true; lastAxis = SDL_GetTicks(); }
                        if (e.jaxis.value > 16000) { pg.scroll += 3; dirty = true; lastAxis = SDL_GetTicks(); }
                    }
                    break;
                default: break;
            }
        }
        if (pg.scroll < 0) pg.scroll = 0;
        if (pg.scroll > static_cast<int>(pg.lines.size())) pg.scroll = static_cast<int>(pg.lines.size());
        if (dirty) present(nullptr);
        SDL_Delay(16);
    }
    net_shutdown();
    return 0;
}
