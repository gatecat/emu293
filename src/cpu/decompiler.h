#pragma once
#include "recompiler_il.h"

namespace Emu293 {
	ScoreChunk do_decompile(uint32_t start_pc, const uint8_t *progmem, uint32_t progmem_mask = 0x01FFFFFC);
}