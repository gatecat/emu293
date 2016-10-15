#include "blndma.h"
#include "../helper.h"
#include "../sys/irq_if.h"
#include "../system.h"

// Using SDL threads due to issues with mingw and C++11 threading
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#include <cstdio>
#include <functional>
#include <vector>
using namespace std;

namespace Emu293 {
const int blndma_srca_addr = 0;
const int blndma_srcb_addr = 1;
const int blndma_dest_addr = 2;
const int blndma_width_height = 3;
const int blndma_fill_pat = 4;
const int blndma_ctrl_1 = 5;

const int blndma_ctrl1_opmode = 0;
const int blndma_ctrl1_blendmd = 8;
const int blndma_ctrl1_filter = 16;
const int blndma_ctrl1_start = 24;

const int blndma_irq_ctrl = 6;

const int blndma_irq_status = 8;
const int blndma_irq_int_en = 16;
const int blndma_irq_int_clr = 24;

const int blndma_blend_factor = 7;
const int blndma_transparent = 8;
const int blndma_addr_mode = 9;

const int blndma_addrmd_a = 0;
const int blndma_addrmd_b = 8;
const int blndma_addrmd_d = 16;

const int blndma_ctrl_2 = 10;

const int blndma_ctrl2_alpha = 0;
const int blndma_ctrl2_clrmd = 8;

const int blndma_abase_addr = 12;
const int blndma_aoffset_xy = 13;
const int blndma_a_bg = 14;

const int blndma_bbase_addr = 16;
const int blndma_boffset_xy = 17;
const int blndma_b_bg = 18;

const int blndma_dbase_addr = 20;
const int blndma_doffset_xy = 21;
const int blndma_d_bg = 22;

const int blndma_regcount = 23;

const int blndma_interrupt = 34;

// Lookup table for 3 bit size values
const uint16_t blndma_width_vals[] = {256, 320, 512, 640, 1024, 2048};
const uint16_t blndma_height_vals[] = {240, 256, 480, 512, 1024, 2048};

static SDL_Thread *blndma_thread = nullptr;
static SDL_cond *blndma_cvar = nullptr;
static SDL_mutex *blndma_mutex = nullptr;
static bool blndma_workAvailable = false;

static uint32_t blndma_regs[blndma_regcount] = {0};

static uint8_t *memptr;

struct AddrInfo {
  bool blockmode;
  // Block (blockmode = true)
  uint32_t base;
  uint16_t height;
  uint16_t width;
  uint16_t offx;
  uint16_t offy;
  // Linear (blockmode = false)
  uint32_t start;
  // Blending
  uint8_t blndFactor;
};

struct TransferInfo {
  uint16_t height;
  uint16_t width;
  bool enableColourKey;
  uint16_t colourKey;
  enum TransferMode {
    Idle = 0,
    CopyAtoDest = 1,
    BlendAandB = 2,
    FillDest = 3,
  };
  TransferMode mode;
  enum BlendMode { Blend_Add = 0, Blend_Sub = 1 };
  BlendMode blndMode;
  enum ColourSpace { RGB565 = 0, ARGB1555 = 1 };
  ColourSpace colourSpace;
  bool enableAlphaChannel;
  uint16_t fillPtrn;
};

static TransferInfo currentTransfer;

static AddrInfo srcA, srcB, dest;

static inline int GetAddress(const AddrInfo &addr, uint16_t x, uint16_t y) {
  if (addr.blockmode) {
    if ((x + addr.offx) >= addr.width)
      return -1;
    if ((y + addr.offy) >= addr.height)
      return -1;

    return addr.base + (addr.width * (y + addr.offy)) + (x + addr.offx);
  } else {
    return addr.start + (currentTransfer.width * y) + x;
  }
}

static void SetBlockInfo(AddrInfo &addr, int base) {
  addr.base = blndma_regs[base] & 0x0FFFFFFF;
  addr.offx = blndma_regs[base + 1] & 0x7FF;
  addr.offy = (blndma_regs[base + 1] >> 16) & 0x7FF;
  addr.width = blndma_width_vals[blndma_regs[base + 2] & 0x7];
  addr.height = blndma_height_vals[(blndma_regs[base + 2] >> 8) & 0x7];
}

int BLNDMA_Thread(void *data) {
  while (true) {
    SDL_LockMutex(blndma_mutex);
    while (!blndma_workAvailable) {
      SDL_CondWait(blndma_cvar, blndma_mutex);
    }
    SDL_UnlockMutex(blndma_mutex);

    if (!check_bit(blndma_regs[blndma_ctrl_1], blndma_ctrl1_start)) {
      blndma_workAvailable = false;
      continue;
    }

    switch (currentTransfer.mode) {
    case TransferInfo::Idle:
      break;
    case TransferInfo::CopyAtoDest: {
      for (uint16_t y = 0; y < currentTransfer.height; y++) {
        for (uint16_t x = 0; x < currentTransfer.width; x++) {
          if (!check_bit(blndma_regs[blndma_ctrl_1], blndma_ctrl1_start)) {
            blndma_workAvailable = false;
            continue;
          }
          // TODO: YUV2RGB conversion, descrambling
          int addrA = GetAddress(srcA, x, y);
          uint16_t val;
          if (addrA == -1)
            val = 0;
          else
            val = get_uint16le(memptr + GetAddress(srcA, x, y));

          if ((currentTransfer.enableColourKey) &&
              (val == currentTransfer.colourKey))
            continue;
          if (currentTransfer.colourSpace == TransferInfo::ARGB1555) {
            // Might also need to convert 1555 to 565
            if ((currentTransfer.enableAlphaChannel) && (check_bit(val, 15)))
              continue;
          }
          int addrD = GetAddress(dest, x, y);
          if (addrD == -1)
            continue;
          set_uint16le(memptr + addrD, val);
        }
      }
    } break;
    case TransferInfo::BlendAandB: {
      for (uint16_t y = 0; y < currentTransfer.height; y++) {
        for (uint16_t x = 0; x < currentTransfer.width; x++) {
          if (!check_bit(blndma_regs[blndma_ctrl_1], blndma_ctrl1_start)) {
            blndma_workAvailable = false;
            continue;
          }

          uint16_t valA;
          uint16_t valB;

          int addrA = GetAddress(srcA, x, y);
          int addrB = GetAddress(srcB, x, y);
          if (addrA == -1)
            valA = 0;
          else
            valA = get_uint16le(memptr + addrA);
          if (addrB == -1)
            valB = 0;
          else
            valB = get_uint16le(memptr + addrB);

          uint8_t rA, rB, bA, bB, gA, gB;

          if (currentTransfer.colourSpace == TransferInfo::ARGB1555) {

            if ((currentTransfer.enableAlphaChannel) && (check_bit(valA, 15))) {
              bA = 0;
              gA = 0;
              rA = 0;
            } else {
              bA = valA & 0x1F;
              gA = (valA >> 5) & 0x1F;
              rA = (valA >> 10) & 0x1F;
            }

            if ((currentTransfer.enableAlphaChannel) && (check_bit(valB, 15))) {
              bB = 0;
              gB = 0;
              rB = 0;
            } else {
              bB = valB & 0x1F;
              gB = (valB >> 5) & 0x1F;
              rB = (valB >> 10) & 0x1F;
            }
          } else {
            bA = valA & 0x1F;
            gA = (valA >> 5) & 0x3F;
            rA = (valA >> 11) & 0x1F;
            bB = valB & 0x1F;
            gB = (valB >> 5) & 0x3F;
            rB = (valB >> 11) & 0x1F;
          }

          uint8_t r, g, b;
          uint16_t aA = srcA.blndFactor, aB = srcB.blndFactor;
          if (currentTransfer.blndMode == TransferInfo::Blend_Sub) {
            r = (((rA * aA) - (rB * aB)) >> 6) & 0x1F;
            g = (((gA * aA) - (gB * aB)) >> 6) & 0x3F;
            b = (((bA * aA) - (bB * aB)) >> 6) & 0x1F;
          } else {
            r = (((rA * aA) + (rB * aB)) >> 6) & 0x1F;
            g = (((gA * aA) + (gB * aB)) >> 6) & 0x3F;
            b = (((bA * aA) + (bB * aB)) >> 6) & 0x1F;
          }
          uint16_t val = 0;
          if (currentTransfer.colourSpace == TransferInfo::ARGB1555) {
            val = b | (g << 5) | (r << 10);
          } else {
            val = b | (g << 5) | (r << 11);
          }

          int addrD = GetAddress(dest, x, y);
          if (addrD == -1)
            continue;
          set_uint16le(memptr + addrD, val);
        }
      }
    } break;
    case TransferInfo::FillDest: {
      if (!check_bit(blndma_regs[blndma_ctrl_1], blndma_ctrl1_start)) {
        blndma_workAvailable = false;
        continue;
      }
      uint16_t val = currentTransfer.fillPtrn;
      if ((currentTransfer.enableColourKey) &&
          (val == currentTransfer.colourKey))
        continue;
      if (currentTransfer.colourSpace == TransferInfo::ARGB1555) {
        if ((currentTransfer.enableAlphaChannel) && (check_bit(val, 15)))
          continue;
      }
      for (uint16_t y = 0; y < currentTransfer.height; y++) {
        for (uint16_t x = 0; x < currentTransfer.width; x++) {
          if (!check_bit(blndma_regs[blndma_ctrl_1], blndma_ctrl1_start)) {
            blndma_workAvailable = false;
            continue;
          }
          int addrD = GetAddress(dest, x, y);
          if (addrD == -1)
            continue;
          set_uint16le(memptr + addrD, val);
        }
      }
    } break;
    }
    clear_bit(blndma_regs[blndma_ctrl_1], blndma_ctrl1_start);
    set_bit(blndma_regs[blndma_irq_ctrl], blndma_irq_status);
    if (check_bit(blndma_regs[blndma_irq_ctrl], blndma_irq_int_en)) {
      SetIRQState(blndma_interrupt, true);
    }
    blndma_workAvailable = false;
  }
  return 0;
}

void InitBLNDMAThreads() {
  blndma_mutex = SDL_CreateMutex();
  blndma_cvar = SDL_CreateCond();
  blndma_thread = SDL_CreateThread(BLNDMA_Thread, "BLNDMA", nullptr);
}

void InitBLNDMADevice(PeripheralInitInfo initInfo) {
  memptr = get_dma_ptr(0xA0000000);
}

uint32_t BLNDMAADeviceReadHandler(uint16_t addr) {
  if ((addr / 4) < blndma_regcount) {
    return blndma_regs[addr / 4];
  } else {
    printf("BLNDMA read error: address 0x%04x out of range\n", addr);
    return 0;
  }
}

void BLNDMADeviceWriteHandler(uint16_t addr, uint32_t val) {
  addr /= 4;
  if (addr == blndma_irq_ctrl) {
    if (check_bit(val, blndma_irq_int_clr)) {
      SetIRQState(blndma_interrupt, false);
      clear_bit(blndma_regs[blndma_irq_ctrl], blndma_irq_status);
    }

    if (check_bit(val, blndma_irq_int_en)) {
      set_bit(blndma_regs[blndma_irq_ctrl], blndma_irq_int_en);
    } else {
      clear_bit(blndma_regs[blndma_irq_ctrl], blndma_irq_int_en);
    }
  } else if (addr < blndma_regcount) {
    blndma_regs[addr] = val;
    if (addr == blndma_ctrl_1) {
      if (check_bit(val, blndma_ctrl1_start)) {
        currentTransfer.width = blndma_regs[blndma_width_height] & 0x7FF;
        currentTransfer.height = blndma_regs[blndma_width_height] >> 16;
        currentTransfer.mode =
            TransferInfo::TransferMode(blndma_regs[blndma_ctrl_1] & 0x03);
        currentTransfer.blndMode = (TransferInfo::BlendMode)check_bit(
            blndma_regs[blndma_ctrl_1], blndma_ctrl1_blendmd);
        currentTransfer.enableColourKey =
            check_bit(blndma_regs[blndma_ctrl_1], blndma_ctrl1_filter);
        currentTransfer.enableAlphaChannel =
            check_bit(blndma_regs[blndma_ctrl_2], blndma_ctrl2_alpha);
        currentTransfer.colourSpace = (TransferInfo::ColourSpace)check_bit(
            blndma_regs[blndma_ctrl_2], blndma_ctrl2_clrmd);
        currentTransfer.colourKey = blndma_regs[blndma_transparent];
        currentTransfer.fillPtrn = blndma_regs[blndma_fill_pat];

        srcA.start = blndma_regs[blndma_srca_addr] & 0x0FFFFFFF;
        srcB.start = blndma_regs[blndma_srcb_addr] & 0x0FFFFFFF;
        dest.start = blndma_regs[blndma_dest_addr] & 0x0FFFFFFF;

        srcA.blndFactor = blndma_regs[blndma_blend_factor] & 0x3F;
        srcB.blndFactor = (blndma_regs[blndma_blend_factor] >> 8) & 0x3F;

        srcA.blockmode =
            check_bit(blndma_regs[blndma_addr_mode], blndma_addrmd_a);
        srcB.blockmode =
            check_bit(blndma_regs[blndma_addr_mode], blndma_addrmd_b);
        dest.blockmode =
            check_bit(blndma_regs[blndma_addr_mode], blndma_addrmd_d);

        SetBlockInfo(srcA, blndma_abase_addr);
        SetBlockInfo(srcB, blndma_abase_addr);
        SetBlockInfo(dest, blndma_abase_addr);

        blndma_workAvailable = true;
      }
    }
  } else {
    printf("BLNDMA write error: address 0x%04x out of range, dat=0x%08x\n",
           addr * 4, val);
  }
}
}
