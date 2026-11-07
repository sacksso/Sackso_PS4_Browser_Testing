// Prueba en PC de AsyncWriter: orden e integridad, contrapresion, abort. g++ -std=c++17 -pthread -fsanitize=address,undefined ...
#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include "asyncwriter.h"
extern "C" int sceKernelUsleep(unsigned us);
extern "C" int sceKernelUsleep(unsigned us) { struct timespec t{0, static_cast<long>(us) * 1000}; nanosleep(&t, nullptr); return 0; }
static std::string slurp(const char* p) { FILE* f = fopen(p, "rb"); std::string s; char b[65536]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s; }
int main() {
    const char* P = "/tmp/aw_test.bin";
    std::mt19937 rng(1);
    {   // 1) 40 MB en trozos de tamano variable, cola pequena (fuerza contrapresion)
        AsyncWriter w;
        assert(w.open(P, 1u * 1024 * 1024));
        std::string all;
        for (int i = 0; i < 1500; i++) {
            std::string c(1 + rng() % 60000, 'a' + (i % 26));
            c[0] = static_cast<char>(rng());
            all += c;
            assert(w.write(c.data(), c.size()));
        }
        assert(w.finish());
        assert(slurp(P) == all);
        printf("ok integridad (%zu bytes)\n", all.size());
    }
    {   // 2) abort a mitad no se cuelga ni pierde la memoria
        AsyncWriter w;
        assert(w.open(P, 64 * 1024));
        std::string c(30000, 'x');
        for (int i = 0; i < 20; i++) assert(w.write(c.data(), c.size()));
        w.abort();
        assert(!w.write(c.data(), c.size()));
        printf("ok abort\n");
    }
    {   // 3) ruta imposible
        AsyncWriter w;
        assert(!w.open("/no/existe/x.bin"));
        printf("ok open fallido\n");
    }
    {   // 4) disco lleno simulado: /dev/full
        AsyncWriter w;
        assert(w.open("/dev/full"));
        std::string c(100000, 'z');
        bool sawFail = false;
        for (int i = 0; i < 200 && !sawFail; i++) sawFail = !w.write(c.data(), c.size());
        bool fin = w.finish();
        assert(sawFail || !fin);
        printf("ok error de disco detectado\n");
    }
    printf("TODO OK\n");
}
