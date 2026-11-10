// Prueba en PC de app/vconv.h: SSE2 y reduccion 2:1 contra la version escalar de referencia, y una medida de velocidad.
// g++ -std=c++17 -O2 -Iapp tools/tests/test_vconv.cpp -o /tmp/test_vconv && /tmp/test_vconv
#include "vconv.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; printf("FALLO: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int maxDiff(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    int m = 0;
    for (size_t i = 0; i < a.size(); i++)
        for (int s = 0; s < 24; s += 8) m = std::max(m, std::abs(static_cast<int>((a[i] >> s) & 255) - static_cast<int>((b[i] >> s) & 255)));
    return m;
}

int main() {
    srand(1234);
    const int sizes[][2] = {{16, 16}, {64, 36}, {1280, 720}, {1920, 1080}, {100, 50}, {33, 17}, {854, 480}, {8, 2}, {17, 9}};
    for (const auto& sz : sizes) {
        const int w = sz[0], h = sz[1], pitch = w + 16;
        std::vector<uint8_t> buf(static_cast<size_t>(pitch) * h * 3 / 2 + 64);
        for (auto& b : buf) b = static_cast<uint8_t>(rand());
        // casos extremos en los primeros bytes
        for (int i = 0; i < 40 && i < static_cast<int>(buf.size()); i++) buf[i] = (i % 3 == 0) ? 0 : ((i % 3 == 1) ? 255 : 16);
        const uint8_t* uv = buf.data() + static_cast<size_t>(pitch) * h;
        for (int hd = 0; hd < 2; hd++) {
            std::vector<uint32_t> ref(static_cast<size_t>(w) * h), fast(static_cast<size_t>(w) * h, 0xDEADBEEF);
            vconv::nv12ToArgbRef(buf.data(), pitch, uv, pitch, w, h, ref.data(), hd);
            vconv::nv12ToArgb(buf.data(), pitch, uv, pitch, w, h, fast.data(), hd);
            const int d = maxDiff(ref, fast);
            CHECK(d <= 2, "%dx%d hd=%d: diferencia maxima %d (>2)", w, h, hd, d);
            for (size_t i = 0; i < fast.size(); i++) if ((fast[i] >> 24) != 0xFF) { CHECK(false, "%dx%d alfa distinto de 255 en %zu", w, h, i); break; }
            // reduccion 2:1: se compara con el promedio 2x2 del resultado de referencia (tolerancia mayor: el promedio se hace sobre Y, no sobre RGB)
            if (w >= 2 && h >= 2) {
                const int ow = w / 2, oh = h / 2;
                std::vector<uint32_t> half(static_cast<size_t>(ow) * oh, 0xDEADBEEF);
                vconv::nv12ToArgbHalfRef(buf.data(), pitch, uv, pitch, w, h, half.data(), hd);
                int worst = 0;
                for (int j = 0; j < oh; j++) for (int i = 0; i < ow; i++) {
                    // referencia: croma exacto del bloque + promedio de Y
                    const uint8_t* y0 = buf.data() + static_cast<size_t>(2 * j) * pitch;
                    const uint8_t* y1 = y0 + pitch;
                    const int yy = (y0[2 * i] + y0[2 * i + 1] + y1[2 * i] + y1[2 * i + 1] + 2) >> 2;
                    std::vector<uint8_t> one(2 * 2 + 2 * 1, 0);  // 2x2 de Y iguales + 1 par UV: se convierte con la referencia
                    one[0] = one[1] = one[2] = one[3] = static_cast<uint8_t>(yy);
                    one[4] = uv[static_cast<size_t>(j) * pitch + 2 * i]; one[5] = uv[static_cast<size_t>(j) * pitch + 2 * i + 1];
                    uint32_t px[4];
                    vconv::nv12ToArgbRef(one.data(), 2, one.data() + 4, 2, 2, 2, px, hd);
                    const uint32_t a = px[0], b = half[static_cast<size_t>(j) * ow + i];
                    for (int s = 0; s < 24; s += 8) worst = std::max(worst, std::abs(static_cast<int>((a >> s) & 255) - static_cast<int>((b >> s) & 255)));
                }
                CHECK(worst == 0, "%dx%d hd=%d: reduccion 2:1 escalar difiere de la referencia en %d", w, h, hd, worst);
                std::vector<uint32_t> hr(static_cast<size_t>(ow) * oh, 0xDEADBEEF);
                vconv::nv12ToArgbHalf(buf.data(), pitch, uv, pitch, w, h, hr.data(), hd);
                CHECK(maxDiff(hr, half) <= 2, "%dx%d hd=%d: reduccion 2:1 SSE2 difiere de la escalar en %d", w, h, hd, maxDiff(hr, half));
            }
        }
    }
    // velocidad (1080p y 720p)
    for (const auto& sz : {std::pair<int, int>{1920, 1080}, std::pair<int, int>{1280, 720}}) {
        const int w = sz.first, h = sz.second;
        std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 3 / 2);
        for (auto& b : buf) b = static_cast<uint8_t>(rand());
        std::vector<uint32_t> out(static_cast<size_t>(w) * h);
        auto bench = [&](const char* name, auto fn) {
            const auto t0 = std::chrono::steady_clock::now();
            const int n = 20;
            for (int i = 0; i < n; i++) fn();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n;
            printf("%dx%d %-10s %.2f ms/fotograma\n", w, h, name, ms);
            return ms;
        };
        const double r = bench("escalar", [&] { vconv::nv12ToArgbRef(buf.data(), w, buf.data() + static_cast<size_t>(w) * h, w, w, h, out.data(), true); });
        const double f = bench("SSE2", [&] { vconv::nv12ToArgb(buf.data(), w, buf.data() + static_cast<size_t>(w) * h, w, w, h, out.data(), true); });
        const double q = bench("mitad 2:1", [&] { vconv::nv12ToArgbHalf(buf.data(), w, buf.data() + static_cast<size_t>(w) * h, w, w, h, out.data(), true); });
        printf("   mejora SSE2 x%.1f, mitad x%.1f (en este PC; la PS4 es mas lenta)\n", r / f, r / q);
    }
    printf(g_fail ? "FALLOS: %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
