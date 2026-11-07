#include "icodec.h"

#include <cstring>

namespace {
inline uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }
}  // namespace

bool ico_looks_like(const std::string& s) {
    if (s.size() < 22) return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
    const uint32_t type = rd16(p + 2), n = rd16(p + 4);
    return rd16(p) == 0 && (type == 1 || type == 2) && n >= 1 && n <= 256;
}

int ico_extract(const std::string& s, int& W, int& H, std::vector<uint8_t>& rgba, std::string& png) {
    if (!ico_looks_like(s)) return 0;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s.data());
    const size_t total = s.size();
    const uint32_t n = rd16(p + 4);
    if (6 + static_cast<size_t>(n) * 16 > total) return 0;
    // entrada de mayor area (a igual area, mas bits por pixel)
    long best = -1, bestScore = -1;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* e = p + 6 + i * 16;
        const uint32_t w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256, bpp = rd16(e + 6), size = rd32(e + 8), off = rd32(e + 12);
        if (size == 0 || off >= total || size > total - off) continue;
        const long score = static_cast<long>(w) * h * 64 + static_cast<long>(bpp > 32 ? 32 : bpp);
        if (score > bestScore) { bestScore = score; best = i; }
    }
    if (best < 0) return 0;
    const uint8_t* e = p + 6 + best * 16;
    const uint8_t* d = p + rd32(e + 12);
    const size_t dsize = rd32(e + 8);
    if (dsize >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0) { png.assign(reinterpret_cast<const char*>(d), dsize); return 2; }
    if (dsize < 40) return 0;
    const uint32_t hdr = rd32(d);
    if (hdr < 40 || hdr > dsize) return 0;
    const int32_t bw = static_cast<int32_t>(rd32(d + 4));
    int32_t bh = static_cast<int32_t>(rd32(d + 8));
    const uint32_t bpp = rd16(d + 14), comp = rd32(d + 16);
    uint32_t clrUsed = rd32(d + 32);
    if (comp != 0 || bw <= 0 || bw > 1024 || bh == 0) return 0;  // solo sin compresion (BI_RGB)
    const bool topDown = bh < 0;
    if (bh < 0) bh = -bh;
    const int h = bh / 2;  // la altura declarada incluye la mascara AND
    if (h <= 0 || h > 1024) return 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return 0;
    const uint32_t palN = bpp <= 8 ? (clrUsed ? clrUsed : (1u << bpp)) : 0;
    if (palN > 256) return 0;
    const size_t palOff = hdr, xorOff = palOff + static_cast<size_t>(palN) * 4;
    const size_t stride = ((static_cast<size_t>(bw) * bpp + 31) / 32) * 4;
    const size_t andOff = xorOff + stride * h, andStride = ((static_cast<size_t>(bw) + 31) / 32) * 4;
    if (xorOff > dsize || andOff > dsize) return 0;
    const bool haveAnd = andOff + andStride * h <= dsize;
    W = bw; H = h;
    rgba.assign(static_cast<size_t>(W) * H * 4, 0);
    bool anyAlpha = false;
    for (int y = 0; y < h; y++) {
        const int sy = topDown ? y : h - 1 - y;  // filas bmp: de abajo arriba
        const uint8_t* row = d + xorOff + stride * sy;
        const uint8_t* mrow = haveAnd ? d + andOff + andStride * sy : nullptr;
        for (int x = 0; x < bw; x++) {
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (bpp == 32) { b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; a = row[x * 4 + 3]; if (a) anyAlpha = true; }
            else if (bpp == 24) { b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; }
            else {
                uint32_t idx;
                if (bpp == 8) idx = row[x];
                else if (bpp == 4) idx = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                else idx = (row[x / 8] >> (7 - (x & 7))) & 1;
                if (idx < palN) { const uint8_t* c = d + palOff + idx * 4; b = c[0]; g = c[1]; r = c[2]; }
            }
            if (bpp != 32 && mrow && ((mrow[x / 8] >> (7 - (x & 7))) & 1)) a = 0;  // mascara AND: 1 = transparente
            uint8_t* o = &rgba[(static_cast<size_t>(y) * bw + x) * 4];
            o[0] = r; o[1] = g; o[2] = b; o[3] = a;
        }
    }
    if (bpp == 32 && !anyAlpha) {  // 32 bits sin canal alfa real: la transparencia sale de la mascara AND
        for (int y = 0; y < h; y++) {
            const int sy = topDown ? y : h - 1 - y;
            const uint8_t* mrow = haveAnd ? d + andOff + andStride * sy : nullptr;
            for (int x = 0; x < bw; x++)
                rgba[(static_cast<size_t>(y) * bw + x) * 4 + 3] = (mrow && ((mrow[x / 8] >> (7 - (x & 7))) & 1)) ? 0 : 255;
        }
    }
    return 1;
}
