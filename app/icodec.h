#pragma once
// Extractor de iconos .ico/.cur (favicons). Pura: se prueba en PC. Elige la imagen mas grande del archivo.
//  1 = devuelto en 'rgba' (w*h*4 bytes, orden R,G,B,A) -- entradas BMP (1/4/8/24/32 bits, con mascara AND)
//  2 = la entrada es un PNG: se devuelve en 'png' para que lo decodifique stb_image
//  0 = no es un .ico valido o no se soporta
#include <cstdint>
#include <string>
#include <vector>

bool ico_looks_like(const std::string& bytes);
int ico_extract(const std::string& bytes, int& w, int& h, std::vector<uint8_t>& rgba, std::string& png);
