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

using namespace Emu293;
int main(int argc, char *argv[]) {
  SDL_Init(SDL_INIT_EVERYTHING);
#if 0
  uint32_t entryPoint;
  entryPoint = LoadElfToRAM("/mnt/data/spg293/a23/rom.elf");
  if (entryPoint == 0) {
    printf("Failed to load ELF\n");
    return 1;
  }
  printf("Loaded ELF to RAM (ep=0x%08x)!\n", entryPoint);
  InitAPBDMAThreads();
  InitBLNDMAThread();
  InitPPUThreads();
  SD_InitCard("/mnt/data/spg293/sd3.img");
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

  entryPoint = 0x9F000E30;
  InitAPBDMAThreads();
  InitBLNDMAThread();
  InitPPUThreads();
  SD_InitCard("/mnt/data/spg293/zone3d.img");
#endif
  CPU scoreCPU;

  scoreCPU.reset();

  //	scoreCPU.cr29 = 0x20000000;

  scoreCPU.pc = entryPoint;

  system_init(&scoreCPU);
  write_memU32(0xFFFFFFEC, 1);
  int icount = 0;
  clock_t start = clock();
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

    if (((clock() - start) / ((double)CLOCKS_PER_SEC)) >= 1) {
      printf("%.02fMIPS\n", (icount / 1000000.0) /
                                ((clock() - start) / ((double)CLOCKS_PER_SEC)));
      icount = 0;
      start = clock();
      printf("PC=0x%08x\n", scoreCPU.pc);
    }

    // SDL_Delay(1);
  }
  SDL_Quit();
  return 0;
}
