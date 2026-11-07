// Prueba de app/vsync.h: sincronizacion de video por reloj de pared con reproductores simulados.
//   g++ -std=c++17 -fsanitize=address,undefined -Iapp tools/tests/test_vsync.cpp -o /tmp/test_vsync && /tmp/test_vsync
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "vsync.h"

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FALLO: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

struct Sim {
    VSync s;
    uint64_t shown = 0, dropped = 0, held = 0, resyncs = 0;
    uint64_t lastShownTs = 0;
};

// Imita el uso de vplayer.cpp::vp_update(). nextTs: siguiente fotograma del reproductor (consumo = avanza).
template <class Pull>
static bool step(Sim& m, uint32_t now, Pull pull) {
    if (m.s.pending()) {
        if (m.s.pendingDue(now)) { m.s.clearPending(); m.shown++; return true; }  // el fotograma retenido ya toca
        return false;
    }
    uint64_t lastTs = 0;
    VSync::Result r = m.s.pump(now, [&](uint64_t& ts) { if (!pull(now, ts)) return false; lastTs = ts; return true; });
    (void)lastTs;
    if (!r.have) return false;
    m.dropped += static_cast<uint64_t>(r.dropped);
    if (r.resynced) m.resyncs++;
    if (r.hold) { m.held++; return false; }
    m.shown++;
    m.lastShownTs = r.ts;
    return true;
}
static uint64_t frameTs(uint64_t n) { return n * 1000 / 24; }

int main() {
    // A) reproductor "consumo": cada peticion entrega el siguiente fotograma; el bucle va a 70 ms por vuelta (la PS4 real).
    {
        Sim m;
        uint64_t n = 0;
        uint64_t lastShown = 0;
        for (uint32_t now = 1000; now < 11000; now += 70) {
            if (m.s.pending() ) {
                if (m.s.pendingDue(now)) { m.s.clearPending(); m.shown++; lastShown = m.lastShownTs; }
                continue;
            }
            step(m, now, [&](uint32_t, uint64_t& ts) { ts = frameTs(n++); return true; });
            lastShown = m.lastShownTs;
        }
        const double speed = static_cast<double>(lastShown) / 10000.0;
        printf("A) consumo, 70 ms/vuelta: mostrados=%llu descartados=%llu retenidos=%llu velocidad=%.2fx\n", (unsigned long long)m.shown,
               (unsigned long long)m.dropped, (unsigned long long)m.held, speed);
        CHECK(speed > 0.93 && speed < 1.07, "A: la velocidad deberia ser ~1.0x (%.2f)", speed);
        CHECK(m.dropped > 50, "A: deberia descartar fotogramas (%llu)", (unsigned long long)m.dropped);
    }
    // B) reproductor "reloj propio": solo entrega un fotograma si ya toca segun SU reloj de pared (no se descarta nada de mas).
    {
        Sim m;
        uint64_t n = 0;
        const uint32_t t0 = 1000;
        for (uint32_t now = t0; now < 11000; now += 70) {
            step(m, now, [&](uint32_t nw, uint64_t& ts) {
                if (frameTs(n) > static_cast<uint64_t>(nw - t0)) return false;
                ts = frameTs(n++);
                return true;
            });
        }
        const double speed = static_cast<double>(m.lastShownTs) / 10000.0;
        printf("B) reloj propio: mostrados=%llu descartados=%llu velocidad=%.2fx\n", (unsigned long long)m.shown, (unsigned long long)m.dropped, speed);
        CHECK(speed > 0.93 && speed < 1.07, "B: velocidad ~1.0x (%.2f)", speed);
    }
    // C) vuelta rapida (4 ms) con reproductor "consumo": va por delante; no debe acelerar el video.
    {
        Sim m;
        uint64_t n = 0;
        uint64_t lastShown = 0;
        for (uint32_t now = 1000; now < 11000; now += 4) {
            if (m.s.pending()) {
                if (m.s.pendingDue(now)) { m.s.clearPending(); m.shown++; lastShown = m.lastShownTs; }
                continue;
            }
            const uint64_t before = n;
            step(m, now, [&](uint32_t, uint64_t& ts) { ts = frameTs(n++); return true; });
            (void)before;
            lastShown = m.lastShownTs;
        }
        // los retenidos no actualizan lastShownTs: se mide con el ultimo mostrado, tolerancia mayor
        const double speed = static_cast<double>(lastShown) / 10000.0;
        printf("C) consumo, 4 ms/vuelta: mostrados=%llu retenidos=%llu velocidad=%.2fx\n", (unsigned long long)m.shown, (unsigned long long)m.held, speed);
        CHECK(speed > 0.9 && speed < 1.1, "C: no debe ir acelerado (%.2f)", speed);
    }
    // D) seek hacia delante (+60 s) a mitad: el reloj debe re-anclarse y no quedar retenido para siempre.
    {
        Sim m;
        uint64_t n = 0;
        uint64_t shownAfter = 0;
        bool jumped = false;
        for (uint32_t now = 1000; now < 11000; now += 70) {
            if (!jumped && now >= 6000) { jumped = true; n += 24 * 60; /* el reproductor salta 60 s */ }
            if (m.s.pending()) { if (m.s.pendingDue(now)) { m.s.clearPending(); m.shown++; } continue; }
            const uint64_t sb = m.shown;
            step(m, now, [&](uint32_t, uint64_t& ts) { ts = frameTs(n++); return true; });
            if (now > 7000) shownAfter += m.shown - sb;
        }
        printf("D) seek +60 s: mostrados despues=%llu resincronizaciones=%llu\n", (unsigned long long)shownAfter, (unsigned long long)m.resyncs);
        CHECK(shownAfter > 20, "D: tras el seek debe seguir mostrando fotogramas (%llu)", (unsigned long long)shownAfter);
        CHECK(m.resyncs >= 2, "D: debe re-anclar (%llu)", (unsigned long long)m.resyncs);
    }
    // E) parada de 2 s (red) en mitad: al volver no debe descartar una avalancha ni ir a saltos.
    {
        Sim m;
        uint64_t n = 0;
        uint64_t droppedAfterStall = 0, resyncs = 0;
        for (uint32_t now = 1000; now < 11000; now += 70) {
            if (now >= 5000 && now < 7000) continue;  // sin vueltas ni fotogramas durante 2 s
            const uint64_t d0 = m.dropped, r0 = m.resyncs;
            if (m.s.pending()) { if (m.s.pendingDue(now)) { m.s.clearPending(); m.shown++; } continue; }
            step(m, now, [&](uint32_t, uint64_t& ts) { ts = frameTs(n++); return true; });
            if (now >= 7000 && now < 7300) { droppedAfterStall += m.dropped - d0; resyncs += m.resyncs - r0; }
        }
        printf("E) parada 2 s: descartados justo al volver=%llu re-anclajes=%llu\n", (unsigned long long)droppedAfterStall, (unsigned long long)resyncs);
        CHECK(resyncs >= 1, "E: debe re-anclar tras la parada");
        CHECK(droppedAfterStall <= 8, "E: no debe descartar una avalancha (%llu)", (unsigned long long)droppedAfterStall);
    }
    printf(fails ? "RESULTADO: %d FALLOS\n" : "RESULTADO: todo OK\n", fails);
    return fails ? 1 : 0;
}
