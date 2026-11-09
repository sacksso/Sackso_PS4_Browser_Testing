#pragma once
// Capa de pagina "avanzada" (v0.9.0): DOM + CSS lite + JavaScript (QuickJS-ng) sobre el maquetador existente.
//
// Flujo: HTML descargado -> Dom::parse -> (CSS externos) -> scripts (inline y externos) -> DOMContentLoaded/load ->
//        Dom::serialize (aplica CSS lite) -> parseHtml() de siempre. Los temporizadores/rAF/Promesas se mueven desde
//        el bucle principal con jsx_pump(); si el DOM cambia, devuelve HTML nuevo y la pestana se vuelve a maquetar.
// Si se compila con -DPSB_NO_JS no hay motor: solo DOM + CSS lite (jsx_has_engine() = false).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct JsHost {
    // Descarga 'url' (absoluta). true = respuesta recibida (status/body validos).
    std::function<bool(const std::string& url, size_t maxBody, std::string& body, std::string& finalUrl, int& status, std::string& contentType)> fetch;
    // v0.9.2: peticion con metodo (GET/POST/HEAD), cuerpo y Content-Type. Si no se define, solo GET/HEAD por 'fetch'.
    std::function<bool(const std::string& method, const std::string& url, const std::string& reqBody, const std::string& reqContentType, size_t maxBody,
                       std::string& body, std::string& finalUrl, int& status, std::string& contentType)> request;
    std::function<void(const std::string& line)> log;
    std::function<uint32_t()> nowMs;
    std::function<bool(const std::string& url)> blocked;          // true = no descargar este script/CSS (publicidad, rastreadores)
    std::function<std::string(const std::string& url)> getCookies;           // document.cookie (sin HttpOnly)
    std::function<void(const std::string& url, const std::string& cookie)> setCookie;
    std::function<std::string(const std::string& base, const std::string& ref)> resolve;
    std::string userAgent = "Mozilla/5.0 (PlayStation 4) AppleWebKit/605.1.15 (KHTML, like Gecko)";
};

struct JsxInfo {
    bool domOk = false;       // se uso el arbol DOM
    bool jsRan = false;
    int scripts = 0;          // ejecutados
    int scriptErrors = 0;
    int cssFiles = 0;         // hojas externas descargadas
    int htmlIn = 0, htmlOut = 0;
    uint32_t ms = 0;          // tiempo total de la carga
};

void jsx_set_host(const JsHost& host);
bool jsx_has_engine();  // true si se compilo QuickJS

// Procesa una pagina HTML recien descargada. 'runJs' = ejecutar scripts. Devuelve el HTML a maquetar (si algo falla o el
// resultado parece vacio, devuelve el original). Crea la sesion activa (cierra la anterior).
std::string jsx_load(const std::string& url, const std::string& html, bool runJs, JsxInfo* info);
void jsx_attach(void* owner);  // la sesion actual pertenece a esa pestana
void jsx_close(void* owner);   // cierra la sesion si es de 'owner' (o de cualquiera si owner == nullptr)
bool jsx_active(void* owner);
// Bucle principal: ejecuta temporizadores/rAF/microtareas vencidos. true = el DOM cambio y *outHtml trae el HTML nuevo
// (como mucho cada ~120 ms).
bool jsx_pump(void* owner, uint32_t nowMs, std::string* outHtml);
// Texto de estado para el log/ajustes.
std::string jsx_status();
