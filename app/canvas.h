#pragma once
// Lienzo de pixeles ARGB en memoria con recorte, mezcla alfa y formas basicas (rectangulos redondeados, circulos,
// lineas, imagenes). Se dibuja en un buffer y se sube a una textura SDL solo cuando cambia algo (el renderer de la
// PS4 es por software). El texto lo dibuja textengine.cpp con fuentes TrueType.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

struct Canvas {
    int w, h;
    std::vector<uint32_t> px;
    int cx0 = 0, cy0 = 0, cx1, cy1;  // rectangulo de recorte [cx0,cx1) x [cy0,cy1)

    Canvas(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_, 0), cx1(w_), cy1(h_) {}

    void setClip(int x, int y, int x2, int y2) {
        cx0 = std::max(0, x); cy0 = std::max(0, y); cx1 = std::min(w, x2); cy1 = std::min(h, y2);
    }
    void resetClip() { cx0 = 0; cy0 = 0; cx1 = w; cy1 = h; }

    void clear(uint32_t c) { std::fill(px.begin(), px.end(), c); }

    static uint32_t mix(uint32_t dst, uint32_t src, int a) {  // a: 0..255 (cobertura * alfa de src)
        if (a <= 0) return dst;
        if (a >= 255) return 0xFF000000u | (src & 0xFFFFFF);
        const int ia = 255 - a;
        const uint32_t r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * ia) / 255;
        const uint32_t g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * ia) / 255;
        const uint32_t b = ((src & 255) * a + (dst & 255) * ia) / 255;
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    void blend(int x, int y, uint32_t c, int cover) {  // cover 0..255, se multiplica por el alfa de c
        if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) return;
        const int a = static_cast<int>((c >> 24) & 255) * cover / 255;
        uint32_t& d = px[static_cast<size_t>(y) * w + x];
        d = mix(d, c, a);
    }

    void rect(int x, int y, int rw, int rh, uint32_t c) {
        int x0 = std::max(x, cx0), y0 = std::max(y, cy0), x1 = std::min(x + rw, cx1), y1 = std::min(y + rh, cy1);
        if (x0 >= x1 || y0 >= y1) return;
        if ((c >> 24) == 255) {
            for (int j = y0; j < y1; j++) {
                uint32_t* row = &px[static_cast<size_t>(j) * w];
                std::fill(row + x0, row + x1, c);
            }
        } else {
            for (int j = y0; j < y1; j++)
                for (int i = x0; i < x1; i++) blend(i, j, c, 255);
        }
    }

    // Rectangulo redondeado (radio r) con borde suavizado.
    void rrect(int x, int y, int rw, int rh, int r, uint32_t c) {
        if (r <= 0) { rect(x, y, rw, rh, c); return; }
        r = std::min(r, std::min(rw, rh) / 2);
        rect(x + r, y, rw - 2 * r, rh, c);
        rect(x, y + r, r, rh - 2 * r, c);
        rect(x + rw - r, y + r, r, rh - 2 * r, c);
        const int cxs[4] = {x + r, x + rw - r, x + r, x + rw - r};
        const int cys[4] = {y + r, y + r, y + rh - r, y + rh - r};
        for (int q = 0; q < 4; q++) {
            const int ox = (q & 1) ? cxs[q] : cxs[q] - r, oy = (q & 2) ? cys[q] : cys[q] - r;
            for (int j = 0; j < r; j++)
                for (int i = 0; i < r; i++) {
                    const float dx = (q & 1) ? (i + 0.5f) : (r - i - 0.5f), dy = (q & 2) ? (j + 0.5f) : (r - j - 0.5f);
                    const float d = sqrtf(dx * dx + dy * dy);
                    const float cov = std::max(0.f, std::min(1.f, r - d + 0.5f));
                    if (cov > 0) blend(ox + i, oy + j, c, static_cast<int>(cov * 255));
                }
        }
    }
    // Borde de 'bw' px alrededor de un rectangulo redondeado: se dibuja el exterior y luego el interior.
    void rrectBorder(int x, int y, int rw, int rh, int r, int bw, uint32_t border, uint32_t fill) {
        rrect(x, y, rw, rh, r, border);
        rrect(x + bw, y + bw, rw - 2 * bw, rh - 2 * bw, std::max(0, r - bw), fill);
    }
    void circle(float cxf, float cyf, float rad, uint32_t c) {
        const int x0 = static_cast<int>(cxf - rad - 1), x1 = static_cast<int>(cxf + rad + 2);
        const int y0 = static_cast<int>(cyf - rad - 1), y1 = static_cast<int>(cyf + rad + 2);
        for (int j = y0; j < y1; j++)
            for (int i = x0; i < x1; i++) {
                const float dx = i + 0.5f - cxf, dy = j + 0.5f - cyf;
                const float cov = std::max(0.f, std::min(1.f, rad - sqrtf(dx * dx + dy * dy) + 0.5f));
                if (cov > 0) blend(i, j, c, static_cast<int>(cov * 255));
            }
    }
    // Linea gruesa suavizada (para iconos).
    void line(float x0, float y0, float x1, float y1, float thick, uint32_t c) {
        const float minx = std::min(x0, x1) - thick - 1, maxx = std::max(x0, x1) + thick + 1;
        const float miny = std::min(y0, y1) - thick - 1, maxy = std::max(y0, y1) + thick + 1;
        const float vx = x1 - x0, vy = y1 - y0;
        const float len2 = vx * vx + vy * vy;
        for (int j = static_cast<int>(miny); j <= static_cast<int>(maxy); j++)
            for (int i = static_cast<int>(minx); i <= static_cast<int>(maxx); i++) {
                const float px_ = i + 0.5f - x0, py_ = j + 0.5f - y0;
                float t = len2 > 0 ? (px_ * vx + py_ * vy) / len2 : 0;
                t = std::max(0.f, std::min(1.f, t));
                const float dx = px_ - t * vx, dy = py_ - t * vy;
                const float cov = std::max(0.f, std::min(1.f, thick * 0.5f - sqrtf(dx * dx + dy * dy) + 0.5f));
                if (cov > 0) blend(i, j, c, static_cast<int>(cov * 255));
            }
    }
    // Copia una imagen ARGB (src de sw x sh) en (x,y) con recorte.
    void blit(const uint32_t* src, int sw, int sh, int x, int y) {
        for (int j = 0; j < sh; j++) {
            const int yy = y + j;
            if (yy < cy0 || yy >= cy1) continue;
            for (int i = 0; i < sw; i++) {
                const int xx = x + i;
                if (xx < cx0 || xx >= cx1) continue;
                const uint32_t s = src[static_cast<size_t>(j) * sw + i];
                const int a = static_cast<int>(s >> 24);
                if (a == 255) px[static_cast<size_t>(yy) * w + xx] = s;
                else if (a > 0) { uint32_t& d = px[static_cast<size_t>(yy) * w + xx]; d = mix(d, s, a); }
            }
        }
    }
};
