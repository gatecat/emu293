#include "sdperiph.h"
#include "../dma/apbdma.h"
#include "../helper.h"
#include "../sys/irq_if.h"
#include "sdcard.h"
#include <cstdio>
using namespace std;

namespace Emu293 {
const uint16_t SD_DATATx = 0;
const uint16_t SD_DATARx = 1;
const uint16_t SD_COMMAND = 2;

const uint16_t SD_CMD_CMDCODE_ST = 0;
const uint16_t SD_CMD_CMDCODE_LEN = 6;
const uint16_t SD_CMD_STPCMD = 6;
const uint16_t SD_CMD_RUNCMD = 7;
const uint16_t SD_CMD_CMDWD = 8;
const uint16_t SD_CMD_TxDATA = 9;
const uint16_t SD_CMD_MULBLK = 10;
const uint16_t SD_CMD_INICARD = 11;
const uint16_t SD_CMD_RESPTYPE_ST = 12;
const uint16_t SD_CMD_RESPTYPE_LEN = 3;

const uint16_t SD_RESPTYPE_NONE = 0;
const uint16_t SD_RESPTYPE_R1 = 1;
const uint16_t SD_RESPTYPE_R2 = 2;
const uint16_t SD_RESPTYPE_R3 = 3;
const uint16_t SD_RESPTYPE_R6 = 6;
const uint16_t SD_RESPTYPE_R1B = 7;

const uint16_t SD_ARGUMENT = 3;
const uint16_t SD_STATUS = 5;

const uint16_t SD_STATUS_BUSY = 0;
const uint16_t SD_STATUS_CARDBUSY = 1;
const uint16_t SD_STATUS_CMDCOM = 2;
const uint16_t SD_STATUS_DATCOM = 3;
const uint16_t SD_STATUS_RSPIDXERR = 4;
const uint16_t SD_STATUS_RSPCRCERR = 5;
const uint16_t SD_STATUS_CMDBUFFULL = 6;
const uint16_t SD_STATUS_DATBUFFULL = 7;
const uint16_t SD_STATUS_DATBUFEMPTY = 8;
const uint16_t SD_STATUS_TIMEOUT = 9;
const uint16_t SD_STATUS_DATCRCERR = 10;
const uint16_t SD_STATUS_CARDWP = 11;
const uint16_t SD_STATUS_CARDPRE = 12;
const uint16_t SD_STATUS_CARDINT = 13;

const uint16_t SD_RESPONSE = 4;

const uint16_t SD_CONTROL = 6;

const uint16_t SD_CTRL_CLKDIV_ST = 0;
const uint16_t SD_CTRL_CLKDIV_LEN = 8;
const uint16_t SD_CTRL_BUSWIDTH = 8;
const uint16_t SD_CTRL_DMAMODE = 9;
const uint16_t SD_CTRL_IOEN = 10;
const uint16_t SD_CTRL_EN_SD = 11;
const uint16_t SD_CTRL_BLKLEN_ST = 16;
const uint16_t SD_CTRL_BLKLEN_LEN = 12;

const uint16_t SD_INTEN = 7;

const uint16_t SD_INTEN_DATCOM = 0;
const uint16_t SD_INTEN_CMDBUFFULL = 1;
const uint16_t SD_INTEN_DATBUFFULL = 2;
const uint16_t SD_INTEN_DATBUFEMPTY = 3;
const uint16_t SD_INTEN_CARDINSREM = 4;
const uint16_t SD_INTEN_SDIO = 5;

static uint32_t sd_txbuf;
static uint32_t sd_status_reg = 0x0000100C;
static uint32_t sd_cmd_setup_reg = 0;
static uint32_t sd_argument_reg = 0;
static uint32_t sd_response_reg = 0;
static uint32_t sd_mode_ctrl = 0x02000954;
static uint32_t sd_int_ctrl = 0;

static uint32_t sd_cmd_bytes_read = 0;
static uint32_t sd_cmd_bytes_expected = 0;

static uint32_t sd_dat_bytes_xfrd = 0;
static uint32_t sd_dat_bytes_expected = 0;
static bool isMultiBlock = false;

void StatusSetFlag(uint16_t flag, bool state) {
  if (state) {
    set_bit(sd_status_reg, flag);
  } else {
    clear_bit(sd_status_reg, flag);
  }
  // update interrupts
}
static CPU *currentCPU;

void SDDMAReadHandler(uint32_t startAddr, uint32_t count, uint8_t *buf) {
  SD_Read(buf, count);
  sd_dat_bytes_xfrd += count;
  if (sd_dat_bytes_xfrd < sd_dat_bytes_expected) {
    StatusSetFlag(SD_STATUS_DATBUFFULL, true);
  } else {
    if (!isMultiBlock) {
      StatusSetFlag(SD_STATUS_DATBUFFULL, false);
    }
    StatusSetFlag(SD_STATUS_DATCOM, true);
  }
}
void SDDMAWriteHandler(uint32_t startAddr, uint32_t count, uint8_t *buf) {
  SD_Write(buf, count);
  sd_dat_bytes_xfrd += count;
  if (sd_dat_bytes_xfrd < sd_dat_bytes_expected) {
    StatusSetFlag(SD_STATUS_DATBUFFULL, true);
  } else {
    if (!isMultiBlock) {
      StatusSetFlag(SD_STATUS_DATBUFFULL, false);
    }
    StatusSetFlag(SD_STATUS_DATCOM, true);
  }
}

void InitSDDevice(PeripheralInitInfo initInfo) {
  sd_status_reg = 0x0000100C;
  sd_cmd_setup_reg = 0;
  sd_argument_reg = 0;
  sd_response_reg = 0;
  sd_mode_ctrl = 0x02000954;
  sd_int_ctrl = 0;
  sd_dat_bytes_xfrd = 0;
  sd_dat_bytes_expected = 0;
  isMultiBlock = false;
  SD_ResetCard();
  currentCPU = initInfo.currentCPU;
  // init APBDMA
  DMAHook writeHook;
  writeHook.StartAddr = initInfo.baseAddress;
  writeHook.RegionSize = 4;
  writeHook.Flags = (DMAHookFlags)(DMA_DIR_WRITE | DMA_MODE_REGUL);
  writeHook.RegularWriteHandler = SDDMAWriteHandler;
  RegisterDMAHook(writeHook);

  DMAHook readHook;
  readHook.StartAddr = initInfo.baseAddress + 4;
  readHook.RegionSize = 4;
  readHook.Flags = (DMAHookFlags)(DMA_DIR_READ | DMA_MODE_REGUL);
  readHook.RegularReadHandler = SDDMAReadHandler;
  RegisterDMAHook(readHook);
  // init GPIO here...
}

uint32_t SDDeviceReadHandler(uint16_t addr) {
  switch (addr / 4) {
  case SD_DATATx:
    return sd_txbuf;
  case SD_DATARx: {
    uint8_t buf[4];
    SD_Read(buf, 4);
    sd_dat_bytes_xfrd += 4;
    if (sd_dat_bytes_xfrd < sd_dat_bytes_expected) {
      StatusSetFlag(SD_STATUS_DATBUFFULL, true);
    } else {
      if (!isMultiBlock) {
        StatusSetFlag(SD_STATUS_DATBUFFULL, false);
      }
      StatusSetFlag(SD_STATUS_DATCOM, true);
    }

    return get_uint32le(buf);
    // update regs, fire interrupts, etc
  }; break;
  case SD_COMMAND:
    return sd_cmd_setup_reg;
  case SD_ARGUMENT:
    return sd_argument_reg;
  case SD_RESPONSE: {
    sd_cmd_bytes_read += 4;
    if (sd_cmd_bytes_read < sd_cmd_bytes_expected) {
      StatusSetFlag(SD_STATUS_CMDBUFFULL, true);
    } else {
      StatusSetFlag(SD_STATUS_CMDBUFFULL, false);
    }
    uint32_t val = SD_Command_ReadResponse();
    printf("read resp = 0x%08x\n",val);
    return val;
  } break;
  case SD_STATUS:
    return sd_status_reg;
  case SD_CONTROL:
    return sd_mode_ctrl;
  case SD_INTEN:
    return sd_int_ctrl;
  default:
    printf("SD Error: read from unmapped address 0x%04x!\n", addr);
    return 0;
  }
}

void SDDeviceWriteHandler(uint16_t addr, uint32_t val) {
  switch (addr / 4) {
  case SD_DATATx:
    uint8_t buf[4];
    set_uint32le(buf, val);

    sd_dat_bytes_xfrd += 4;
    if (sd_dat_bytes_xfrd < sd_dat_bytes_expected) {
      StatusSetFlag(SD_STATUS_DATBUFFULL, true);
    } else {
      if (!isMultiBlock) {
        StatusSetFlag(SD_STATUS_DATBUFFULL, false);
      }
      StatusSetFlag(SD_STATUS_DATCOM, true);
    }
    SD_Write(buf, 4);
    break;
  case SD_COMMAND:
    sd_cmd_setup_reg = val & 0x0000773F;
    if (check_bit(val, SD_CMD_INICARD)) {
      SD_ResetCard();
    } else if (check_bit(val, SD_CMD_STPCMD)) {
      StatusSetFlag(SD_STATUS_CMDBUFFULL, false);
    } else if (check_bit(val, SD_CMD_RUNCMD)) {
      if (check_bit(sd_cmd_setup_reg, SD_CMD_CMDWD)) {
        if (check_bit(sd_cmd_setup_reg, SD_CMD_MULBLK)) {
          sd_dat_bytes_expected = (sd_mode_ctrl >> 16) & 0xFFF;
          sd_dat_bytes_xfrd = 0;
          isMultiBlock = true;
        } else {
          sd_dat_bytes_expected = (sd_mode_ctrl >> 16) & 0xFFF;
          sd_dat_bytes_xfrd = 0;
          isMultiBlock = false;
        }
        if (check_bit(sd_cmd_setup_reg, SD_CMD_TxDATA)) {
          StatusSetFlag(SD_STATUS_DATBUFEMPTY, true);
        } else {
          StatusSetFlag(SD_STATUS_DATBUFFULL, true);
        }
        StatusSetFlag(SD_STATUS_DATCOM, false);
      }
      uint8_t respType = (sd_cmd_setup_reg >> 12) & 0x7;
      sd_cmd_bytes_read = 0;
      StatusSetFlag(SD_STATUS_CMDBUFFULL, false);
      switch (respType) {
      case SD_RESPTYPE_NONE:
        sd_cmd_bytes_expected = 0;
        break;
      case SD_RESPTYPE_R1:
      case SD_RESPTYPE_R3:
      case SD_RESPTYPE_R6:
      case SD_RESPTYPE_R1B:
        sd_cmd_bytes_expected = 4;
        StatusSetFlag(SD_STATUS_CMDBUFFULL, true);
        break;
      case SD_RESPTYPE_R2:
        sd_cmd_bytes_expected = 16;
        StatusSetFlag(SD_STATUS_CMDBUFFULL, true);
        break;
      }
      //  printf("SD cmd %d dat=0x%08x val=0x%08x from 0x%08x\n",
      //         (sd_cmd_setup_reg & 0x3F), sd_cmd_setup_reg, val,
      //         currentCPU->pc);

      SD_Command((sd_cmd_setup_reg & 0x3F), sd_argument_reg);
      StatusSetFlag(SD_STATUS_CMDCOM, true);
      // sd_argument_reg = 0;
    }
    break;
  case SD_ARGUMENT:
    sd_argument_reg = val;
    break;
  case SD_STATUS:
    break;
  case SD_CONTROL:
    sd_mode_ctrl = val;
    break;
  case SD_INTEN:
    sd_int_ctrl = val;
    // update interrupts
    break;
  default:
    printf("SD Error: write 0x%08x to unmapped address 0x%04x!\n", val, addr);
  }
}

const Peripheral SDPeripheral = {"SD", InitSDDevice, SDDeviceReadHandler,
                                 SDDeviceWriteHandler};
}
