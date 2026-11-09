#pragma once
// v0.9.3: reconoce por los primeros bytes los archivos que el AvPlayer de la PS4 NO sabe reproducir, para avisar al instante
// (el log de v0.9.1 mostro 14 s de espera sin imagen con un .webm: "sceAvPlayerStreamCount = -1").
// Logica pura (sin APIs de la consola): se prueba en PC con tools/tests/test_vsniff.cpp.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// true = formato NO compatible (why explica el motivo y que hacer). false = parece MP4/MOV o no se sabe: dejar que lo intente el AvPlayer.
inline bool vsniff(const uint8_t* b, size_t n, std::string& why) {
    if (!b || n < 4) return false;
    if (b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3) {
        why = "Este video es WebM/MKV y la consola solo reproduce MP4 (H.264 + AAC). Descarga una version MP4 o usa !media con el ayudante del PC.";
        return true;
    }
    if (n >= 3 && b[0] == 'F' && b[1] == 'L' && b[2] == 'V') {
        why = "Este video es FLV y la consola solo reproduce MP4 (H.264 + AAC).";
        return true;
    }
    if (n >= 12 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "AVI ", 4) == 0) {
        why = "Este video es AVI y la consola solo reproduce MP4 (H.264 + AAC).";
        return true;
    }
    if (n >= 7 && memcmp(b, "#EXTM3U", 7) == 0) {
        why = "Esto es una lista HLS (.m3u8), no un archivo de video: la reproduccion HLS no esta probada. Usa !media con el ayudante del PC para obtener un MP4.";
        return true;
    }
    size_t i = 0;
    while (i < n && (b[i] == ' ' || b[i] == '\t' || b[i] == '\r' || b[i] == '\n')) i++;
    if (i < n && b[i] == '<') {
        why = "El servidor devolvio una pagina web y no un video (el enlace caduco, pide verificacion o inicio de sesion).";
        return true;
    }
    return false;
}
