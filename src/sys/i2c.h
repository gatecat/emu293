#pragma once
#include "../peripheral.h"
#include "../helper.h"
namespace Emu293 {

class I2CDevice {
	virtual void start() {};
	virtual void stop() {};
	virtual bool write(uint8_t value) { return false; };
	virtual bool read(uint8_t &value) { return false; };
	virtual ~I2CDevice() {};
};

extern const Peripheral I2CPeripheral;

void register_i2c(I2CDevice &dev, uint8_t addr);

}
