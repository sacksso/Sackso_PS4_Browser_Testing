#pragma once
// v0.9.7: conversion NV12 -> ARGB8888 rapida para el reproductor (sin dependencias de la consola; se prueba en PC con
// tools/tests/test_vconv.cpp, que compara SSE2 y reduccion 2:1 contra la version escalar de referencia).
//
//  - nv12ToArgbRef:    version escalar (la de v0.9.0-v0.9.6), sirve de referencia y de respaldo.
//  - nv12ToArgb:       usa SSE2 (8 pixeles por iteracion, 2 filas a la vez) si el compilador lo permite; si no, la escalar.
//                      Compilar con -DPSB_NO_SIMD fuerza la escalar. Diferencia maxima con la referencia: 2 niveles por canal.
//  - nv12ToArgbHalf:   salida a MITAD de resolucion en cada eje (promedio 2x2 del brillo + croma directo): 4 veces menos pixeles
//                      que convertir, subir como textura y escalar. Pensado para cuando el video se muestra a 720p/540p.
// Formato de salida: 0xAARRGGBB (en memoria B,G,R,A), rango limitado, BT.709 si hd (alto >= 720) y BT.601 si no.
#include <algorithm>
#include <cstddef>
#include <cstdint>

#if defined(__SSE2__) && !defined(PSB_NO_SIMD)
#include <emmintrin.h>
#define PSB_VCONV_SSE2 1
#endif

namespace vconv {

struct Coef { int rv, gu, gv, bu; };
inline Coef coefs(bool hd) {
    return hd ? Coef{459, 55, 136, 541} : Coef{409, 100, 208, 516};  // x256
}
inline uint8_t clip8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); }
inline uint32_t pack(int y16x298, int r, int g, int b) {  // y16x298 = 298*(Y-16); r/g/b = desplazamientos de croma (x256)
    return 0xFF000000u | (static_cast<uint32_t>(clip8((y16x298 + r) >> 8)) << 16) |
           (static_cast<uint32_t>(clip8((y16x298 - g) >> 8)) << 8) | clip8((y16x298 + b) >> 8);
}

// Un par de filas (j, j+1) desde la columna i0 hasta w, en escalar. o1 puede ser igual a o0 si no hay segunda fila.
inline void rowPairScalar(const uint8_t* y0, const uint8_t* y1, const uint8_t* c, uint32_t* o0, uint32_t* o1, bool two, int i0, int w, const Coef& k) {
    for (int i = i0; i < w; i += 2) {
        const int u = c[i] - 128, v = c[i + 1] - 128;
        const int r = k.rv * v, g = k.gu * u + k.gv * v, b = k.bu * u;
        const int ni = i + 1 < w ? i + 1 : i;
        o0[i] = pack(298 * (y0[i] - 16), r, g, b);
        if (i + 1 < w) o0[i + 1] = pack(298 * (y0[ni] - 16), r, g, b);
        if (two) {
            o1[i] = pack(298 * (y1[i] - 16), r, g, b);
            if (i + 1 < w) o1[i + 1] = pack(298 * (y1[ni] - 16), r, g, b);
        }
    }
}

inline void nv12ToArgbRef(const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h, uint32_t* out, bool hd) {
    const Coef k = coefs(hd);
    for (int j = 0; j < h; j += 2) {
        const bool two = j + 1 < h;
        const uint8_t* y0 = y + static_cast<size_t>(j) * ys;
        const uint8_t* y1 = two ? y0 + ys : y0;
        uint32_t* o0 = out + static_cast<size_t>(j) * w;
        rowPairScalar(y0, y1, uv + static_cast<size_t>(j / 2) * uvs, o0, two ? o0 + w : o0, two, 0, w, k);
    }
}

