#pragma once
// Escritura a disco en un hilo aparte (v0.7.0). Las descargas leian de la red y escribian a disco en el MISMO hilo, asi que
// mientras el disco (HDD de la PS4) terminaba un fwrite no se leia el socket. Con este hilo, la red y el disco avanzan a la vez.
// v0.9.0: escritura en posicion (writeAt, para descargas por trozos/paralelas), reserva de tamano, reapertura para reanudar
// y waitWritten() (garantiza que un tramo ya esta en disco antes de anotarlo como completo).
// Pura (std::thread + FILE*): se prueba en PC.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class AsyncWriter {
public:
    ~AsyncWriter() { abort(); }
    // Abre 'path' para escribir (wb) y lanza el hilo. 'maxQueued' = bytes maximos en cola antes de que write() espere.
    bool open(const std::string& path, size_t maxQueued = 12u * 1024 * 1024);
    // Abre para escritura en posicion. create=true: crea/trunca (y reserva 'prealloc' bytes si > 0); create=false: abre el existente
    // sin tocarlo (reanudar). false = no se pudo abrir.
    bool openRW(const std::string& path, bool create, long long prealloc, size_t maxQueued = 24u * 1024 * 1024);
    // Copia los datos a la cola. Espera si la cola esta llena. false = el disco fallo (o se aborto).
    bool write(const void* data, size_t n);
    // Igual, pero en la posicion 'offset' del archivo. Devuelve en *seq el numero de orden de la escritura (para waitWritten).
    bool writeAt(long long offset, const void* data, size_t n, uint64_t* seq = nullptr);
    uint64_t lastSeq() const { return enqSeq_.load(); }
    // Espera a que todo lo encolado hasta 'seq' este escrito (y vaciado a disco con fflush). false = fallo/abortado.
    bool waitWritten(uint64_t seq);
    // Espera a que se vacie la cola, cierra el archivo. true = todo se escribio bien.
    bool finish();
    // Descarta lo pendiente y cierra (el archivo queda a medias: borrarlo es cosa del que llama).
    void abort();
    bool failed() const { return failed_.load(); }
    long long queued() const;  // bytes pendientes en cola (para diagnostico)
private:
    struct Item { long long off = -1; std::string data; uint64_t seq = 0; };
    void run();
    bool start(size_t maxQueued);
    FILE* f_ = nullptr;
    std::thread th_;
    mutable std::mutex m_;
    std::deque<Item> q_;
    size_t queuedBytes_ = 0, maxQueued_ = 0;
    std::atomic<bool> closing_{false}, failed_{false}, aborted_{false};
    std::atomic<uint64_t> enqSeq_{0}, writtenSeq_{0};
    long long pos_ = 0;
    bool running_ = false;
};
