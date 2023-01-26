#include "apbdma.h"
#include "../helper.h"
#include "../sys/irq_if.h"
#include "../system.h"

// Using SDL threads due to issues with mingw and C++11 threading
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#include <cstdio>
#include <functional>
#include <vector>
#include <atomic>
#include <stdexcept>

using namespace std;

namespace Emu293 {
const int dma_nCh = 4;

static SDL_Thread *threads[dma_nCh] = {nullptr};
static SDL_cond *condVars[dma_nCh] = {nullptr};
static SDL_mutex *mutexes[dma_nCh] = {nullptr};
static atomic<bool> workAvailable[dma_nCh] = {};
const int dma_nregs = 34;
static atomic<uint32_t> dma_regs[dma_nregs];

static vector<DMAHook> hooks;

// memory access pointer
static uint8_t *memptr;

// global regs - channel is determined by bit number
const int dma_busy_sts = 0x00;
const int dma_irq_sts = 0x01;
const int dma_soft_rst = 0x1F;
// per-channel regs - add channel number (0-3) to address
const int dma_ahb_start_a = 0x02;
const int dma_ahb_end_a = 0x06;
const int dma_apb_start = 0x0A;
const int dma_ahb_start_b = 0x13;
const int dma_ahb_end_b = 0x17;
const int dma_setting = 0x1B;
const int dma_align = 0x21;

// dma_setting bit fields
const int dma_set_dir = 0;
const int dma_set_dma_mode = 1;
const int dma_set_addr_mode = 2;
const int dma_set_mem = 3;
const int dma_set_trans = 4;
const int dma_set_trans_len = 2;
const int dma_set_irq_msk = 6;
const int dma_set_en = 7;
// options for dma_set_trans
const int dma_trans_8sgl = 0;
const int dma_trans_16sgl = 1;
const int dma_trans_32sgl = 2;
const int dma_trans_32bst = 3;

const uint8_t dma_irqs[4] = {37, 36, 33, 32};

void apbdma_work(int chn) {
  uint32_t cur_setting = dma_regs[dma_setting + chn];
  if (!check_bit(cur_setting, dma_set_en)) {
    workAvailable[chn] = false;
    return;
  }
  // try and find a hook that works
  int foundHook = -1;
  for (int i = 0; i < hooks.size(); i++) {
    // Skip hook if it doesn't support the modes that we are using
    if (check_bit(cur_setting, dma_set_dir) &&
        ((hooks[i].Flags & DMA_DIR_READ) == 0))
      continue;
    if ((!check_bit(cur_setting, dma_set_dir)) &&
        ((hooks[i].Flags & DMA_DIR_WRITE) == 0))
      continue;
    if (check_bit(cur_setting, dma_set_addr_mode) &&
        ((hooks[i].Flags & DMA_MODE_REGUL) == 0))
      continue;
    if ((!check_bit(cur_setting, dma_set_dir)) &&
        ((hooks[i].Flags & DMA_MODE_CONT) == 0))
      continue;
    if (dma_regs[dma_apb_start + chn] < hooks[i].StartAddr)
      continue;
    if (dma_regs[dma_apb_start + chn] >=
        (hooks[i].StartAddr + hooks[i].RegionSize))
      continue;
    // appears to work...
    foundHook = i;
    break;
  }

  // printf("ahb begin=0x%08x, ahb end=0x%08x, apb begin=0x%08x\n",
  //       dma_regs[dma_ahb_start_a + chn], dma_regs[dma_ahb_end_a + chn],
  //       dma_regs[dma_apb_start + chn]);

  if (foundHook != -1) {
    // printf("using hook %d\n", foundHook);
    // use accelerated access
    if ((dma_regs[dma_ahb_start_a + chn] & 0xFE000000) != 0xA0000000) {
      printf("FIXME: AHB address not main RAM!!!\n");
      workAvailable[chn] = false;
      return;
    }
    uint8_t *ramBuf = memptr + (dma_regs[dma_ahb_start_a + chn] & 0x01FFFFFF);
    uint32_t len =
        dma_regs[dma_ahb_end_a + chn] - dma_regs[dma_ahb_start_a + chn];
    if (len < 0) {
      throw runtime_error("bad dma length");
    }
    switch (get_bits(cur_setting, dma_set_trans, dma_set_trans_len)) {
    case dma_trans_8sgl:
      len += 1;
      break;
    case dma_trans_16sgl:
      len += 2;
      break;
    case dma_trans_32sgl:
    case dma_trans_32bst:
      len += 4;
      break;
    }

    uint32_t paddr =
        dma_regs[dma_apb_start + chn] - hooks[foundHook].StartAddr;
    if (check_bit(cur_setting, dma_set_dir)) {
      if (check_bit(cur_setting, dma_set_addr_mode)) {
        hooks[foundHook].RegularReadHandler(paddr, len, ramBuf);
      } else {
        hooks[foundHook].ContinuousReadHandler(paddr, len, ramBuf);
      }
    } else {
      if (check_bit(cur_setting, dma_set_addr_mode)) {
        hooks[foundHook].RegularWriteHandler(paddr, len, ramBuf);
      } else {
        hooks[foundHook].ContinuousWriteHandler(paddr, len, ramBuf);
      }
    }
  } else {
    // printf("not using hook\n");
    uint32_t ahbAddr = dma_regs[dma_ahb_start_a + chn];
    uint32_t apbAddr = dma_regs[dma_apb_start + chn];
    uint32_t ahbEnd = dma_regs[dma_ahb_end_a + chn];
    switch (get_bits(cur_setting, dma_set_trans, dma_set_trans_len)) {
    case dma_trans_8sgl:
      for (; ahbAddr <= ahbEnd; ahbAddr++) {
        if (check_bit(cur_setting, dma_set_dir)) {
          write_memU8(ahbAddr, read_memU8(apbAddr));
        } else {
          write_memU8(apbAddr, read_memU8(ahbAddr));
        }
        if (!check_bit(cur_setting, dma_set_addr_mode))
          apbAddr++;
        if (!check_bit(cur_setting, dma_set_en))
          break;
      };
      break;
    case dma_trans_16sgl:
      for (; ahbAddr <= ahbEnd; ahbAddr += 2) {
        if (check_bit(cur_setting, dma_set_dir)) {
          write_memU16(ahbAddr, read_memU16(apbAddr));
        } else {
          write_memU16(apbAddr, read_memU16(ahbAddr));
        }
        if (!check_bit(cur_setting, dma_set_addr_mode))
          apbAddr += 2;
        if (!check_bit(cur_setting, dma_set_en))
          break;
      };
      break;
    case dma_trans_32sgl:
    case dma_trans_32bst:
      for (; ahbAddr <= ahbEnd; ahbAddr += 4) {
        if (check_bit(cur_setting, dma_set_dir)) {
          write_memU32(ahbAddr, read_memU32(apbAddr));
        } else {
          write_memU32(apbAddr, read_memU32(ahbAddr));
        }
        if (!check_bit(cur_setting, dma_set_addr_mode))
          apbAddr += 4;
        if (!check_bit(cur_setting, dma_set_en))
          break;
      };
      break;
    }
  }
  clear_bit(dma_regs[dma_busy_sts], chn);
  set_bit(dma_regs[dma_irq_sts], chn);
  if (check_bit(cur_setting, dma_set_irq_msk)) {
    SetIRQState(dma_irqs[chn], true);
  }
  workAvailable[chn] = false;
}


void InitAPBDMAThreads() {
  /* for (int i = 0; i < dma_nCh; i++) {
    mutexes[i] = SDL_CreateMutex();
    condVars[i] = SDL_CreateCond();
    threads[i] = SDL_CreateThread(APBDMA_Thread, "APBDMA", (void *)i);
  } */
}

void ResetChannel(int chn) {
  clear_bit(dma_regs[dma_busy_sts], chn);
  clear_bit(dma_regs[dma_irq_sts], chn);
  SetIRQState(dma_irqs[chn], false);
  dma_regs[dma_ahb_start_a + chn] = 0;
  dma_regs[dma_ahb_end_a + chn] = 0;
  dma_regs[dma_apb_start + chn] = 0;
  dma_regs[dma_ahb_start_b + chn] = 0;
  dma_regs[dma_ahb_end_b + chn] = 0;
  dma_regs[dma_setting + chn] = 0;
}

void InitAPBDMADevice(PeripheralInitInfo initInfo) {
  memptr = get_dma_ptr(0xA0000000);
  for (int i = 0; i < dma_nCh; i++) {
    ResetChannel(i);
  }
}

uint32_t APBDMADeviceReadHandler(uint16_t addr) {
  if ((addr / 4) < dma_nregs) {
    printf("APBDMA read %04x = %08x\n", addr, uint32_t(dma_regs[addr / 4]));
    return dma_regs[addr / 4];
  } else {
    printf("APBDMA read error: address 0x%04x out of range\n", addr);
    return 0;
  }
}

void APBDMADeviceWriteHandler(uint16_t addr, uint32_t val) {
  // addr %= 0x190; //not sure about this...
  printf("APBDMA write : 0x%04x=0x%08x\n", addr, val);
  addr /= 4;

  if (addr == dma_irq_sts) {
    for (int i = 0; i < dma_nCh; i++) {
      if (check_bit(val, i)) {
        clear_bit(dma_regs[dma_irq_sts], i);
        SetIRQState(dma_irqs[i], false);
      }
    }
  } else if (addr == dma_soft_rst) {
    for (int i = 0; i < dma_nCh; i++) {
      if (check_bit(val, i)) {
        ResetChannel(i);
      }
    }
  } else if (addr < dma_nregs) {
    dma_regs[addr] = val;
    if ((addr >= dma_setting) && (addr < (dma_setting + dma_nCh))) {
      int chn = addr - dma_setting;
      if (check_bit(val, dma_set_en) &&
          (!check_bit(dma_regs[dma_busy_sts], chn))) {
        if (check_bit(val, dma_set_mem)) {
          printf("FIXME: APBDMA double buffer mode not supported.\n");
        } else {
          printf("APBDMA begin (ahb = 0x%08x)!\n", uint32_t(dma_regs[dma_ahb_start_a + chn]));
          set_bit(dma_regs[dma_busy_sts], chn);
          /*SDL_LockMutex(mutexes[chn]);
          workAvailable[chn] = true;
          SDL_CondSignal(condVars[chn]);
          SDL_UnlockMutex(mutexes[chn]);*/
          apbdma_work(chn);
        }
      }
    }
  } else {
    printf("APBDMA write error: address 0x%04x out of range, dat=0x%08x\n",
           addr * 4, val);
  }
}

void APBDMADeviceResetHandler() {
  for (auto &r : dma_regs)
    r = 0;
}

void RegisterDMAHook(DMAHook hook) { hooks.push_back(hook); }

const Peripheral APBDMAPeripheral = {"APBDMA", InitAPBDMADevice,
                                     APBDMADeviceReadHandler,
                                     APBDMADeviceWriteHandler,
                                     APBDMADeviceResetHandler};
}
