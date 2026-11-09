#pragma once
#include <cstdint>
typedef uint32_t Uint32;
struct SDL_Window; struct SDL_Renderer; struct SDL_Texture; struct SDL_Joystick;
struct SDL_Rect { int x, y, w, h; };
enum { SDL_QUIT = 0x100, SDL_JOYAXISMOTION = 0x600, SDL_JOYHATMOTION = 0x602, SDL_JOYBUTTONDOWN = 0x603, SDL_JOYBUTTONUP = 0x604 };
enum { SDL_HAT_UP = 1, SDL_HAT_RIGHT = 2, SDL_HAT_DOWN = 4, SDL_HAT_LEFT = 8 };
enum { SDL_INIT_VIDEO = 0x20, SDL_INIT_JOYSTICK = 0x200, SDL_INIT_EVENTS = 0x4000 };
enum { SDL_RENDERER_SOFTWARE = 1, SDL_RENDERER_ACCELERATED = 2 };
enum { SDL_PIXELFORMAT_ARGB8888 = 1 };
enum { SDL_TEXTUREACCESS_STATIC = 0, SDL_TEXTUREACCESS_STREAMING = 1 };
enum { SDL_BLENDMODE_BLEND = 1 };
struct SDL_JoyButtonEvent { uint32_t type; uint8_t which; uint8_t button; };
struct SDL_JoyHatEvent { uint32_t type; uint8_t hat; uint8_t value; };
struct SDL_JoyAxisEvent { uint32_t type; uint8_t axis; int16_t value; };
union SDL_Event { uint32_t type; SDL_JoyButtonEvent jbutton; SDL_JoyHatEvent jhat; SDL_JoyAxisEvent jaxis; };
int SDL_Init(uint32_t); int SDL_InitSubSystem(uint32_t); const char* SDL_GetError();
SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, uint32_t);
SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, uint32_t);
SDL_Texture* SDL_CreateTexture(SDL_Renderer*, uint32_t, int, int, int);
int SDL_SetTextureBlendMode(SDL_Texture*, int);
int SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void*, int);
int SDL_RenderClear(SDL_Renderer*); int SDL_RenderFillRect(SDL_Renderer*, const SDL_Rect*); int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*); void SDL_RenderPresent(SDL_Renderer*);
int SDL_PollEvent(SDL_Event*); int SDL_PushEvent(SDL_Event*); uint32_t SDL_GetTicks(); void SDL_Delay(uint32_t);
int SDL_NumJoysticks(); SDL_Joystick* SDL_JoystickOpen(int); const char* SDL_JoystickName(SDL_Joystick*);
void SDL_DestroyTexture(SDL_Texture*); int SDL_SetRenderDrawColor(SDL_Renderer*, uint8_t, uint8_t, uint8_t, uint8_t);
struct SDL_Surface { int w, h; };
SDL_Surface* SDL_GetWindowSurface(SDL_Window*); int SDL_UpdateWindowSurface(SDL_Window*); int SDL_UpdateWindowSurfaceRects(SDL_Window*, const SDL_Rect*, int); void SDL_PumpEvents();

struct SDL_RendererInfo { const char* name; uint32_t flags; uint32_t num_texture_formats; uint32_t texture_formats[16]; int max_texture_width; int max_texture_height; };
int SDL_GetNumVideoDrivers(); const char* SDL_GetVideoDriver(int); const char* SDL_GetCurrentVideoDriver();
int SDL_GetNumRenderDrivers(); int SDL_GetRenderDriverInfo(int, SDL_RendererInfo*); int SDL_GetRendererInfo(SDL_Renderer*, SDL_RendererInfo*);
void SDL_DestroyRenderer(SDL_Renderer*);
