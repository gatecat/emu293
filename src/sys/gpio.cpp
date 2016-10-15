#include "gpio.h"
#include "../helper.h"
#include "irq_if.h"
#include <cstdio>
#include <list>
#include <vector>
using namespace std;

#ifdef LITTLE_ENDIAN
//a bit hacky but it works
#define get_u32(arr) (*reinterpret_cast<uint32_t*>(&arr))
#endif

namespace Emu293 {

	/*
	 * Reg addresses are LITTLE ENDIAN byte addresses relative to start of GPIO peripherals
	 * NumberOfPins is actually a max number of pins determined by the register layout, as actual
	 * number of pins is currently unknown
	 *
	 * Some addresses may not be accurate. A datasheet or programming guide would be required to know for sure
	 */

	struct GPIOPortInfo {
		GPIOPort PortIndex;
		char PortLetter;
		uint8_t NumberOfPins;
		bool HasInterrupts;
		uint16_t OutputDataReg;
		uint16_t OutputEnReg;
		uint16_t InputPUReg;
		uint16_t InputPDReg;
		uint16_t InputDataReg;
		uint16_t InputInterruptReg;
	};

	static const GPIOPortInfo PortInfo[GPIOPortCount] = {
			{GPIO_PORT_A, 'A', 32, true,  0x0014, 0x0018, 0x001C, 0x0020, 0x0064, 0x0080},
			{GPIO_PORT_C, 'C', 16, true,  0x002C, 0x002E, 0x0030, 0x0032, 0x006C, 0x0088},
			{GPIO_PORT_D, 'D',  8, true,  0x0034, 0x0035, 0x0036, 0x0037, 0x0070, 0x008C},
			{GPIO_PORT_E, 'E',  8, false, 0x0038, 0x0039, 0x003A, 0x003B, 0x0074, 0xFFFF},
			{GPIO_PORT_F, 'F',  8, true,  0x003C, 0x003D, 0x003E, 0x003F, 0x0075, 0x00A0},
			{GPIO_PORT_G, 'G',	8, true,  0x0040, 0x0041, 0x0042, 0x0043, 0x0076, 0x0094},
			{GPIO_PORT_I, 'I',  8, true,  0x0048, 0x0049, 0x004A, 0x004B, 0x0078, 0x009C},
			{GPIO_PORT_K, 'K',  8, false, 0x0050, 0x0051, 0x0052, 0xFFFF, 0x0072, 0xFFFF}, //CHECK: 0x0072. could also be 0x0071
			{GPIO_PORT_R, 'R',  8, false, 0x0128, 0x0129, 0x012A, 0x012B, 0x0130, 0xFFFF},
			{GPIO_PORT_S, 'S',  8, true , 0x0141, 0x0140, 0x0142, 0xFFFF, 0x0143, 0x0144} //almost certainly fudged - needs more data
	};

	struct RegBitInfo {
		bool firesUpdate = false;
		GPIOPort port;
		uint8_t pin;
	};

	const int GPIORegCount = 0x148;
	static uint8_t gpio_reg[GPIORegCount] = {0};
	static bool gpio_reg_readonly[GPIORegCount] = {false};
	static RegBitInfo gpio_reg_bitinfo[GPIORegCount][8]; //msb = 7, lsb = 0
	static bool gpio_reg_interrupt[GPIORegCount] = {false};
	static vector<vector<list<GPIOStatusListener>>> listeners(GPIOPortCount); //[port][pin]

	const uint8_t GPIOInterrupt = 28;

	void InitGPIODevice(PeripheralInitInfo initInfo) {
		//reset bitinfo
		for(int i = 0; i < GPIORegCount; i++) {
			for(int j = 0; j < 8; j++) {
				gpio_reg_bitinfo[i][j].firesUpdate = false;
			}
		}
		for(int i = 0; i < GPIOPortCount; i++) {
			listeners[i].clear();
			listeners[i].resize(PortInfo[i].NumberOfPins);
			uint8_t numBytes = (PortInfo[i].NumberOfPins / 8);
			//mark input regs as readonly
			for(uint16_t j = PortInfo[i].InputDataReg; j < (PortInfo[i].InputDataReg + numBytes);j++) {
				gpio_reg_readonly[j] = true;
			}
			//set output regs listener arrays
			uint8_t pin = 0;
			for(uint16_t j = PortInfo[i].OutputDataReg; j < (PortInfo[i].OutputDataReg + numBytes);j++) {
				for(int k = 0; k < 8; k++) {
					gpio_reg_bitinfo[j][k].firesUpdate = true;
					gpio_reg_bitinfo[j][k].port = (GPIOPort)i;
					gpio_reg_bitinfo[j][k].pin = pin;
				}
			}

			//set output enable regs listener arrays
			pin = 0;
			for(uint16_t j = PortInfo[i].OutputEnReg; j < (PortInfo[i].OutputEnReg + numBytes);j++) {
				for(int k = 0; k < 8; k++) {
					gpio_reg_bitinfo[j][k].firesUpdate = true;
					gpio_reg_bitinfo[j][k].port = (GPIOPort)i;
					gpio_reg_bitinfo[j][k].pin = pin;
				}
			}

			//set PU regs listener arrays
			pin = 0;
			for(uint16_t j = PortInfo[i].InputPUReg; j < (PortInfo[i].InputPUReg + numBytes);j++) {
				for(int k = 0; k < 8; k++) {
					gpio_reg_bitinfo[j][k].firesUpdate = true;
					gpio_reg_bitinfo[j][k].port = (GPIOPort)i;
					gpio_reg_bitinfo[j][k].pin = pin;
				}
			}
			if(PortInfo[i].InputPDReg != 0xFFFF) {
				//set PD regs listener arrays
				pin = 0;
				for(uint16_t j = PortInfo[i].InputPDReg; j < (PortInfo[i].InputPDReg + numBytes);j++) {
					for(int k = 0; k < 8; k++) {
						gpio_reg_bitinfo[j][k].firesUpdate = true;
						gpio_reg_bitinfo[j][k].port = (GPIOPort)i;
						gpio_reg_bitinfo[j][k].pin = pin;
					}
				}
			}


			//set interrupt array
			if(PortInfo[i].HasInterrupts) {
				for(int j = PortInfo[i].InputInterruptReg; j < (PortInfo[i].InputInterruptReg + 4);j++) {
					gpio_reg_interrupt[j] = true;
				}
				//upper bytes are status, and effectively read only
				for(int j = PortInfo[i].InputInterruptReg+2; j < (PortInfo[i].InputInterruptReg + 4);j++) {
					gpio_reg_readonly[j] = true;
				}
			}
		}
	};

