#pragma once
#include <cstdint>
#define ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO 1
#define ORBIS_AUDIO_OUT_PORT_TYPE_MAIN 0
#define ORBIS_USER_SERVICE_USER_ID_SYSTEM 255
extern "C" { int sceAudioOutInit(); int sceAudioOutOpen(int,int,int,int,int,int); int sceAudioOutOutput(int,const void*); int sceAudioOutClose(int); }
