#pragma once
// Conversion de texto a UTF-8. Detecta la codificacion en Content-Type o en <meta charset> (primeros 2 KB).
// Soporta UTF-8, ISO-8859-1/-15 y Windows-1252 (cubren casi todo el contenido en espanol/europeo no UTF-8).
#include <string>

// Devuelve la etiqueta de charset en minusculas ("utf-8", "iso-8859-1"...) o "" si no se indica.
std::string charset_detect(const std::string& contentType, const std::string& head);
// Convierte 'body' a UTF-8 si hace falta. 'detected' puede ser vacio. Devuelve true si convirtio algo.
bool charset_to_utf8(std::string& body, const std::string& detected);
bool is_valid_utf8(const std::string& s);