#ifdef PSB_VCONV_SSE2
// 8 pixeles de dos filas: Y de 8 bytes por fila, 4 pares UV (8 bytes). Aritmetica de 16 bits con _mm_mulhi_epi16.
inline void nv12Sse2(const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h, uint32_t* out, bool hd) {
    const Coef k = coefs(hd);
    const __m128i zero = _mm_setzero_si128();
    const __m128i c16 = _mm_set1_epi16(16), c128 = _mm_set1_epi16(128), a255 = _mm_set1_epi8(static_cast<char>(0xFF));
    const __m128i cY = _mm_set1_epi16(1192);  // 298/256 = 1192/1024 exacto: mulhi((Y-16)<<6, 1192)
    // coeficientes de croma a escala 2^13: mulhi(x<<3, c) = x*coef
    const __m128i cRV = _mm_set1_epi16(static_cast<short>((k.rv * 8192 + 128) / 256));
    const __m128i cGU = _mm_set1_epi16(static_cast<short>((k.gu * 8192 + 128) / 256));
    const __m128i cGV = _mm_set1_epi16(static_cast<short>((k.gv * 8192 + 128) / 256));
    const __m128i cBU = _mm_set1_epi16(static_cast<short>((k.bu * 8192 + 128) / 256));
    const __m128i mask16 = _mm_set1_epi32(0x0000FFFF);
    int j = 0;
    for (; j + 1 < h; j += 2) {
        const uint8_t* y0 = y + static_cast<size_t>(j) * ys;
        const uint8_t* y1 = y0 + ys;
        const uint8_t* c = uv + static_cast<size_t>(j / 2) * uvs;
        uint32_t* o0 = out + static_cast<size_t>(j) * w;
        uint32_t* o1 = o0 + w;
        int i = 0;
        for (; i + 8 <= w; i += 8) {
            const __m128i uv8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(c + i));
            const __m128i x = _mm_unpacklo_epi8(uv8, zero);                      // u0 v0 u1 v1 u2 v2 u3 v3 (16 bits)
            const __m128i u32 = _mm_and_si128(x, mask16), v32 = _mm_srli_epi32(x, 16);
            __m128i ud = _mm_or_si128(u32, _mm_slli_epi32(u32, 16));              // u0 u0 u1 u1 u2 u2 u3 u3
            __m128i vd = _mm_or_si128(v32, _mm_slli_epi32(v32, 16));
            ud = _mm_slli_epi16(_mm_sub_epi16(ud, c128), 3);
            vd = _mm_slli_epi16(_mm_sub_epi16(vd, c128), 3);
            const __m128i rOff = _mm_mulhi_epi16(vd, cRV);
            const __m128i gOff = _mm_add_epi16(_mm_mulhi_epi16(ud, cGU), _mm_mulhi_epi16(vd, cGV));
            const __m128i bOff = _mm_mulhi_epi16(ud, cBU);
            for (int row = 0; row < 2; row++) {
                const uint8_t* yr = row ? y1 : y0;
                uint32_t* o = row ? o1 : o0;
                const __m128i y8 = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(yr + i));
                const __m128i a = _mm_mulhi_epi16(_mm_slli_epi16(_mm_sub_epi16(_mm_unpacklo_epi8(y8, zero), c16), 6), cY);
                const __m128i r = _mm_packus_epi16(_mm_add_epi16(a, rOff), zero);
                const __m128i g = _mm_packus_epi16(_mm_sub_epi16(a, gOff), zero);
                const __m128i b = _mm_packus_epi16(_mm_add_epi16(a, bOff), zero);
                const __m128i bg = _mm_unpacklo_epi8(b, g), ra = _mm_unpacklo_epi8(r, a255);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(o + i), _mm_unpacklo_epi16(bg, ra));
                _mm_storeu_si128(reinterpret_cast<__m128i*>(o + i + 4), _mm_unpackhi_epi16(bg, ra));
            }
        }
        if (i < w) rowPairScalar(y0, y1, c, o0, o1, true, i, w, k);  // resto (ancho no multiplo de 8)
    }
    if (j < h) {  // altura impar: ultima fila en escalar
        const uint8_t* y0 = y + static_cast<size_t>(j) * ys;
        uint32_t* o0 = out + static_cast<size_t>(j) * w;
        rowPairScalar(y0, y0, uv + static_cast<size_t>(j / 2) * uvs, o0, o0, false, 0, w, k);
    }
}
#endif

inline void nv12ToArgb(const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h, uint32_t* out, bool hd) {
#ifdef PSB_VCONV_SSE2
    nv12Sse2(y, ys, uv, uvs, w, h, out, hd);
#else
    nv12ToArgbRef(y, ys, uv, uvs, w, h, out, hd);
#endif
}

// Salida de (w/2) x (h/2): cada pixel = promedio 2x2 del brillo y el par UV que le corresponde (el croma de NV12 ya va a mitad).
inline void halfRowScalar(const uint8_t* y0, const uint8_t* y1, const uint8_t* c, uint32_t* o, int i0, int ow, const Coef& k) {
    for (int i = i0; i < ow; i++) {
        const int u = c[2 * i] - 128, v = c[2 * i + 1] - 128;
        const int yy = (y0[2 * i] + y0[2 * i + 1] + y1[2 * i] + y1[2 * i + 1] + 2) >> 2;
        o[i] = pack(298 * (yy - 16), k.rv * v, k.gu * u + k.gv * v, k.bu * u);
    }
}

