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
#include <vector>
#include <functional>

#ifdef WIN32
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

#include <wx/msgdlg.h>
#include <wx/wx.h>


using namespace Emu293;



namespace {
std::string webcam_dev;
std::string elf_file;
std::string sd_card;
std::string save_dir = "../roms";

bool nor_boot, igame_boot;

void null_configure() {};
void zone3d_configure() { zone3d_pad_mode = true; }
void a21_configure() { webcam_dev = "/dev/video0"; }

struct PlatformPreset {
  std::string id;
  std::string name;
  std::string sd_card;
  std::string elf;
  std::string nor;
  std::function<void()> post_configure;
};
static const std::vector<PlatformPreset> platforms = {
  {"lx_jg7425", "Lexibook JG7425 221-in-1", "lexibook_jg7425_4gbsd.img", "lead.sys", "mx29lv160.u6", null_configure},
  {"lx_aven", "Lexibook Avengers 200-in-1", "sd-card.img", "lead.sys", "29lv800.bin", null_configure},
  {"sb_a21", "Subor A21", "sb_a21.img", "rom.elf", "", a21_configure},
  {"zone3d", "Zone Interactive 3D", "zone3d.img", "", "zone_25l8006e_c22014.bin", zone3d_configure},
};


class LoadUI : public wxApp {
  bool OnInit() {
    while (true ){
      int index = display_plat_picker();
      auto &platform = platforms.at(index);
      std::string romdir = "./roms";
      if (access(romdir.c_str(), F_OK) != 0) {
        romdir = "../roms";
        if (access(romdir.c_str(), F_OK) != 0) {
          wxMessageBox("Failed to locate ROM directory (tried ./roms and ../roms)", "ROM directory not found", wxICON_ERROR);
          exit(1);
        }
      }
      romdir += '/';
      romdir += platform.id;
      printf("ROM directory: %s\n", romdir.c_str());
      save_dir = romdir;
      sd_card = stringf("%s/%s", romdir.c_str(), platform.sd_card.c_str());
      if (access(sd_card.c_str(), F_OK) != 0) {
        std::string msg = stringf("Failed to locate SD card image '%s'.\nPlease refer to README.md in the relevant ROM folder.", sd_card.c_str());
        wxMessageBox(msg, "SD card image not found.", wxICON_ERROR);
        continue;
      }
      if (platform.elf != "") {
        elf_file = stringf("%s/%s", romdir.c_str(), platform.elf.c_str());
        if (access(elf_file.c_str(), F_OK) != 0) {
          elf_file = "";
        } else {
          nor_boot = false;
        }
      }

      if (elf_file == "" && platform.nor != "") {
        elf_file = stringf("%s/%s", romdir.c_str(), platform.nor.c_str());
        if (access(elf_file.c_str(), F_OK) != 0) {
          elf_file = "";
        } else {
          nor_boot = true;
        }
      }
      if (elf_file == "") {
        wxMessageBox("Neither a rom.elf file nor a NOR flash image was found.\nPlease refer to README.md in the relevant ROM folder.", "Boot image not found.", wxICON_ERROR);
        continue;
      }
      platform.post_configure();
      break;
    }
    return false;
  }

  int display_plat_picker() {
    wxArrayString choices;
    for (auto &plat : platforms)
      choices.Add(plat.name);
    wxSingleChoiceDialog dialog(nullptr, "Please select a platform to emulate",
                                     "Platforms", choices);
    dialog.SetSize(wxSize(600, 400));
    if (dialog.ShowModal() == wxID_CANCEL)
      exit(2);
    int sel = dialog.GetSelection();
    if (sel == -1)
      exit(2);
    return sel;
  };
};

wxIMPLEMENT_APP_NO_MAIN(LoadUI);

void show_load_ui(int argc, char *argv[]) {
  wxApp *app = new LoadUI();
  wxApp::SetInstance(app);
  wxEntry(argc, argv);
  while (app->IsMainLoopRunning()) {
    app->MainLoop();
  }
}

std::string state_file(int slot) {
  return stringf("%s/slot_%d.sav", save_dir.c_str(), slot);
}

}


int main(int argc, char *argv[]) {
  SDL_Init(SDL_INIT_EVERYTHING);

  if (argc == 1) {
    // TODO: show selector GUI
    show_load_ui(argc, argv);
  } else {

    int argidx = 1;

    while (true) {
        if (argidx >= argc) {
          goto usage;
        } else if (strcmp(argv[argidx], "-cam") == 0) {
          argidx++;
          webcam_dev = std::string(argv[argidx++]);
        } else if (strcmp(argv[argidx], "-scale") == 0) {
          argidx++;
          video_scale = std::atoi(argv[argidx++]);
          if (video_scale < 1 || video_scale > 4) {
            printf("Video scale must be between 1 and 4.\n");
            return 1;
          }
        } else if (strcmp(argv[argidx], "-nor") == 0) {
          argidx++;
          nor_boot = true;
        } else if (strcmp(argv[argidx], "-spudebug") == 0) {
          argidx++;
          spu_debug_flag = true;
        } else if (strcmp(argv[argidx], "-zone3d") == 0) {
          argidx++;
          zone3d_pad_mode = true;
        } else if (strcmp(argv[argidx], "-igame") == 0) {
          argidx++;
          igame_boot = true;
        } else if (*(argv[argidx]) != '-') {
          break;
        } else {
          goto usage;
        }
    }
    if ((argidx + 2) != argc) {
      goto usage;
    }
    elf_file = argv[argidx++];
    sd_card = argv[argidx++];

    if (false) {
usage:
      printf("Usage: ./emu293 [-cam /dev/videoN] [-scale {1,2,3,4}] [-zone3d] [-igame] [-nor] lead.sys sdcard.img\n");
      return 2;
    }

  }

  uint32_t entryPoint, stackAddr;
  InitPPUThreads();
  SPUInitSound();
  InitCSIThreads();
  if (!SD_InitCard(sd_card.c_str())) {
    printf("Failed to load SD card image\n");
  }

  if (!webcam_dev.empty()) {
    webcam_init(webcam_dev);
  }

  CPU scoreCPU;

  scoreCPU.reset();

  //	scoreCPU.cr29 = 0x20000000;
  auto do_load_image = [&]() {
    if (igame_boot) {
      if (!LoadIGameToRAM(elf_file.c_str(), entryPoint)) {
        printf("Failed to load iGame binary\n");
        exit(1);
      }
    } else if (nor_boot) {
      if (!LoadNORToRAM(elf_file.c_str(), entryPoint, stackAddr)) {
        printf("Failed to load NOR\n");
        exit(1);
      }
      scoreCPU.r0 = stackAddr;
    } else {
      entryPoint = LoadElfToRAM(elf_file.c_str());
      if (entryPoint == 0) {
        printf("Failed to load ELF\n");
        exit(1);
      }
      printf("Loaded ELF to RAM (ep=0x%08x)!\n", entryPoint);
    }
    scoreCPU.pc = entryPoint;
  };
  do_load_image();

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
        do_load_image();
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
  ShutdownSPU();
  webcam_stop();
  SDL_Quit();
  return 0;
}
