// Prueba en el PC del enlace QuickJS <-> Canvas 2D (jsx.cpp + jsprelude.h + canvas2d.cpp). REQUIERE QuickJS real (ver tools/README.md).
// g++ -std=c++17 -O1 -I/tmp/qjs -Iapp tools/tests/test_jsx_canvas.cpp app/jsx.cpp app/dom.cpp ... (igual que test_jsx) /tmp/qobj/*.o -o /tmp/tjc
#include "../../app/jsx.h"
#include "../../app/canvas2d.h"
#include <chrono>
#include <cstdio>
#include <string>
static uint32_t nowms() { using namespace std::chrono; return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count(); }
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint32_t px(int id, int x, int y) { int w, h; const uint32_t* p; c2d_info(id, w, h, &p); return p[(size_t)y * w + x]; }
int main() {
    JsHost h;
    h.nowMs = nowms;
    h.log = [](const std::string& l) { printf("%s\n", l.c_str()); };
    h.resolve = [](const std::string&, const std::string& r) { return r; };
    h.fetch = [](const std::string&, size_t, std::string& b, std::string& f, int& st, std::string& ct) { b.clear(); f.clear(); st = 404; ct.clear(); return true; };
    jsx_set_host(h);
    const char* page = R"(<!DOCTYPE html><html><body><script>
      var cv = document.createElement('canvas'); cv.width = 64; cv.height = 48; document.body.appendChild(cv);
      var c = cv.getContext('2d');
      c.fillStyle = '#ff0000'; c.fillRect(0, 0, 10, 10);
      c.save(); c.translate(20, 20); c.fillStyle = 'rgba(0,0,255,1)'; c.fillRect(0, 0, 5, 5); c.restore();
      var g = c.createLinearGradient(0, 30, 64, 30); g.addColorStop(0, '#000'); g.addColorStop(1, '#fff'); c.fillStyle = g; c.fillRect(0, 30, 64, 4);
      window.__ts = null; requestAnimationFrame(function (t) { window.__ts = t; });
      window.__keys = []; document.addEventListener('keydown', function (e) { window.__keys.push(e.key + ':' + e.keyCode); });
      window.__pad = null; setInterval(function () { var p = navigator.getGamepads()[0]; if (p) window.__pad = p.buttons[0].pressed + ',' + p.axes[0]; }, 5);
      var d = c.getImageData(2, 2, 1, 1).data; window.__px = d[0] + ',' + d[1] + ',' + d[2] + ',' + d[3];
      document.title = 'listo';
    </script></body></html>)";
    JsxInfo info;
    int tok = 0;
    jsx_load("https://x.test/", page, true, &info);
    jsx_attach(&tok);
    CHECK(jsx_active(&tok));
    const int id = c2d_main();
    CHECK(id > 0);
    CHECK(px(id, 5, 5) == 0xFFFF0000u);
    CHECK(px(id, 22, 22) == 0xFF0000FFu);
    CHECK(((px(id, 60, 31) >> 8) & 255) > 230);
    CHECK(c2d_used_count() == 1);
    std::string html;
    for (int i = 0; i < 10; i++) { jsx_pump(&tok, nowms(), &html); }
    CHECK(jsx_eval(&tok, "__psbKey(1,\"ArrowUp\",\"ArrowUp\",38)"));
    CHECK(jsx_eval(&tok, "__psbPad([0.5,0,0,0],[1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0])"));
    jsx_set_game_mode(true);
    for (int i = 0; i < 20; i++) { jsx_pump(&tok, nowms() + 20 * i, &html); }
    CHECK(jsx_eval(&tok, "if (window.__keys.join() !== 'ArrowUp:38') throw new Error('tecla: ' + window.__keys.join())"));
    CHECK(jsx_eval(&tok, "if (window.__pad !== 'true,0.5') throw new Error('pad: ' + window.__pad)"));
    CHECK(jsx_eval(&tok, "if (window.__px !== '255,0,0,255') throw new Error('getImageData: ' + window.__px)"));
    CHECK(jsx_eval(&tok, "if (typeof window.__ts !== 'number') throw new Error('rAF sin marca de tiempo')"));
    jsx_set_game_mode(false);
    jsx_close(&tok);
    CHECK(c2d_main() == 0);  // los canvas desaparecen con la pagina
    printf(fails ? "test_jsx_canvas: %d FALLOS\n" : "test_jsx_canvas OK\n", fails);
    return fails ? 1 : 0;
}
