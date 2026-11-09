// Prueba de app/vsniff.h: g++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -Iapp tools/tests/test_vsniff.cpp -o /tmp/test_vsniff && /tmp/test_vsniff
#include <cstdio>
#include <cstring>
#include <string>
#include "vsniff.h"
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FALLO linea %d: %s\n", __LINE__, #c); fails++; } } while (0)
static bool S(const void* d, size_t n, std::string* w = nullptr) { std::string why; const bool r = vsniff(static_cast<const uint8_t*>(d), n, why); if (w) *w = why; return r; }
int main() {
    const uint8_t webm[] = {0x1A, 0x45, 0xDF, 0xA3, 0x9F, 0x42, 0x86, 0x81};
    std::string w;
    CHECK(S(webm, sizeof(webm), &w)); CHECK(w.find("WebM") != std::string::npos);
    const uint8_t mp4[] = {0, 0, 0, 0x20, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'};
    CHECK(!S(mp4, sizeof(mp4)));
    const uint8_t mp4b[] = {0, 0, 0, 8, 'f', 'r', 'e', 'e'};
    CHECK(!S(mp4b, sizeof(mp4b)));
    CHECK(S("FLV\x01\x05", 5));
    CHECK(S("RIFF\x10\x00\x00\x00" "AVI LIST", 12));
    CHECK(!S("RIFF\x10\x00\x00\x00" "WAVEfmt ", 12));          // WAV no se rechaza aqui (lo decide el AvPlayer)
    CHECK(S("#EXTM3U\n#EXT-X", 14, &w)); CHECK(w.find("HLS") != std::string::npos);
    CHECK(S("<!DOCTYPE html>", 15, &w)); CHECK(w.find("pagina web") != std::string::npos);
    CHECK(S("\r\n  <html>", 11));
    CHECK(!S("abc", 3));          // muy corto
    CHECK(!S(nullptr, 0));
    CHECK(!S("\x00\x00\x00\x18", 4));
    printf(fails ? "HAY FALLOS\n" : "test_vsniff OK\n");
    return fails ? 1 : 0;
}
