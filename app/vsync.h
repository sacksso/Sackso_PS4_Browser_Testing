#pragma once
// Sincronizacion de video por reloj de pared (sin dependencias de la consola; se prueba en PC: tools/tests/test_vsync.cpp).
//
// Problema que resuelve (v0.8.0 en PS4): el bucle principal consume UN fotograma por vuelta (~70 ms) y el AvPlayer sin audio
// entrega "el siguiente" de su cola, asi que un video de 24 fps se veia a ~0,5x (camara lenta). Aqui se ancla el primer
// fotograma al reloj de pared y, si el siguiente va atrasado, se piden mas fotogramas seguidos y se descartan los atrasados
// (se muestra solo el ultimo). Si un fotograma llega MUY adelantado se retiene hasta que le toque.
#include <cstdint>

class VSync {
public:
    struct Result {
        bool have = false;       // se obtuvo al menos un fotograma (el ultimo pedido es el que hay que mostrar)
        bool hold = false;       // ese fotograma va adelantado: convertirlo a un buffer aparte y esperar (pendingDue)
        bool resynced = false;   // se re-ancloo el reloj (primer fotograma, tras seek, parada o salto grande)
        uint64_t ts = 0;         // marca de tiempo (ms) del fotograma elegido
        int dropped = 0;         // fotogramas atrasados descartados en esta llamada
    };

    void reset() { set_ = false; pend_ = false; haveLast_ = false; gotT_ = false; }
    bool pending() const { return pend_; }
    bool pendingDue(uint32_t now) const { return pend_ && set_ && pendTs_ <= target(now) + frameMs_ / 2; }
    void clearPending() { pend_ = false; }
    uint32_t frameMs() const { return frameMs_; }
    // Instante del contenido (ms) que "toca" ahora segun el reloj de pared.
    uint64_t target(uint32_t now) const { return ts0_ + static_cast<uint32_t>(now - wall0_); }

    // pull(uint64_t& ts) -> bool: pide el siguiente fotograma al reproductor (false = no hay). El llamador guarda los datos
    // del ULTIMO fotograma pedido (su memoria vale hasta la siguiente peticion). Se piden como mucho maxPull por llamada.
    template <class Pull>
    Result pump(uint32_t now, Pull pull, int maxPull = 8) {
        Result r;
        if (set_ && gotT_ && static_cast<uint32_t>(now - lastGot_) > 400) set_ = false;  // parada larga (red, pausa): re-anclar
        int pulled = 0;
        for (int k = 0; k < maxPull; k++) {
            uint64_t ts = 0;
            if (!pull(ts)) break;
            pulled++;
            if (haveLast_ && ts > lastTs_ && ts - lastTs_ < 250) {  // intervalo entre fotogramas = 1/fps
                const uint64_t d = ts - lastTs_;
                frameMs_ = static_cast<uint32_t>(d < 8 ? 8 : (d > 100 ? 100 : d));
            }
            lastTs_ = ts;
            haveLast_ = true;
            lastGot_ = now;
            gotT_ = true;
            r.have = true;
            r.ts = ts;
            if (!set_) { anchor(ts, now); r.resynced = true; break; }
            const uint64_t tgt = target(now);
            if (ts > tgt + 2000 || tgt > ts + 2000) { anchor(ts, now); r.resynced = true; break; }  // salto grande (seek): re-anclar
            if (ts + frameMs_ >= tgt) break;  // al dia o adelantado: es el que se muestra
            // atrasado: se descarta y se intenta el siguiente
        }
        if (pulled > 1) r.dropped = pulled - 1;
        if (r.have && !r.resynced && r.ts > target(now) + 90) { r.hold = true; pend_ = true; pendTs_ = r.ts; }
        return r;
    }

private:
    void anchor(uint64_t ts, uint32_t now) { set_ = true; wall0_ = now; ts0_ = ts; }
    bool set_ = false, haveLast_ = false, gotT_ = false, pend_ = false;
    uint32_t wall0_ = 0, lastGot_ = 0, frameMs_ = 42;
    uint64_t ts0_ = 0, lastTs_ = 0, pendTs_ = 0;
};
