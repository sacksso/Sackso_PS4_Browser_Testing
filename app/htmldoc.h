#pragma once
// Modelo de documento de la fase 1: HTML -> lineas de texto con "spans" (estilo + enlace).
// Sin motor de render real: texto monoespaciado, enlaces, titulos, listas, <pre>, marcadores [img].
#include <cstdint>
#include <string>
#include <vector>

enum SpanStyle : uint8_t { ST_NORMAL = 0, ST_HEADING = 1, ST_LINK = 2, ST_BOLD = 3, ST_PRE = 4, ST_DIM = 5 };

struct Span {
    int col = 0;          // columna donde empieza
    std::string text;     // ASCII imprimible (de momento, sin FreeType)
    uint8_t style = ST_NORMAL;
    int link = -1;        // indice en Document::links o -1
};

struct DocLine {
    std::vector<Span> spans;
};

struct Document {
    std::string title;
    std::vector<DocLine> lines;
    std::vector<std::string> links;  // href tal cual aparece en el HTML (sin resolver)
    std::vector<int> linkLine;       // primera linea donde aparece cada enlace
    std::string refreshUrl;          // <meta http-equiv=refresh content="0;url=..."> (vacio si no hay)
};

// 'cols' = columnas de texto disponibles.
Document parseHtml(const std::string& html, size_t cols);
// Texto plano (text/plain, text/x-wiki...): cada linea tal cual, partida a 'cols'.
Document parsePlain(const std::string& text, size_t cols);
// Cadena UTF-8 -> ASCII (acentos sin tilde, comillas tipograficas, etc.; el resto '?').
std::string foldToAscii(const std::string& utf8);
