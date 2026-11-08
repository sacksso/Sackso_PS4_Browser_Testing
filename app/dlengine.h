#pragma once
// Motor de descargas por trozos (v0.9.0): reanudable, con reintentos automaticos (WiFi con microcortes) y conexiones paralelas opcionales.
//
//  - El archivo se reparte en trozos de 16 MB pedidos con 'Range'. Cada trozo completado se anota en <archivo>.part.meta; si la descarga se
//    corta, se cancela o se cierra el navegador, la siguiente descarga de la misma URL continua donde iba.
//  - Un trozo que falla se reintenta desde el ultimo byte recibido (hasta 'retries' veces seguidas sin avanzar, con espera creciente).
//  - 'conns' trabajadores toman trozos de una cola comun (se adaptan solos a la velocidad de cada conexion).
//  - Servidor sin soporte de Range (responde 200): se descarga en un solo flujo, como antes (sin reanudar).
//  - Modo 'discard' (medidor about:dlbench): no escribe a disco; repite trozos durante 'benchMs' y cuenta los bytes.
// Todo el acceso a red va por 'fetch' (inyectable): en la consola es netclient, en el PC una simulacion (tools/tests/test_dlengine.cpp).
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct DlFetchResult {
    int status = 0;
    long long rangeTotal = -1, contentLength = -1;
    std::string finalUrl, contentType, contentDisposition;
    int sceError = 0;
    std::string stage;
};
struct DlChunkSink {
    virtual ~DlChunkSink() {}
    virtual bool begin(const DlFetchResult& r) = 0;               // cabeceras de la respuesta final; false = abortar
    virtual bool write(const void* data, size_t n) = 0;           // false = abortar
};
// Pide los bytes [start, end] de 'url' (end = -1: hasta el final). true = la peticion termino sin error de red (aunque el sink haya abortado).
typedef std::function<bool(const std::string& url, long long start, long long end, DlChunkSink& sink, DlFetchResult& out)> DlFetchFn;

struct DlParams {
    std::string url, dir, name, referer;
    long long knownTotal = -1;       // tamano ya conocido (cabeceras de la pagina de confirmacion), -1 si no
    int conns = 1;                   // conexiones paralelas 1..8
    long long chunk = 16LL << 20;    // tamano de trozo
    int retries = 6;                 // reintentos seguidos sin avanzar antes de rendirse
    int backoffMs = 500;             // espera base entre reintentos (x intento, tope 4 s)
    bool resume = true;
    bool discard = false;            // medidor: no escribe, repite trozos hasta benchMs
    int benchMs = 0;
    DlFetchFn fetch;
    std::function<bool(const DlFetchResult&)> accept;              // false = llego una pagina web en vez del archivo
    std::function<std::string(const DlFetchResult&)> pickPath;     // ruta final de una descarga NUEVA (la unica funcion que decide el nombre)
    std::function<void(const std::string&)> log;
};

enum class DlResult { RUNNING, OK, CANCELLED, NET_ERROR, DISK_ERROR, BAD_STATUS, HTML_INSTEAD };
struct DlProgress2 {
    long long done = 0, total = -1;
    int activeConns = 0, retryCount = 0;
    long long queuedBytes = 0;
    bool resumed = false;
    long long resumedFrom = 0;
};
struct DlOutcome {
    DlResult result = DlResult::RUNNING;
    std::string finalPath, partPath, name, finalUrl, stage, contentType;
    int status = 0, sceError = 0;
    long long bytes = 0;
    bool kept = false;   // quedo un .part reanudable
    int connsUsed = 1;
    bool chunked = false;  // el servidor admitio Range
};

class DlEngine {
public:
    DlEngine();
    ~DlEngine();
    bool start(const DlParams& p);  // lanza el hilo coordinador (no bloquea)
    bool finished() const;
    DlProgress2 progress() const;
    void cancel();
    void join();
    DlOutcome outcome() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

// Descargas incompletas (.part + .part.meta) de una carpeta.
struct DlPartial {
    std::string url, finalPath, partPath, metaPath;
    long long total = 0, done = 0;
};
std::vector<DlPartial> dl_list_partials(const std::string& dir);
void dl_discard_partial(const DlPartial& p);
