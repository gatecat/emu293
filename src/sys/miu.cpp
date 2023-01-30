#include "miu.h"
#include "../helper.h"
#include "../sys/irq_if.h"
#include "../system.h"
#include <cstdio>
using namespace std;

namespace Emu293 {
const int miu_regs_size = 0x5C;
uint32_t miu_regs[miu_regs_size] = {0};

void MIUDeviceWriteHandler(uint16_t addr, uint32_t val) {
  addr /= 4;
  if (addr < miu_regs_size) {
    miu_regs[addr] = val;
  } else {
    printf("MIU write error: address 0x%04x out of range, dat=0x%08x\n",
           addr * 4, val);
  }
}

uint32_t MIUDeviceReadHandler(uint16_t addr) {
  addr /= 4;
  if (addr < miu_regs_size) {
    return miu_regs[addr];
  } else {
    printf("MIU read error: address 0x%04x out of range\n", addr * 4);
    return 0;
  }
}

void MIUDeviceResetHandler() {
  for (auto &r : miu_regs)
    r = 0;
}

void MIUDeviceState(SaveStater &s) {
  s.tag("MIU");
  s.a(miu_regs);
}

void InitMIUDevice(PeripheralInitInfo initInfo) {}

const Peripheral MIUPeripheral = {"MIU", InitMIUDevice, MIUDeviceReadHandler,
                                  MIUDeviceWriteHandler, MIUDeviceResetHandler, MIUDeviceState};
}
