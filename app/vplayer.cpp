#include "vplayer.h"

#include <orbis/AudioOut.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <orbis/libkernel.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "canvas.h"
#include "log.h"
#include "vsource.h"
#include "vsync.h"

static uint32_t SDL_GetTicksCompat() { return static_cast<uint32_t>(sceKernelGetProcessTime() / 1000); }  // ms desde el inicio del proceso

// ---------------------------------------------------------------------------------------------------------------------
// libSceAvPlayer: la cabecera de OpenOrbis solo trae "void f();" sin parametros, asi que se declaran aqui las firmas reales
// (estructuras tal como las usan los juegos de PS4; layout contrastado con la implementacion de shadPS4). No se incluye
// <orbis/AvPlayer.h> para no chocar con esas declaraciones.
// ---------------------------------------------------------------------------------------------------------------------
namespace {

struct AvAllocator {
    void* obj;
    void* (*allocate)(void*, uint32_t, uint32_t);
    void (*deallocate)(void*, void*);
    void* (*allocateTexture)(void*, uint32_t, uint32_t);
    void (*deallocateTexture)(void*, void*);
};
struct AvFileReplacement {
    void* obj;
    int32_t (*open)(void*, const char*);
    int32_t (*close)(void*);
    int32_t (*readOffset)(void*, uint8_t*, uint64_t, uint32_t);
    uint64_t (*size)(void*);
};
struct AvEventReplacement {
    void* obj;
    void (*callback)(void*, int32_t, int32_t, void*);
};
struct AvInitData {
    AvAllocator memory;
    AvFileReplacement file;
    AvEventReplacement event;
    int32_t debugLevel;  // 0 = ninguno
    uint32_t basePriority;
    int32_t numOutputVideoFrameBuffers;
    bool autoStart;
    uint8_t reserved[3];
    const char* defaultLanguage;
};
struct AvDecoderInit {
    uint32_t type;       // union decoder_type: 0 defecto, 2 software, 3 software2
    uint8_t params[28];  // union decoder_params (avc: affinity i32, priority i32, depth u8, pipe u8, queue u8, interlaced u8, ...)
};
struct AvPostInit {
    uint32_t demuxVideoBufferSize;
    AvDecoderInit video, audio;
    uint32_t httpCtx, sslCtx;
    uint8_t reserved[56];
};
struct AvStreamDetails {  // union de 16 bytes
    union {
        uint8_t raw[16];
        struct { uint16_t channels; uint8_t r[2]; uint32_t sampleRate; uint32_t size; char lang[4]; } audio;
        struct { uint32_t width; uint32_t height; float aspect; char lang[4]; } video;
    };
};
struct AvStreamInfo {
    uint32_t type;  // 0 video, 1 audio, 2 texto
    uint8_t reserved[4];
    AvStreamDetails details;
    uint64_t duration;   // ms
    uint64_t startTime;
};
struct AvFrameInfo {
    uint8_t* data;
    uint8_t reserved[4];
    uint64_t timestamp;  // ms
    AvStreamDetails details;
};
struct AvVideoEx {
    uint32_t width, height;
    float aspect;
    uint8_t lang[4];
    uint32_t framerate;
    uint32_t cropLeft, cropRight, cropTop, cropBottom;
    uint32_t pitch;
    uint8_t lumaBits, chromaBits;
    bool fullRange;
    uint8_t reserved1[37];
};
struct AvFrameInfoEx {
    void* data;
    uint8_t reserved[4];
    uint64_t timestamp;
    union { AvVideoEx video; uint8_t raw[80]; } details;
};
static_assert(sizeof(AvInitData) == 120, "AvInitData: tamano inesperado");
static_assert(sizeof(AvPostInit) == 132, "AvPostInit: tamano inesperado");
static_assert(sizeof(AvStreamInfo) == 40, "AvStreamInfo: tamano inesperado");
static_assert(sizeof(AvFrameInfo) == 40, "AvFrameInfo: tamano inesperado");
static_assert(sizeof(AvFrameInfoEx) == 104, "AvFrameInfoEx: tamano inesperado");

}  // namespace

extern "C" {
void* sceAvPlayerInit(AvInitData* data);
int32_t sceAvPlayerPostInit(void* h, AvPostInit* data);
int32_t sceAvPlayerAddSource(void* h, const char* name);
int32_t sceAvPlayerStreamCount(void* h);
int32_t sceAvPlayerGetStreamInfo(void* h, uint32_t id, AvStreamInfo* info);
int32_t sceAvPlayerStart(void* h);
int32_t sceAvPlayerStop(void* h);
int32_t sceAvPlayerPause(void* h);
int32_t sceAvPlayerResume(void* h);
bool sceAvPlayerIsActive(void* h);
uint64_t sceAvPlayerCurrentTime(void* h);
int32_t sceAvPlayerJumpToTime(void* h, uint64_t ms);
bool sceAvPlayerGetAudioData(void* h, AvFrameInfo* info);
bool sceAvPlayerGetVideoDataEx(void* h, AvFrameInfoEx* info);
int32_t sceAvPlayerClose(void* h);
}

