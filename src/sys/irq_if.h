//IRQ control - an external interface for other devices and the CPU
#pragma once
#include "../helper.h"
namespace Emu293 {
//Other peripherals should call this to request an interrupt
void SetIRQState(uint8_t IRQ, bool value);
//CPU can call this to enable or disable interrupts
void SetInterruptsEnabled(bool enable);
}

