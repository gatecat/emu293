#pragma once
#include "helper.h"
#include "system.h"
#include <cstdio>
#include <cstring>
using namespace std;
namespace Emu293 {
	//Loads ELF file to RAM, and returns entry point
	uint32_t LoadElfToRAM(const char *filename);

}
