#include "textengine.h"

#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "canvas.h"
#include "fonts_data.h"
#include "log.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb/stb_truetype.h>

namespace {

struct Glyph {
    float adv = 0;
    int w = 0, h = 0, xoff = 0, yoff = 0;
    std::vector<uint8_t> a;
};

struct Face {
    stbtt_fontinfo info;
    bool ok = false;
    int ascent = 0, descent = 0, gap = 0;  // en unidades de fuente
};

Face g_face[2];
std::unordered_map<uint64_t, Glyph> g_cache;

const Glyph& glyph(int font, int px, uint32_t cp) {
    const uint64_t key = (static_cast<uint64_t>(font) << 60) | (static_cast<uint64_t>(px & 0xFFF) << 32) | cp;
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    if (g_cache.size() > 9000) g_cache.clear();
    Glyph g;
    Face& f = g_face[font];
    if (f.ok) {
        uint32_t use = cp;
        if (cp == 0xA0 || cp == 0x2002 || cp == 0x2003 || cp == 0x2009 || cp == 0x202F || cp == '\t' || cp == '\n' || cp == '\r') use = ' ';
        int gi = stbtt_FindGlyphIndex(&f.info, static_cast<int>(use));
        if (gi == 0 && use != ' ') {  // sin glifo: aproximaciones ASCII habituales, si no '?'
            switch (use) {
                case 0x2010: case 0x2011: case 0x2012: case 0x2212: use = '-'; break;
                case 0x2018: case 0x2019: case 0x201A: use = '\''; break;
                case 0x201C: case 0x201D: case 0x201E: use = '"'; break;
                case 0x2022: case 0x25CF: use = '*'; break;
                default: use = '?'; break;
            }
            gi = stbtt_FindGlyphIndex(&f.info, static_cast<int>(use));
        }
        const float scale = stbtt_ScaleForMappingEmToPixels(&f.info, static_cast<float>(px));
        int adv = 0, lsb = 0;
        stbtt_GetGlyphHMetrics(&f.info, gi, &adv, &lsb);
        g.adv = adv * scale;
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&f.info, gi, scale, scale, &x0, &y0, &x1, &y1);
        g.w = x1 - x0;
        g.h = y1 - y0;
        g.xoff = x0;
        g.yoff = y0;
        if (g.w > 0 && g.h > 0) {
            g.a.assign(static_cast<size_t>(g.w) * g.h, 0);
            stbtt_MakeGlyphBitmap(&f.info, g.a.data(), g.w, g.h, g.w, scale, scale, gi);
        }
    } else {
        g.adv = px * 0.5f;
    }
    return g_cache.emplace(key, std::move(g)).first->second;
}

bool skipCp(uint32_t cp) {
    return cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || cp == 0xFEFF || cp == 0x2060 || (cp < 32 && cp != '\t');
}

}  // namespace

uint32_t te_next(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp;
    int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { i++; return '?'; }
    if (extra > 0 && i + extra >= s.size()) { i = s.size(); return '?'; }
    for (int k = 1; k <= extra; k++) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += extra + 1;
    return cp;
}

bool te_init() {
    const unsigned char* data[2] = {kFontSansData, kFontMonoData};
    bool all = true;
    for (int f = 0; f < 2; f++) {
        Face& fc = g_face[f];
        const int off = stbtt_GetFontOffsetForIndex(data[f], 0);
        fc.ok = off >= 0 && stbtt_InitFont(&fc.info, data[f], off) != 0;
        if (fc.ok) stbtt_GetFontVMetrics(&fc.info, &fc.ascent, &fc.descent, &fc.gap);
        psb_log("[font] %s: %s", f == 0 ? "sans" : "mono", fc.ok ? "ok" : "FALLO");
        all = all && fc.ok;
    }
    return all;
}

int te_ascent(int font, int px) {
    const Face& f = g_face[font];
    if (!f.ok) return px;
    return static_cast<int>(std::ceil(f.ascent * stbtt_ScaleForMappingEmToPixels(&f.info, static_cast<float>(px))));
}
int te_descent(int font, int px) {
    const Face& f = g_face[font];
    if (!f.ok) return px / 4;
    return static_cast<int>(std::ceil(-f.descent * stbtt_ScaleForMappingEmToPixels(&f.info, static_cast<float>(px))));
}

int te_width(const std::string& s, int font, int px, bool bold) {
    float x = 0;
    const float extra = (bold && px < 26) ? 1.f : 0.f;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = te_next(s, i);
        if (skipCp(cp)) continue;
        x += glyph(font, px, cp).adv + extra;
    }
    return static_cast<int>(std::lround(x));
}

int te_draw(Canvas& cv, int x, int baseY, const std::string& s, int font, int px, bool bold, uint32_t argb) {
    float fx = static_cast<float>(x);
    const float extra = (bold && px < 26) ? 1.f : 0.f;
    const int passes = (bold && px < 26) ? 2 : 1;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = te_next(s, i);
        if (skipCp(cp)) continue;
        const Glyph& g = glyph(font, px, cp);
        if (!g.a.empty() && static_cast<int>(fx) < cv.cx1 + 64) {
            for (int p = 0; p < passes; p++) {
                const int gx = static_cast<int>(fx + 0.5f) + g.xoff + p;
                const int gy = baseY + g.yoff;
                if (gx + g.w < cv.cx0 || gx >= cv.cx1 || gy + g.h < cv.cy0 || gy >= cv.cy1) continue;
                for (int j = 0; j < g.h; j++) {
                    const int yy = gy + j;
                    if (yy < cv.cy0 || yy >= cv.cy1) continue;
                    for (int k = 0; k < g.w; k++) {
                        const uint8_t a = g.a[static_cast<size_t>(j) * g.w + k];
                        if (a) cv.blend(gx + k, yy, argb, a);
                    }
                }
            }
        }
        fx += g.adv + extra;
    }
    return static_cast<int>(std::lround(fx)) - x;
}

std::string te_fit(const std::string& s, int maxW, int font, int px, bool bold) {
    if (te_width(s, font, px, bold) <= maxW) return s;
    const int ell = te_width("...", font, px, bold);
    std::string out;
    float x = 0;
    const float extra = (bold && px < 26) ? 1.f : 0.f;
    for (size_t i = 0; i < s.size();) {
        const size_t st = i;
        const uint32_t cp = te_next(s, i);
        if (skipCp(cp)) continue;
        x += glyph(font, px, cp).adv + extra;
        if (x + ell > maxW) break;
        out.append(s, st, i - st);
    }
    return out + "...";
}
