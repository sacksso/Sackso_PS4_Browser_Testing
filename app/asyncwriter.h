#pragma once
// Escritura a disco en un hilo aparte (v0.7.0). Las descargas leian de la red y escribian a disco en el MISMO hilo, asi que
// mientras el disco (HDD de la PS4) terminaba un fwrite no se leia el socket. Con este hilo, la red y el disco avanzan a la vez.
// Pura (std::thread + FILE*): se prueba en PC.
#include <atomic>
#include <cstddef>
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
    // Copia los datos a la cola. Espera si la cola esta llena. false = el disco fallo (o se aborto).
    bool write(const void* data, size_t n);
    // Espera a que se vacie la cola, cierra el archivo. true = todo se escribio bien.
    bool finish();
    // Descarta lo pendiente y cierra (el archivo queda a medias: borrarlo es cosa del que llama).
    void abort();
    bool failed() const { return failed_.load(); }
    long long queued() const;  // bytes pendientes en cola (para diagnostico)
private:
    void run();
    FILE* f_ = nullptr;
    std::thread th_;
    mutable std::mutex m_;
    std::deque<std::string> q_;
    size_t queuedBytes_ = 0, maxQueued_ = 0;
    std::atomic<bool> closing_{false}, failed_{false}, aborted_{false};
    bool running_ = false;
};
