#include "asyncwriter.h"

#include <orbis/libkernel.h>  // sceKernelUsleep (en PC lo da el stub de tools/stubs)

bool AsyncWriter::open(const std::string& path, size_t maxQueued) {
    abort();
    f_ = fopen(path.c_str(), "wb");
    if (!f_) return false;
    setvbuf(f_, nullptr, _IOFBF, 256 * 1024);
    maxQueued_ = maxQueued;
    closing_ = false; failed_ = false; aborted_ = false;
    queuedBytes_ = 0;
    q_.clear();
    running_ = true;
    th_ = std::thread(&AsyncWriter::run, this);
    return true;
}

void AsyncWriter::run() {
    for (;;) {
        std::string chunk;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!q_.empty()) { chunk = std::move(q_.front()); q_.pop_front(); }
            else if (closing_.load() || aborted_.load()) return;
        }
        if (chunk.empty()) { sceKernelUsleep(2 * 1000); continue; }
        if (!failed_.load() && fwrite(chunk.data(), 1, chunk.size(), f_) != chunk.size()) failed_ = true;
        std::lock_guard<std::mutex> lk(m_);
        queuedBytes_ -= chunk.size();
    }
}

bool AsyncWriter::write(const void* data, size_t n) {
    if (!running_ || failed_.load() || aborted_.load()) return false;
    for (;;) {  // contrapresion: no acumular sin limite si el disco va mas lento que la red
        {
            std::lock_guard<std::mutex> lk(m_);
            if (queuedBytes_ + n <= maxQueued_ || queuedBytes_ == 0) {
                q_.emplace_back(static_cast<const char*>(data), n);
                queuedBytes_ += n;
                return true;
            }
        }
        if (failed_.load() || aborted_.load()) return false;
        sceKernelUsleep(2 * 1000);
    }
}

bool AsyncWriter::finish() {
    if (!running_) return !failed_.load();
    closing_ = true;
    if (th_.joinable()) th_.join();
    running_ = false;
    bool ok = !failed_.load();
    if (f_) { if (fclose(f_) != 0) ok = false; f_ = nullptr; }
    return ok;
}

void AsyncWriter::abort() {
    if (running_) {
        aborted_ = true;
        if (th_.joinable()) th_.join();
        running_ = false;
    }
    if (f_) { fclose(f_); f_ = nullptr; }
}

long long AsyncWriter::queued() const {
    std::lock_guard<std::mutex> lk(m_);
    return static_cast<long long>(queuedBytes_);
}
