#include "csi.h"
#include "webcam.h"
#include "../system.h"
#include "../sys/irq_if.h"
#include "../sys/timer.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace Emu293 {

static constexpr int csi_nreg = 64;
static uint32_t csi_regs[csi_nreg];

static const int csi_tg_cr = 0;
static const int csi_tg_cr_csien = 0;
static const int csi_tg_cr_vgaen = 2;

static const int csi_tg_fbaddr1 = 8;
static const int csi_tg_fbaddr2 = 9;
static const int csi_tg_fbaddr3 = 10;

static const int csi_tg_irqen = 30;
static const int csi_tg_irqst = 31;

static const int csi_end_irq = 51;

static uint8_t *memptr;

void CSIDeviceWriteHandler(uint16_t addr, uint32_t val) {
  addr /= 4;
  if (addr < csi_nreg) {
    if (addr == csi_tg_irqst) {
      if (check_bit(val, 2)) { // FRAME_END
        clear_bit(csi_regs[csi_tg_irqst], 2);
        SetIRQState(csi_end_irq, false);
      }
    } else {
      csi_regs[addr] = val;
    }
  } else {
    printf("CSI write error: address 0x%04x out of range, dat=0x%08x\n",
           addr * 4, val);
  }
}

uint32_t CSIDeviceReadHandler(uint16_t addr) {
  addr /= 4;
  if (addr < csi_nreg) {
    return csi_regs[addr];
  } else {
    printf("CSI read error: address 0x%04x out of range\n", addr * 4);
    return 0;
  }
}

void CSIDeviceResetHandler() {
  for (auto &r : csi_regs)
    r = 0;
}

void CSIState(SaveStater &s) {
  s.tag("CSI");
  s.a(csi_regs);
}

void InitCSIDevice(PeripheralInitInfo initInfo) {
    memptr = get_dma_ptr(0xA0000000);
}

const Peripheral CSIPeripheral = {"CSI", InitCSIDevice,
                                     CSIDeviceReadHandler,
                                     CSIDeviceWriteHandler,
                                     CSIDeviceResetHandler,
                                     CSIState};

static std::atomic<bool> csi_frame_need;
static std::atomic<bool> csi_frame_done;

static atomic<bool> kill_capture;
static condition_variable do_capture_cv;
static mutex do_capture_m;
static thread csi_thread;

static int16_t csi_frame_tmp[640*480];

inline bool csi_enabled() {
  // we need to check the clock is still enabled, because A21 rom.elf will shut off the camera
  // by stopping clock/asserting reset in the CKG before handing off to a non-camera app.
  // if we fired an interrupt after that, we would crash whatever we entered as it wouldn't handle
  // the CSI IRQ
  return get_clock_enable(0x18) && check_bit(csi_regs[csi_tg_cr], csi_tg_cr_csien);
}

void csi_copy_frame() {
    uint32_t cr = csi_regs[csi_tg_cr];
    if (!check_bit(cr, csi_tg_cr_csien))
        return;
    int w = 0, h = 0;
    if (check_bit(cr, csi_tg_cr_vgaen)) {
        w = 640;
        h = 480;
    } else {
        w = 320;
        h = 240;
    }
    // TODO: buffer flipping
    // TODO: YUV, etc etc

    webcam_grab_frame_rgb565(reinterpret_cast<uint8_t*>(csi_frame_tmp), w, h);
    // use temp buf first, in case grabbing frame was slow and addr or
    // enablement changed and we overwrite something useful in the meantime
    if (csi_enabled()) {
      uint32_t baseaddr = csi_regs[csi_tg_fbaddr1];
      uint8_t *ptr = (memptr + (baseaddr & 0x03FFFFFE));
      std::copy(csi_frame_tmp, csi_frame_tmp+(w*h), reinterpret_cast<uint16_t*>(ptr));
    }
    // TODO: frame end interrupt
}

void csi_capture_thread() {
  while (!kill_capture) {
    std::unique_lock<std::mutex> lk(do_capture_m);
    do_capture_cv.wait(lk, [] { return bool(csi_frame_need); });
    // Signal might be to die rather than render again
    if (!kill_capture) {
      csi_copy_frame();
      csi_frame_done = true;
    }
    csi_frame_need = false;
    lk.unlock();
  }
}

void InitCSIThreads() {
  csi_thread = thread(csi_capture_thread);
}

void ShutdownCSI() {
  {
    lock_guard<mutex> lk(do_capture_m);
    csi_frame_need = true;
    kill_capture = true;
  }
  do_capture_cv.notify_one();
  csi_thread.join();
}

void CSITick(bool get_frame) {
    if (get_frame && !csi_frame_need && csi_enabled()) {
        {
          lock_guard<mutex> lk(do_capture_m);
          csi_frame_need = true;
          csi_frame_done = false;
        }
        do_capture_cv.notify_one();
    } else if (csi_frame_done) {
      // raise IRQ in main thread, not CSI thread
      if (csi_enabled() &&
            check_bit(csi_regs[csi_tg_irqen], 2)) { // FRAME_END
        set_bit(csi_regs[csi_tg_irqst], 2);
        SetIRQState(csi_end_irq, true);
      }
      csi_frame_done = false;
    }
}

};