namespace {

struct Player {
    std::mutex m;  // protege los textos y campos compuestos
    std::atomic<int> state{VP_CLOSED};
    std::string title, error, stage;
    void* av = nullptr;
    VSource* src = nullptr;
    int audio = -1;
    std::thread openTh, audioTh;
    std::atomic<bool> cancel{false}, audioStop{false}, userPaused{false}, flushAudio{false}, buffering{false};
    std::atomic<bool> haveVideo{false}, haveAudio{false};
    std::atomic<int> volume{80};
    uint64_t durMs = 0;
    std::atomic<uint64_t> posMs{0};
    int vw = 0, vh = 0;
    // fotograma actual (solo hilo principal)
    std::vector<uint32_t> argb;
    int argbW = 0, argbH = 0;
    unsigned long frames = 0;
    uint32_t fpsT0 = 0, fpsN = 0, startTick = 0, lastActiveCheck = 0, lastFrameTick = 0;
    float fps = 0, speed = 0, convMs = 0;
    // sincronizacion por reloj de pared (solo video sin audio, ver vsync.h) y fotograma retenido
    VSync sync;
    std::vector<uint32_t> pend;
    int pendW = 0, pendH = 0;
    uint64_t pendTs = 0, fpsTs0 = 0, convUsAcc = 0;
    unsigned long dropped = 0, convN = 0;
    int inactiveCount = 0;
    bool dumped = false;
};
Player P;
int g_vdec = 0;
int g_volDefault = 80;

void setStage(const std::string& s) {
    std::lock_guard<std::mutex> lk(P.m);
    P.stage = s;
}
void setError(const std::string& e) {
    psb_log("[vp] ERROR: %s", e.c_str());
    {
        std::lock_guard<std::mutex> lk(P.m);
        P.error = e;
        P.stage = e;
    }
    P.state = VP_ERROR;
}

// ---------------------------------------------------------------- memoria para AvPlayer
void* alignedAlloc(size_t align, size_t size) {
    if (align < 16) align = 16;
    void* raw = malloc(size + align + sizeof(void*));
    if (!raw) return nullptr;
    const uintptr_t a = (reinterpret_cast<uintptr_t>(raw) + sizeof(void*) + align - 1) & ~(static_cast<uintptr_t>(align) - 1);
    reinterpret_cast<void**>(a)[-1] = raw;
    return reinterpret_cast<void*>(a);
}
void alignedFree(void* p) {
    if (p) free(reinterpret_cast<void**>(p)[-1]);
}
void* avAlloc(void*, uint32_t align, uint32_t size) { return alignedAlloc(align, size); }
void avFree(void*, void* p) { alignedFree(p); }

struct TexAlloc { void* addr; off_t phys; size_t len; bool direct; };
std::mutex g_texMtx;
std::vector<TexAlloc> g_tex;

// Memoria para los fotogramas decodificados: el decodificador del sistema escribe aqui, asi que se pide memoria "directa" de la
// consola (como hacen los juegos); si falla se usa malloc. Se registra lo que se pide para diagnosticar.
void* avAllocTexture(void*, uint32_t align, uint32_t size) {
    const size_t al = std::max<size_t>(align, 0x4000);
    const size_t len = (static_cast<size_t>(size) + 0x3FFF) & ~static_cast<size_t>(0x3FFF);
    off_t phys = 0;
    int rc = sceKernelAllocateDirectMemory(0, static_cast<off_t>(sceKernelGetDirectMemorySize()), len, al, ORBIS_KERNEL_WB_ONION, &phys);
    if (rc == 0) {
        void* addr = nullptr;
        rc = sceKernelMapDirectMemory(&addr, len, ORBIS_KERNEL_PROT_CPU_RW | ORBIS_KERNEL_PROT_GPU_RW, 0, phys, al);
        if (rc == 0 && addr) {
            psb_log("[vp] textura: pedido align=0x%X size=%u -> memoria directa %p (%zu bytes)", align, size, addr, len);
            std::lock_guard<std::mutex> lk(g_texMtx);
            g_tex.push_back({addr, phys, len, true});
            return addr;
        }
        psb_log("[vp] textura: sceKernelMapDirectMemory fallo 0x%08X", static_cast<unsigned>(rc));
        sceKernelReleaseDirectMemory(phys, len);
    } else {
        psb_log("[vp] textura: sceKernelAllocateDirectMemory fallo 0x%08X (len=%zu)", static_cast<unsigned>(rc), len);
    }
    void* p = alignedAlloc(al, size);
    psb_log("[vp] textura: pedido align=0x%X size=%u -> malloc %p", align, size, p);
    if (p) { std::lock_guard<std::mutex> lk(g_texMtx); g_tex.push_back({p, 0, 0, false}); }
    return p;
}
void avFreeTexture(void*, void* p) {
    std::lock_guard<std::mutex> lk(g_texMtx);
    for (size_t i = 0; i < g_tex.size(); i++) {
        if (g_tex[i].addr != p) continue;
        if (g_tex[i].direct) { sceKernelMunmap(p, g_tex[i].len); sceKernelReleaseDirectMemory(g_tex[i].phys, g_tex[i].len); }
        else alignedFree(p);
        g_tex.erase(g_tex.begin() + static_cast<long>(i));
        return;
    }
}

// Tras cerrar el AvPlayer no deberia quedar ninguna textura; si queda, se libera aqui (y se avisa en el log).
void releaseLeftoverTextures() {
    std::lock_guard<std::mutex> lk(g_texMtx);
    if (!g_tex.empty()) psb_log("[vp] AVISO: quedaban %zu texturas sin liberar tras cerrar el AvPlayer; se liberan ahora", g_tex.size());
    for (const TexAlloc& t : g_tex) {
        if (t.direct) { sceKernelMunmap(t.addr, t.len); sceKernelReleaseDirectMemory(t.phys, t.len); }
        else alignedFree(t.addr);
    }
    g_tex.clear();
}

// ---------------------------------------------------------------- lectura de datos (fileReplacement)
int32_t frOpen(void*, const char*) { return P.src ? 0 : -1; }
int32_t frClose(void*) { return 0; }
int32_t frRead(void*, uint8_t* buf, uint64_t pos, uint32_t len) {
    VSource* s = P.src;
    if (!s) return -1;
    const int n = s->read(buf, pos, len);
    static unsigned cnt = 0;
    if (cnt < 12 || n < 0) { cnt++; psb_log("[vp] read(pos=%llu, len=%u) = %d", static_cast<unsigned long long>(pos), len, n); }
    return n;
}
uint64_t frSize(void*) { return P.src ? P.src->size() : 0; }

void avEvent(void*, int32_t ev, int32_t src, void*) {
    psb_log("[vp] evento %d (fuente %d)", ev, src);
    if (ev == 5) P.buffering = true;      // buffering
    else if (ev == 3) P.buffering = false;  // play
}

// ---------------------------------------------------------------- audio
void audioMain() {
    const int LEN = 1024;  // muestras por llamada a sceAudioOutOutput (21 ms a 48 kHz)
    std::vector<int16_t> ring;
    size_t rp = 0;
    std::vector<int16_t> out(static_cast<size_t>(LEN) * 2);
    bool logged = false;
    while (!P.audioStop) {
        if (P.flushAudio.exchange(false)) { ring.clear(); rp = 0; }
        if (P.userPaused) { sceKernelUsleep(20 * 1000); continue; }
        bool got = false;
        if (ring.size() - rp < static_cast<size_t>(LEN) * 2 * 6) {  // no acumular mas de ~120 ms
            AvFrameInfo fi;
            memset(&fi, 0, sizeof(fi));
            if (sceAvPlayerGetAudioData(P.av, &fi) && fi.data && fi.details.audio.size > 0) {
                got = true;
                const int ch = std::max<int>(1, fi.details.audio.channels);
                const uint32_t rate = fi.details.audio.sampleRate ? fi.details.audio.sampleRate : 48000;
                const size_t frames = fi.details.audio.size / (2 * static_cast<size_t>(ch));
                const int16_t* s = reinterpret_cast<const int16_t*>(fi.data);
                if (!logged) { logged = true; psb_log("[vp] audio: %d canales, %u Hz, %u bytes/trozo", ch, rate, fi.details.audio.size); }
                if (rp > 65536) { ring.erase(ring.begin(), ring.begin() + static_cast<long>(rp)); rp = 0; }
                const size_t outFrames = rate == 48000 ? frames : static_cast<size_t>(static_cast<double>(frames) * 48000.0 / rate);
                for (size_t i = 0; i < outFrames; i++) {
                    size_t si = rate == 48000 ? i : std::min(frames - 1, static_cast<size_t>(static_cast<double>(i) * rate / 48000.0));
                    const int16_t l = s[si * ch];
                    const int16_t r = ch >= 2 ? s[si * ch + 1] : l;
                    ring.push_back(l);
                    ring.push_back(r);
                }
            }
        }
        if (ring.size() - rp >= static_cast<size_t>(LEN) * 2) {
            const int gain = P.volume * 256 / 100;
            for (size_t i = 0; i < static_cast<size_t>(LEN) * 2; i++) out[i] = static_cast<int16_t>((ring[rp + i] * gain) >> 8);
            rp += static_cast<size_t>(LEN) * 2;
            if (P.audio > 0) sceAudioOutOutput(P.audio, out.data());  // bloquea ~21 ms: marca el ritmo
            else sceKernelUsleep(21 * 1000);
        } else if (!got) {
            sceKernelUsleep(3 * 1000);
        }
    }
}

// ---------------------------------------------------------------- apertura (hilo propio)
void openMain(std::string source, std::string referer) {
    psb_log("[vp] abriendo: %s", source.c_str());
    setStage("Preparando la fuente...");
    const bool isHttp = source.compare(0, 7, "http://") == 0 || source.compare(0, 8, "https://") == 0;
    P.src = isHttp ? vsource_http(source, referer) : vsource_file(source);
    std::string err;
    setStage(isHttp ? "Conectando con el servidor..." : "Abriendo el archivo...");
    if (!P.src->open(err)) { setError(err); return; }
    if (P.cancel) return;

    setStage("Cargando el reproductor del sistema...");
    int rc = 0;
    static bool s_sysInit = false;  // modulo del sistema, UserService y AudioOut se inicializan UNA sola vez por proceso
    if (!s_sysInit) {
        s_sysInit = true;
        rc = sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER);
        psb_log("[vp] sceSysmoduleLoadModule(AV_PLAYER) = 0x%08X", static_cast<unsigned>(rc));
        rc = sceUserServiceInitialize(nullptr);
        psb_log("[vp] sceUserServiceInitialize = 0x%08X (ya inicializado es normal)", static_cast<unsigned>(rc));
        rc = sceAudioOutInit();
        psb_log("[vp] sceAudioOutInit = 0x%08X", static_cast<unsigned>(rc));
    }
    P.audio = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, 1024, 48000, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    psb_log("[vp] sceAudioOutOpen = %d", P.audio);
    if (P.cancel) return;

