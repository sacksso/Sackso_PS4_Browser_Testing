#pragma once
// Almacen de cookies (v0.9.1). Logica pura (sin libSceHttp): se prueba en el PC (tools/tests/test_cookies.cpp).
//  - Set-Cookie de cada respuesta (Domain, Path, Expires, Max-Age, Secure, HttpOnly), cabecera Cookie en cada peticion.
//  - Se guarda en /data/psbrowser/cookies.txt en formato Netscape (el mismo que exportan curl y las extensiones de navegador), asi se
//    pueden importar las cookies de una verificacion resuelta en el PC (p. ej. cf_clearance).
//  - Cambio de User-Agent por dominio (ua_overrides): algunas verificaciones atan la cookie al User-Agent con el que se resolvieron.
#include <cstddef>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

extern bool g_cookiesEnabled;   // false = no se guarda ni se envia nada

void cookies_set_clock(std::function<long long()> nowSec);       // solo para pruebas
void cookies_set_path(const std::string& cookiePath, const std::string& uaPath);
void cookies_load();                                              // lee los archivos (si existen)
void cookies_save();                                              // escribe cookies.txt (solo si hubo cambios)
size_t cookies_count();

// Peticion: valor de la cabecera Cookie para 'url' ("" si no hay cookies que enviar).
std::string cookies_header(const std::string& url);
// Respuesta: procesa todas las lineas "Set-Cookie:" del bloque de cabeceras. Devuelve cuantas cookies se guardaron/borraron.
int cookies_store_from_headers(const std::string& url, const char* hdrs, size_t len);
// Una sola cabecera Set-Cookie (sin el nombre de la cabecera).
bool cookies_store_one(const std::string& url, const std::string& setCookie, bool fromScript = false);
// document.cookie: lectura (sin HttpOnly) y escritura.
std::string cookies_for_script(const std::string& url);
void cookies_set_from_script(const std::string& url, const std::string& cookie);

// Importacion / exportacion (formato Netscape; acepta el prefijo "#HttpOnly_"). Devuelve cuantas cookies se importaron.
int cookies_import_netscape(const std::string& text);
std::string cookies_export_netscape();
// Importa una cabecera "Cookie: a=b; c=d" para 'domain' (cookies de sesion del dominio y subdominios).
int cookies_import_header(const std::string& domain, const std::string& header);
void cookies_clear(const std::string& domain = std::string());   // "" = todas; si no, ese dominio y sus subdominios
struct CookieDomainInfo { std::string domain; int count = 0; };
std::vector<CookieDomainInfo> cookies_domains();

// User-Agent por dominio.
std::string ua_override_for(const std::string& host);             // "" = ninguno
void ua_override_set(const std::string& domain, const std::string& ua);   // ua vacio = quitar
std::vector<std::pair<std::string, std::string>> ua_overrides();

// Utilidades (publicas para las pruebas).
long long http_date_to_epoch(const std::string& s);              // -1 si no se entiende
bool url_split(const std::string& url, std::string& scheme, std::string& host, std::string& path);
