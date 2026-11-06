#pragma once
// Descompresion deflate / zlib / gzip propia (sin dependencias, sin recursion, sin buffers grandes en la pila:
// segura en hilos de la PS4). Salida limitada a 'maxOut' bytes.
#include <cstddef>
#include <cstdint>
#include <string>

// Resultado: 0 = ok, 1 = datos corruptos, 2 = se alcanzo maxOut (out contiene la parte descomprimida; truncada).
int inflate_raw(const uint8_t* src, size_t n, std::string& out, size_t maxOut);
int inflate_zlib(const uint8_t* src, size_t n, std::string& out, size_t maxOut);  // cabecera zlib de 2 bytes
int inflate_gzip(const uint8_t* src, size_t n, std::string& out, size_t maxOut);  // cabecera gzip (RFC 1952)
// Decodifica segun el valor de Content-Encoding ("gzip", "x-gzip", "deflate", "identity"/vacio). Devuelve 0/1/2 como arriba;
// 3 = codificacion desconocida (out queda intacto).
int content_decode(const std::string& encoding, const std::string& body, std::string& out, size_t maxOut);
