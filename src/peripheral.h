#pragma once
#include "helper.h"
#include "cpu/cpu.h"
namespace Emu293 {
//Info passed to peripheral when initialising
struct PeripheralInitInfo {
	uint32_t baseAddress;
	CPU *currentCPU;
};

//Generic peripheral device
typedef void (*InitHandler)(PeripheralInitInfo initInfo);
typedef uint32_t (*ReadHandler)(uint16_t addr);
typedef void (*WriteHandler)(uint16_t addr, uint32_t val);
typedef void (*ResetHandler)();
typedef void (*StateHandler)(SaveStater &s);

struct Peripheral {
	const char *name;
	InitHandler initPeriph;
	ReadHandler regRead;
	WriteHandler regWrite;
	ResetHandler reset;
	StateHandler state;
};

}
