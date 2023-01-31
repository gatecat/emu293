#include "../peripheral.h"
#include "../helper.h"
#include "i2c.h"

namespace Emu293 {

static constexpr int i2c_nreg = 16;
static uint32_t i2c_regs[i2c_nreg];

static const int i2c_cr = 8;
static const int i2c_cr_start8 = 0;
static const int i2c_cr_start16 = 1;
static const int i2c_cr_startn = 2;
static const int i2c_cr_ack8 = 3;
static const int i2c_cr_ack16 = 4;
static const int i2c_cr_ackn = 5;
static const int i2c_cr_mode = 6;
static const int i2c_cr_gak = 7;
static const int i2c_cr_stopn = 8;

static const int i2c_intr = 9;
static const int i2c_cvr = 10;
static const int i2c_id = 11;
static const int i2c_addr = 12;
static const int i2c_wdata = 13;
static const int i2c_rdata = 14;

static const int i2c_irq_idx = 24;

void I2CDeviceWriteHandler(uint16_t addr, uint32_t val) {
  addr /= 4;
  if (addr < i2c_nreg) {
  	// TODO: actually make an I2C transfer to simulated camera
    i2c_regs[addr] = val;
  } else {
    printf("I2C write error: address 0x%04x out of range, dat=0x%08x\n",
           addr * 4, val);
  }
}

uint32_t I2CDeviceReadHandler(uint16_t addr) {
  addr /= 4;
  if (addr < i2c_nreg) {
  	// just pretend the transfer completed successfully...
  	if (addr == i2c_intr)
  		return (i2c_regs[addr] | 0x1);
    return i2c_regs[addr];
  } else {
    printf("I2C read error: address 0x%04x out of range\n", addr * 4);
    return 0;
  }
}

void I2CDeviceResetHandler() {
  for (auto &r : i2c_regs)
    r = 0;
}

void I2CState(SaveStater &s) {
  s.tag("I2C");
  s.a(i2c_regs);
}

void InitI2CDevice(PeripheralInitInfo initInfo) {}

const Peripheral I2CPeripheral = {"I2C", InitI2CDevice,
                                     I2CDeviceReadHandler,
                                     I2CDeviceWriteHandler,
                                     I2CDeviceResetHandler,
                                     I2CState};
}
 