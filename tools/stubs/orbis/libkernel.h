#pragma once
#include <cstdint>
#include <cstddef>
extern "C" {
int sceKernelUsleep(unsigned); int sceKernelLoadStartModule(const char*, size_t, const void*, unsigned, void*, int*);
int sceKernelDlsym(int, const char*, void**); int sceUserServiceGetInitialUser(int*);
}
