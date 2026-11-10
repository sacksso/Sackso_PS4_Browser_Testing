#pragma once
#include <cstdint>
#include <cstddef>
#define ORBIS_KERNEL_PROT_CPU_RW 3
#define ORBIS_KERNEL_PROT_GPU_RW 0x30
#define ORBIS_KERNEL_WB_ONION 3
extern "C" {
int sceKernelUsleep(unsigned); int sceKernelLoadStartModule(const char*, size_t, const void*, unsigned, void*, int*);
int sceKernelDlsym(int, const char*, void**); int sceUserServiceGetInitialUser(int*);
uint64_t sceKernelGetProcessTime(); int sceKernelAllocateDirectMemory(long,long,size_t,size_t,int,long*);
int sceKernelMapDirectMemory(void**,size_t,int,int,long,size_t); int sceKernelMunmap(void*,size_t);
int sceKernelReleaseDirectMemory(long,size_t); size_t sceKernelGetDirectMemorySize();
}
