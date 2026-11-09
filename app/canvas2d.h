#pragma once
// Canvas 2D (v0.9.5, segunda tanda): rasterizador por software propio, SIN dependencias de SDL ni de QuickJS (se prueba en el PC).
// Cada "canvas" es un buffer ARGB (no premultiplicado) con estado (transformacion, estilos, ruta, recorte rectangular).
// Las imagenes decodificadas de la pagina (new Image) se guardan en el mismo registro (un canvas sin uso de estado).
// Limitaciones conocidas (documentadas en docs/V0.9.5.md): recorte solo rectangular (bounding box de la ruta), sin sombras,
// sin filtros, sin lineDash, uniones de linea siempre redondeadas, texto con fuente 8x8 escalada (monoespaciada), degradado radial aproximado.
#include <cstdint>
#include <string>
#include <vector>

// Operaciones (codigos compartidos con el prelude de JS: ver jsprelude.h, objeto C2D_OPS).
enum C2dOp {
    C2D_FILLRECT = 1, C2D_STROKERECT, C2D_CLEARRECT, C2D_BEGINPATH, C2D_CLOSEPATH, C2D_MOVETO, C2D_LINETO, C2D_QUADTO, C2D_BEZIERTO,
    C2D_ARC, C2D_RECT, C2D_FILL, C2D_STROKE, C2D_SAVE, C2D_RESTORE, C2D_TRANSLATE, C2D_SCALE, C2D_ROTATE, C2D_TRANSFORM,
    C2D_SETTRANSFORM, C2D_RESETTRANSFORM, C2D_DRAWIMAGE, C2D_LINEWIDTH, C2D_ALPHA, C2D_CLIP, C2D_FILLTEXT, C2D_STROKETEXT,
    C2D_LINECAP, C2D_LINEJOIN, C2D_SMOOTH, C2D_ELLIPSE, C2D_MEASURETEXT, C2D_GRAD_LINEAR, C2D_GRAD_RADIAL, C2D_GRAD_STOP,
    C2D_FILLGRAD, C2D_STROKEGRAD, C2D_ARCTO
};
// Textos (c2d_set_str): que propiedad se cambia.
enum C2dStr { C2S_FILL = 0, C2S_STROKE, C2S_FONT, C2S_ALIGN, C2S_BASELINE, C2S_COMPOSITE, C2S_TEXT, C2S_STOPCOLOR };

bool c2d_parse_color(const std::string& s, uint32_t& argb);       // CSS: #rgb #rgba #rrggbb #rrggbbaa rgb() rgba() hsl() hsla() y nombres comunes
int  c2d_create(int w, int h);                                     // devuelve un id > 0 (0 = error/limite)
void c2d_resize(int id, int w, int h);                             // cambia el tamano y borra (como canvas.width = ...)
void c2d_reset_all();                                              // al cambiar de pagina
int  c2d_add_image(const std::vector<uint32_t>& argb, int w, int h);  // imagen decodificada -> id
bool c2d_info(int id, int& w, int& h, const uint32_t** px);       // puntero valido hasta la proxima operacion
bool c2d_take_dirty(int id);                                       // true si cambio desde la ultima vez (y lo limpia)
int  c2d_main();                                                   // el canvas "principal": el de mayor area en el que se ha dibujado (0 = ninguno)
int  c2d_used_count();                                             // cuantos canvas se han usado para dibujar
void c2d_set_str(int id, int what, const std::string& s);
double c2d_op(int id, int op, const double* a, int n);             // devuelve un valor (measureText, gradiente...) o 0
std::vector<uint8_t> c2d_get_pixels(int id, int x, int y, int w, int h);                // RGBA
void c2d_put_pixels(int id, int x, int y, int w, int h, const uint8_t* rgba, size_t len);
