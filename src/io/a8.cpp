// Notes:
//
// ** PS/2 IO
// Keyboard data -- C12
// Keyboard clock -- C0
// Mouse data -- C4
// Mouse clock -- C5
//
// ** Unknown serial protocol
// Dance clock -- I0
// Dance data 0 -- I3
// Dance data 1 -- I5
// 
// ** LTD ("learning technology device?") 
// I0/I1 ?
//

#include "ir_gamepad.h"
#include "a8.h"
#include "../system.h"
#include "../sys/gpio.h"

#include <cstdio>
#include <map>
#include <queue>

using namespace std;

namespace Emu293 {
	void A8IOInit() {
		// TODO
	}
	void A8IOTick() {
		// TODO
	}
	void A8IOEvent(SDL_Event *ev) {
		// TODO
	}
}
