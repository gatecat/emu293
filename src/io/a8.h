//Emulation of A8 keyboard/mouse
#pragma once
#include "../helper.h"
#include "SDL2/SDL.h"
namespace Emu293 {
	void A8IOInit();
	void A8IOTick();
	void A8IOEvent(SDL_Event *ev);
}
