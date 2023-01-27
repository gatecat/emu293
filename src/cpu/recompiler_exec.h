#pragma once
#include "recompiler_il.h"
#include <unordered_map>

namespace Emu293 {
	struct RecompilerState {
		// register file
		uint32_t regs[128];
		// pointers to memory read functions
		void (*memwr8)(uint32_t, uint8_t);
		void (*memwr16)(uint32_t, uint16_t);
		void (*memwr32)(uint32_t, uint32_t);
		uint8_t (*memrd8)(uint32_t);
		uint16_t (*memrd16)(uint32_t);
		uint32_t (*memrd32)(uint32_t);
	}  __attribute__((packed));
	struct RecompilerCache {
		RecompilerCache();
		static constexpr unsigned cache_size=64*1024*1024;
		uint8_t *code;
		std::vector<bool> dirty;
		std::unordered_map<uint32_t, uint32_t> chunks; // crude heap; offset->size
		// this full PC->dynarec map is super annoying and expensive
		// we can probably get rid of it with better analysis of function calls etc
		std::unordered_map<uint32_t, uint32_t> pc2recomp;
		uint32_t alloc_chunk(uint32_t size);
		void free_chunk(uint32_t offset);
		typedef void (*dynarec_func_t)(RecompilerState *state);
		uint32_t yeet_to(uint32_t pc, RecompilerState &state);
		~RecompilerCache();
	};
};