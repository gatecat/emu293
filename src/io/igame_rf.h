//Emulation of the iGame RF gamepad
#pragma once
#include "../helper.h"
#include "SDL2/SDL.h"
namespace Emu293 {

	void IGameRFInit();
	void IGameRFTick();
	void IGameRFEvent(SDL_Event *ev);
}
