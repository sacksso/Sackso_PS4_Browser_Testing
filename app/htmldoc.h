#pragma once
// Modelo de documento: HTML -> lineas de "spans" ya maquetadas en pixeles (fuente proporcional, titulos, listas,
// <pre>, imagenes, formularios). No es un motor de render completo: no hay CSS (solo color/display:none en linea).
#include <cstdint>
#include <string>
#include <vector>

enum SpanKind : uint8_t { SK_TEXT = 0, SK_IMAGE, SK_INPUT, SK_BUTTON, SK_CHECK, SK_RULE };
enum SpanRole : uint8_t { ROLE_TEXT = 0, ROLE_HEAD, ROLE_LINK, ROLE_DIM, ROLE_CODE };

struct Span {
    int x = 0, w = 0, h = 0;  // x relativa al borde izquierdo del contenido; h solo para imagenes/controles
    SpanKind kind = SK_TEXT;
    uint8_t font = 0;         // FontId
    uint8_t role = ROLE_TEXT;
    bool bold = false;
    int size = 16;            // px
    uint32_t color = 0;       // 0 = el del tema; si no, ARGB pedido por la pagina
    int link = -1;            // indice en Document::links
    int focus = -1;           // indice en Document::focus
    int ref = -1;             // imagen (images) o campo (fields)
    std::string text;
};

struct DocLine {
    int y = 0, h = 0, baseline = 0;  // baseline: distancia desde la parte alta de la linea a la linea base
    std::vector<Span> spans;
};

struct Focusable {  // elementos a los que se llega con Tab / mando
    uint8_t kind = 0;  // 0 = enlace, 1 = campo de formulario
    int ref = -1;      // indice en links o fields
    int line = -1;     // primera linea donde aparece
};

struct ImageRef {
    std::string src, alt;
    int w = 0, h = 0;               // caja reservada en la maquetacion
    int state = 0;                  // 0 pendiente, 1 pedida, 2 lista, 3 fallo
    int pw = 0, ph = 0;             // tamano de 'pix' (ya escalada para caber en la caja)
    std::vector<uint32_t> pix;      // ARGB
};

struct Form {
    std::string action;
    bool post = false;
};

struct FormField {
    int form = -1;
    std::string type;  // text, password, hidden, submit, checkbox, radio
    std::string name, value, placeholder;
    bool checked = false;
};

struct Document {
    std::string title;
    std::vector<DocLine> lines;
    std::vector<std::string> links;  // href tal cual aparece en el HTML (sin resolver)
    std::vector<Focusable> focus;
    std::vector<ImageRef> images;
    std::vector<Form> forms;
    std::vector<FormField> fields;
    std::vector<std::pair<std::string, int>> anchors;  // id/name -> indice de linea
    std::string refreshUrl;   // <meta http-equiv=refresh content="0;url=...">
    int height = 0;           // alto total en px
    int width = 0;            // ancho de maquetacion
};

// 'width' = ancho del contenido en px; 'zoomPct' = tamano del texto (100 = normal).
Document parseHtml(const std::string& html, int width, int zoomPct);
// Texto plano (text/plain...): monoespaciado, cada linea tal cual, partida al ancho.
Document parsePlain(const std::string& text, int width, int zoomPct);
// Parsea "#rgb", "#rrggbb", "rgb(r,g,b)" o un nombre de color basico -> ARGB; 0 si no se entiende.
uint32_t parseCssColor(const std::string& s);
