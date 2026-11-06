#pragma once
// Cliente HTTP/HTTPS sobre libSceHttp + libSceSsl del propio sistema de la PS4.
// Ventajas: DNS, TLS y almacen de certificados los pone la consola (no hace falta libcurl ni mbedTLS para empezar).
#include <cstddef>
#include <string>

struct HttpResult {
    int status = -1;          // codigo HTTP (200, 404...), -1 si no hubo respuesta
    int sceError = 0;         // codigo SCE de error (0 = ninguno), p. ej. 0x80431xxx
    std::string stage;        // en que paso fallo ("sendRequest", "connection"...) o "" si ok
    std::string finalUrl;     // URL final tras redirecciones
    std::string contentType;
    long long contentLength = -1;
    int redirects = 0;
    bool truncated = false;   // el cuerpo se corto por llegar al maximo
    std::string body;
};

// Cuerpo de un POST (por ejemplo application/x-www-form-urlencoded).
struct HttpPost {
    std::string body;
    std::string contentType;
};

bool net_init();
void net_shutdown();
// GET con redirecciones manuales (maximo 'maxRedirects'). 'maxBody' limita el cuerpo en bytes.
bool http_get(const std::string& url, HttpResult& out, size_t maxBody = 1024 * 1024, int maxRedirects = 5);
// Igual que http_get pero con POST si 'post' no es nulo (las redirecciones posteriores se siguen con GET).
bool http_request(const std::string& url, const HttpPost* post, HttpResult& out, size_t maxBody = 1024 * 1024, int maxRedirects = 5);
