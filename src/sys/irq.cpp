#include "irq.h"
#include "irq_if.h"
#include <bitset>
#include <cstdio>
using namespace std;

#define INT_PNDL 0x0000
#define INT_PNDH 0x0001
#define INT_PRIORITY_M 0x0002
#define INT_PRIORITY_SG0 0x0004
#define INT_PRIORITY_SG1 0x0005
#define INT_PRIORITY_SG2 0x0006
#define INT_PRIORITY_SG3 0x0007
#define INT_MASKL 0x0008
#define INT_MASKH 0x0009
#define INT_REGS_SIZE 0x000A

namespace Emu293 {
static uint32_t int_regs[INT_REGS_SIZE] = {0};
static CPU *currentCPU;
static bool interruptsEnabled = true;
static bitset<64> intsFired;
void InitIRQDevice(PeripheralInitInfo initInfo) {
  currentCPU = initInfo.currentCPU;
}
void ProcessInterrupts() {
  if (interruptsEnabled) {
    // TODO: interrupt priorities (although the standard D99+ firmware does not
    // use them)
    for (int i = 0; i < 32; i++) {
      if (check_bit(int_regs[INT_PNDL], i) &&
          !check_bit(int_regs[INT_MASKL], i) && !intsFired[63 - i]) {
        // printf("INT %d!\n",63 - i);
        currentCPU->interrupt(63 - i);
        intsFired[63 - i] = true;
        return;
      }
    }
    for (int i = 0; i < 32; i++) {
      if (check_bit(int_regs[INT_PNDH], i) &&
          !check_bit(int_regs[INT_MASKH], i) && !intsFired[31 - i]) {
        // printf("INT %d!\n",31 - i);
        currentCPU->interrupt(31 - i);
        intsFired[31 - i] = true;
        return;
      }
    }
  }
}
uint32_t IRQDeviceReadHandler(uint16_t addr) {
  uint16_t addr32 = addr / 4;
  if (addr32 >= INT_REGS_SIZE) {
    printf("IRQ device read error: address 0x%04x out of bounds\n", addr);
    return 0;
  } else {
    return int_regs[addr32];
  }
}
void IRQDeviceWriteHandler(uint16_t addr, uint32_t val) {
  uint16_t addr32 = addr / 4;
  if (addr32 >= INT_REGS_SIZE) {
    printf("IRQ device write error: address 0x%04x out of bounds\n", addr);
    return;
  } else {
    int_regs[addr32] = val;
    if ((addr32 == INT_MASKL) || (addr32 == INT_MASKH)) {
      ProcessInterrupts();
    }
  }
}
void SetIRQState(uint8_t IRQ, bool value) {
  // printf("IRQ %d = %d\n", IRQ, value);
  if (IRQ >= 64)
    return;
  if (IRQ >= 32) {
    if (value) {
      set_bit(int_regs[INT_PNDL], 63 - IRQ);
      ProcessInterrupts();
    } else {
      clear_bit(int_regs[INT_PNDL], 63 - IRQ);
      intsFired[IRQ] = false;
    }
  } else {
    if (value) {
      set_bit(int_regs[INT_PNDH], 31 - IRQ);
      ProcessInterrupts();
    } else {
      clear_bit(int_regs[INT_PNDH], 31 - IRQ);
      intsFired[IRQ] = false;
    }
  }
}
void SetInterruptsEnabled(bool enable) {
  interruptsEnabled = enable;
  if (enable) {
    ProcessInterrupts();
  }
}
const Peripheral IRQPeripheral = {"PIC", InitIRQDevice, IRQDeviceReadHandler,
                                  IRQDeviceWriteHandler};
}
