//Emulation of a gamepad
#pragma once
#include "../helper.h"
#include "SDL2/SDL.h"
namespace Emu293 {

	void IRGamepadTick();
	void IRGamepadUpdate(int player, uint16_t state);
	void IRGamepadEvent(SDL_Event *ev);
	extern bool softreset_flag;
}