    AvInitData init;
    memset(&init, 0, sizeof(init));
    init.memory = {nullptr, avAlloc, avFree, avAllocTexture, avFreeTexture};
    init.file = {nullptr, frOpen, frClose, frRead, frSize};
    init.event = {nullptr, avEvent};
    init.debugLevel = 0;
    init.basePriority = 700;
    init.numOutputVideoFrameBuffers = 4;
    init.autoStart = true;
    init.defaultLanguage = "eng";
    setStage("Iniciando AvPlayer...");
    P.av = sceAvPlayerInit(&init);
    psb_log("[vp] sceAvPlayerInit = %p", P.av);
    if (!P.av) { setError("sceAvPlayerInit devolvio NULL: el reproductor del sistema no arranco en este homebrew."); return; }
    if (g_vdec != 0) {
        AvPostInit pi;
        memset(&pi, 0, sizeof(pi));
        pi.video.type = static_cast<uint32_t>(g_vdec);
        int32_t aff = 0x3F, prio = 700;  // avc software: nucleos 0-5, prioridad de hilo
        memcpy(pi.video.params, &aff, 4);
        memcpy(pi.video.params + 4, &prio, 4);
        pi.video.params[8] = 4;  // profundidad de la cola de decodificacion
        rc = sceAvPlayerPostInit(P.av, &pi);
        psb_log("[vp] sceAvPlayerPostInit(decodificador=%d) = 0x%08X", g_vdec, static_cast<unsigned>(rc));
    }
    if (P.cancel) return;

