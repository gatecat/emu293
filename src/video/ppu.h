#pragma once
#include "../helper.h"
#include "../peripheral.h"
#include "SDL2/SDL.h"

namespace Emu293 {
extern const Peripheral PPUPeripheral;

extern bool shutdown_flag;
extern int savestate_flag, loadstate_flag;

extern int video_scale;

typedef void (*SDLInputHandler)(SDL_Event *ev);
extern SDLInputHandler sdl_input_handler;

// Must only be called once
void InitPPUThreads();
void ShutdownPPU();

void PPUTick();
}
