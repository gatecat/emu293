#include "recompiler_exec.h"
#include <sys/mman.h>
#undef NDEBUG
#include <assert.h>

namespace Emu293 {
	RecompilerCache::RecompilerCache() {
		code = reinterpret_cast<uint8_t*>(mmap(
			nullptr, cache_size,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_ANONYMOUS | MAP_PRIVATE,
			0, 0
		));
		assert(code != nullptr);
		chunks[0] = cache_size; // whole area is free
	}
	uint32_t RecompilerCache::yeet_to(uint32_t pc, RecompilerState &state) {
		state.regs[SCORE_NPC] = pc;
		uint32_t offset = pc2recomp.at(pc);
		dynarec_func_t func = reinterpret_cast<dynarec_func_t>(code+offset);
		func(&state);
		return state.regs[SCORE_CYC];
	}
	static inline bool chunk_used(uint32_t state) {
		return state & (1U << 31);
	}
	static inline uint32_t chunk_size(uint32_t state) {
		return state & ~(1U << 31);
	}
	uint32_t RecompilerCache::alloc_chunk(uint32_t size) {
		// evict something that wasn't properly purged?
		auto iter = chunks.begin();
		while (iter != chunks.end()) {
			uint32_t chunk_state = iter->second;
			if (!chunk_used(chunk_state) && (chunk_size(chunk_state) < size)) {
				break;
			}
			++iter;
		}
		if (iter == chunks.end()) {
			// TODO: evict some other function we don't like
			printf("failed to allocate dynarec chunk of size %d\n", size);
			assert(false);
		}
		uint32_t start = iter->first;
		uint32_t remain = chunk_size(iter->second) - size;
		if (remain > 0) {
			// new chunk with what's remaining
			chunks[start+size] = remain;
		}
		// mark as in use
		chunks[start] = (1U<<31) | size;
		return start;
	}
	void RecompilerCache::free_chunk(uint32_t offset) {
		uint32_t this_state = chunks.at(offset);
		assert(chunk_used(this_state));
		uint32_t this_size = chunk_size(this_state);
		uint32_t next = offset + this_size;
		if (chunks.count(next) && !chunk_used(chunks.at(next))) {
			// coalesce with next free
			uint32_t next_size = chunk_size(chunks.at(next));
			this_size += next_size;
			chunks.erase(next);
		}
		chunks[offset] = this_size;
	}
	RecompilerCache::~RecompilerCache() {
		munmap(reinterpret_cast<void*>(code), cache_size);
	}
};