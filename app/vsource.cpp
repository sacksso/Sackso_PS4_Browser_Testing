#include "vsource.h"

#include <orbis/libkernel.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "log.h"
#include "netclient.h"

namespace {

// ---------------------------------------------------------------- archivo local
class FileSource : public VSource {
public:
    explicit FileSource(const std::string& p) : path_(p) {}
    ~FileSource() override { close(); }
    bool open(std::string& err) override {
        f_ = fopen(path_.c_str(), "rb");
        if (!f_) { err = "No se pudo abrir el archivo: " + path_; return false; }
        fseek(f_, 0, SEEK_END);
        const long n = ftell(f_);
        if (n <= 0) { err = "El archivo esta vacio."; fclose(f_); f_ = nullptr; return false; }
        size_ = static_cast<uint64_t>(n);
        return true;
    }
    uint64_t size() const override { return size_; }
    int read(uint8_t* buf, uint64_t pos, uint32_t len) override {
        std::lock_guard<std::mutex> lk(m_);
        if (!f_) return -1;
        if (pos >= size_) return 0;
        if (pos + len > size_) len = static_cast<uint32_t>(size_ - pos);
        if (fseek(f_, static_cast<long>(pos), SEEK_SET) != 0) return -1;
        const size_t n = fread(buf, 1, len, f_);
        return static_cast<int>(n);
    }
    void close() override {
        std::lock_guard<std::mutex> lk(m_);
        if (f_) { fclose(f_); f_ = nullptr; }
    }
    std::string describe() const override { return "archivo " + path_; }

private:
    std::string path_;
    FILE* f_ = nullptr;
    uint64_t size_ = 0;
    std::mutex m_;
};

// ---------------------------------------------------------------- HTTP por rangos
class HttpSource : public VSource {
public:
    static const uint32_t kBlock = 1u << 20;   // 1 MB por peticion
    static const int kAhead = 8;               // bloques que se descargan por delante de la lectura
    static const size_t kMaxBlocks = 40;       // tope de la cache (40 MB)
    static const int kTimeoutMs = 45000;       // espera maxima de una lectura

    HttpSource(const std::string& url, const std::string& referer, RangeFetch f) : url_(url), referer_(referer), fetch_(std::move(f)) {
        if (!fetch_) {
            fetch_ = [this](long long start, long long len, std::string& data, long long& total, std::string& err) {
                HttpResult r;
                const bool ok = http_range(cur_.empty() ? url_ : cur_, start, len, referer_, r);
                if (!ok) {
                    char b[160];
                    snprintf(b, sizeof(b), "HTTP %d, paso '%s', codigo 0x%08X%s", r.status, r.stage.c_str(), static_cast<unsigned>(r.sceError),
                             r.status == 200 ? " (el servidor ignora 'Range')" : "");
                    err = b;
                    return false;
                }
                if (start == 0 && !r.finalUrl.empty()) cur_ = r.finalUrl;  // el enlace final (tras redirecciones) para los siguientes bloques
                data.swap(r.body);
                total = r.rangeTotal;
                return true;
            };
        }
    }
    ~HttpSource() override { close(); }

    bool open(std::string& err) override {
        std::string data;
        long long total = -1;
        std::string e;
        bool ok = false;
        for (int i = 0; i < 2 && !ok; i++) ok = fetch_(0, kBlock, data, total, e);
        if (!ok) { err = "No se pudo leer el video: " + e; return false; }
        if (total <= 0) { err = "El servidor no dice el tamano del archivo (sin 'Content-Range'): no permite verlo en streaming. Descargalo."; return false; }
        if (data.empty() || (static_cast<long long>(data.size()) < std::min<long long>(kBlock, total))) {
            err = "El servidor devolvio menos datos de los pedidos.";
            return false;
        }
        total_ = static_cast<uint64_t>(total);
        nblocks_ = static_cast<uint32_t>((total_ + kBlock - 1) / kBlock);
        blocks_[0].assign(data.begin(), data.end());
        stop_ = false;
        th_ = std::thread(&HttpSource::loop, this);
        psb_log("[vsrc] streaming listo: %llu bytes, %u bloques de 1 MB (%s)", static_cast<unsigned long long>(total_), nblocks_, url_.c_str());
        return true;
    }
    uint64_t size() const override { return total_; }

