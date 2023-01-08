#include "cpu/cpu.h"
#include "dma/apbdma.h"
#include "dma/blndma.h"
#include "loadelf.h"
#include "stor/sdcard.h"
#include "sys/timer.h"
#include "video/ppu.h"
#include "video/tve.h"

#include "system.h"
#include <SDL2/SDL.h>
#include <ctime>
#include <chrono>

using namespace Emu293;
int main(int argc, char *argv[]) {
  SDL_Init(SDL_INIT_EVERYTHING);
#if 1
  uint32_t entryPoint;
  entryPoint = LoadElfToRAM("../roms/jg7425/Lead.sys");
  if (entryPoint == 0) {
    printf("Failed to load ELF\n");
    return 1;
  }
  printf("Loaded ELF to RAM (ep=0x%08x)!\n", entryPoint);
  InitAPBDMAThreads();
  InitBLNDMAThread();
  InitPPUThreads();
  SD_InitCard(/*"../roms/jg7425/test.img"*/ "../../re/zone3d/sd_card.img");
  // write_memU32(0xa0e00400, 0x1); // Leadsysfileflag
#else
  uint32_t entryPoint;

  FILE *romFile;
  romFile = fopen("/mnt/data/spg293/zone3d.u14","rb");
  if(!romFile) {
    printf("Failed to open ROM file\n");
    return 0;
  }
  uint32_t addr = 0x9F000000;
  unsigned char c;
  while (!feof(romFile)){
    c = fgetc(romFile);
    write_memU8(addr++, c);
  }
  fclose(romFile); 

  entryPoint = 0x9F000000;
  SD_InitCard("/mnt/data/spg293/zone3d.img");
#endif
  CPU scoreCPU;

  scoreCPU.reset();

  //	scoreCPU.cr29 = 0x20000000;

  scoreCPU.pc = entryPoint;

  system_init(&scoreCPU);
  write_memU32(0xFFFFFFEC, 1);
  int icount = 0;
  auto start = std::chrono::steady_clock::now();
  while (1) {
    scoreCPU.step();
    icount++;

    if ((icount % 10000) == 0) {
      TimerTick();

      // SDL_Delay(1);
    }

    if ((icount % 10000) == 5000) {
      PPUTick();

      // SDL_Delay(1);
    }

    if ((icount % 10000) == 7000) {
      TVETick();

      // SDL_Delay(1);
    }

    fflush(stdout);

    auto t = std::chrono::steady_clock::now();
    auto delta = std::chrono::duration<float>(t - start).count();
    if (delta > 1) {
      printf("%.02fMIPS\n", (icount / 1000000.0) /
                                delta);
      icount = 0;
      start = t;
      printf("PC=0x%08x\n", scoreCPU.pc);
    }

    // SDL_Delay(1);
  }
  SDL_Quit();
  return 0;
}
