#pragma once
// Utilidades de URL: resolver enlaces relativos, normalizar lo que escribe el usuario, quitar fragmentos.
#include <string>

std::string urlResolve(const std::string& base, const std::string& ref);   // 'ref' relativo a 'base'
std::string urlStripFragment(const std::string& url);                      // quita "#algo"
std::string urlPercentEncode(const std::string& s);                        // para consultas de busqueda
// Convierte lo escrito en la barra: URL completa, dominio sin esquema o texto de busqueda (DuckDuckGo version HTML).
std::string urlFromUserInput(const std::string& input);
std::string urlPercentDecode(const std::string& s);                       // %XX -> byte, '+' -> espacio
// Si 'url' es un enlace de salida de DuckDuckGo (duckduckgo.com/l/?uddg=...), devuelve el destino real; si no, ''.
std::string urlUnwrapSearchRedirect(const std::string& url);
bool urlIsHttp(const std::string& url);                                    // empieza por http:// o https://
// Motor de busqueda de la barra: 0 DuckDuckGo (HTML), 1 DuckDuckGo Lite, 2 Bing, 3 Google, 4 Startpage.
extern int g_searchEngine;
const char* urlSearchEngineName(int idx);