    setStage("Leyendo la cabecera del video...");
    rc = sceAvPlayerAddSource(P.av, "psbrowser-video.mp4");  // el nombre solo sirve de etiqueta: los bytes salen de frRead()
    psb_log("[vp] sceAvPlayerAddSource = 0x%08X", static_cast<unsigned>(rc));
    if (rc < 0) { setError("AddSource fallo (0x" + [&] { char b[16]; snprintf(b, sizeof(b), "%08X", static_cast<unsigned>(rc)); return std::string(b); }() + "): formato no reconocido."); return; }

    int count = 0;
    for (int i = 0; i < 400 && !P.cancel; i++) {  // hasta 40 s esperando a que el demultiplexor lea la cabecera (moov)
        count = sceAvPlayerStreamCount(P.av);
        if (count > 0) break;
        if (P.state == VP_ERROR) return;
        sceKernelUsleep(100 * 1000);
    }
    psb_log("[vp] sceAvPlayerStreamCount = %d", count);
    if (P.cancel) return;
    if (count <= 0) { setError("No se encontraron pistas de video/audio (formato no compatible con el reproductor de la consola, o la cabecera tardo demasiado)."); return; }

    for (int i = 0; i < count; i++) {
        AvStreamInfo si;
        memset(&si, 0, sizeof(si));
        rc = sceAvPlayerGetStreamInfo(P.av, static_cast<uint32_t>(i), &si);
        if (rc < 0) { psb_log("[vp] GetStreamInfo(%d) = 0x%08X", i, static_cast<unsigned>(rc)); continue; }
        if (si.type == 0) {
            P.haveVideo = true;
            P.vw = static_cast<int>(si.details.video.width);
            P.vh = static_cast<int>(si.details.video.height);
            psb_log("[vp] pista %d: VIDEO %ux%u, duracion %llu ms", i, si.details.video.width, si.details.video.height, static_cast<unsigned long long>(si.duration));
        } else if (si.type == 1) {
            P.haveAudio = true;
            psb_log("[vp] pista %d: AUDIO %u canales, %u Hz, duracion %llu ms", i, si.details.audio.channels, si.details.audio.sampleRate, static_cast<unsigned long long>(si.duration));
        } else {
            psb_log("[vp] pista %d: tipo %u (ignorada)", i, si.type);
        }
        std::lock_guard<std::mutex> lk(P.m);
        P.durMs = std::max<uint64_t>(P.durMs, si.duration);
    }
    P.startTick = SDL_GetTicksCompat();
    P.state = VP_PLAYING;
    setStage("");
    if (P.haveAudio) P.audioTh = std::thread(audioMain);
    psb_log("[vp] reproduciendo (video=%d audio=%d)", P.haveVideo.load() ? 1 : 0, P.haveAudio.load() ? 1 : 0);
}

