#pragma once
// Teclado en pantalla para el mando (v0.5.0). Logica y dibujo independientes de SDL/PS4: se prueba en PC.
// Capas: letras (con Mayus de un toque o bloqueo), simbolos y letras acentuadas. Texto en UTF-8; el cursor va por caracteres.
#include <cstddef>
#include <cstdint>
#include <string>

struct Canvas;
struct Theme;

enum OskInput {
    OSK_UP, OSK_DOWN, OSK_LEFT, OSK_RIGHT,
    OSK_SELECT,       // pulsar la tecla resaltada (boton X)
    OSK_BACKSPACE,    // borrar (boton O); con el texto vacio cierra el teclado
    OSK_SPACE,        // espacio (boton cuadrado)
    OSK_SHIFT,        // mayusculas (boton triangulo)
    OSK_CURSOR_L, OSK_CURSOR_R
};

enum OskResult {
    OSK_NONE,        // nada cambio
    OSK_REDRAW,      // solo cambio la interfaz (seleccion, capa, cursor)
    OSK_CHANGED,     // cambio el texto
    OSK_SUBMITTED,   // tecla "Ir": aceptar
    OSK_CANCELLED    // tecla "Cerrar" (o O con el texto vacio)
};

struct Osk {
    bool open = false;
    bool password = false;
    std::string title;
    std::string text;
    size_t cur = 0;     // posicion del cursor en bytes (siempre en un limite de caracter UTF-8)
    int layer = 0;      // 0 letras, 1 simbolos, 2 acentos
    int shift = 0;      // 0 no, 1 una letra, 2 bloqueo
    int row = 1, col = 0;
};

void osk_open(Osk& k, const std::string& title, const std::string& initial, bool password);
OskResult osk_input(Osk& k, OskInput in);
// Dibuja el panel abajo y al centro. caretOn = fase del parpadeo del cursor de texto.
void osk_draw(const Osk& k, Canvas& cv, const Theme& th, int screenW, int screenH, bool caretOn);

// Utilidades UTF-8 (tambien las usa main.cpp para borrar con el teclado fisico).
size_t utf8_prev_pos(const std::string& s, size_t pos);
size_t utf8_next_pos(const std::string& s, size_t pos);
void utf8_pop_back(std::string& s);
