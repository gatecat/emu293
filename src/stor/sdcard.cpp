#include "sdcard.h"
#include "../helper.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace std;

namespace Emu293 {
namespace CMD {
const uint8_t GO_IDLE_STATE = 0;
const uint8_t ALL_SEND_CID = 2;
const uint8_t SEND_RELATIVE_ADDR = 3;
const uint8_t SELECT_CARD = 7;
const uint8_t SEND_IF_COND			= 8;
const uint8_t SEND_CSD = 9;
const uint8_t SEND_CID = 10;
const uint8_t STOP_TRANSMISSION = 12;
const uint8_t SEND_STATUS = 13;
const uint8_t GO_INACTIVE_STATE = 15;
const uint8_t SET_BLOCKLEN = 16;
const uint8_t READ_SINGLE_BLOCK = 17;
const uint8_t READ_MULTIPLE_BLOCK = 18;
const uint8_t WRITE_SINGLE_BLOCK = 24;
const uint8_t WRITE_MULTIPLE_BLOCK = 25;
const uint8_t ERASE_WR_BLK_START = 32;
const uint8_t ERASE_WR_BLK_END = 33;
const uint8_t ERASE = 34;
const uint8_t APP_CMD = 55;
const uint8_t SD_SEND_OP_COND = 41;
const uint8_t SD_SEND_SCR = 51;
const uint8_t SD_SEND_OCR = 58;
const uint8_t SD_SET_WIDTH = 6;
} // namespace CMD
// TODO: correct CRCs
const uint32_t reg_ocr = 0xC0FF8000;
const uint32_t reg_cid[4] = {0x42445345, 0x6D753239, 0x10000000,
                             0x0000F7FF}; // most significant word first
// this is populated with size at runtime, but we can prepopulate some stuff
// beforehand
static uint32_t reg_csd[4] = {
      0x400E015A, // [127:96] version 1, speed, etc
      0x5B99E000, // [95:64]  command classes,READ_BL_LEN=0xb=2048 bytes
      0x00000000, // [63:32]
      0x026000FF,  // [31:0]
}; // most significant word first

const uint8_t reg_scr[8] = {0x00, 0x00, 0xA5, 0x01, 0x00, 0x00, 0x00, 0x00};

static int imgfd;
static uint64_t cardsize;
const uint32_t def_blocklen = 512;
const uint16_t def_sizemult = 512;
static uint32_t blocklen = 512;
const uint32_t file_alignment = def_blocklen * def_sizemult;
const uint64_t max_size = uint64_t(65536) * file_alignment;
static uint16_t RCA = 0;
static uint32_t bytecount = 0;
static bool shdc_mode = 0;

enum SDCardState {
  SD_STATE_IDLE = 0, // initial state
  SD_STATE_READY = 1,
  SD_STATE_IDENT = 2,
  // data transfer mode
  SD_STATE_STDBY = 3,
  SD_STATE_TRANS = 4,
  SD_STATE_SEND = 5,
  SD_STATE_RECV = 6,
  SD_STATE_PROG = 7,
  SD_STATE_DIS = 8,
  SD_STATE_INACTIVE,

};

static SDCardState currentState = SD_STATE_IDLE;
static bool expectingMultiBlock = false;
static uint32_t cmdRespBuf[4];
static int cmdRespBufPtr = 0;
const uint32_t defCardStatus = 0x00000100;
static uint32_t currentCardStatus = defCardStatus;

static uint32_t eraseBegin = 0;
static uint32_t eraseEnd = 0;
static bool readingScr = false;
const uint8_t cardStatus_outOfRange = 31;
const uint8_t cardStatus_addressErr = 30;
const uint8_t cardStatus_blockLenErr = 29;
const uint8_t cardStatus_eraseSeqErr = 28;
const uint8_t cardStatus_eraseParam = 27;
const uint8_t cardStatus_wpViolation = 26;
const uint8_t cardStatus_cardLocked = 25;
const uint8_t cardStatus_lockUnlockFail = 24;
const uint8_t cardStatus_comCrcErr = 23;
const uint8_t cardStatus_iglCommand = 22;
const uint8_t cardStatus_cEccFail = 21;
const uint8_t cardStatus_ccError = 20;
const uint8_t cardStatus_error = 19;
const uint8_t cardStatus_csdOverwrite = 16;
const uint8_t cardStatus_wpEraseSkip = 15;
const uint8_t cardStatus_cardEccDis = 14;
const uint8_t cardStatus_eraseReset = 13;
const uint8_t cardStatus_readyForDat = 8;
const uint8_t cardStatus_appCmd = 5;

static void updateCardStatus() {
  currentCardStatus &= 0xFFFF81FF;
  currentCardStatus |= ((currentState & 0x0F) << 9);
}

uint64_t offset = 0;

bool SD_InitCard(const char *filename) {
  imgfd = open64(filename, O_RDWR);
  if (imgfd < 0) {
    printf("Failed to open SD image file %s.\n", filename);
    return false;
  }
  struct stat64 st;
  fstat64(imgfd, &st);
  cardsize = (uint64_t)st.st_size;
  if (cardsize > max_size) {
    printf("SD image file %s exceeds maximum size of %dMB.\n", filename,
           int(max_size / (1024 * 1024)));
    return false;
  } else if (cardsize == 0) {
    printf("SD card image size is 0; did file fail to open?\n");
    return false;
  }
  if ((cardsize % file_alignment) != 0) {
    printf("Note: fixing alignment of SD card image file.\n");
    // append suitable number of bytes to end of file
    int padding = file_alignment - (cardsize % file_alignment);
    vector<uint8_t> buf(padding, 0xFF);
    if (write(imgfd, &(*buf.begin()), padding) != padding) {
      printf("Failed to pad SD image file - check file can be written to.\n");
      return false;
    }
    cardsize += padding;
  }
  uint32_t c_size = ((cardsize / (1024ULL * 512ULL)) - 1) & 0x3FFFFF;
  reg_csd[2] |= (c_size & 0xFFFF) << 16U;
  reg_csd[1] |= ((c_size >> 16U) & 0x3F);
  printf("csd = %08x %08x %08x %08x\n", reg_csd[0], reg_csd[1], reg_csd[2], reg_csd[3]);
  SD_ResetCard();
  return true;
}

void SD_ResetCard() {
  currentState = SD_STATE_IDLE;
  blocklen = 512;
  cmdRespBufPtr = 0;
  bytecount = 0;
  currentCardStatus = defCardStatus;
  expectingMultiBlock = false;
  eraseBegin = 0;
  eraseEnd = 0;
  readingScr = false;
  offset = 0;
}

static void sendR1() {
  updateCardStatus();
  cmdRespBufPtr = 0;
  cmdRespBuf[0] = currentCardStatus;
  clear_bit(currentCardStatus, cardStatus_outOfRange);
}

static void beginRead(uint32_t addr) {
  uint64_t paddr = addr * uint64_t(512);
  printf("SD begin read at 0x%08llx\n", (unsigned long long)paddr);
  if (currentState == SD_STATE_TRANS) {
    if (paddr >= cardsize) {
      set_bit(currentCardStatus, cardStatus_outOfRange);
      printf("Address %lld out of range\n", (unsigned long long)paddr);
    } else {
      offset = paddr;
      currentState = SD_STATE_SEND;
      bytecount = 0;
    }
  } else {
    printf("Read not allowed in state %d\n", currentState);
    set_bit(currentCardStatus, cardStatus_iglCommand);
  }
}

static void beginWrite(uint32_t addr) {
  uint64_t paddr = addr * uint64_t(512);
  // printf("SD begin write at 0x%08x\n", addr);
  if (currentState == SD_STATE_TRANS) {
    if (paddr >= cardsize) {
      set_bit(currentCardStatus, cardStatus_outOfRange);
    } else {
      offset = paddr;
      currentState = SD_STATE_RECV;
      bytecount = 0;
    }
  } else {
    set_bit(currentCardStatus, cardStatus_iglCommand);
  }
}

void SD_Command(uint8_t command, uint32_t argument) {
  printf("SD cmd %d arg=0x%08x\n", command, argument);
  if (currentState == SD_STATE_INACTIVE)
    return;
  using namespace CMD;
  clear_bit(currentCardStatus, cardStatus_iglCommand);

  bool isAddressed =
      (currentState == SD_STATE_STDBY) || (currentState == SD_STATE_TRANS) ||
      (currentState == SD_STATE_SEND) || (currentState == SD_STATE_RECV) ||
      (currentState == SD_STATE_PROG) || (currentState == SD_STATE_DIS);

  if (check_bit(currentCardStatus, cardStatus_appCmd)) {
    switch (command) {
    case SD_SEND_OP_COND:
      printf("SD_SEND_OP_COND!!\n");
      cmdRespBufPtr = 0;
      cmdRespBuf[0] = reg_ocr;
      if (currentState == SD_STATE_IDLE) {
        currentState = SD_STATE_READY;
      }
      break;
    case SD_SEND_OCR:
      printf("SD_SEND_OCR!!\n");
      cmdRespBufPtr = 0;
      cmdRespBuf[0] = reg_ocr;
      if (currentState == SD_STATE_IDLE) {
        currentState = SD_STATE_READY;
      }
      break;
    case SD_SEND_SCR:
      /*	cmdRespBufPtr = 0;
              cmdRespBuf[0] = reg_scr[0];*/
      readingScr = true;
      expectingMultiBlock = false;
      bytecount = 0;
      break;
    case SEND_STATUS:
      sendR1();
      break;
    case APP_CMD:
      set_bit(currentCardStatus, cardStatus_appCmd);
      break;
    case SD_SET_WIDTH:
      sendR1();
      break;
    default:
      // set_bit(currentCardStatus, cardStatus_iglCommand);
      /*sendR1();
      printf("SD Error: unknown app command %d, arg=0x%08x\n", command,
      argument);*/
      clear_bit(currentCardStatus, cardStatus_appCmd);
      break;
    }
  }
  if (!check_bit(currentCardStatus, cardStatus_appCmd)) {
    switch (command) {
    case GO_IDLE_STATE:
      SD_ResetCard();
      break;
    case ALL_SEND_CID:
      cmdRespBufPtr = 0;
      copy(reg_cid, reg_cid + 4, cmdRespBuf);
      currentState = SD_STATE_IDENT;
      break;
    case SEND_RELATIVE_ADDR: {
      cmdRespBuf[0] = 0;
      RCA = 0x9001;
      if (currentState == SD_STATE_IDENT)
        currentState = SD_STATE_STDBY;

      uint32_t response = (RCA << 16);
      response |= check_bit(currentCardStatus, 23) << 15;
      response |= check_bit(currentCardStatus, 22) << 14;
      response |= check_bit(currentCardStatus, 19) << 13;
      response |= currentCardStatus & 0xFFF;
      cmdRespBufPtr = 0;
      cmdRespBuf[0] = response;
    } break;

    case SELECT_CARD: {
      if ((argument >> 16) == RCA) {
        if (currentState == SD_STATE_STDBY)
          currentState = SD_STATE_TRANS;
        else if (currentState == SD_STATE_DIS)
          currentState = SD_STATE_TRANS;
        else
          set_bit(currentCardStatus, cardStatus_iglCommand);
        sendR1();
      } else {
        if ((currentState == SD_STATE_STDBY) ||
            (currentState == SD_STATE_TRANS) || (currentState == SD_STATE_SEND))
          currentState = SD_STATE_STDBY;
        else if (currentState == SD_STATE_PROG)
          currentState = SD_STATE_STDBY;
        else
          set_bit(currentCardStatus, cardStatus_iglCommand);
      }

    } break;
    case SEND_CSD: {
      if (currentState == SD_STATE_STDBY) {
        cmdRespBufPtr = 0;
        printf("sending csd\n");
        for (int i = 0; i < 4; i++) {
          cmdRespBuf[i] = reg_csd[i];
        }

      } else {
        set_bit(currentCardStatus, cardStatus_iglCommand);
        sendR1();
      }
    } break;
    case SEND_CID: {
      if (currentState == SD_STATE_STDBY) {
        cmdRespBufPtr = 0;

        for (int i = 0; i < 4; i++) {
          cmdRespBuf[i] = reg_cid[i];
        }
      } else {
        set_bit(currentCardStatus, cardStatus_iglCommand);
      }
    } break;
    case STOP_TRANSMISSION: {
      if (currentState == SD_STATE_SEND) {
        currentState = SD_STATE_TRANS;
        sendR1();
      } else if (currentState == SD_STATE_RECV) {
        currentState = SD_STATE_TRANS;
        sendR1();
      } else {
        set_bit(currentCardStatus, cardStatus_iglCommand);
      }

    } break;
    case SEND_STATUS: {
      if (isAddressed) {
        sendR1();
      } else {
        set_bit(currentCardStatus, cardStatus_iglCommand);
      }
    } break;
    case GO_INACTIVE_STATE: {
      if (isAddressed) {
        currentState = SD_STATE_INACTIVE;
      } else {
        set_bit(currentCardStatus, cardStatus_iglCommand);
      }
    } break;
    case SET_BLOCKLEN: {
      if (currentState == SD_STATE_TRANS) {
        printf("set blocklen to %d\n", blocklen);
        blocklen = argument;
      } else {
        set_bit(currentCardStatus, cardStatus_iglCommand);
      }
      sendR1();
    } break;
    case READ_SINGLE_BLOCK:
        printf("read single!!\n");
      expectingMultiBlock = false;
      beginRead(argument);
      break;
    case READ_MULTIPLE_BLOCK:
        printf("read multi!!\n");
      expectingMultiBlock = true;
      beginRead(argument);
      break;
    case WRITE_SINGLE_BLOCK:
      expectingMultiBlock = false;
      beginWrite(argument);
      break;
    case WRITE_MULTIPLE_BLOCK:
      expectingMultiBlock = true;
      beginWrite(argument);
      break;
    case ERASE_WR_BLK_START:
      eraseBegin = argument * blocklen;
      break;
    case ERASE_WR_BLK_END:
      eraseEnd = argument * blocklen;
      break;
    case ERASE:
      if (currentState == SD_STATE_TRANS) {
        vector<uint8_t> buf(blocklen, 0xFF);
        for (uint32_t i = eraseBegin; i <= eraseEnd; i += blocklen) {
          if ((i + blocklen) > cardsize) {
            set_bit(currentCardStatus, cardStatus_outOfRange);
            break;
          } else {
            pwrite(imgfd, (void *)(&(buf[0])), blocklen, i);
          }
        }
      } else {
        set_bit(currentCardStatus, cardStatus_iglCommand);
      }
      sendR1();
      break;
    case APP_CMD:
      set_bit(currentCardStatus, cardStatus_appCmd);
      break;
    case SEND_IF_COND:
      cmdRespBufPtr = 0;
      cmdRespBuf[0] = argument;
      break;
    default:
      set_bit(currentCardStatus, cardStatus_iglCommand);
      sendR1();
      printf("SD Error: unknown command %d, arg=0x%08x\n", command, argument);
      break;
    }
  };

  updateCardStatus();
}

uint32_t SD_Command_ReadResponse() {
  if (cmdRespBufPtr < 4) {
    return cmdRespBuf[cmdRespBufPtr++];
  } else {
    return 0;
  }
}

void SD_Write(uint8_t *buf, int len) {
  printf("SD: writing %d bytes.\n", len);
  if (currentState == SD_STATE_RECV) {
    uint8_t *tempbuf = new uint8_t[len];
    copy(buf, buf + len, tempbuf);
    int bytes = pwrite(imgfd, (void *)tempbuf, len, offset);
    // int bytes = len; // don't actually touch image....
    delete[] tempbuf;
    bytecount += bytes;
    offset += bytes;
    if(bytecount != len) {
      printf("SD Error: write failed\n");
    }
    if ((!expectingMultiBlock) && (bytecount >= blocklen)) {
      currentState = SD_STATE_TRANS;
    }
  } else {
    printf("SD Error: write not permitted\n");
  }
}

void SD_Read(uint8_t *buf, int len) {
  printf("SD: reading %d bytes.\n", len);

  if (readingScr) {
    for (int i = 0; i < len; i++) {
      buf[i] = reg_scr[bytecount];
      bytecount++;
      if (bytecount >= 8) {
        readingScr = false;
        break;
      }
    }
  } else {
    if (currentState == SD_STATE_SEND) {
      uint8_t *tempbuf = new uint8_t[len];
      int bytesread = pread(imgfd, tempbuf, len, offset);
      copy(tempbuf, tempbuf + len, buf);
      delete[] tempbuf;
      // int errorcode = errno;
      offset += bytesread;
      bytecount += bytesread;
      if (bytesread != len) {
        perror("SD Error: read failed. Details");
      }
      /*
      for (int i = 0; i < len; i++) {
        printf("%02x ", buf[i]);
        if ((i % 32) == 31)
          printf("\n");
      }

      printf("\n");
      */
      if ((!expectingMultiBlock) && (bytecount >= blocklen)) {
        currentState = SD_STATE_TRANS;
      }

    } else {
      printf("SD Error: read not permitted\n");
    }
  }
}

void SD_State(SaveStater &s) {
  s.i(offset);
  s.e(currentState);
  s.i(bytecount);
  s.i(cmdRespBufPtr);
  s.a(cmdRespBuf);
  s.i(currentCardStatus);
}

} // namespace Emu293
