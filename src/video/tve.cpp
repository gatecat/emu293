#include "tve.h"
#include "../helper.h"
#include "../sys/irq_if.h"
#include "../system.h"
#include <cstdio>
using namespace std;

namespace Emu293 {
const uint8_t tve_vblank_int = 55;
const int tve_regs_size = 0x12;
const int tve_irq_ctrl = 0x10;
const int tve_irq_status = 0x11;
const int tve_irqctl_vblank_start = 0;
const int tve_irqctl_vblank_end = 1;
uint32_t tve_regs[tve_regs_size] = {0};

void TVEDeviceWriteHandler(uint16_t addr, uint32_t val) {
  addr /= 4;
  if (addr < tve_regs_size) {
    tve_regs[addr] = val;
  } else {
    printf("TVE write error: address 0x%04x out of range, dat=0x%08x\n",
           addr * 4, val);
  }
  if (addr == tve_irq_status) {
    // writing to IRQ status appears to clear IRQ based on app code?
    if (check_bit(val, 0)) {
      SetIRQState(tve_vblank_int, false);
      clear_bit(tve_regs[tve_irq_status], 0);
    }
  }
}

uint32_t TVEDeviceReadHandler(uint16_t addr) {
  addr /= 4;
  if (addr < tve_regs_size) {
    return tve_regs[addr];
  } else {
    printf("TVE read error: address 0x%04x out of range\n", addr * 4);
    return 0;
  }
}
uint16_t tve_curr_line = 100;

void TVEDeviceResetHandler() {
  for (auto &r : tve_regs)
    r = 0;
}

void InitTVEDevice(PeripheralInitInfo initInfo) { tve_curr_line = 100; }
void TVETick() {
  // simulate some kind of vblank to keep the app happy
  if (tve_curr_line == 800) {
    tve_curr_line = 0;
    if (check_bit(tve_regs[tve_irq_ctrl], tve_irqctl_vblank_start)) {
      SetIRQState(tve_vblank_int, true);
      set_bit(tve_regs[tve_irq_status], 0);
    }
  } else if (tve_curr_line == 50) {
    tve_curr_line++;
    if (check_bit(tve_regs[tve_irq_ctrl], tve_irqctl_vblank_end)) {
      SetIRQState(tve_vblank_int, true);
      set_bit(tve_regs[tve_irq_status], 0);
    }
  } else {
    tve_curr_line++;
  }
}

void TVEState(SaveStater &s) {
  s.tag("TVE");
  s.a(tve_regs);
  s.i(tve_curr_line);
}

const Peripheral TVEPeripheral = {"TVE", InitTVEDevice, TVEDeviceReadHandler,
                                  TVEDeviceWriteHandler, TVEDeviceResetHandler, TVEState};
}
