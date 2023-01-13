#pragma once
#include "helper.h"
#include "cpu/cpu.h"
using namespace std;
namespace Emu293 {
	void system_init(CPU *cpu);

	uint8_t read_memU8(uint32_t addr);
	void write_memU8(uint32_t addr, uint8_t val);

	uint16_t read_memU16(uint32_t addr);
	void write_memU16(uint32_t addr, uint16_t val);

	uint32_t read_memU32(uint32_t addr);
	void write_memU32(uint32_t addr, uint32_t val);

	//Get a pointer for fast RAM access - returns nullptr if start address invalid
	uint8_t *get_dma_ptr(uint32_t addr);
}
