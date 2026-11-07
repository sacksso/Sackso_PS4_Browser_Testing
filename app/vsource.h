#pragma once
// Fuentes de bytes con acceso aleatorio para el reproductor de video (libSceAvPlayer las pide por "fileReplacement"):
//  - archivo local (descargas), o
//  - HTTP/HTTPS por rangos ('Range'): cache de bloques de 1 MB + hilo de lectura anticipada, para ver el video sin descargarlo.
// Logica pura (sin APIs de la consola salvo sceKernelUsleep): se prueba en PC con un servidor de rangos simulado.
#include <cstdint>
#include <functional>
#include <string>

class VSource {
public:
    virtual ~VSource() {}
    virtual bool open(std::string& err) = 0;   // false = no se puede usar (err explica por que)
    virtual uint64_t size() const = 0;
    // Lee hasta 'len' bytes desde 'pos'. Devuelve los bytes leidos (0 = fin de archivo) o <0 si hay error.
    virtual int read(uint8_t* buf, uint64_t pos, uint32_t len) = 0;
    virtual void close() {}
    virtual std::string describe() const = 0;
    virtual int bufferedPct() const { return 100; }  // % de la ventana de lectura anticipada ya descargado
    virtual bool waiting() const { return false; }   // true si una lectura espera datos de la red
    virtual std::string lastError() const { return std::string(); }
};

// Pide los bytes [start, start+len): data = contenido, total = tamano del archivo. false = error (err explica).
typedef std::function<bool(long long start, long long len, std::string& data, long long& total, std::string& err)> RangeFetch;

VSource* vsource_file(const std::string& path);
// fetch = nullptr -> usa http_range() de netclient (con 'referer').
VSource* vsource_http(const std::string& url, const std::string& referer, RangeFetch fetch = nullptr);
