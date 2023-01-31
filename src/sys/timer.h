//Timer peripheral
#pragma once
#include "../peripheral.h"
#include "../helper.h"
namespace Emu293 {
	extern const Peripheral CKGPeripheral;
	extern const Peripheral TimerPeripheral;
	void TimerTick(bool is_32khz);
	bool get_clock_enable(uint32_t offset);
}