// ---------------------------------------------------------------- NV12 -> ARGB
uint8_t g_clip[1024];
bool g_clipInit = false;
void initClip() {
    if (g_clipInit) return;
    for (int i = 0; i < 1024; i++) g_clip[i] = static_cast<uint8_t>(std::max(0, std::min(255, i - 256)));
    g_clipInit = true;
}
void nv12ToArgb(const uint8_t* y, int ys, const uint8_t* uv, int uvs, int w, int h, uint32_t* out, bool hd) {
    initClip();
    const int rv = hd ? 459 : 409, gu = hd ? 55 : 100, gv = hd ? 136 : 208, bu = hd ? 541 : 516;  // BT.709 (HD) / BT.601 (SD), rango limitado
    for (int j = 0; j < h; j += 2) {
        const uint8_t* y0 = y + static_cast<size_t>(j) * ys;
        const uint8_t* y1 = (j + 1 < h) ? y0 + ys : y0;
        const uint8_t* c = uv + static_cast<size_t>(j / 2) * uvs;
        uint32_t* o0 = out + static_cast<size_t>(j) * w;
        uint32_t* o1 = (j + 1 < h) ? o0 + w : o0;
        for (int i = 0; i < w; i += 2) {
            const int u = c[i] - 128, v = c[i + 1] - 128;
            const int r = rv * v, g = gu * u + gv * v, b = bu * u;
            const int a0 = 298 * (y0[i] - 16), a1 = 298 * (y0[i + 1 < w ? i + 1 : i] - 16);
            const int b0 = 298 * (y1[i] - 16), b1 = 298 * (y1[i + 1 < w ? i + 1 : i] - 16);
            o0[i] = 0xFF000000u | (g_clip[((a0 + r) >> 8) + 256] << 16) | (g_clip[((a0 - g) >> 8) + 256] << 8) | g_clip[((a0 + b) >> 8) + 256];
            if (i + 1 < w) o0[i + 1] = 0xFF000000u | (g_clip[((a1 + r) >> 8) + 256] << 16) | (g_clip[((a1 - g) >> 8) + 256] << 8) | g_clip[((a1 + b) >> 8) + 256];
            if (j + 1 < h) {
                o1[i] = 0xFF000000u | (g_clip[((b0 + r) >> 8) + 256] << 16) | (g_clip[((b0 - g) >> 8) + 256] << 8) | g_clip[((b0 + b) >> 8) + 256];
                if (i + 1 < w) o1[i + 1] = 0xFF000000u | (g_clip[((b1 + r) >> 8) + 256] << 16) | (g_clip[((b1 - g) >> 8) + 256] << 8) | g_clip[((b1 + b) >> 8) + 256];
            }
        }
    }
}

