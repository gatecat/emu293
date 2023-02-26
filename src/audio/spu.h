#pragma once
#include "../helper.h"
#include "../peripheral.h"

namespace Emu293 {
extern const Peripheral SPUPeripheral;

extern bool spu_debug_flag;

void SPUInitSound();
void SPUUpdate();
void ShutdownSPU();

}
