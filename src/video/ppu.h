#pragma once
#include "../peripheral.h"
#include "../helper.h"

namespace Emu293 {
	extern const Peripheral PPUPeripheral;

	//Must only be called once
	void InitPPUThreads();
}
