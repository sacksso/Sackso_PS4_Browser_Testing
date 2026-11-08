// Prueba en el PC del motor de descargas (app/dlengine.cpp) con un servidor simulado.
#include "../../app/dlengine.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/stat.h>
#include <ctime>
extern "C" int sceKernelUsleep(unsigned us);
extern "C" int sceKernelUsleep(unsigned us) { struct timespec t{0, static_cast<long>(us) * 1000}; nanosleep(&t, nullptr); return 0; }
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static inline unsigned char pat(long long i) { return static_cast<unsigned char>((i * 131 + (i >> 8) + 7) & 255); }

struct Server {
    long long size = 40LL << 20;
    bool range = true;
    int status = 206;          // 404 / 403: error
    bool html = false;
    int dropEvery = 0;         // cada N peticiones se corta tras dropBytes
    long long dropBytes = 700000;
    int slowUs = 0;            // pausa por trozo de 64 KB
    std::atomic<int> reqs{0};
    std::atomic<long long> served{0};
    std::atomic<int> maxParallel{0}, parallel{0};
};
static DlFetchFn makeFetch(Server& S) {
    return [&S](const std::string& url, long long start, long long end, DlChunkSink& sink, DlFetchResult& r) {
        const int n = ++S.reqs;
        const int par = ++S.parallel;
        int mp = S.maxParallel.load();
        while (par > mp && !S.maxParallel.compare_exchange_weak(mp, par)) {}
        struct Dec { Server& s; ~Dec() { --s.parallel; } } dec{S};
        r = DlFetchResult();
        r.finalUrl = url;
        if (S.status != 206 && S.status != 200) { r.status = S.status; sink.begin(r); r.stage = "cancelado"; return false; }
        const bool useRange = S.range;
        if (!useRange) { start = 0; end = S.size - 1; r.status = 200; r.contentLength = S.size; }
        else { if (end < 0 || end >= S.size) end = S.size - 1; r.status = 206; r.rangeTotal = S.size; r.contentLength = end - start + 1; }
        r.contentType = S.html ? "text/html" : "application/octet-stream";
        if (!sink.begin(r)) { r.stage = "cancelado"; return false; }
        long long sent = 0;
        const bool drop = S.dropEvery > 0 && n % S.dropEvery == 0;
        std::string buf;
        for (long long p = start; p <= end;) {
            if (drop && sent >= S.dropBytes) { r.stage = "readData"; r.sceError = -1; return false; }
            const long long k = std::min<long long>(65536, end - p + 1);
            buf.resize(static_cast<size_t>(k));
            for (long long i = 0; i < k; i++) buf[static_cast<size_t>(i)] = static_cast<char>(pat(p + i));
            if (!sink.write(buf.data(), buf.size())) { r.stage = "cancelado"; return false; }
            S.served += k; sent += k; p += k;
            if (S.slowUs) std::this_thread::sleep_for(std::chrono::microseconds(S.slowUs));
        }
        return true;
    };
}
static bool verify(const std::string& path, long long size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string b(1 << 20, 0);
    long long off = 0;
    size_t n;
    bool ok = true;
    while ((n = fread(&b[0], 1, b.size(), f)) > 0) {
        for (size_t i = 0; i < n; i++) if (static_cast<unsigned char>(b[i]) != pat(off + static_cast<long long>(i))) { ok = false; break; }
        if (!ok) break;
        off += static_cast<long long>(n);
    }
    fclose(f);
    return ok && off == size;
}
static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }
static DlParams base(Server& S, const std::string& dir, int conns) {
    DlParams p;
    p.url = "http://t.test/f.bin"; p.dir = dir; p.conns = conns; p.chunk = 2 << 20; p.backoffMs = 20; p.retries = 6;
    p.fetch = makeFetch(S);
    p.pickPath = [dir](const DlFetchResult&) { return dir + "/f.bin"; };
    p.accept = [](const DlFetchResult& r) { return r.contentType.compare(0, 4, "text") != 0; };
    p.log = [](const std::string& l) { printf("   %s\n", l.c_str()); };
    return p;
}
static DlOutcome runWait(DlEngine& e, const DlParams& p) {
    e.start(p);
    e.join();
    return e.outcome();
}
int main() {
    char tmpl[] = "/tmp/dltestXXXXXX";
    const std::string root = mkdtemp(tmpl);
    int tcount = 0;
    auto fresh = [&]() { char t[64]; snprintf(t, sizeof(t), "%s/t%d", root.c_str(), ++tcount); mkdir(t, 0777); return std::string(t); };

    for (int conns : {1, 3, 6}) {
        printf("[1] descarga limpia, %d conexion(es)\n", conns);
        Server S; std::string d = fresh();
        DlEngine e; DlOutcome o = runWait(e, base(S, d, conns));
        CHECK(o.result == DlResult::OK); CHECK(o.chunked); CHECK(verify(d + "/f.bin", S.size));
        CHECK(!exists(d + "/f.bin.part")); CHECK(!exists(d + "/f.bin.part.meta"));
        if (conns > 1) CHECK(S.maxParallel.load() > 1);
        CHECK(S.maxParallel.load() <= conns);
    }
    {
        printf("[2] cortes de red (cada 3a peticion), 3 conexiones\n");
        Server S; S.dropEvery = 3; std::string d = fresh();
        DlEngine e; DlOutcome o = runWait(e, base(S, d, 3));
        CHECK(o.result == DlResult::OK); CHECK(verify(d + "/f.bin", S.size)); CHECK(e.progress().retryCount > 0);
    }
    {
        printf("[3] servidor sin Range (200), 4 conexiones\n");
        Server S; S.range = false; S.size = 12LL << 20; std::string d = fresh();
        DlEngine e; DlOutcome o = runWait(e, base(S, d, 4));
        CHECK(o.result == DlResult::OK); CHECK(!o.chunked); CHECK(verify(d + "/f.bin", S.size)); CHECK(S.reqs.load() == 1);
    }
    {
        printf("[4] cancelar y reanudar\n");
        Server S; S.slowUs = 300; std::string d = fresh();
        DlEngine a; DlParams p = base(S, d, 2);
        a.start(p);
        while (a.progress().done < (S.size * 35 / 100)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        a.cancel(); a.join();
        DlOutcome o1 = a.outcome();
        CHECK(o1.result == DlResult::CANCELLED); CHECK(o1.kept); CHECK(exists(d + "/f.bin.part")); CHECK(exists(d + "/f.bin.part.meta"));
        CHECK(!exists(d + "/f.bin"));
        auto parts = dl_list_partials(d);
        CHECK(parts.size() == 1 && parts[0].url == p.url && parts[0].done > 0 && parts[0].done < S.size);
        const long long servedBefore = S.served.load();
        S.slowUs = 0;
        DlEngine b; DlOutcome o2 = runWait(b, base(S, d, 2));
        CHECK(o2.result == DlResult::OK); CHECK(verify(d + "/f.bin", S.size));
        CHECK(b.progress().resumed && b.progress().resumedFrom > 0);
        CHECK(S.served.load() - servedBefore < S.size);   // no volvio a bajar todo
        CHECK(!exists(d + "/f.bin.part.meta"));
    }
    {
        printf("[5] servidor sin Range al reanudar: empieza de cero\n");
        Server S; S.slowUs = 300; S.size = 20LL << 20; std::string d = fresh();
        DlEngine a; a.start(base(S, d, 2));
        while (a.progress().done < (S.size * 40 / 100)) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        a.cancel(); a.join();
        S.range = false; S.slowUs = 0;
        DlEngine b; DlOutcome o = runWait(b, base(S, d, 2));
        CHECK(o.result == DlResult::OK); CHECK(verify(d + "/f.bin", S.size));
    }
    {
        printf("[6] errores: 404 y pagina HTML\n");
        Server S; S.status = 404; std::string d = fresh();
        DlEngine e; DlOutcome o = runWait(e, base(S, d, 2));
        CHECK(o.result == DlResult::BAD_STATUS); CHECK(o.status == 404); CHECK(!exists(d + "/f.bin")); CHECK(!exists(d + "/f.bin.part"));
        Server H; H.html = true; std::string d2 = fresh();
        DlEngine e2; DlOutcome o2 = runWait(e2, base(H, d2, 2));
        CHECK(o2.result == DlResult::HTML_INSTEAD); CHECK(!exists(d2 + "/f.bin.part"));
    }
    {
        printf("[7] reintentos agotados (todas las peticiones se cortan)\n");
        Server S; S.dropEvery = 1; S.dropBytes = 0; std::string d = fresh();
        DlParams p = base(S, d, 2); p.retries = 3;
        DlEngine e; DlOutcome o = runWait(e, p);
        CHECK(o.result == DlResult::NET_ERROR);
        CHECK(!exists(d + "/f.bin"));
    }
    {
        printf("[8] medidor (discard), 3 conexiones, 300 ms\n");
        Server S; S.slowUs = 200; std::string d = fresh();
        DlParams p = base(S, d, 3); p.discard = true; p.benchMs = 300;
        DlEngine e; DlOutcome o = runWait(e, p);
        CHECK(o.result == DlResult::OK); CHECK(o.bytes > 0); CHECK(!exists(d + "/f.bin")); CHECK(!exists(d + "/f.bin.part"));
        printf("   medidor: %lld bytes, %d conexiones usadas\n", o.bytes, o.connsUsed);
    }
    {
        printf("[9] archivo pequeno (menor que un trozo), 4 conexiones\n");
        Server S; S.size = 300000; std::string d = fresh();
        DlEngine e; DlOutcome o = runWait(e, base(S, d, 4));
        CHECK(o.result == DlResult::OK); CHECK(verify(d + "/f.bin", S.size));
    }
    printf(fails ? "\n%d FALLOS\n" : "\nTODO OK\n", fails);
    std::string cmd = "rm -rf " + root; (void)!system(cmd.c_str());
    return fails ? 1 : 0;
}
