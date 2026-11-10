#pragma once
#include <cstdint>
#define ORBIS_SYSMODULE_INTERNAL_NET 0x80000001
#define ORBIS_SYSMODULE_INTERNAL_SSL 0x80000002
#define ORBIS_SYSMODULE_INTERNAL_HTTP 0x80000003
#define ORBIS_SYSMODULE_AV_PLAYER 0x1
extern "C" { int sceSysmoduleLoadModuleInternal(uint32_t); int sceSysmoduleLoadModule(uint16_t); }
