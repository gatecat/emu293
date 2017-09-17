//Basic implementation of the SPG293 APBDMA
#pragma once
#include "../peripheral.h"
#include "../helper.h"
namespace Emu293 {
	extern const Peripheral APBDMAPeripheral;

	//Must only be called once
	void InitAPBDMAThreads();

	//DMA hooks enable fast peripheral read/writes
	//Buf is a byte buffer. startAddr is start address relative to peripheral hook start
	typedef void (*DMAHandler)(uint32_t startAddr, uint32_t count, volatile uint8_t *buf);

	enum DMAHookFlags {
		DMA_DIR_WRITE 	= 1, //allows reads
		DMA_DIR_READ    = 2, //allows writes
		DMA_MODE_CONT	= 4, //allows continuous addressing
		DMA_MODE_REGUL	= 8, //allows fixed addressing
	};

	struct DMAHook {
		uint32_t StartAddr;
		uint32_t RegionSize;
		DMAHookFlags Flags;
		DMAHandler ContinuousReadHandler;
		DMAHandler ContinuousWriteHandler;
		DMAHandler RegularReadHandler;
		DMAHandler RegularWriteHandler;
	};

	void RegisterDMAHook(DMAHook hook);
}
