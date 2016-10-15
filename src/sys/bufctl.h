#pragma once
#include "../peripheral.h"
#include "../helper.h"

namespace Emu293 {
	extern const Peripheral BUFCTLPeripheral;

	//Interface for PPU
	int BufCtl_GetTextBufferIndex(int textNum);
	int BufCtl_GetPPUBufferIndex();
	void BufCtl_PPUPaintDone();
	//Interface for TVE
	int BufCtl_GetTVEBufferIndex();
}
