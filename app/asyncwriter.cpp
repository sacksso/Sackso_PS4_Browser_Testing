#include "asyncwriter.h"

#include <orbis/libkernel.h>  // sceKernelUsleep (en PC lo da el stub de tools/stubs)

bool AsyncWriter::start(size_t maxQueued) {
    setvbuf(f_, nullptr, _IOFBF, 1024 * 1024);
    maxQueued_ = maxQueued;
    closing_ = false; failed_ = false; aborted_ = false;
    queuedBytes_ = 0;
    enqSeq_ = 0; writtenSeq_ = 0;
    pos_ = 0;
    q_.clear();
    running_ = true;
    th_ = std::thread(&AsyncWriter::run, this);
    return true;
}

bool AsyncWriter::open(const std::string& path, size_t maxQueued) {
    abort();
    f_ = fopen(path.c_str(), "wb");
    if (!f_) return false;
    return start(maxQueued);
}

bool AsyncWriter::openRW(const std::string& path, bool create, long long prealloc, size_t maxQueued) {
    abort();
    f_ = fopen(path.c_str(), create ? "w+b" : "r+b");
    if (!f_) return false;
    if (create && prealloc > 0) {  // reserva el tamano final (archivo disperso): evita ampliarlo a cada escritura y reduce la fragmentacion
        if (fseek(f_, static_cast<long>(prealloc - 1), SEEK_SET) != 0 || fputc(0, f_) == EOF || fflush(f_) != 0) { fclose(f_); f_ = nullptr; return false; }
        fseek(f_, 0, SEEK_SET);
    }
    return start(maxQueued);
}

void AsyncWriter::run() {
    for (;;) {
        Item it;
        bool have = false;
        {
            std::lock_guard<std::mutex> lk(m_);
            if (!q_.empty()) { it = std::move(q_.front()); q_.pop_front(); have = true; }
            else if (closing_.load() || aborted_.load()) return;
        }
        if (!have) { sceKernelUsleep(2 * 1000); continue; }
        if (!failed_.load()) {
            if (it.off >= 0 && it.off != pos_) {
                if (fseek(f_, static_cast<long>(it.off), SEEK_SET) != 0) failed_ = true;
                else pos_ = it.off;
            }
            if (!failed_.load()) {
                if (fwrite(it.data.data(), 1, it.data.size(), f_) != it.data.size()) failed_ = true;
                else pos_ += static_cast<long long>(it.data.size());
            }
        }
        writtenSeq_ = it.seq;
        std::lock_guard<std::mutex> lk(m_);
        queuedBytes_ -= it.data.size();
    }
}

bool AsyncWriter::writeAt(long long offset, const void* data, size_t n, uint64_t* seq) {
    if (!running_ || failed_.load() || aborted_.load()) return false;
    for (;;) {  // contrapresion: no acumular sin limite si el disco va mas lento que la red
        {
            std::lock_guard<std::mutex> lk(m_);
            if (queuedBytes_ + n <= maxQueued_ || queuedBytes_ == 0) {
                Item it;
                it.off = offset;
                it.data.assign(static_cast<const char*>(data), n);
                it.seq = ++enqSeq_;
                if (seq) *seq = it.seq;
                q_.push_back(std::move(it));
                queuedBytes_ += n;
                return true;
            }
        }
        if (failed_.load() || aborted_.load()) return false;
        sceKernelUsleep(2 * 1000);
    }
}

bool AsyncWriter::write(const void* data, size_t n) { return writeAt(-1, data, n, nullptr); }

bool AsyncWriter::waitWritten(uint64_t seq) {
    if (!running_) return !failed_.load();
    while (writtenSeq_.load() < seq) {
        if (failed_.load() || aborted_.load()) return false;
        sceKernelUsleep(2 * 1000);
    }
    if (f_ && fflush(f_) != 0) failed_ = true;
    return !failed_.load();
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
