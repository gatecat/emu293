// Subor D99+ system definition
#include "dma/apbdma.h"
#include "dma/blndma.h"
#include "helper.h"
#include "peripheral.h"
#include "stor/sdperiph.h"
#include "sys/bufctl.h"
#include "sys/gpio.h"
#include "sys/irq.h"
#include "sys/miu.h"
#include "sys/timer.h"

#include "system.h"
#include "video/ppu.h"
#include "video/tve.h"
#include <cstdio>
#include <cstdlib>
using namespace std;

namespace Emu293 {
#define RAM_START 0xA0000000
#define RAM_SIZE (32 * 1024 * 1024) // not sure about this

#define PERIPH_START 0x88000000
#define PERIPH_SIZE 0x01000000

#define IMEM_START 0x9F000000
#define IMEM_START_ALT 0xBF000000
#define IMEM_SIZE 0x01000000

uint8_t ram[RAM_SIZE];
bool ram_active[RAM_SIZE];
uint8_t imem[IMEM_SIZE];

const Peripheral *peripherals[256] = {NULL};

CPU *currentCPU;

void registerPeripheral(const Peripheral *periph, uint8_t addr) {
  peripherals[addr] = periph;
  PeripheralInitInfo initInfo = {PERIPH_START + (addr << 16), currentCPU};
  periph->initPeriph(initInfo);
}

uint8_t read_memU8(uint32_t addr) {
  if ((addr >= RAM_START) && (addr < (RAM_START + RAM_SIZE))) {
    // if (!ram_active[addr - RAM_START])
      // printf("Read8 from uninit memory location 0x%08x\n", addr);
    return ram[addr - RAM_START];
  } else {
    printf("Read8 from unmapped memory location 0x%08x\n", addr);
    // currentCPU->debugDump();

    return 0;
  }
}
void write_memU8(uint32_t addr, uint8_t val) {

  if ((addr >= RAM_START) && (addr < (RAM_START + RAM_SIZE))) {
    ram[addr - RAM_START] = val;
    ram_active[addr - RAM_START] = true;
  } else if ((addr >= IMEM_START) && (addr < (IMEM_START + IMEM_SIZE))) {
    imem[addr - IMEM_START] = val;
  } else {
    // printf("Write 0x%02x to unmapped memory location 0x%08x\n", val, addr);
  }
}
uint16_t read_memU16(uint32_t addr) {
  /*	if(addr == 0xa0e002dc) {
                  printf("...\n");
          }*/
  if ((addr >= RAM_START) && (addr < (RAM_START + RAM_SIZE))) {
    //if (!ram_active[addr - RAM_START])
      //printf("Read16 from uninit memory location 0x%08x\n", addr);
    return get_uint16le(&(ram[addr - RAM_START]));
    } else if ((addr >= IMEM_START) && (addr < (IMEM_START + IMEM_SIZE))) {
    printf("Read from imem 0x%08x at 0x%08x\n", addr, currentCPU->pc);

    return get_uint16le(&(imem[addr - IMEM_START]));
  } else if ((addr >= IMEM_START_ALT) &&
             (addr < (IMEM_START_ALT + IMEM_SIZE))) {
    printf("Read from imem 0x%08x at 0x%08x\n", addr, currentCPU->pc);
    return get_uint16le(&(imem[addr - IMEM_START_ALT]));
  } else {
    printf("Read16 from unmapped memory location 0x%08x at 0x%08x\n", addr,
           currentCPU->pc);
    if (currentCPU->pc == addr)
      currentCPU->debugDump();
    //    currentCPU->debugDump();
    return 0x8080;
  }
}
void write_memU16(uint32_t addr, uint16_t val) {
  if ((addr >= RAM_START) && (addr < (RAM_START + RAM_SIZE))) {
    set_uint16le(&(ram[addr - RAM_START]), val);
    for (int i = 0; i < 2; i++)
      ram_active[addr - RAM_START + i] = true;
  } else {
    // printf("Write 0x%04x to unmapped memory location 0x%08x at 0x%08x\n",
    // val,
    //       addr, currentCPU->pc);
  }
}

// NB peripheral read/writes are only 32 bit
uint32_t read_memU32(uint32_t addr) {
  if ((addr >= RAM_START) && (addr < (RAM_START + RAM_SIZE))) {
    // if (!ram_active[addr - RAM_START])
    // printf("Read32 from uninit memory location 0x%08x at 0x%08x\n", addr,
    //         currentCPU->pc);
    return get_uint32le(&(ram[addr - RAM_START]));
  } else if ((addr >= IMEM_START) && (addr < (IMEM_START + IMEM_SIZE))) {
    printf("Read from imem 0x%08x at 0x%08x\n", addr, currentCPU->pc);
    return get_uint32le(&(imem[addr - IMEM_START]));
  } else if ((addr >= IMEM_START_ALT) &&
             (addr < (IMEM_START_ALT + IMEM_SIZE))) {
    printf("Read from imem 0x%08x at 0x%08x\n", addr, currentCPU->pc);
    return get_uint32le(&(imem[addr - IMEM_START_ALT]));
  } else if ((addr >= PERIPH_START) && (addr < (PERIPH_START + PERIPH_SIZE))) {
    uint8_t pAddr = (addr >> 16) & 0xFF;
    if (peripherals[pAddr] != NULL) {
      return peripherals[pAddr]->regRead(addr & 0xFFFF);
    } else {
      if (pAddr != 0x05)
        printf("Read32 from unmapped peripheral location 0x%08x at 0x%08x\n",
               addr, currentCPU->pc);
      /*  if ((pAddr != 0x21) && (pAddr != 0x05))
          currentCPU->debugDump();*/

      return 0;
    }
  } else {
    printf("Read32 from unmapped memory location 0x%08x at 0x%08x\n", addr,
           currentCPU->pc);
    // currentCPU->debugDump();
    if (currentCPU->pc == addr)
      currentCPU->debugDump();
    return 0;
  }
}
void write_memU32(uint32_t addr, uint32_t val) {
  if ((addr >= RAM_START) && (addr < (RAM_START + RAM_SIZE))) {
    set_uint32le(&(ram[addr - RAM_START]), val);
    for (int i = 0; i < 4; i++)
      ram_active[addr - RAM_START + i] = true;
  } else if ((addr >= IMEM_START) && (addr < (IMEM_START + IMEM_SIZE))) {
    set_uint32le(&(imem[addr - IMEM_START]), val);
    printf("Write 0x%08x to imem 0x%08x at 0x%08x\n", val, addr,
           currentCPU->pc);
  } else if ((addr >= IMEM_START_ALT) &&
             (addr < (IMEM_START_ALT + IMEM_SIZE))) {
    set_uint32le(&(imem[addr - IMEM_START_ALT]), val);
    printf("Write 0x%08x to imem 0x%08x at 0x%08x\n", val, addr,
           currentCPU->pc);
  } else if ((addr >= PERIPH_START) && (addr < (PERIPH_START + PERIPH_SIZE))) {
    uint8_t pAddr = (addr >> 16) & 0xFF;
    // printf("Paddr = 0x%02x\n",pAddr);
    if (peripherals[pAddr] != NULL) {
      peripherals[pAddr]->regWrite(addr & 0xFFFF, val);
    } else {
      // printf("Write 0x%08x to unmapped peripheral location 0x%08x at
      // 0x%08x\n",
      //         val, addr, currentCPU->pc);
    }
  } else {
    // printf("Write 0x%08x to unmapped memory location 0x%08x at 0x%08x\n",
    // val,
    //           addr, currentCPU->pc);
    //    fflush(stdout);
  }
}

uint8_t *get_dma_ptr(uint32_t addr) {
  if ((addr >= RAM_START) && (addr < (RAM_START + RAM_SIZE)))
    return &(ram[addr - RAM_START]);
  else if ((addr >= IMEM_START) && (addr < (IMEM_START + IMEM_SIZE)))
    return &(imem[addr - IMEM_START]);
  else if ((addr >= IMEM_START_ALT) && (addr < (IMEM_START_ALT + IMEM_SIZE)))
    return &(imem[addr - IMEM_START_ALT]);
  else
    return nullptr;
}

static uint32_t softreset_entryPoint;

void system_init(CPU *cpu) {
  currentCPU = cpu;
  /*for (int i = 0x40; i < 0x48; i++)
    imem[i] = 0xFF;*/
  registerPeripheral(&IRQPeripheral, 0x0A);
  registerPeripheral(&TimerPeripheral, 0x16);
  registerPeripheral(&CKGPeripheral, 0x21);
  registerPeripheral(&GPIOPeripheral, 0x20);
  registerPeripheral(&APBDMAPeripheral, 0x08);
  registerPeripheral(&BLNDMAPeripheral, 0x0D);
  registerPeripheral(&PPUPeripheral, 0x01);
  registerPeripheral(&TVEPeripheral, 0x03);
  registerPeripheral(&BUFCTLPeripheral, 0x09);
  registerPeripheral(&MIUPeripheral, 0x07);

  registerPeripheral(&SDPeripheral, 0x18);
  // write_memU32(0x9F000040, 0x00434150);
  // write_memU32(0x9F000044, 0x41303034);
  // stubs for internal functions: just br r3 to return
  write_memU32(0xBF000024, 0x8003bc08);
  write_memU32(0xBF000038, 0x8003bc08);
  write_memU32(0xBF000800, 0x8003bc08);
  write_memU32(0xBF000D40, 0x8003bc08);
  write_memU32(0xa0fffff8, 0x12345678);
}

void system_softreset() {
  for (auto p : peripherals)
    if (p)
      p->reset();
  currentCPU->reset();
}

}
