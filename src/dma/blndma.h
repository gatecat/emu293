//Basic implementation of the SPG293 BLNDMA
#pragma once
#include "../peripheral.h"
#include "../helper.h"

namespace Emu293 {
	extern const Peripheral BLNDMAPeripheral;

	//Must only be called once
	void InitBLNDMAThread();
}