    int read(uint8_t* buf, uint64_t pos, uint32_t len) override {
        if (pos >= total_) return 0;
        if (pos + len > total_) len = static_cast<uint32_t>(total_ - pos);
        uint32_t done = 0;
        while (len > 0) {
            const uint32_t b = static_cast<uint32_t>(pos / kBlock);
            {
                std::lock_guard<std::mutex> lk(m_);
                cursor_ = b;
                if (!blocks_.count(b)) { need_ = static_cast<int64_t>(b); failed_.erase(b); }
            }
            bool got = false;
            int waited = 0;
            while (!got) {
                {
                    std::lock_guard<std::mutex> lk(m_);
                    auto it = blocks_.find(b);
                    if (it != blocks_.end()) {
                        const uint64_t off = pos - static_cast<uint64_t>(b) * kBlock;
                        if (off >= it->second.size()) return done > 0 ? static_cast<int>(done) : -1;  // bloque corto: servidor inconsistente
                        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(len, it->second.size() - off));
                        memcpy(buf + done, it->second.data() + off, n);
                        pos += n; len -= n; done += n;
                        got = true;
                        waiting_ = false;
                        break;
                    }
                    if (failed_.count(b)) { failed_.erase(b); waiting_ = false; return done > 0 ? static_cast<int>(done) : -1; }
                    waiting_ = true;
                }
                if (stop_) return -1;
                if (waited >= kTimeoutMs) { psb_log("[vsrc] tiempo agotado esperando el bloque %u", b); waiting_ = false; return done > 0 ? static_cast<int>(done) : -1; }
                sceKernelUsleep(5 * 1000);
                waited += 5;
            }
        }
        return static_cast<int>(done);
    }

    void close() override {
        stop_ = true;
        if (th_.joinable()) th_.join();
        std::lock_guard<std::mutex> lk(m_);
        blocks_.clear();
    }
    std::string describe() const override { return "HTTP " + url_; }
    int bufferedPct() const override {
        std::lock_guard<std::mutex> lk(m_);
        int have = 0, want = 0;
        for (int k = 0; k <= kAhead; k++) {
            const uint32_t b = cursor_ + k;
            if (b >= nblocks_) break;
            want++;
            if (blocks_.count(b)) have++;
        }
        return want == 0 ? 100 : have * 100 / want;
    }
    bool waiting() const override { return waiting_; }
    std::string lastError() const override {
        std::lock_guard<std::mutex> lk(m_);
        return err_;
    }

private:
    void loop() {
        while (!stop_) {
            int64_t pick = -1;
            {
                std::lock_guard<std::mutex> lk(m_);
                if (need_ >= 0 && !blocks_.count(static_cast<uint32_t>(need_))) pick = need_;
                else {
                    need_ = -1;
                    for (int k = 0; k <= kAhead; k++) {
                        const uint32_t c = cursor_ + k;
                        if (c >= nblocks_) break;
                        if (!blocks_.count(c) && !failed_.count(c)) { pick = c; break; }
                    }
                }
            }
            if (pick < 0) { sceKernelUsleep(10 * 1000); continue; }
            const uint32_t b = static_cast<uint32_t>(pick);
            const long long start = static_cast<long long>(b) * kBlock;
            const long long want = std::min<long long>(kBlock, static_cast<long long>(total_) - start);
            std::string data, e;
            long long total = -1;
            bool ok = false;
            for (int attempt = 0; attempt < 3 && !ok && !stop_; attempt++) {
                if (attempt > 0) sceKernelUsleep((attempt == 1 ? 300 : 900) * 1000);
                ok = fetch_(start, want, data, total, e) && static_cast<long long>(data.size()) >= want;
                if (!ok && e.empty()) e = "respuesta corta (" + std::to_string(data.size()) + " de " + std::to_string(want) + " bytes)";
            }
            std::lock_guard<std::mutex> lk(m_);
            if (ok) {
                blocks_[b].assign(data.begin(), data.end());
                evict();
            } else {
                failed_.insert(b);
                err_ = "bloque " + std::to_string(b) + ": " + e;
                psb_log("[vsrc] fallo el bloque %u: %s", b, e.c_str());
            }
        }
    }
    void evict() {  // m_ tomado
        while (blocks_.size() > kMaxBlocks) {
            int64_t worst = -1;
            int64_t worstDist = -1;
            for (auto& kv : blocks_) {
                if (kv.first == 0 || kv.first + 1 == nblocks_) continue;  // cabecera y cola (el 'moov' del mp4 suele estar en uno de los dos)
                const int64_t d = kv.first >= cursor_ ? static_cast<int64_t>(kv.first) - cursor_ : (static_cast<int64_t>(cursor_) - kv.first) * 4;
                if (d > worstDist) { worstDist = d; worst = kv.first; }
            }
            if (worst < 0) break;
            blocks_.erase(static_cast<uint32_t>(worst));
        }
    }

    std::string url_, referer_, cur_, err_;
    RangeFetch fetch_;
    uint64_t total_ = 0;
    uint32_t nblocks_ = 0, cursor_ = 0;
    int64_t need_ = -1;
    mutable std::mutex m_;
    std::map<uint32_t, std::vector<uint8_t>> blocks_;
    std::set<uint32_t> failed_;
    std::atomic<bool> stop_{false}, waiting_{false};
    std::thread th_;
};

}  // namespace

VSource* vsource_file(const std::string& path) { return new FileSource(path); }
VSource* vsource_http(const std::string& url, const std::string& referer, RangeFetch fetch) { return new HttpSource(url, referer, std::move(fetch)); }
