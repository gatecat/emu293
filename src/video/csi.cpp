#include "csi.h"
#include "webcam.h"

namespace Emu293 {

static constexpr int csi_nreg = 64;
static uint32_t csi_regs[csi_nreg];


void CSIDeviceWriteHandler(uint16_t addr, uint32_t val) {
  addr /= 4;
  if (addr < csi_nreg) {
  	// TODO: actually make an I2C transfer to simulated camera
    csi_regs[addr] = val;
  } else {
    printf("CSI write error: address 0x%04x out of range, dat=0x%08x\n",
           addr * 4, val);
  }
}

uint32_t CSIDeviceReadHandler(uint16_t addr) {
  addr /= 4;
  if (addr < csi_nreg) {
    return csi_regs[addr];
  } else {
    printf("CSI read error: address 0x%04x out of range\n", addr * 4);
    return 0;
  }
}

void CSIDeviceResetHandler() {
  for (auto &r : csi_regs)
    r = 0;
}

void CSIState(SaveStater &s) {
  s.tag("CSI");
  s.a(csi_regs);
}

void InitCSIDevice(PeripheralInitInfo initInfo) {}

const Peripheral CSIPeripheral = {"CSI", InitCSIDevice,
                                     CSIDeviceReadHandler,
                                     CSIDeviceWriteHandler,
                                     CSIDeviceResetHandler,
                                     CSIState};

void CSITick(bool get_frame) {
	
}

};