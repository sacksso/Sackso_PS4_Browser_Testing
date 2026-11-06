#pragma once
// Utilidades de URL: resolver enlaces relativos, normalizar lo que escribe el usuario, quitar fragmentos.
#include <string>

std::string urlResolve(const std::string& base, const std::string& ref);   // 'ref' relativo a 'base'
std::string urlStripFragment(const std::string& url);                      // quita "#algo"
std::string urlPercentEncode(const std::string& s);                        // para consultas de busqueda
// Convierte lo escrito en la barra: URL completa, dominio sin esquema o texto de busqueda (DuckDuckGo version HTML).
std::string urlFromUserInput(const std::string& input);
bool urlIsHttp(const std::string& url);                                    // empieza por http:// o https://
