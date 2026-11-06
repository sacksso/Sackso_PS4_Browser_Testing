#pragma once
// Temas claro/oscuro (paleta inspirada en los navegadores de escritorio).
#include <cstdint>

struct Theme {
    bool dark;
    uint32_t bg, text, dim, link, head, code, codeBg, rule, imgBg, imgBorder;
    uint32_t tabbar, tabActive, tabHover, toolbar, omni, omniBorder, accent, focusBg, ink;
    uint32_t inputBg, inputBorder, btnBg, btnBorder, scrollTrack, scrollThumb, statusBg, statusText, bad;
};

inline Theme themeLight() {
    return Theme{false, 0xFFFFFFFF, 0xFF202124, 0xFF5F6368, 0xFF1A5DC8, 0xFF202124, 0xFF1F2328, 0xFFF1F3F4, 0xFFDADCE0, 0xFFF1F3F4, 0xFFDADCE0,
                 0xFFDEE1E6, 0xFFFFFFFF, 0xFFE8EAED, 0xFFFFFFFF, 0xFFF1F3F4, 0xFFDADCE0, 0xFF1A73E8, 0xFFD2E3FC, 0xFF3C4043,
                 0xFFFFFFFF, 0xFF9AA0A6, 0xFFF1F3F4, 0xFFDADCE0, 0xFFF1F3F4, 0xFFBDC1C6, 0xFFF1F3F4, 0xFF3C4043, 0xFFD93025};
}
inline Theme themeDark() {
    return Theme{true, 0xFF202124, 0xFFE8EAED, 0xFF9AA0A6, 0xFF8AB4F8, 0xFFF1F3F4, 0xFFE8EAED, 0xFF2D2E31, 0xFF3C4043, 0xFF2D2E31, 0xFF3C4043,
                 0xFF171717, 0xFF35363A, 0xFF292A2D, 0xFF35363A, 0xFF202124, 0xFF5F6368, 0xFF8AB4F8, 0xFF174EA6, 0xFFE8EAED,
                 0xFF2D2E31, 0xFF80868B, 0xFF3C4043, 0xFF5F6368, 0xFF2D2E31, 0xFF5F6368, 0xFF2D2E31, 0xFFE8EAED, 0xFFF28B82};
}

inline int lumOf(uint32_t c) { return (static_cast<int>((c >> 16) & 255) * 299 + static_cast<int>((c >> 8) & 255) * 587 + static_cast<int>(c & 255) * 114) / 1000; }
// Si el color pedido por la pagina casi no se distingue del fondo, se usa el del tema.
inline uint32_t fixContrast(uint32_t fg, uint32_t bg, uint32_t fallback) {
    int d = lumOf(fg) - lumOf(bg);
    if (d < 0) d = -d;
    return d < 70 ? fallback : fg;
}
