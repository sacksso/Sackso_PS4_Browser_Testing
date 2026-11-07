// Prueba en PC de vsource (cache de bloques + lectura anticipada) con un servidor de rangos simulado.
// g++ -std=c++17 -pthread -fsanitize=address,undefined -Itools/stubs -Iapp tools/tests/test_vsource.cpp app/vsource.cpp -o /tmp/test_vs
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <time.h>
#include "netclient.h"
#include "vsource.h"
extern "C" int sceKernelUsleep(unsigned us) { struct timespec t{0, static_cast<long>(us) * 1000}; nanosleep(&t, nullptr); return 0; }
void psb_log(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); printf("\n"); }
bool http_range(const std::string&, long long, long long, const std::string&, HttpResult&, int) { return false; }  // no se usa: se inyecta fetch

static unsigned char byteAt(long long p) { return static_cast<unsigned char>((p * 31 + 7 + (p >> 17)) & 255); }
struct Fake {
    long long total; std::atomic<int> calls{0}; long long failBlockStart = -1; bool noTotal = false; bool shortData = false; int delayUs = 0;
    bool operator()(long long start, long long len, std::string& data, long long& tot, std::string& err) {
        calls++;
        if (delayUs) sceKernelUsleep(delayUs);
        if (failBlockStart >= 0 && start == failBlockStart) { err = "HTTP 503"; return false; }
        long long n = std::min(len, total - start);
        if (shortData) n = n / 2;
        data.resize(static_cast<size_t>(n));
        for (long long i = 0; i < n; i++) data[i] = static_cast<char>(byteAt(start + i));
        tot = noTotal ? -1 : total;
        return true;
    }
};
static bool check(const uint8_t* b, long long pos, int n) { for (int i = 0; i < n; i++) if (b[i] != byteAt(pos + i)) return false; return true; }

int main() {
    std::mt19937 rng(7);
    {   // 1) lectura secuencial y saltos aleatorios (archivo de 37,3 MB: obliga a expulsar bloques), tamanos de lectura variados
        Fake f; f.total = 37LL * 1024 * 1024 + 300000;
        std::string err;
        VSource* s = vsource_http("http://x/v.mp4", "", [&](long long a, long long b, std::string& d, long long& t, std::string& e) { return f(a, b, d, t, e); });
        assert(s->open(err)); assert(s->size() == (uint64_t)f.total);
        std::vector<uint8_t> buf(3 * 1024 * 1024);
        long long pos = 0;
        for (int i = 0; i < 60; i++) {
            int len = 1 + rng() % 300000;
            int n = s->read(buf.data(), pos, len); assert(n == len); assert(check(buf.data(), pos, n)); pos += n;
        }
        for (int i = 0; i < 300; i++) {  // saltos
            long long p = (long long)(rng() % (uint64_t)f.total); int len = 1 + rng() % 2500000;
            int n = s->read(buf.data(), p, len); int want = (int)std::min<long long>(len, f.total - p);
            assert(n == want); assert(check(buf.data(), p, n));
        }
        int n = s->read(buf.data(), f.total - 10, 100); assert(n == 10 && check(buf.data(), f.total - 10, 10));
        assert(s->read(buf.data(), f.total, 100) == 0);
        printf("ok lectura (peticiones al servidor: %d)\n", f.calls.load());
        delete s;
    }
    {   // 2) sin Content-Range -> no permite streaming
        Fake f; f.total = 5 << 20; f.noTotal = true; std::string err;
        VSource* s = vsource_http("http://x/v.mp4", "", [&](long long a, long long b, std::string& d, long long& t, std::string& e) { return f(a, b, d, t, e); });
        assert(!s->open(err)); assert(err.find("Content-Range") != std::string::npos); delete s; printf("ok sin Content-Range: %s\n", err.c_str());
    }
    {   // 3) bloque que falla siempre -> la lectura devuelve error (no se cuelga) y los demas siguen leyendose
        Fake f; f.total = 8 << 20; f.failBlockStart = 3LL << 20; std::string err;
        VSource* s = vsource_http("http://x/v.mp4", "", [&](long long a, long long b, std::string& d, long long& t, std::string& e) { return f(a, b, d, t, e); });
        assert(s->open(err)); std::vector<uint8_t> buf(4096);
        assert(s->read(buf.data(), 2 << 20, 4096) == 4096 && check(buf.data(), 2 << 20, 4096));
        assert(s->read(buf.data(), (3LL << 20) + 5, 4096) < 0);
        assert(!s->lastError().empty());
        assert(s->read(buf.data(), 5 << 20, 4096) == 4096 && check(buf.data(), 5 << 20, 4096));
        delete s; printf("ok bloque fallido: %s\n", "(error devuelto, sin bloqueo)");
    }
    {   // 4) respuesta corta en el bloque 0 -> open falla
        Fake f; f.total = 8 << 20; f.shortData = true; std::string err;
        VSource* s = vsource_http("http://x/v.mp4", "", [&](long long a, long long b, std::string& d, long long& t, std::string& e) { return f(a, b, d, t, e); });
        assert(!s->open(err)); delete s; printf("ok respuesta corta: %s\n", err.c_str());
    }
    {   // 5) cerrar con la red lenta no deja hilos colgando (ASan/TSan lo verian)
        Fake f; f.total = 30 << 20; f.delayUs = 20000; std::string err;
        VSource* s = vsource_http("http://x/v.mp4", "", [&](long long a, long long b, std::string& d, long long& t, std::string& e) { return f(a, b, d, t, e); });
        assert(s->open(err)); sceKernelUsleep(100000); s->close(); delete s; printf("ok cierre\n");
    }
    {   // 6) archivo local
        FILE* fp = fopen("/tmp/vs_local.bin", "wb"); for (int i = 0; i < 100000; i++) fputc(byteAt(i), fp); fclose(fp);
        std::string err; VSource* s = vsource_file("/tmp/vs_local.bin"); assert(s->open(err)); assert(s->size() == 100000);
        std::vector<uint8_t> b(5000); assert(s->read(b.data(), 12345, 5000) == 5000 && check(b.data(), 12345, 5000));
        assert(s->read(b.data(), 99990, 5000) == 10); assert(s->read(b.data(), 100000, 10) == 0); delete s;
        VSource* m = vsource_file("/tmp/no_existe.mp4"); assert(!m->open(err)); delete m; printf("ok archivo local\n");
    }
    printf("TODO OK\n");
}
