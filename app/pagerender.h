#pragma once
// Dibujo de un Document en una region del lienzo + localizacion de elementos bajo el puntero.
#include "canvas.h"
#include "htmldoc.h"
#include "theme.h"

struct RenderCtx {
    const Theme* th;
    int viewX, viewY, viewW, viewH;  // region de la pagina en el lienzo
    int contentLeft;                 // x (en el lienzo) del origen del contenido
    int scroll;                      // desplazamiento vertical en px
    int focus = -1;                  // elemento seleccionado (teclado/mando)
    int hover = -1;                  // elemento bajo el raton
    int editField = -1;              // campo en edicion
    bool caretOn = false;
};

void drawDocument(Canvas& cv, const Document& d, const RenderCtx& rc);
// Devuelve el indice de Document::focus bajo (mx,my) o -1.
int hitFocus(const Document& d, const RenderCtx& rc, int mx, int my);
// Indice de la primera linea visible con y+h > scroll (busqueda binaria).
int firstVisibleLine(const Document& d, int scroll);