void dumpFirstFrame(const AvFrameInfoEx& fi, int w, int h, int pitch) {
    const AvVideoEx& v = fi.details.video;
    psb_log("[vp] PRIMER FOTOGRAMA: data=%p ts=%llu ms width=%u height=%u pitch=%u aspecto=%.3f fps=%u crop(L%u R%u T%u B%u) bits(%u/%u) fullRange=%d",
            fi.data, static_cast<unsigned long long>(fi.timestamp), v.width, v.height, v.pitch, v.aspect, v.framerate, v.cropLeft, v.cropRight,
            v.cropTop, v.cropBottom, v.lumaBits, v.chromaBits, v.fullRange ? 1 : 0);
    const uint8_t* d = static_cast<const uint8_t*>(fi.data);
    char hex[200];
    int n = 0;
    for (int i = 0; i < 24; i++) n += snprintf(hex + n, sizeof(hex) - static_cast<size_t>(n), "%02X ", d[i]);
    psb_log("[vp] primeros bytes (Y): %s", hex);
    n = 0;
    const uint8_t* uvp = d + static_cast<size_t>(pitch) * h;
    for (int i = 0; i < 24; i++) n += snprintf(hex + n, sizeof(hex) - static_cast<size_t>(n), "%02X ", uvp[i]);
    psb_log("[vp] primeros bytes (UV supuesto en pitch*alto): %s", hex);
    FILE* f = fopen("/data/psbrowser/video_frame0.bin", "wb");  // volcado crudo para analizar el formato si la imagen sale mal
    if (f) {
        const size_t bytes = std::min<size_t>(static_cast<size_t>(pitch) * h * 3 / 2 + 65536, 8u << 20);
        fwrite(d, 1, bytes, f);
        fclose(f);
        psb_log("[vp] volcado: /data/psbrowser/video_frame0.bin (%zu bytes, NV12 supuesto: pitch=%d alto=%d)", bytes, pitch, h);
    }
}

}  // namespace

// ======================================================================================================================
// API publica
// ======================================================================================================================
void vp_set_decoder(int type) { g_vdec = (type == 2 || type == 3) ? type : 0; }
void vp_set_volume_default(int pct) { g_volDefault = std::max(0, std::min(100, pct)); }

bool vp_open(const std::string& source, const std::string& title, const std::string& referer) {
    if (P.state != VP_CLOSED) return false;
    {
        std::lock_guard<std::mutex> lk(P.m);
        P.title = title;
        P.error.clear();
        P.stage = "Iniciando...";
        P.durMs = 0;
    }
    P.cancel = false; P.audioStop = false; P.userPaused = false; P.buffering = false; P.flushAudio = false;
    P.haveVideo = false; P.haveAudio = false;
    P.posMs = 0; P.vw = P.vh = 0;
    P.frames = 0; P.fps = 0; P.fpsN = 0; P.fpsT0 = 0; P.inactiveCount = 0; P.dumped = false; P.lastFrameTick = 0;
    P.argbW = P.argbH = 0;
    P.sync.reset(); P.pend.clear(); P.pendW = P.pendH = 0; P.pendTs = 0; P.fpsTs0 = 0; P.convUsAcc = 0; P.convN = 0; P.dropped = 0; P.speed = 0; P.convMs = 0;
    P.volume = g_volDefault;
    P.av = nullptr; P.audio = -1; P.src = nullptr;
    P.state = VP_OPENING;
    P.openTh = std::thread(openMain, source, referer);
    return true;
}

void vp_close() {
    if (P.state == VP_CLOSED) return;
    psb_log("[vp] cerrando");
    P.cancel = true;
    if (P.src) P.src->close();  // desbloquea cualquier lectura de red pendiente
    if (P.openTh.joinable()) P.openTh.join();
    P.audioStop = true;
    if (P.audioTh.joinable()) P.audioTh.join();
    if (P.av) {
        const int rs = sceAvPlayerStop(P.av);
        sceKernelUsleep(60 * 1000);  // deja que terminen los hilos internos del AvPlayer antes de cerrarlo
        const int rc = sceAvPlayerClose(P.av);
        psb_log("[vp] sceAvPlayerStop = 0x%08X, sceAvPlayerClose = 0x%08X", static_cast<unsigned>(rs), static_cast<unsigned>(rc));
        P.av = nullptr;
    }
    if (P.audio > 0) {
        const int rc = sceAudioOutClose(P.audio);
        psb_log("[vp] sceAudioOutClose = 0x%08X", static_cast<unsigned>(rc));
        P.audio = -1;
    }
    releaseLeftoverTextures();
    delete P.src;
    P.src = nullptr;
    P.argb.clear();
    P.argb.shrink_to_fit();
    P.pend.clear();
    P.pend.shrink_to_fit();
    P.sync.reset();
    psb_log("[vp] cerrado limpio");
    P.state = VP_CLOSED;
}

void vp_shutdown() { vp_close(); }

bool vp_active() { return P.state != VP_CLOSED; }

