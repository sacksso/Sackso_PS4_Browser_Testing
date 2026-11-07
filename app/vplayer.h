#pragma once
// Reproductor de video integrado: libSceAvPlayer (decodifica H.264/AAC en MP4) + libSceAudioOut (sonido).
// Origen: archivo local (descargas) o HTTP/HTTPS por rangos (streaming sin descargar), ver vsource.h.
// El AvPlayer del sistema pide los bytes mediante "fileReplacement" y devuelve fotogramas NV12 y audio PCM; aqui se
// convierten a ARGB y se dibujan en el lienzo del navegador (render por software de SDL).
// IMPORTANTE: nunca se ha probado en una consola real; cada paso se registra en log.txt con prefijo [vp].
#include <cstdint>
#include <string>

struct Canvas;

enum VpState { VP_CLOSED = 0, VP_OPENING, VP_PLAYING, VP_PAUSED, VP_BUFFERING, VP_ENDED, VP_ERROR };

struct VpInfo {
    VpState state = VP_CLOSED;
    std::string title, error, stage;  // stage: lo que se esta haciendo / diagnostico para mostrar en pantalla
    uint64_t posMs = 0, durMs = 0;
    int vw = 0, vh = 0;
    bool hasVideo = false, hasAudio = false;
    int volume = 80;                  // 0..100
    unsigned long frames = 0;         // fotogramas de video recibidos
    float fps = 0;                    // fotogramas mostrados por segundo (ventana de 2 s)
    float speed = 0;                  // velocidad real = contenido / reloj de pared (1.0 = tiempo real; 0.5 = camara lenta)
    float convMs = 0;                 // coste medio de convertir un fotograma NV12 -> ARGB
    unsigned long dropped = 0;        // fotogramas descartados por ir atrasados (solo sincronizacion por reloj de pared)
    bool clockSync = false;           // true = video sin audio sincronizado con el reloj de pared
    int bufPct = 100;                 // % de la ventana de lectura anticipada (solo streaming)
    bool netWait = false;             // esperando datos de la red
    int noFrameSecs = 0;              // segundos reproduciendo sin recibir ningun fotograma (sospecha de decodificador roto)
    int vdec = 0;
};

// Tipo de decodificador de video: 0 = el del sistema (hardware), 2 = software, 3 = software2 (PostInit de AvPlayer). Antes de vp_open.
void vp_set_decoder(int type);
void vp_set_volume_default(int pct);
// Empieza a abrir (en un hilo; no bloquea). source = URL http(s) o ruta de archivo. false si ya hay un video abierto.
bool vp_open(const std::string& source, const std::string& title, const std::string& referer);
void vp_close();
void vp_shutdown();                   // cierra video, audio y texturas; llamar justo antes de salir del proceso
bool vp_active();                     // hay un video abierto, cargando o con error visible (state != VP_CLOSED)
VpInfo vp_info();
void vp_pause_toggle();
void vp_seek_rel(int64_t deltaMs);
void vp_seek_abs(uint64_t ms);
void vp_volume_rel(int deltaPct);
// Hilo principal, una vez por vuelta del bucle: recoge el siguiente fotograma si hay uno nuevo (true = imagen nueva).
bool vp_update();
// Dibuja el ultimo fotograma escalado a (x,y,w,h) (el llamador calcula el recuadro con la proporcion). half = mitad de resolucion (mas rapido).
void vp_draw_video(Canvas& cv, int x, int y, int w, int h, bool half);
