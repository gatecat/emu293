#pragma once
#include "helper.h"
#include "system.h"
#include <cstdio>
#include <cstring>
#include <unordered_map>
using namespace std;
namespace Emu293 {
	//Loads ELF file to RAM, and returns entry point
	uint32_t LoadElfToRAM(const char *filename);
	extern std::unordered_map<std::string, uint32_t> symbols_fwd;
	extern std::unordered_map<uint32_t, std::string> symbols_bwd;

}