VpInfo vp_info() {
    VpInfo r;
    r.state = static_cast<VpState>(P.state.load());
    {
        std::lock_guard<std::mutex> lk(P.m);
        r.title = P.title; r.error = P.error; r.stage = P.stage; r.durMs = P.durMs;
    }
    r.posMs = P.posMs;
    r.vw = P.vw; r.vh = P.vh;
    r.hasVideo = P.haveVideo; r.hasAudio = P.haveAudio;
    r.volume = P.volume;
    r.frames = P.frames; r.fps = P.fps; r.dropped = P.dropped; r.speed = P.speed; r.convMs = P.convMs; r.clockSync = !P.haveAudio;
    if (P.src) { r.bufPct = P.src->bufferedPct(); r.netWait = P.src->waiting(); }
    if (r.state == VP_PLAYING && P.buffering) r.state = VP_BUFFERING;
    if (r.state == VP_PLAYING && P.userPaused) r.state = VP_PAUSED;
    r.vdec = g_vdec;
    if ((r.state == VP_PLAYING || r.state == VP_BUFFERING) && r.hasVideo && P.frames == 0 && P.startTick)
        r.noFrameSecs = static_cast<int>((SDL_GetTicksCompat() - P.startTick) / 1000);
    return r;
}

void vp_pause_toggle() {
    const int s = P.state;
    if (s != VP_PLAYING && s != VP_PAUSED) return;
    if (!P.userPaused) { P.userPaused = true; sceAvPlayerPause(P.av); psb_log("[vp] pausa"); }
    else { sceAvPlayerResume(P.av); P.userPaused = false; P.sync.reset(); psb_log("[vp] continuar"); }
}

void vp_seek_abs(uint64_t ms) {
    if (!P.av || (P.state != VP_PLAYING && P.state != VP_PAUSED)) return;
    uint64_t dur;
    { std::lock_guard<std::mutex> lk(P.m); dur = P.durMs; }
    if (dur > 0 && ms > dur) ms = dur > 1000 ? dur - 1000 : 0;
    const int rc = sceAvPlayerJumpToTime(P.av, ms);
    psb_log("[vp] JumpToTime(%llu ms) = 0x%08X", static_cast<unsigned long long>(ms), static_cast<unsigned>(rc));
    P.posMs = ms;
    P.flushAudio = true;
    P.sync.reset();  // tras un salto hay que volver a anclar el reloj de pared al primer fotograma nuevo
}

void vp_seek_rel(int64_t deltaMs) {
    int64_t t = static_cast<int64_t>(P.posMs) + deltaMs;
    if (t < 0) t = 0;
    vp_seek_abs(static_cast<uint64_t>(t));
}

void vp_volume_rel(int d) { P.volume = std::max(0, std::min(100, P.volume.load() + d)); }

// Contabilidad de un fotograma mostrado (hilo principal): contador, fps, velocidad real y, cada 2 s, una linea de diagnostico.
static bool vpCountFrame(uint32_t now, uint64_t ts) {
    P.frames++;
    P.lastFrameTick = now;
    if (P.fpsT0 == 0) { P.fpsT0 = now; P.fpsTs0 = ts; P.fpsN = 0; }
    P.fpsN++;
    if (now - P.fpsT0 >= 2000) {
        const uint32_t wallMs = now - P.fpsT0;
        const uint64_t contMs = ts >= P.fpsTs0 ? ts - P.fpsTs0 : 0;
        P.fps = P.fpsN * 1000.0f / static_cast<float>(wallMs);
        P.speed = static_cast<float>(contMs) / static_cast<float>(wallMs);
        P.convMs = P.convN ? static_cast<float>(P.convUsAcc) / 1000.0f / static_cast<float>(P.convN) : 0.0f;
        if (P.frames < 400 || (P.frames % 300) == 0)
            psb_log("[vp] %.1f fps mostrados | velocidad %.2fx (%llu ms de contenido en %u ms de pared) | descartados %lu | conversion %.1f ms | total %lu pos=%llu ms | reloj=%s",
                    P.fps, P.speed, static_cast<unsigned long long>(contMs), wallMs, P.dropped, P.convMs, P.frames, static_cast<unsigned long long>(P.posMs.load()),
                    P.haveAudio ? "audio" : "pared");
        P.fpsT0 = now; P.fpsTs0 = ts; P.fpsN = 0; P.convUsAcc = 0; P.convN = 0;
    }
    return true;
}

