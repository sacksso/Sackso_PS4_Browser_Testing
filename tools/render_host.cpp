#include <cstdio>
#include <fstream>
#include <sstream>
#include "canvas.h"
#include "htmldoc.h"
#include "pagerender.h"
#include "textengine.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
void psb_log(const char* f, ...) {}
int main(int argc, char** argv) {
    if (!te_init()) return 1;
    std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf();
    int dark = argc > 4 ? atoi(argv[4]) : 0; int scroll = argc > 5 ? atoi(argv[5]) : 0; int zoom = argc > 6 ? atoi(argv[6]) : 100;
    Document d = parseHtml(ss.str(), 1100, zoom);
    printf("lineas=%zu enlaces=%zu imagenes=%zu campos=%zu alto=%d titulo=%s\n", d.lines.size(), d.links.size(), d.images.size(), d.fields.size(), d.height, d.title.c_str());
    Theme th = dark ? themeDark() : themeLight();
    Canvas cv(1920, 1080); cv.clear(th.bg);
    RenderCtx rc{&th, 0, 0, 1920, 1080, (1920 - 1100) / 2, scroll};
    drawDocument(cv, d, rc);
    std::vector<uint8_t> rgb(1920 * 1080 * 3);
    for (int i = 0; i < 1920 * 1080; i++) { uint32_t p = cv.px[i]; rgb[i*3] = p >> 16; rgb[i*3+1] = p >> 8; rgb[i*3+2] = p; }
    stbi_write_png(argv[2], 1920, 1080, 3, rgb.data(), 1920 * 3);
    return 0;
}
