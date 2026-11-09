// Prueba en PC de app/canvas2d.cpp (rasterizador de Canvas 2D). Uso: g++ -std=c++17 -fsanitize=address,undefined -Iapp tools/tests/test_canvas2d.cpp app/canvas2d.cpp -o /tmp/t && /tmp/t
#include "canvas2d.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLA linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static uint32_t px(int id, int x, int y) { int w, h; const uint32_t* p; c2d_info(id, w, h, &p); return p[(size_t)y * w + x]; }
static double op(int id, int o, std::initializer_list<double> a) { std::vector<double> v(a); return c2d_op(id, o, v.data(), (int)v.size()); }
static int near(uint32_t a, uint32_t b, int tol = 2) { for (int s = 0; s < 32; s += 8) if (std::abs((int)((a >> s) & 255) - (int)((b >> s) & 255)) > tol) return 0; return 1; }
static long coverage(int id) { int w, h; const uint32_t* p; c2d_info(id, w, h, &p); long n = 0; for (int i = 0; i < w * h; i++) n += p[i] >> 24; return n; }

int main() {
    uint32_t c;
    CHECK(c2d_parse_color("#f00", c) && c == 0xFFFF0000u);
    CHECK(c2d_parse_color("#00ff0080", c) && c == 0x8000FF00u);
    CHECK(c2d_parse_color("rgb(10, 20, 30)", c) && c == 0xFF0A141Eu);
    CHECK(c2d_parse_color("rgba(255,0,0,0.5)", c) && (c >> 24) == 128 && ((c >> 16) & 255) == 255);
    CHECK(c2d_parse_color("hsl(120, 100%, 50%)", c) && near(c, 0xFF00FF00u));
    CHECK(c2d_parse_color("  Red ", c) && c == 0xFFFF0000u);
    CHECK(!c2d_parse_color("nocolor", c));
    CHECK(!c2d_parse_color("#12", c));

    int id = c2d_create(100, 80);
    CHECK(id == 1);
    CHECK(px(id, 5, 5) == 0);  // transparente al inicio
    c2d_set_str(id, C2S_FILL, "#ff0000");
    op(id, C2D_FILLRECT, {10, 10, 20, 20});
    CHECK(px(id, 15, 15) == 0xFFFF0000u);
    CHECK(px(id, 9, 15) == 0 && px(id, 30, 15) == 0 && px(id, 15, 30) == 0);
    CHECK(c2d_take_dirty(id)); CHECK(!c2d_take_dirty(id));
    // alfa sobre rojo opaco
    c2d_set_str(id, C2S_FILL, "rgba(0,0,255,0.5)");
    op(id, C2D_FILLRECT, {10, 10, 5, 5});
    CHECK(near(px(id, 12, 12), 0xFF800080u, 2));
    // clearRect
    op(id, C2D_CLEARRECT, {10, 10, 5, 5});
    CHECK(px(id, 12, 12) == 0);
    // translate/scale/save/restore
    op(id, C2D_SAVE, {}); op(id, C2D_TRANSLATE, {50, 40}); op(id, C2D_SCALE, {2, 2});
    c2d_set_str(id, C2S_FILL, "#00ff00"); op(id, C2D_FILLRECT, {0, 0, 5, 5});
    CHECK(px(id, 55, 45) == 0xFF00FF00u && px(id, 59, 49) == 0xFF00FF00u && px(id, 60, 50) == 0);
    op(id, C2D_RESTORE, {});
    c2d_set_str(id, C2S_FILL, "#ffffff"); op(id, C2D_FILLRECT, {0, 70, 4, 4});
    CHECK(px(id, 1, 71) == 0xFFFFFFFFu);
    // circulo: area ~ pi r^2
    int cc = c2d_create(100, 100);
    c2d_set_str(cc, C2S_FILL, "#fff");
    op(cc, C2D_BEGINPATH, {}); op(cc, C2D_ARC, {50, 50, 30, 0, 2 * M_PI, 0}); op(cc, C2D_FILL, {0});
    double area = coverage(cc) / 255.0;
    CHECK(std::fabs(area - M_PI * 900) < M_PI * 900 * 0.02);
    CHECK(px(cc, 50, 50) == 0xFFFFFFFFu && px(cc, 5, 5) == 0);
    // trazo de linea horizontal de 4 px
    int sl = c2d_create(60, 40);
    c2d_set_str(sl, C2S_STROKE, "#fff"); op(sl, C2D_LINEWIDTH, {4});
    op(sl, C2D_BEGINPATH, {}); op(sl, C2D_MOVETO, {10, 20}); op(sl, C2D_LINETO, {50, 20}); op(sl, C2D_STROKE, {});
    CHECK(px(sl, 30, 18) == 0xFFFFFFFFu && px(sl, 30, 21) == 0xFFFFFFFFu && px(sl, 30, 16) == 0 && px(sl, 30, 23) == 0);
    CHECK(px(sl, 5, 20) == 0 && px(sl, 55, 20) == 0);  // sin remates: no pasa de los extremos
    // evenodd: cuadrado con agujero
    int eo = c2d_create(50, 50);
    c2d_set_str(eo, C2S_FILL, "#fff");
    op(eo, C2D_BEGINPATH, {}); op(eo, C2D_RECT, {5, 5, 40, 40}); op(eo, C2D_RECT, {15, 15, 20, 20}); op(eo, C2D_FILL, {1});
    CHECK(px(eo, 10, 10) == 0xFFFFFFFFu && px(eo, 25, 25) == 0);
    // clip rectangular
    int cl = c2d_create(50, 50);
    c2d_set_str(cl, C2S_FILL, "#fff");
    op(cl, C2D_BEGINPATH, {}); op(cl, C2D_RECT, {10, 10, 10, 10}); op(cl, C2D_CLIP, {});
    op(cl, C2D_FILLRECT, {0, 0, 50, 50});
    CHECK(px(cl, 15, 15) == 0xFFFFFFFFu && px(cl, 5, 5) == 0 && px(cl, 25, 25) == 0);
    // drawImage: copia 1:1, escalado y espejo
    std::vector<uint32_t> img(4 * 4, 0xFF123456u); img[0] = 0xFFFF0000u;
    int im = c2d_add_image(img, 4, 4);
    int dd = c2d_create(40, 40);
    op(dd, C2D_DRAWIMAGE, {(double)im, 0, 0, 4, 4, 10, 10, 4, 4});
    CHECK(px(dd, 10, 10) == 0xFFFF0000u && px(dd, 11, 10) == 0xFF123456u && px(dd, 14, 14) == 0);
    op(dd, C2D_SMOOTH, {0});
    op(dd, C2D_DRAWIMAGE, {(double)im, 0, 0, 4, 4, 20, 20, 8, 8});
    CHECK(px(dd, 20, 20) == 0xFFFF0000u && px(dd, 21, 21) == 0xFFFF0000u && px(dd, 22, 20) == 0xFF123456u);
    int dm = c2d_create(40, 40);
    op(dm, C2D_DRAWIMAGE, {(double)im, 0, 0, 4, 4, 14, 0, -4, 4});  // espejo horizontal: el rojo pasa a la derecha
    CHECK(px(dm, 13, 0) == 0xFFFF0000u && px(dm, 10, 0) == 0xFF123456u);
    // degradado lineal
    int gr = c2d_create(100, 10);
    int g = (int)op(gr, C2D_GRAD_LINEAR, {0, 0, 100, 0});
    c2d_set_str(gr, C2S_STOPCOLOR, "#000000"); op(gr, C2D_GRAD_STOP, {(double)g, 0});
    c2d_set_str(gr, C2S_STOPCOLOR, "#ffffff"); op(gr, C2D_GRAD_STOP, {(double)g, 1});
    op(gr, C2D_FILLGRAD, {(double)g}); op(gr, C2D_FILLRECT, {0, 0, 100, 10});
    CHECK(((px(gr, 2, 5) >> 8) & 255) < 15 && std::abs((int)((px(gr, 50, 5) >> 8) & 255) - 128) < 8 && ((px(gr, 97, 5) >> 8) & 255) > 240);
    // texto: dibuja algo y measureText
    int tx = c2d_create(200, 40);
    c2d_set_str(tx, C2S_FILL, "#fff"); c2d_set_str(tx, C2S_FONT, "16px monospace"); c2d_set_str(tx, C2S_TEXT, "Hola");
    op(tx, C2D_FILLTEXT, {10, 30, 0});
    CHECK(coverage(tx) > 255 * 20);
    CHECK(std::fabs(op(tx, C2D_MEASURETEXT, {}) - 4 * 16) < 0.01);
    // composicion destination-out y lighter
    int co = c2d_create(10, 10);
    c2d_set_str(co, C2S_FILL, "#ff0000"); op(co, C2D_FILLRECT, {0, 0, 10, 10});
    c2d_set_str(co, C2S_COMPOSITE, "destination-out"); op(co, C2D_FILLRECT, {0, 0, 5, 10});
    CHECK(px(co, 2, 5) == 0 && px(co, 7, 5) == 0xFFFF0000u);
    c2d_set_str(co, C2S_COMPOSITE, "lighter"); c2d_set_str(co, C2S_FILL, "#0000ff"); op(co, C2D_FILLRECT, {5, 0, 5, 10});
    CHECK(near(px(co, 7, 5), 0xFFFF00FFu));
    // get/put pixels
    auto v = c2d_get_pixels(id, 15, 15, 2, 2);
    CHECK(v.size() == 16 && v[0] == 255 && v[1] == 0 && v[2] == 0 && v[3] == 255);
    uint8_t blk[4] = {1, 2, 3, 4};
    c2d_put_pixels(id, 0, 0, 1, 1, blk, 4);
    CHECK(px(id, 0, 0) == 0x04010203u);
    // limites y argumentos raros no revientan
    op(id, C2D_FILLRECT, {NAN, 0, 10, 10}); op(id, C2D_FILLRECT, {-1e9, -1e9, 2e9, 2e9}); op(id, C2D_ARC, {1e9, 1e9, 1e9, 0, 7, 0});
    op(id, C2D_BEGINPATH, {}); op(id, C2D_LINETO, {5, 5}); op(id, C2D_STROKE, {}); op(id, C2D_FILL, {0}); op(id, C2D_CLOSEPATH, {});
    op(id, C2D_SETTRANSFORM, {0, 0, 0, 0, 0, 0}); op(id, C2D_FILLRECT, {0, 0, 10, 10}); op(id, C2D_DRAWIMAGE, {(double)id, 0, 0, 10, 10, 0, 0, 10, 10});
    op(id, C2D_RESTORE, {}); op(id, C2D_RESTORE, {});
    CHECK(c2d_create(5000, 5000) == 0);
    c2d_resize(id, 10, 10); int w, h; c2d_info(id, w, h, nullptr); CHECK(w == 10 && h == 10);
    CHECK(c2d_main() > 0 && c2d_used_count() > 3);
    c2d_reset_all(); CHECK(c2d_main() == 0);
    printf(fails ? "test_canvas2d: %d FALLOS\n" : "test_canvas2d OK\n", fails);
    return fails ? 1 : 0;
}
