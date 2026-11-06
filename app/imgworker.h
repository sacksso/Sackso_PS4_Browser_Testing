#pragma once
// Descarga y decodificacion de imagenes (PNG/JPEG/GIF/BMP con stb_image) en un hilo aparte, para que la interfaz
// no se congele. El hilo principal envia trabajos y recoge resultados sin bloquearse.
#include <cstdint>
#include <string>
#include <vector>

struct ImgJob {
    int tabId = 0;
    unsigned gen = 0;
    int idx = 0;
    std::string url;
    int boxW = 0, boxH = 0;
};

struct ImgResult {
    int tabId = 0;
    unsigned gen = 0;
    int idx = 0;
    bool ok = false;
    int pw = 0, ph = 0;
    std::vector<uint32_t> pix;  // ARGB, ya escalada para caber en la caja
    std::string url;
    int boxW = 0, boxH = 0;
};

void imgw_start();
void imgw_submit(const ImgJob& j);
bool imgw_poll(ImgResult& out);
int imgw_pending();  // trabajos en cola o en curso
void imgw_cancel();  // descarta los trabajos en cola (la navegacion cambia de pagina)
void imgw_pause(bool paused);  // mientras es true el hilo no empieza peticiones nuevas (deja la red libre al hilo principal)
