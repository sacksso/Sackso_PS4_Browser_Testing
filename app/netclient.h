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
    bool stopped = false;     // la puerta (HttpGate) pidio no leer el cuerpo: solo hay cabeceras
    std::string contentDisposition;  // cabecera Content-Disposition (nombre de archivo en descargas)
    long long received = 0;   // bytes entregados a un HttpSink
    std::string body;
};

// Destino en streaming para descargas grandes: el cuerpo se entrega por trozos y no se acumula en memoria.
struct HttpSink {
    virtual ~HttpSink() {}
    // Llamado una vez, con las cabeceras de la respuesta final (sin redirecciones). false = abortar.
    virtual bool begin(const HttpResult& r) = 0;
    // Llamado por cada trozo recibido. false = abortar (cancelado o error de disco).
    virtual bool write(const void* data, size_t n) = 0;
};
// Puerta de cabeceras: se llama con las cabeceras de la respuesta final antes de leer el cuerpo.
// Si devuelve false no se lee el cuerpo y el resultado queda con stopped=true (la peticion cuenta como correcta).
typedef bool (*HttpGate)(const HttpResult& r);

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
// 'sink' (opcional): el cuerpo va al sink (descarga de un archivo, sin limite de tamano; se pide sin compresion).
// 'gate' (opcional): ver HttpGate.
bool http_request(const std::string& url, const HttpPost* post, HttpResult& out, size_t maxBody = 1024 * 1024, int maxRedirects = 5,
                  HttpSink* sink = nullptr, HttpGate gate = nullptr);
