#pragma once
// Lienzo de pixeles en memoria + texto con la fuente 8x8 (dominio publico, third_party/font8x8_basic.h).
// Se dibuja directo en un buffer ARGB y se sube a una textura solo cuando cambia (el renderer de la PS4 es por software).
#include <cstdint>
#include <cstring>
#include <vector>

#include "../third_party/font8x8_basic.h"

struct Canvas {
    int w, h;
    std::vector<uint32_t> px;
    Canvas(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_, 0) {}

    void clear(uint32_t c) { for (auto& p : px) p = c; }

    void rect(int x, int y, int rw, int rh, uint32_t c) {
        for (int j = y; j < y + rh; j++) {
            if (j < 0 || j >= h) continue;
            for (int i = x; i < x + rw; i++)
                if (i >= 0 && i < w) px[static_cast<size_t>(j) * w + i] = c;
        }
    }

    void glyph(int x, int y, unsigned char ch, int scale, uint32_t c) {
        if (ch > 127) ch = '?';
        const unsigned char* g = font8x8_basic[ch];
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (g[row] & (1 << col)) rect(x + col * scale, y + row * scale, scale, scale, c);
    }

    // Dibuja una linea; devuelve el ancho en pixeles.
    int text(int x, int y, const char* s, int scale, uint32_t c) {
        int cx = x;
        for (; *s; ++s) { glyph(cx, y, static_cast<unsigned char>(*s), scale, c); cx += 8 * scale; }
        return cx - x;
    }
};
