#include "cpu/cpu.h"
#include "dma/apbdma.h"
#include "dma/blndma.h"
#include "loadelf.h"
#include "stor/sdcard.h"
#include "sys/timer.h"
#include "video/ppu.h"
#include "video/tve.h"
#include "video/webcam.h"
#include "video/csi.h"

#include "io/ir_gamepad.h"
#include "audio/spu.h"

#include "system.h"
#include <SDL2/SDL.h>
#include <ctime>
#include <chrono>

using namespace Emu293;

static std::string state_file(int slot) {
  return stringf("../roms/slot_%d.sav", slot);
}

int main(int argc, char *argv[]) {
  SDL_Init(SDL_INIT_EVERYTHING);

  if (argc < 3) {
    printf("Usage: ./emu293 lead.sys sdcard.img\n");
    return 1;
  }

  int argidx = 1;
  std::string webcam_dev;

  while (true) {
      if (strcmp(argv[argidx], "-cam") == 0) {
        argidx++;
        webcam_dev = std::string(argv[argidx++]);
      } else if (strcmp(argv[argidx], "-scale") == 0) {
        argidx++;
        video_scale = std::atoi(argv[argidx++]);
        if (video_scale < 1 || video_scale > 4) {
          printf("Vidoe scale must be between 1 and 4.\n");
          return 1;
        }
      } else {
        break;
      }
  }

  uint32_t entryPoint;
  const char *elf = argv[argidx++];
  entryPoint = LoadElfToRAM(elf);
  if (entryPoint == 0) {
    printf("Failed to load ELF\n");
    return 1;
  }
  printf("Loaded ELF to RAM (ep=0x%08x)!\n", entryPoint);
  InitPPUThreads();
  SPUInitSound();
  InitCSIThreads();
  if (!SD_InitCard(argv[argidx++])) {
    printf("Failed to load SD card image\n");
  }

  if (!webcam_dev.empty()) {
    webcam_init(webcam_dev);
  }

  CPU scoreCPU;

  scoreCPU.reset();

  //	scoreCPU.cr29 = 0x20000000;

  scoreCPU.pc = entryPoint;

  system_init(&scoreCPU);
  write_memU32(0xFFFFFFEC, 1);
  int icount = 0;
  auto start = std::chrono::steady_clock::now();
  int64_t t32k_rate = 1000000000/32768;
  int64_t t32k_next = std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count()
    + t32k_rate;

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
      int64_t t_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
      auto delta = std::chrono::duration<float>(t - start).count();

      if (t_ns >= t32k_next) {
        TimerTick(true); // 32kHz
        t32k_next = std::max(t_ns+t32k_rate/2, t32k_next+t32k_rate);
      }

      if (delta > 1) {
        printf("%.02fMIPS\n", (icount / 1000000.0) /
                                  delta);
        icount = 0;
        start = t;
        printf("PC=0x%08x\n", scoreCPU.pc);
      }
      if (softreset_flag) {
        system_softreset();
        entryPoint = LoadElfToRAM(elf);
        scoreCPU.pc = entryPoint;
        softreset_flag = false;
      }
      if (shutdown_flag) {
        break;
      }
      if (savestate_flag != -1 || loadstate_flag != -1) {
        bool is_save = (savestate_flag != -1);
        auto file = state_file(is_save ? savestate_flag : loadstate_flag);
        SaveStater ss; 
        if(is_save ? ss.begin_save(file) : ss.begin_load(file)) {
          ss.i(icount);
          system_state(ss);
          ss.finalise();
          printf("%s state %s slot %d\n", (is_save ? "Saved" : "Loaded"), (is_save ? "to" : "from"),
            is_save ? savestate_flag : loadstate_flag);
        }
        savestate_flag = -1;
        loadstate_flag = -1;
      }
    }
    // SDL_Delay(1);
  }
  ShutdownCSI();
  webcam_stop();
  SDL_Quit();
  return 0;
}