bool vp_update() {
    const int st = P.state;
    if (st != VP_PLAYING && st != VP_PAUSED) return false;
    const uint32_t now = SDL_GetTicksCompat();
    P.posMs = sceAvPlayerCurrentTime(P.av);
    if (now - P.lastActiveCheck > 250) {  // fin de la reproduccion
        P.lastActiveCheck = now;
        if (!P.userPaused && now - P.startTick > 3000) {
            if (!sceAvPlayerIsActive(P.av)) { if (++P.inactiveCount >= 3) { psb_log("[vp] fin de la reproduccion"); P.state = VP_ENDED; return false; } }
            else P.inactiveCount = 0;
        }
    }
    if (!P.haveVideo) return false;
    // Sin audio manda el reloj de pared (VSync): se descartan los fotogramas atrasados en vez de ir en camara lenta. Con audio manda
    // el reloj de audio del AvPlayer (sin tocar: un fotograma por vuelta, como en v0.8.0).
    const bool useClock = !P.haveAudio;
    uint64_t ts = 0;
    int dropped = 0;
    bool hold = false;
    AvFrameInfoEx fi;
    memset(&fi, 0, sizeof(fi));
    if (useClock && P.sync.pending()) {  // hay un fotograma ya convertido esperando su turno
        if (!P.sync.pendingDue(now)) return false;
        P.sync.clearPending();
        P.argb.swap(P.pend);
        std::swap(P.argbW, P.pendW);
        std::swap(P.argbH, P.pendH);
        ts = P.pendTs;
        P.vw = P.argbW; P.vh = P.argbH;
        return vpCountFrame(now, ts);
    }
    auto pull = [&](uint64_t& t) {
        AvFrameInfoEx tmp;
        memset(&tmp, 0, sizeof(tmp));
        if (!sceAvPlayerGetVideoDataEx(P.av, &tmp) || !tmp.data) return false;
        fi = tmp;  // solo se sobrescribe si hubo fotograma: la ultima peticion fallida no debe borrar el elegido
        t = tmp.timestamp;
        return true;
    };
    if (useClock) {
        const VSync::Result r = P.sync.pump(now, pull);
        if (!r.have) return false;
        ts = r.ts; dropped = r.dropped; hold = r.hold;
    } else if (!pull(ts)) {
        return false;
    }
    const AvVideoEx& v = fi.details.video;
    int w = static_cast<int>(v.width), h = static_cast<int>(v.height);
    if (w < 16 || h < 16 || w > 4096 || h > 4096) {
        static int bad = 0;
        if (bad++ < 5) psb_log("[vp] fotograma con dimensiones raras: %dx%d (pitch=%u)", w, h, v.pitch);
        P.sync.clearPending();
        return false;
    }
    const int pitch = v.pitch >= v.width ? static_cast<int>(v.pitch) : w;
    if (!P.dumped) { P.dumped = true; dumpFirstFrame(fi, w, h, pitch); }
    std::vector<uint32_t>& dst = hold ? P.pend : P.argb;
    int& dw = hold ? P.pendW : P.argbW;
    int& dh = hold ? P.pendH : P.argbH;
    if (static_cast<int>(dst.size()) < w * h || dw != w || dh != h) { dst.assign(static_cast<size_t>(w) * h, 0xFF000000u); dw = w; dh = h; }
    const uint8_t* y = static_cast<const uint8_t*>(fi.data);
    const uint64_t c0 = sceKernelGetProcessTime();
    nv12ToArgb(y, pitch, y + static_cast<size_t>(pitch) * h, pitch, w, h, dst.data(), h >= 720);
    P.convUsAcc += sceKernelGetProcessTime() - c0;
    P.convN++;
    P.dropped += static_cast<unsigned long>(dropped);
    if (hold) { P.pendTs = ts; return false; }  // va adelantado: se muestra cuando le toque (pendingDue)
    P.vw = w; P.vh = h;
    return vpCountFrame(now, ts);
}

const uint32_t* vp_frame(int& w, int& h, unsigned long& serial) {
    w = P.argbW; h = P.argbH; serial = P.frames;
    if (P.argb.empty() || w <= 0 || h <= 0 || static_cast<int>(P.argb.size()) < w * h) return nullptr;
    return P.argb.data();
}

void vp_draw_video(Canvas& cv, int x, int y, int w, int h, bool half) {
    if (P.argb.empty() || P.argbW <= 0 || w <= 0 || h <= 0) return;
    const int sw = P.argbW, sh = P.argbH;
    std::vector<int> xmap(static_cast<size_t>(w));
    for (int i = 0; i < w; i++) xmap[static_cast<size_t>(i)] = std::min(sw - 1, static_cast<int>(static_cast<int64_t>(i) * sw / w));
    const int step = half ? 2 : 1;
    for (int j = 0; j < h; j += step) {
        const int dy = y + j;
        if (dy < cv.cy0 || dy >= cv.cy1) continue;
        const uint32_t* srow = P.argb.data() + static_cast<size_t>(std::min(sh - 1, static_cast<int>(static_cast<int64_t>(j) * sh / h))) * sw;
        uint32_t* drow = &cv.px[static_cast<size_t>(dy) * cv.w];
        const int x0 = std::max(0, cv.cx0 - x), x1 = std::min(w, cv.cx1 - x);
        for (int i = x0; i < x1; i += step) {
            const uint32_t c = srow[xmap[static_cast<size_t>(i)]];
            drow[x + i] = c;
            if (half && i + 1 < x1) drow[x + i + 1] = c;
        }
        if (half && j + 1 < h && dy + 1 < cv.cy1) memcpy(&cv.px[static_cast<size_t>(dy + 1) * cv.w + static_cast<size_t>(x + x0)], drow + x + x0, static_cast<size_t>(x1 - x0) * 4);
    }
}
