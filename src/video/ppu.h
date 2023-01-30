#pragma once
#include "../helper.h"
#include "../peripheral.h"

namespace Emu293 {
extern const Peripheral PPUPeripheral;

extern bool shutdown_flag;
extern int savestate_flag, loadstate_flag;

// Must only be called once
void InitPPUThreads();
void ShutdownPPU();

void PPUTick();
}