	void AttachGPIOListener(GPIOPort port, uint8_t pin, GPIOStatusListener listener) {
		listeners[port][pin].push_back(listener);
	}
	void SetGPIOInputState (GPIOPort port, uint8_t pin, bool newState) {
		if(pin >= PortInfo[port].NumberOfPins) {
			printf("Pin %d out of range for port %c\n",pin,PortInfo[port].PortLetter);
		}

		if(newState) {
			set_bit(get_u32(gpio_reg[PortInfo[port].InputDataReg]),pin);
		} else {
			clear_bit(get_u32(gpio_reg[PortInfo[port].InputDataReg]),pin);
		}
	}
	GPIOState GetGPIOState (GPIOPort port, uint8_t pin) {
		if(check_bit(get_u32(gpio_reg[PortInfo[port].OutputEnReg]),pin)) {
			//is an output
			if(check_bit(get_u32(gpio_reg[PortInfo[port].OutputEnReg]),pin)) {
				return GPIO_HIGH;
			} else {
				return GPIO_LOW;
			}
		} else {
			//is an input
			if(check_bit(get_u32(gpio_reg[PortInfo[port].InputPUReg]),pin)) {
				return GPIO_PULLUP;
			} else if ((PortInfo[port].InputPDReg != 0xFFFF) && (check_bit(get_u32(gpio_reg[PortInfo[port].InputPDReg]),pin))) {
				return GPIO_PULLDOWN;
			} else {
				return GPIO_FLOAT;
			}
		}
	}
	uint32_t GPIODeviceReadHandler(uint16_t addr) {
		if(addr < GPIORegCount) {
			return get_uint32le(&(gpio_reg[addr]));
		} else {
			printf("GPIO device read error: address 0x%04x out of bounds\n",addr);
			return 0;
		}
	}

	void FireUpdate(GPIOPort port, uint8_t pin) {
		GPIOState newState = GetGPIOState(port, pin);
		for(auto iter = listeners[port][pin].begin(); iter != listeners[port][pin].end(); ++iter) {
			(*iter)(port, pin, newState);
		}
	}

	void GPIODeviceWriteHandler(uint16_t addr, uint32_t data) {
		if(addr < GPIORegCount) {
			//process each byte individually
			for(int i = 0; i < 4; i++) {
				uint8_t oldValue = gpio_reg[addr + i];
				uint8_t newValue = get_bytele(data,i);
				if(!gpio_reg_readonly[addr + i]) {
					gpio_reg[addr + i] = newValue;
					for(int bit = 0; bit < 7; bit++) {
						if(check_bit(oldValue,bit) != check_bit(newValue,bit)){
							if(gpio_reg_bitinfo[addr + i][bit].firesUpdate) {
								FireUpdate(gpio_reg_bitinfo[addr + i][bit].port, gpio_reg_bitinfo[addr + i][bit].pin);
							}
						}
					}
					if(gpio_reg_interrupt[addr + i] && i >= 2) {
						for(int bit = 0; bit < 7; bit++) {
							if(check_bit(newValue,bit) == 1){
								SetIRQState(GPIOInterrupt,false);
								clear_bit(gpio_reg[addr + i], bit);
							}
						}
					}
				}
			}
		} else {
			printf("GPIO device write error: address 0x%04x out of bounds\n",addr);
		}
	}

	void FireInterrupt(GPIOPort port, uint8_t intno, GPIOInterruptType type) {
		if(PortInfo[port].HasInterrupts) {
			uint8_t offset = 0;
			if(type == GPIO_FALLING) {
				offset = 1;
			}
			if(check_bit(gpio_reg[PortInfo[port].InputInterruptReg+offset],intno)) {
				set_bit(gpio_reg[PortInfo[port].InputInterruptReg+offset+2],intno);
				SetIRQState(GPIOInterrupt,true);
			}
		} else {
			printf("Port %c does not have an interrupt capability.\n",PortInfo[port].PortLetter);
		}
	}
	const Peripheral GPIOPeripheral = {
			"GPIO",
			InitGPIODevice,
			GPIODeviceReadHandler,
			GPIODeviceWriteHandler
	};
}