inline void nv12ToArgbHalfRef(const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h, uint32_t* out, bool hd) {
    const Coef k = coefs(hd);
    const int ow = w / 2, oh = h / 2;
    for (int j = 0; j < oh; j++) {
        const uint8_t* y0 = y + static_cast<size_t>(2 * j) * ys;
        halfRowScalar(y0, y0 + ys, uv + static_cast<size_t>(j) * uvs, out + static_cast<size_t>(j) * ow, 0, ow, k);
    }
}

#ifdef PSB_VCONV_SSE2
// 8 pixeles de salida por iteracion: 16 bytes de Y por fila (promedio 2x2 exacto en 16 bits) y 16 bytes de UV (8 pares).
inline void nv12HalfSse2(const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h, uint32_t* out, bool hd) {
    const Coef k = coefs(hd);
    const __m128i zero = _mm_setzero_si128();
    const __m128i c16 = _mm_set1_epi16(16), c128 = _mm_set1_epi16(128), a255 = _mm_set1_epi8(static_cast<char>(0xFF));
    const __m128i cY = _mm_set1_epi16(1192), m00FF = _mm_set1_epi16(0x00FF);
    const __m128i cRV = _mm_set1_epi16(static_cast<short>(k.rv * 32)), cGU = _mm_set1_epi16(static_cast<short>(k.gu * 32));
    const __m128i cGV = _mm_set1_epi16(static_cast<short>(k.gv * 32)), cBU = _mm_set1_epi16(static_cast<short>(k.bu * 32));
    const int ow = w / 2, oh = h / 2;
    for (int j = 0; j < oh; j++) {
        const uint8_t* y0 = y + static_cast<size_t>(2 * j) * ys;
        const uint8_t* y1 = y0 + ys;
        const uint8_t* c = uv + static_cast<size_t>(j) * uvs;
        uint32_t* o = out + static_cast<size_t>(j) * ow;
        int i = 0;
        for (; i + 8 <= ow; i += 8) {
            const __m128i r0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(y0 + 2 * i));
            const __m128i r1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(y1 + 2 * i));
            __m128i ysum = _mm_add_epi16(_mm_add_epi16(_mm_and_si128(r0, m00FF), _mm_srli_epi16(r0, 8)),
                                         _mm_add_epi16(_mm_and_si128(r1, m00FF), _mm_srli_epi16(r1, 8)));
            const __m128i yh = _mm_srli_epi16(_mm_add_epi16(ysum, _mm_set1_epi16(2)), 2);          // 8 brillos (16 bits), promedio 2x2 exacto
            const __m128i uvAll = _mm_loadu_si128(reinterpret_cast<const __m128i*>(c + 2 * i));   // u0 v0 ... u7 v7
            const __m128i ud = _mm_slli_epi16(_mm_sub_epi16(_mm_and_si128(uvAll, m00FF), c128), 3);
            const __m128i vd = _mm_slli_epi16(_mm_sub_epi16(_mm_srli_epi16(uvAll, 8), c128), 3);
            const __m128i rOff = _mm_mulhi_epi16(vd, cRV);
            const __m128i gOff = _mm_add_epi16(_mm_mulhi_epi16(ud, cGU), _mm_mulhi_epi16(vd, cGV));
            const __m128i bOff = _mm_mulhi_epi16(ud, cBU);
            const __m128i a = _mm_mulhi_epi16(_mm_slli_epi16(_mm_sub_epi16(yh, c16), 6), cY);
            const __m128i rr = _mm_packus_epi16(_mm_add_epi16(a, rOff), zero);
            const __m128i gg = _mm_packus_epi16(_mm_sub_epi16(a, gOff), zero);
            const __m128i bb = _mm_packus_epi16(_mm_add_epi16(a, bOff), zero);
            const __m128i bg = _mm_unpacklo_epi8(bb, gg), ra = _mm_unpacklo_epi8(rr, a255);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(o + i), _mm_unpacklo_epi16(bg, ra));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(o + i + 4), _mm_unpackhi_epi16(bg, ra));
        }
        if (i < ow) halfRowScalar(y0, y1, c, o, i, ow, k);
    }
}
#endif

inline void nv12ToArgbHalf(const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h, uint32_t* out, bool hd) {
#ifdef PSB_VCONV_SSE2
    nv12HalfSse2(y, ys, uv, uvs, w, h, out, hd);
#else
    nv12ToArgbHalfRef(y, ys, uv, uvs, w, h, out, hd);
#endif
}

}  // namespace vconv
