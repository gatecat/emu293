#pragma once
#include "../helper.h"
#include "../peripheral.h"

namespace Emu293 {
extern const Peripheral CSIPeripheral;

void CSITick(bool get_frame);
void InitCSIThreads();
void ShutdownCSI();

}
