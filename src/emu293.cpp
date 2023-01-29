#include "cpu/cpu.h"
#include "dma/apbdma.h"
#include "dma/blndma.h"
#include "loadelf.h"
#include "stor/sdcard.h"
#include "sys/timer.h"
#include "video/ppu.h"
#include "video/tve.h"
#include "io/ir_gamepad.h"
#include "audio/spu.h"

#include "system.h"
#include <SDL2/SDL.h>
#include <ctime>
#include <chrono>

using namespace Emu293;
int main(int argc, char *argv[]) {
  SDL_Init(SDL_INIT_EVERYTHING);

  if (argc < 3) {
    printf("Usage: ./emu293 lead.sys sdcard.img\n");
    return 1;
  }

  uint32_t entryPoint;
  entryPoint = LoadElfToRAM(argv[1]);
  if (entryPoint == 0) {
    printf("Failed to load ELF\n");
    return 1;
  }
  printf("Loaded ELF to RAM (ep=0x%08x)!\n", entryPoint);
  InitAPBDMAThreads();
  InitBLNDMAThread();
  InitPPUThreads();
  SPUInitSound();
  if (!SD_InitCard(argv[2])) {
    printf("Failed to load SD card image\n");
  }

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

    if ((icount % 4) == 0) {
      TimerTick(false); // main PCLK/2
      // SDL_Delay(1);
    }

    if ((icount % 320) == 0) {
      IRGamepadTick();
    }

    if ((icount % 200) == 0) {
      SPUUpdate();
    }

    if ((icount % 5000) == 0) {
      TimerTick(true); // 32kHz
      // SDL_Delay(1);
    }


    if ((icount % 2000) == 1000) {
      PPUTick();

      // SDL_Delay(1);
    }

    if ((icount % 2000) == 1500) {
      TVETick();

      // SDL_Delay(1);
    }

    if ((icount % 100) == 0) {
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
      if (softreset_flag) {
        system_softreset();
        entryPoint = LoadElfToRAM(argv[1]);
        scoreCPU.pc = entryPoint;
        softreset_flag = false;
      }
    }
    // SDL_Delay(1);
  }
  SDL_Quit();
  return 0;
}
