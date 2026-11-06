#pragma once
// Texto con fuentes TrueType incrustadas (stb_truetype del toolchain de OpenOrbis): proporcional, UTF-8 completo
// (tildes, simbolos), tamanos arbitrarios y cache de glifos.
#include <cstdint>
#include <string>

struct Canvas;
enum FontId { FONT_SANS = 0, FONT_MONO = 1 };

bool te_init();
int te_width(const std::string& utf8, int font, int px, bool bold);
int te_ascent(int font, int px);   // pixeles por encima de la linea base
int te_descent(int font, int px);  // pixeles por debajo (positivo)
// Dibuja con la linea base en baseY. Devuelve el ancho dibujado.
int te_draw(Canvas& cv, int x, int baseY, const std::string& utf8, int font, int px, bool bold, uint32_t argb);
// Recorta con "..." para que quepa en maxW.
std::string te_fit(const std::string& utf8, int maxW, int font, int px, bool bold);
// Decodifica un codepoint UTF-8 en s[i] y avanza i.
uint32_t te_next(const std::string& s, size_t& i);
