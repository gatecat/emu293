#pragma once
#include "../helper.h"
#include "../peripheral.h"

namespace Emu293 {
extern const Peripheral PPUPeripheral;

// Must only be called once
void InitPPUThreads();
void ShutdownPPU();

void PPUTick();
}
