//Partial implementation of a GPIO peripheral
//Due to lack of documentation, not perfect yet
//E.g. no special function support
#pragma once
#include "../peripheral.h"
#include "../helper.h"



namespace Emu293 {

	enum GPIOPort {
		GPIO_PORT_A = 0,
		GPIO_PORT_C,
		GPIO_PORT_D,
		GPIO_PORT_E,
		GPIO_PORT_F,
		GPIO_PORT_G,
		GPIO_PORT_I,
		GPIO_PORT_K,
		GPIO_PORT_R,
		GPIO_PORT_S,
	};
	const int GPIOPortCount = 10;
	enum GPIOState {
		GPIO_FLOAT,
		GPIO_LOW,
		GPIO_HIGH,
		GPIO_PULLUP,
		GPIO_PULLDOWN
	};

	enum GPIOInterruptType {
		GPIO_RISING,
		GPIO_FALLING
	};

	extern const Peripheral GPIOPeripheral;
	typedef	void (*GPIOStatusListener) (GPIOPort port, uint8_t pin, GPIOState newState);

	//External interface, so that other peripherals can monitor and manipulate GPIOs
	void AttachGPIOListener(GPIOPort port, uint8_t pin, GPIOStatusListener listener);
	void SetGPIOInputState (GPIOPort port, uint8_t pin, bool newState);
	GPIOState GetGPIOState (GPIOPort port, uint8_t pin);
	//Due to missing info on interrupt trigger mechanism (particularly how interrupt numbers map to pins), interrupts must be fired manually
	void FireInterrupt(GPIOPort port, uint8_t intno, GPIOInterruptType type);
}
