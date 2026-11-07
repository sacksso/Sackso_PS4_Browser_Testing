#pragma once
#include <cstdint>
#include <cstddef>
extern "C" { int sceNetInit(); int sceNetPoolCreate(const char*, int, int); int sceNetPoolDestroy(int); }
