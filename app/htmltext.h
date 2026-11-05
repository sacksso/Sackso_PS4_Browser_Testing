#pragma once
// Conversion MUY basica de HTML a lineas de texto para la primera version del visor (sin motor de render).
// Quita <script>/<style>, etiquetas y entidades comunes; los bloques (p, div, br, li, h1..) cortan linea.
#include <string>
#include <vector>

std::vector<std::string> htmlToLines(const std::string& html, size_t cols);
