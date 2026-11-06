#include "imgworker.h"

#include <orbis/libkernel.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>

#include "log.h"
#include "netclient.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
// GIF desactivado a proposito: stb_image lo decodifica con una funcion recursiva (hasta ~4000 niveles) y la pila
// del hilo en la PS4 es pequena. Se reactivara cuando el hilo tenga una pila grande verificada.
#define STBI_ONLY_BMP
#define STBI_NO_FAILURE_STRINGS
#include <stb/stb_image.h>

namespace {

std::mutex g_mtx;
std::deque<ImgJob> g_jobs;
std::deque<ImgResult> g_results;
int g_busy = 0;
bool g_started = false;
std::atomic<bool> g_paused{false};

// Escala (RGBA 8 bits) para que quepa en bw x bh conservando la proporcion: promedio por area al reducir, bilineal al ampliar.
bool decode(const std::string& bytes, int bw, int bh, std::vector<uint32_t>& out, int& ow, int& oh) {
    int w = 0, h = 0, n = 0;
    if (!stbi_info_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n)) return false;
    if (w <= 0 || h <= 0 || static_cast<long long>(w) * h > 12LL * 1000 * 1000) {  // >12 Mpx: no cabe con seguridad en memoria
        psb_log("[img] imagen demasiado grande (%dx%d), se omite", w, h);
        return false;
    }
    unsigned char* d = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w, &h, &n, 4);
    if (!d) return false;
    if (w <= 0 || h <= 0) { stbi_image_free(d); return false; }
    const float s = std::min(static_cast<float>(bw) / w, static_cast<float>(bh) / h);
    ow = std::max(1, static_cast<int>(std::lround(w * s)));
    oh = std::max(1, static_cast<int>(std::lround(h * s)));
    out.assign(static_cast<size_t>(ow) * oh, 0);
    for (int y = 0; y < oh; y++) {
        const float sy0 = static_cast<float>(y) * h / oh, sy1 = static_cast<float>(y + 1) * h / oh;
        for (int x = 0; x < ow; x++) {
            const float sx0 = static_cast<float>(x) * w / ow, sx1 = static_cast<float>(x + 1) * w / ow;
            uint32_t r = 0, g = 0, b = 0, a = 0, cnt = 0;
            if (s < 1.0f) {
                const int ix0 = static_cast<int>(sx0), ix1 = std::max(ix0 + 1, std::min(w, static_cast<int>(std::ceil(sx1))));
                const int iy0 = static_cast<int>(sy0), iy1 = std::max(iy0 + 1, std::min(h, static_cast<int>(std::ceil(sy1))));
                for (int yy = iy0; yy < iy1; yy++)
                    for (int xx = ix0; xx < ix1; xx++) {
                        const unsigned char* p = d + (static_cast<size_t>(yy) * w + xx) * 4;
                        r += p[0]; g += p[1]; b += p[2]; a += p[3]; cnt++;
                    }
            } else {
                const int xx = std::min(w - 1, static_cast<int>((sx0 + sx1) * 0.5f)), yy = std::min(h - 1, static_cast<int>((sy0 + sy1) * 0.5f));
                const unsigned char* p = d + (static_cast<size_t>(yy) * w + xx) * 4;
                r = p[0]; g = p[1]; b = p[2]; a = p[3]; cnt = 1;
            }
            if (cnt == 0) cnt = 1;
            out[static_cast<size_t>(y) * ow + x] = ((a / cnt) << 24) | ((r / cnt) << 16) | ((g / cnt) << 8) | (b / cnt);
        }
    }
    stbi_image_free(d);
    return true;
}

void workerMain() {
    for (;;) {
        ImgJob job;
        bool have = false;
        {
            std::lock_guard<std::mutex> lk(g_mtx);
            if (!g_jobs.empty()) { job = g_jobs.front(); g_jobs.pop_front(); have = true; g_busy++; }
        }
        if (!have) { sceKernelUsleep(30 * 1000); continue; }
        while (g_paused.load()) sceKernelUsleep(20 * 1000);  // el hilo principal esta cargando una pagina
        ImgResult r;
        r.tabId = job.tabId; r.gen = job.gen; r.idx = job.idx; r.url = job.url; r.boxW = job.boxW; r.boxH = job.boxH;
        HttpResult res;
        // 429/503 (p. ej. upload.wikimedia.org limita el ritmo): esperar y reintentar hasta 2 veces.
        bool got = false;
        for (int attempt = 0; attempt < 3; attempt++) {
            got = http_get(job.url, res, 3 * 1024 * 1024, 4);
            if (got && (res.status == 429 || res.status == 503) && attempt < 2) {
                psb_log("[img] http %d, reintento %d en %d ms: %s", res.status, attempt + 1, 1500 + attempt * 2000, job.url.c_str());
                sceKernelUsleep((1500 + attempt * 2000) * 1000);
                while (g_paused.load()) sceKernelUsleep(20 * 1000);
                continue;
            }
            break;
        }
        const bool slowHost = job.url.find("wikimedia.org") != std::string::npos;  // ritmo suave con Wikimedia
        if (got && res.status == 200 && !res.truncated) {
            psb_log("[img] decodificando %zu bytes (%s)", res.body.size(), res.contentType.c_str());
            r.ok = decode(res.body, job.boxW, job.boxH, r.pix, r.pw, r.ph);
            if (r.ok) psb_log("[img] ok %dx%d", r.pw, r.ph);
            if (!r.ok) psb_log("[img] no se pudo decodificar (%s, %zu bytes): %s", res.contentType.c_str(), res.body.size(), job.url.c_str());
        } else {
            psb_log("[img] fallo la descarga (http %d, stage %s, trunc=%d): %s", res.status, res.stage.c_str(), res.truncated ? 1 : 0, job.url.c_str());
        }
        if (slowHost) sceKernelUsleep(250 * 1000);
        std::lock_guard<std::mutex> lk(g_mtx);
        g_results.push_back(std::move(r));
        g_busy--;
    }
}

}  // namespace

void imgw_start() {
    if (g_started) return;
    g_started = true;
    std::thread t(workerMain);
    t.detach();
}

void imgw_submit(const ImgJob& j) {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_jobs.push_back(j);
}

bool imgw_poll(ImgResult& out) {
    std::lock_guard<std::mutex> lk(g_mtx);
    if (g_results.empty()) return false;
    out = std::move(g_results.front());
    g_results.pop_front();
    return true;
}

int imgw_pending() {
    std::lock_guard<std::mutex> lk(g_mtx);
    return static_cast<int>(g_jobs.size()) + g_busy;
}

void imgw_cancel() {
    std::lock_guard<std::mutex> lk(g_mtx);
    g_jobs.clear();
}

void imgw_pause(bool paused) { g_paused.store(paused); }
