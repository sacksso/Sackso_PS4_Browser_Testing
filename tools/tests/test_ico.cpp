// Uso: test_ico in.ico out.raw  -> escribe "R w h\n"+RGBA, o "P\n"+png, o "X\n" si no es valido
#include <cstdio>
#include <fstream>
#include <iterator>
#include "icodec.h"
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::ifstream f(argv[1], std::ios::binary);
    std::string b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    int w = 0, h = 0; std::vector<uint8_t> px; std::string png;
    const int r = ico_extract(b, w, h, px, png);
    FILE* o = fopen(argv[2], "wb");
    if (r == 1) { fprintf(o, "R %d %d\n", w, h); fwrite(px.data(), 1, px.size(), o); }
    else if (r == 2) { fprintf(o, "P\n"); fwrite(png.data(), 1, png.size(), o); }
    else fprintf(o, "X\n");
    fclose(o);
    return 0;
}
