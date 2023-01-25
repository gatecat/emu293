#include "ppu.h"
#include "../helper.h"
#include "../sys/irq_if.h"
#include "../system.h"
#include "../io/ir_gamepad.h"

// Using SDL threads due to issues with mingw and C++11 threading
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>

#include <cstdio>
#include <functional>
#include <vector>
#include <fstream>
using namespace std;

#define get_bits_msbfirst(var, start, count)                                   \
  (((var) >> (8 - (start) - (count))) % (1 << (count)))

static int32_t sign_extend(uint32_t x, uint8_t b) {
  uint32_t m = 1UL << (b - 1);

  x = x & ((1UL << b) - 1);
  return (x ^ m) - m;
}

namespace Emu293 {
const int ppu_control = 0;

const int ppu_control_en = 12;
enum PPUResolutions {
  PPU_QVGA = 0,
  PPU_VGA = 1,
  /*		PPU_HVGA    = 2,
                  PPU_VGA2CIF = 3, [these modes NYI] */
};

const int ppu_screen_width[4] = {320, 640, 480, 640};
const int ppu_screen_height[4] = {240, 480, 320, 480};

const int ppu_layer_width[4] = {512, 1024, 1024, 1024};
const int ppu_layer_height[4] = {512, 1024, 1024, 1024};

const int ppu_sprite_control = 1;

const int ppu_sprite_enable = 0;
const int ppu_sprite_origin = 1;

const int ppu_sprite_maxnum = 2;
const int ppu_blend_mode = 3;
const int ppu_trans_rgb = 4;

const int ppu_transrgb_en = 16;

const int ppu_irq_control = 0x20;
const int ppu_irq_status = 0x21;

// register fields
const int ppu_irq_vblkstart = 0;
const int ppu_irq_vblkend = 1;
const int ppu_irq_ppudma = 3;
// interrupt numbers
const int ppu_intno_vblkstart = 53;
const int ppu_intno_vblkend = 46;
const int ppu_intno_ppudma = 16; // not sure about this
static uint32_t ppu_regs[65536];
/*---TEXT LAYER SUPPORT---*/
// register start for text layers
const int ppu_text_begin[3] = {0x08, 0x0F, 0x16};
// one each for each text layer, relative to ppu_text_begin
const int ppu_text_xpos = 0;
const int ppu_text_ypos = 1;
const int ppu_text_attr = 2;

enum PPUColourMode {
  PPU_2BPP = 0,
  PPU_4BPP = 1,
  PPU_6BPP = 2,
  PPU_8BPP = 3,
};

const int ppu_bpp_values[4] = {2, 4, 6, 8};

const int ppu_tattr_hflip = 2;
const int ppu_tattr_vflip = 3;

// Text char size possibilities
const int ppu_text_sizes[4] = {8, 16, 32, 64};
// per-text layer values
const int ppu_text_ctrl = 3;

const int ppu_tctrl_bitmap = 0;
const int ppu_tctrl_regmode = 1;
const int ppu_tctrl_wallpaper = 2;
const int ppu_tctrl_enable = 3;
const int ppu_tctrl_hmoveen = 4;
const int ppu_tctrl_hcmpen = 5;
const int ppu_tctrl_vcmpen = 6;
const int ppu_tctrl_rgb555 = 7;
const int ppu_tctrl_blenden = 8;
const int ppu_tctrl_rgb565 = 12;

const int ppu_text_chnumarray = 4;
const int ppu_text_blendlevel = 6;
// text buffer pointer registers
const int ppu_text_databufptrs[3][3] = {
    {0x28, 0x29, 0x2A}, {0x2B, 0x2C, 0x2D}, {0x2E, 0x2F, 0x30}};
// end of per-text layer values
const int ppu_text_hmve_start = 0x800;
const int ppu_text1_hcmp_start = 0xC00;
const int ppu_text2_hcmp_start = 0x1400;

// Vcmp register start addresses per layer
const int ppu_text_vcmp_regs[3] = {0x1D, 0x8C, 0x90};
// One for each set of vcmp registers
const int ppu_vcmp_value = 0;
const int ppu_vcmp_offset = 1;
const int ppu_vcmp_step = 2;
// add the zero-based text layer number to this register
const int ppu_text_trans_iidx = 0x80;

// Limited PPUDMA support
const int ppu_dma_ctrl = 0x94;

const int ppu_dma_ctrl_en = 0;
const int ppu_dma_ctrl_dir = 1; // 0=miu2ppu, 1=ppu2miu

const int ppu_dma_ppu_saddr = 0x95;
const int ppu_dma_miu_saddr = 0x96;
const int ppu_dma_word_cnt = 0x97;
/*SPRITE LAYER SUPPORT*/
const int ppu_sprite_begin = 0x1000;
const int ppu_sprite_data_begin_ptr = 0x34;
const int ppu_sprite_lowword = 0;
const int ppu_sprite_hiword = 1;

const int ppu_spritel_rgben = 26;
const int ppu_spritel_rgb565 = 27;
const int ppu_spritel_roen = 31; // rotation is not currently supported
const int ppu_spriteh_blnden = 15;

// Palette storage
const int ppu_palette_begin = 0x400;

static uint8_t *memptr = nullptr;

static SDL_Thread *ppudma_thread;
static SDL_mutex *ppudma_mutex;
static SDL_cond *ppudma_cvar;
static bool ppudma_workAvailable = false;

// Slightly unusual pixel format
// Packing: (msb first) Ab000000 00000000 RRRRRGGG GGGBBBBB
static uint32_t textLayers[3][1024][1024];
uint16_t rendered[480][640];
uint16_t scaled[480][640];

uint16_t curr_line = 0;

void ppudma_worker() {
    if (!check_bit(ppu_regs[ppu_dma_ctrl], ppu_dma_ctrl_en)) {
      ppudma_workAvailable = false;
      return;
    }

    uint8_t *ramptr = memptr + (ppu_regs[ppu_dma_miu_saddr] & 0x01FFFFFF);
    uint32_t *ppuptr = ppu_regs + ((ppu_regs[ppu_dma_ppu_saddr] & 0xFFFF) / 4);
    /*printf("ppudma start: %08x; ram start: %08x; count=%d; dir=%s\n",
      ppu_regs[ppu_dma_ppu_saddr], ppu_regs[ppu_dma_miu_saddr], ppu_regs[ppu_dma_word_cnt], 
      check_bit(ppu_regs[ppu_dma_ctrl], ppu_dma_ctrl_dir) ? "P2R" : "R2P");*/
    //+1 based on driver, needs checking
    for (int i = 0; i < (ppu_regs[ppu_dma_word_cnt] + 1); i++) {
      if (check_bit(ppu_regs[ppu_dma_ctrl], ppu_dma_ctrl_dir)) {
        // PPU to RAM
        set_uint32le(ramptr, *ppuptr);
      } else {
        // RAM to PPU
        *ppuptr = get_uint32le(ramptr);
      }
      ramptr += 4;
      ppuptr++;
    }

    clear_bit(ppu_regs[ppu_dma_ctrl], ppu_dma_ctrl_en);
    set_bit(ppu_regs[ppu_irq_status], ppu_irq_ppudma);
    if (check_bit(ppu_regs[ppu_irq_control], ppu_irq_ppudma)) {
      SetIRQState(ppu_intno_ppudma, true);
    }
    ppudma_workAvailable = false;
}

static inline uint32_t Argb1555ToCustomFormat(uint16_t argb1555) {
  if (argb1555 & 0x8000)
    return 0x80000000;
  uint32_t val = 0;
  val = argb1555 & 0x003F;
  val |= (argb1555 << 1) & 0xFFC0;
  return val;
}

static inline uint32_t Rgb565ToCustomFormat(uint16_t rgb565) { return rgb565; }

static inline void BlendCustomFormatToSurface(uint32_t data, uint8_t alpha,
                                              uint16_t &surface) {
  if ((data & 0x80000000) == 0) {
    if (check_bit(ppu_regs[ppu_trans_rgb], ppu_transrgb_en)
        && (data & 0xFFFF) == (ppu_regs[ppu_trans_rgb] & 0xFFFF))
        return;
    if ((data & 0x40000000) == 0) {
      surface = data & 0xFFFF;
    } else {
      uint16_t r0, g0, b0, r1, g1, b1, r, g, b;
      b0 = data & 0x1F;
      b1 = surface & 0x1F;
      g0 = (data >> 5) & 0x3F;
      g1 = (surface >> 5) & 0x3F;
      r0 = (data >> 11) & 0x1F;
      r1 = (surface >> 11) & 0x1F;
      uint8_t beta = 63 - alpha;
      r = (beta * r0 + alpha * r1) >> 6;
      g = (beta * g0 + alpha * g1) >> 6;
      b = (beta * b0 + alpha * b1) >> 6;
      surface = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
    }
  }
}

static inline int wrap_mod(int a, int b) {
  int m = a % b;
  return m >= 0 ? m : (m + b);
}

static void MergeTextLayer(int layerNo) {
  int swidth = ppu_screen_width[ppu_regs[ppu_control] & 0x03];
  int sheight = ppu_screen_height[ppu_regs[ppu_control] & 0x03];
  int lwidth = ppu_layer_width[ppu_regs[ppu_control] & 0x03];
  int lheight = ppu_layer_height[ppu_regs[ppu_control] & 0x03];

  if (check_bit(ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl],
                ppu_tctrl_enable)) {
    // printf("layer %d enable\n", layerNo);
    /* printf("layer %d dx %04x dy %04x mode %01x\n",
       layerNo,
       ppu_regs[ppu_text_begin[layerNo] + ppu_text_xpos],
       ppu_regs[ppu_text_begin[layerNo] + ppu_text_ypos],
       ppu_regs[ppu_control] & 0x03
    ); */
    int offX =
        sign_extend(ppu_regs[ppu_text_begin[layerNo] + ppu_text_xpos] & 0x7FF, 11);
    int offY = ppu_regs[ppu_text_begin[layerNo] + ppu_text_ypos] & 0x3FF;
    bool hmve = check_bit(ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl],
                          ppu_tctrl_hmoveen);
    int alpha = ppu_regs[ppu_text_begin[layerNo] + ppu_text_blendlevel] & 0x3F;
    bool blnden = check_bit(ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl],
                            ppu_tctrl_blenden);
    // TODO: HCMP/VCMP support
    for (int y = 0; y < sheight; y++) {
      int ly = wrap_mod(y + offY, lheight);
      int mvx = (swidth == 320) ? 0 : offX; // needed to make NES emu work
      if (hmve) {
        mvx += ppu_regs[ppu_text_hmve_start + ly] & 0x7FF;
      }
      for (int x = 0; x < swidth; x++) {
        if (blnden) {
          BlendCustomFormatToSurface(
              textLayers[layerNo][ly][wrap_mod(x + mvx, lwidth)], alpha,
              rendered[y][x]);

        } else {
          BlendCustomFormatToSurface(
              textLayers[layerNo][ly][wrap_mod(x + mvx, lwidth)], 63, rendered[y][x]);
        }
      }
    }
  }
}

// Convert a 1, 2, 4 or 6 bpp array to an 8bpp array

static inline void UnpackByteArray(uint8_t *in, uint8_t *out, int bpp,
                                   int count) {
  int inIndex = 0;
  int inBitIndex = 0;
  int pixCount = 0;
  while (pixCount < count) {
    uint8_t val;
    if (bpp != 6) {
      val = get_bits_msbfirst(in[inIndex], inBitIndex, bpp);
      inBitIndex += bpp;
      if (inBitIndex >= 8) {
        inBitIndex = 0;
        inIndex++;
      }
    } else {
      if (inBitIndex <= 2) {
        val = get_bits_msbfirst(in[inIndex], inBitIndex, bpp);
        inBitIndex += 6;
        if (inBitIndex >= 8) {
          inBitIndex = 0;
          inIndex++;
        }
      } else {
        val = get_bits_msbfirst(in[inIndex], inBitIndex, 8 - inBitIndex)
              << (inBitIndex - 2);
        val |= get_bits_msbfirst(in[inIndex + 1], 0, inBitIndex - 2);
        inBitIndex = inBitIndex - 2;
        inIndex++;
      }
    }
    out[pixCount] = val;
    pixCount++;
  }
}

// Depalettize byte array (from UnpackByteArray) to custom format (see above)
// Assumes RGB1555 - as I believe this is how palettes are always encoded.
static inline void DepalettizeByteArray(uint8_t *in, uint32_t *out, int count,
                                        int bank, int bpp,
                                        bool sprite = false) {
  int offset = bank * 16;
  for (int i = 0; i < count; i++) {
    out[i] = Argb1555ToCustomFormat(uint16_t(
        ppu_regs[ppu_palette_begin + offset + in[i] + (sprite ? 0x200 : 0)] &
        0xFFFF));
  }
}

static inline void RAMToCustomFormat(uint8_t *ram, uint32_t *out, int count,
                                     int pbank, bool argb1555, bool rgb565,
                                     int bpp, bool sprite = false) {
  if ((!argb1555) & (!rgb565)) { // palette encoded
    uint8_t *temp = new uint8_t[count];
    UnpackByteArray(ram, temp, bpp, count);
    DepalettizeByteArray(temp, out, count, pbank, bpp, sprite);
    delete[] temp;
  } else if (argb1555) {
    for (int i = 0; i < count; i++) {
      out[i] = Argb1555ToCustomFormat((ram[i * 2]) |
                                      (uint16_t(ram[i * 2 + 1]) << 8));
    }
  } else if (rgb565) {
    for (int i = 0; i < count; i++) {
      out[i] =
          Rgb565ToCustomFormat((ram[i * 2]) | (uint16_t(ram[i * 2 + 1]) << 8));
    }
  }
}

static void RenderTextBitmapLine(uint32_t ctrl, bool rgb565, bool argb1555,
                                 int line, int layerNo, uint32_t *out) {
  uint32_t attr = ppu_regs[ppu_text_begin[layerNo] + ppu_text_attr];
  int lwidth = ppu_layer_width[ppu_regs[ppu_control] & 0x03];
  int lheight = ppu_layer_height[ppu_regs[ppu_control] & 0x03];

  uint8_t *ramBuf =
      memptr +
      (ppu_regs[ppu_text_begin[layerNo] + ppu_text_chnumarray] & 0x01FFFFFF);
  uint8_t *datbuf =
        memptr + (ppu_regs[ppu_text_databufptrs[layerNo][0]] & 0x01FFFFFF);
  // always use attribute array in bitmap mode???
  /* uint16_t attr = ramBuf[lheight * 4 + line * 2] |
                  (uint16_t(ramBuf[lheight * 4 + line * 2 + 1]) << 8);*/
  int bpp;
  if (rgb565 | argb1555) {
    bpp = 16;
  } else {
    bpp = ppu_bpp_values[attr & 0x03];
  }
  uint32_t lineBegin;
  if (bpp == 16)
    lineBegin = get_uint32le(&(ramBuf[line * 4]));
  else
    lineBegin = line * (lwidth == 1024 ? 1024 : 256);
  int bank = get_bits(attr, 8, 5);
  uint8_t *linebuf = datbuf + ((lineBegin * (bpp / 8)) & 0x01FFFFFF);
  RAMToCustomFormat(linebuf, out, lwidth, bank, argb1555, rgb565, bpp);
}

static void TransformRZ(int x0, int y0, int &x1, int &y1, int entry, int w, int h) {
  entry = entry & 0x7;
  x0 -= w/2;
  y0 -= h/2;
  int hx = ppu_regs[0x40+4*entry+0];
  int hy = ppu_regs[0x40+4*entry+1];
  int vx = ppu_regs[0x40+4*entry+2];
  int vy = ppu_regs[0x40+4*entry+3];
  x1 = w/2 + (x0 * hx + y0 * vx) / 1024;
  y1 = h/2 + (x0 * hy + y0 * vy) / 1024;
}

static void RenderTextChar(uint8_t *chbuf, uint16_t attr, uint32_t chno,
                           int chwidth, int chheight, int posx, int posy,
                           int layerNo, bool rgb = false, bool rgb565 = false,
                           int rz = -1, int blend = -1) {
  bool hflip = check_bit(attr, ppu_tattr_hflip);
  bool vflip = check_bit(attr, ppu_tattr_vflip);

  bool trans;
  if (layerNo == -1) {
    trans = false;
  } else {
    trans = (chno == ppu_regs[ppu_text_trans_iidx + layerNo]);
  }
  if (chno == 0xffffffff)
    return;
  int bank = get_bits(attr, 8, 5);
  int bpp = ppu_bpp_values[attr & 0x03];
  if (rgb || rgb565)
    bpp = 16;
  // convert char to a format we like
  uint32_t *chfmtd = new uint32_t[chwidth * chheight];
  int chsize = (chwidth * chheight * bpp) / 8;

  /* if (rgb && !rgb565 && chno != 0 && layerNo == -1) {
    printf("chr 0x%08x\n", chno);
    printf("dat = 0x%08x\n",
               *reinterpret_cast<uint32_t *>(chbuf + ((chno * chsize) & 0x01FFFFFF)));
    // exit(1);
  } */

  if (!trans)
    RAMToCustomFormat(chbuf + ((chno * chsize) & 0x01FFFFFF), chfmtd, chwidth * chheight,
                      bank, rgb && !rgb565, rgb && rgb565, bpp, (layerNo == -1));

/*
  if (layerNo == -1)
    printf("w=%d h=%d, bpp=%d bank=%d chr0=%02x fmtd0=%08x\n", chwidth, chheight, bpp, bank, *(chbuf + ((chno * chsize) & 0x01FFFFFF)), chfmtd[0]);
*/

  for (int y = 0; y < chheight; y++) {
    int outy;
    if (vflip) {
      outy = posy + (chheight - 1) - y;
    } else {
      outy = posy + y;
    }
    for (int x = 0; x < chwidth; x++) {
      int outx;
      if (hflip) {
        outx = posx + (chwidth - 1) - x;
      } else {
        outx = posx + x;
      }
      if (trans) {
        if (layerNo != -1) {
          textLayers[layerNo][outy][outx] = 0x80000000;
        }
      } else {
        if (layerNo == -1) {
          if ((outy >= 0) && (outy < 480) && (outx >= 0) && (outx < 640)) {
            int chx = x, chy = y;
            if (rz != -1) {
              // rotate and zoom
              TransformRZ(x, y, chx, chy, rz, chwidth, chheight);
              // printf("rz=%d x=%d y=%d chx=%d chy=%d\n", rz, x, y, chx, chy);
              if (chx < 0 || chx >= chwidth || chy < 0 || chy >= chheight)
                continue;
            }
            uint32_t pixel = chfmtd[chy * chwidth + chx];
            if (pixel & 0x80000000)
              continue; // ARGB1555 transparency
            if (check_bit(ppu_regs[ppu_trans_rgb], ppu_transrgb_en)
                && (pixel & 0xFFFF) == (ppu_regs[ppu_trans_rgb] & 0xFFFF))
                continue; // magic colour transparency
            if (blend != -1)
              BlendCustomFormatToSurface((pixel & 0xFFFF) | 0x40000000, blend, rendered[outy][outx]);
            else
              rendered[outy][outx] = uint16_t(pixel & 0xFFFF);
          }
        } else {
          textLayers[layerNo][outy][outx] = chfmtd[y * chwidth + x];
        }
      }
    }
  }
  delete[] chfmtd;
}

static void RenderTextLayer(int layerNo) {
  uint32_t attr = ppu_regs[ppu_text_begin[layerNo] + ppu_text_attr];
  uint32_t ctrl = ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl];
  // printf("layer %d attr %08x ctrl %08x\n", layerNo, attr, ctrl);
  int lwidth = ppu_layer_width[ppu_regs[ppu_control] & 0x03];
  int lheight = ppu_layer_height[ppu_regs[ppu_control] & 0x03];

  bool rgb565 = false, argb1555 = false;
  if (check_bit(ctrl, ppu_tctrl_rgb555)) {
    argb1555 = true;
  } else if (check_bit(ctrl, ppu_tctrl_rgb565)) {
    rgb565 = true;
  }

  if (check_bit(ctrl, ppu_tctrl_bitmap)) {
    // bitmap mode
    uint32_t *outptr = &(textLayers[layerNo][0][0]);
    for (int y = 0; y < lheight; y++) {
      if (!check_bit(ctrl, ppu_tctrl_wallpaper)) {
        RenderTextBitmapLine(ctrl, rgb565, argb1555, y, layerNo, outptr);
      } else {
        RenderTextBitmapLine(ctrl, rgb565, argb1555, 0, layerNo, outptr);
      }
      outptr += lwidth;
    }
  } else {
    // char mode
    int chwidth = ppu_text_sizes[get_bits(attr, 4, 2)];
    int chheight = ppu_text_sizes[get_bits(attr, 6, 2)];
    bool reg_mode = check_bit(ctrl, ppu_tctrl_regmode);
    int gridwidth = lwidth / chwidth;
    int gridheight = lheight / chheight;
    uint8_t *numbuf =
        memptr +
        (ppu_regs[ppu_text_begin[layerNo] + ppu_text_chnumarray] & 0x01FFFFFF);
    uint8_t *datbuf =
        memptr + (ppu_regs[ppu_text_databufptrs[layerNo][0]] & 0x01FFFFFF);
    for (int y = 0; y < gridheight; y++) {
      for (int x = 0; x < gridwidth; x++) {
        uint32_t chnum = get_uint16le(&(numbuf[(gridwidth * y + x) * 2]));
        uint16_t chattr;
        if (reg_mode) {
          chattr = attr;
        } else {
          uint32_t attr_offs =
              gridwidth * gridheight * 2 + (gridwidth * y + x) * 2;
          chattr = numbuf[attr_offs] + (uint16_t(numbuf[attr_offs + 1]) << 8U);
        }
        RenderTextChar(datbuf, chattr, chnum, chwidth, chheight, x * chwidth,
                       y * chheight, layerNo, argb1555, rgb565);
      }
    }
  }
}

static void RenderSprite(int idx, int currdepth) {
  if (idx < ppu_regs[ppu_sprite_maxnum]) {
    uint32_t num = ppu_regs[ppu_sprite_begin + 2 * idx];
    uint32_t attr = ppu_regs[ppu_sprite_begin + 2 * idx + 1];
    if (get_bits(attr, 13, 2) != currdepth) {
      // sprite not on this depth layer; skip
      return;
    }
    int chwidth = ppu_text_sizes[get_bits(attr, 4, 2)];
    int chheight = ppu_text_sizes[get_bits(attr, 6, 2)];
    uint16_t chnum = num & 0xFFFF;
    int16_t xpos = (num >> 16) & 0x3FF;
    int16_t ypos = (attr >> 16) & 0x3FF;
    if (xpos >= (1024 - 96))
      xpos = xpos - 1024;
    if (ypos >= (1024 - 128))
      ypos = ypos - 1024;
    bool rgb = check_bit(num, 26);
    bool rgb565 = check_bit(num, 27);
    int rz = -1;
    if (check_bit(num, 31)) {
      // rotate/zoom enable
      rz = (num >> 28) & 0x7;
    }
    int blend = -1;
    if (check_bit(attr, 15)) {
      blend = (attr >> 26) & 0x3F;
    }
    uint8_t *dataptr =
        (memptr + (ppu_regs[ppu_sprite_data_begin_ptr] & 0x01FFFFFF));
    /* if (xpos != 0) {
      printf("sprite %d at (%d, %d), chr %d, begin 0x%08x, n %08x, attr 0x%08x\n", idx, xpos, ypos,
             chnum, ppu_regs[ppu_sprite_data_begin_ptr], num, attr);
    } */
    if (num != 0)
      RenderTextChar(dataptr, attr & 0xFFFF, chnum, chwidth, chheight, xpos, ypos,
                     -1, rgb, rgb565, rz, blend);
  }
};

void PPUDeviceWriteHandler(uint16_t addr, uint32_t val) {
  addr /= 4;
  ppu_regs[addr] = val;
  // printf("ppu write to %04x dat=%08x\n", addr, val);
  if (addr == ppu_dma_ctrl) {
    if (check_bit(val, ppu_dma_ctrl_en)) {
      ppudma_workAvailable = true;
      ppudma_worker();
    } else {
      // clear IRQ
      SetIRQState(ppu_intno_ppudma, false);
      clear_bit(ppu_regs[ppu_irq_status], ppu_irq_ppudma);
    }
  } else if (addr == ppu_irq_status) {
    // writing to IRQ status appears to clear IRQ based on app code?
    if (check_bit(val, ppu_irq_vblkstart)) {
      SetIRQState(ppu_intno_vblkstart, false);
      clear_bit(ppu_regs[ppu_irq_status], ppu_irq_vblkstart);
    }
    if (check_bit(val, ppu_irq_vblkend)) {
      SetIRQState(ppu_intno_vblkend, false);
      clear_bit(ppu_regs[ppu_irq_status], ppu_irq_vblkend);
    }
    if (check_bit(val, ppu_irq_ppudma)) {
      SetIRQState(ppu_intno_ppudma, false);
      clear_bit(ppu_regs[ppu_irq_status], ppu_irq_ppudma);
    }
  }
}

uint32_t PPUDeviceReadHandler(uint16_t addr) { return ppu_regs[addr / 4]; }

void InitPPUDevice(PeripheralInitInfo initInfo) {
  memptr = get_dma_ptr(0xA0000000);
}

SDL_Window *ppu_window;
SDL_Renderer *ppuwin_renderer;
void InitPPUThreads() {
/*
  ppudma_mutex = SDL_CreateMutex();
  ppudma_cvar = SDL_CreateCond();
  ppudma_thread = SDL_CreateThread(PPUDMA_Thread, "PPUDMA", nullptr);
*/
  ppu_window = SDL_CreateWindow("emu293", SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED, 640, 480, 0);
  if (ppu_window == nullptr) {
    printf("Failed to create window: %s.\n", SDL_GetError());
    exit(1);
  }
  ppuwin_renderer =
      SDL_CreateRenderer(ppu_window, -1, SDL_RENDERER_ACCELERATED);
}
static void PPURender() {
  for (int y = 0; y < 480; y++) {
    for (int x = 0; x < 640; x++) {
      rendered[y][x] = 0;
    }
  }
  for (int layer = 0; layer < 3; layer++) {
    RenderTextLayer(layer);
  }
  for (int depth = 0; depth < 4; depth++) {
    for (int layer = 0; layer < 3; layer++) {
      if (get_bits(ppu_regs[ppu_text_begin[layer] + ppu_text_attr], 13, 2) ==
          depth) {
        // printf("render layer %d\n", layer);
        MergeTextLayer(layer);
      }
    }
    for (int i = 0; i < 512; i++) {
      RenderSprite(i, depth);
    }
  }

  int swidth = ppu_screen_width[ppu_regs[ppu_control] & 0x03];
  int sheight = ppu_screen_height[ppu_regs[ppu_control] & 0x03];
  bool scaling = false;
  if (swidth == 640 && sheight == 480) {
    scaling = false;
  } else {
    int sx = 640/swidth;
    int sy = 480/sheight;
    for (int y = 0; y < 480; y++) {
      for (int x = 0; x < 640; x++) {
        scaled[y][x] = rendered[y/sy][x/sx];
      }
    }
    scaling = true;
  }

  SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
      (void *)(scaling?scaled:rendered), 640, 480, 16, 640 * 2, 0xF800, 0x07E0, 0x001F, 0x0);

  SDL_Texture *tex = SDL_CreateTextureFromSurface(ppuwin_renderer, surf);
  SDL_RenderClear(ppuwin_renderer);
  SDL_RenderCopy(ppuwin_renderer, tex, nullptr, nullptr);
  SDL_RenderPresent(ppuwin_renderer);
  SDL_DestroyTexture(tex);
  SDL_FreeSurface(surf);
}

static void PPUDebugRegisters() {
  std::ofstream out("../../test/ppudebug/regs.txt");
  for (int i = 0; i < 0x100; i++) {
    out << stringf("%08x %08x\n", 0x88010000 + (4*i), ppu_regs[i]);
  }
}

static void PPUDebugTextLayer(int layerNo) {
  uint32_t attr = ppu_regs[ppu_text_begin[layerNo] + ppu_text_attr];
  uint32_t ctrl = ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl];
  // printf("layer %d attr %08x ctrl %08x\n", layerNo, attr, ctrl);
  int lwidth = ppu_layer_width[ppu_regs[ppu_control] & 0x03];
  int lheight = ppu_layer_height[ppu_regs[ppu_control] & 0x03];

  bool rgb565 = false, argb1555 = false;
  if (check_bit(ctrl, ppu_tctrl_rgb555)) {
    argb1555 = true;
  } else if (check_bit(ctrl, ppu_tctrl_rgb565)) {
    rgb565 = true;
  }
  {
    uint32_t *lbuf = new uint32_t[lwidth*lheight];
    for (int y = 0; y < lheight; y++) {
      for (int x = 0; x < lwidth; x++) {
        uint32_t data = textLayers[layerNo][y][x];
        lbuf[y*lwidth+x] = 
            (uint32_t(data & 0x1f) << 3) |
            (uint32_t((data >> 5) & 0x3f) << 10) |
            (uint32_t((data >> 11) & 0x1f) << 19) |
            0xFF000000;
      }
    }
    write_bmp(stringf("../../test/ppudebug/layer%d_full.bmp", layerNo), lwidth, lheight, lbuf);
    delete []lbuf;
  }

uint8_t *numbuf =
        memptr +
        (ppu_regs[ppu_text_begin[layerNo] + ppu_text_chnumarray] & 0x01FFFFFF);

  if (check_bit(ctrl, ppu_tctrl_bitmap)) {
    std::ofstream out(stringf("../../test/ppudebug/layer%d_bmp.txt", layerNo));
    for (int y = 0; y < lheight*2; y++) {
      uint32_t lineBegin = get_uint32le(&(numbuf[y * 4]));
      out << stringf("%08x %08x %08x %08x\n", get_uint32le(&(numbuf[y*16+0])), get_uint32le(&(numbuf[y*16+4])), get_uint32le(&(numbuf[y*16+8])), get_uint32le(&(numbuf[y*16+12])));
    }
  } else {
    std::ofstream out(stringf("../../test/ppudebug/layer%d.csv", layerNo));
    if (!out)
      printf("failed to open debug log!\n");

    int chwidth = ppu_text_sizes[get_bits(attr, 4, 2)];
    int chheight = ppu_text_sizes[get_bits(attr, 6, 2)];
    bool reg_mode = check_bit(ctrl, ppu_tctrl_regmode);
    int gridwidth = lwidth / chwidth;
    int gridheight = lheight / chheight;
    uint8_t *datbuf =
        memptr + (ppu_regs[ppu_text_databufptrs[layerNo][0]] & 0x01FFFFFF);
    for (int y = 0; y < gridheight; y++) {
      for (int x = 0; x < gridwidth; x++) {
        uint32_t chnum = get_uint16le(&(numbuf[(gridwidth * y + x) * 2]));
        uint16_t chattr;
        if (reg_mode) {
          chattr = attr;
        } else {
          uint32_t attr_offs =
              gridwidth * gridheight * 2 + (gridwidth * y + x) * 2;
          chattr = numbuf[attr_offs] + (uint16_t(numbuf[attr_offs + 1]) << 8U);
        }
        if (reg_mode)
          out << stringf("%04x,", chnum, chattr);
        else
          out << stringf("%04x %04x,", chnum, chattr);
      }
      out << std::endl;
    }
    // dump the first 4096 chars
    uint32_t buf_w = 64*chwidth;
    uint32_t buf_h = 64*chheight;
    uint32_t *buf = new uint32_t[buf_w*buf_h];
    for (int y = 0; y < 64; y++) {
      for (int x = 0; x < 64; x++) {
        int chno = y*64 + x;
        int bank = get_bits(attr, 8, 5);
        int bpp = ppu_bpp_values[attr & 0x03];
        if (rgb565 || argb1555)
          bpp = 16;
        bool trans = (chno == ppu_regs[ppu_text_trans_iidx + layerNo]);
        if (trans)
          continue;
        // convert char to a format we like
        uint32_t *chfmtd = new uint32_t[chwidth * chheight];
        int chsize = (chwidth * chheight * bpp) / 8;
        RAMToCustomFormat(datbuf + ((chno * chsize) & 0x01FFFFFF), chfmtd, chwidth * chheight,
                          bank, argb1555, rgb565, bpp, false);
        for (int dy = 0; dy < chheight; dy++) {
          for (int dx = 0; dx < chwidth; dx++) {
            uint16_t data = chfmtd[dy*chwidth + dx];
            // RGB565 -> bmp ARGB8888
            buf[(y*chheight+dy)*buf_w+(x*chwidth+dx)] = 
              (uint32_t(data & 0x1f) << 3) |
              (uint32_t((data >> 5) & 0x3f) << 10) |
              (uint32_t((data >> 11) & 0x1f) << 19) |
              0xFF000000;
          }
        }
        delete [] chfmtd;
      }
    }
    write_bmp(stringf("../../test/ppudebug/layer%d_tiles.bmp", layerNo), buf_w, buf_h, buf);
    delete[] buf;
  }
}

static void PPUDebugSprites() {
  std::ofstream out("../../test/ppudebug/sprites.txt");
  for (int idx = 0; idx < 512; idx++) {
    if (idx < ppu_regs[ppu_sprite_maxnum]) {
      uint32_t num = ppu_regs[ppu_sprite_begin + 2 * idx];
      uint32_t attr = ppu_regs[ppu_sprite_begin + 2 * idx + 1];
      int chwidth = ppu_text_sizes[get_bits(attr, 4, 2)];
      int chheight = ppu_text_sizes[get_bits(attr, 6, 2)];
      uint16_t chnum = num & 0xFFFF;
      uint16_t xpos = (num >> 16) & 0x3FF;
      uint16_t ypos = (attr >> 16) & 0x3FF;
      bool rgb = check_bit(num, 26);
      bool rgb565 = check_bit(num, 27);
      uint8_t *dataptr =
          (memptr + (ppu_regs[ppu_sprite_data_begin_ptr] & 0x01FFFFFF));
      if (xpos != 0) {
        out << stringf("sprite %d at (%d, %d), chr %d, begin 0x%08x, n %08x, attr 0x%08x\n", idx, xpos, ypos,
               chnum, ppu_regs[ppu_sprite_data_begin_ptr], num, attr);
      }
    }
  }
  // rotation table?
  out << std::endl;
  out << "rz table: " << std::endl;
  for (int idx = 0; idx < 8; idx += 1) {
    int base = 0x40 + 4 * idx;
    out << stringf("%d: hx %4d hy %4d vx %4d vy %4d\n", idx, ppu_regs[base], ppu_regs[base+1], ppu_regs[base+2], ppu_regs[base+3]);
  }
}

static void PPUDebug() {
  for (int i = 0; i < 3; i++)
    PPUDebugTextLayer(i);
  PPUDebugSprites();
  PPUDebugRegisters();
}


void PPUUpdate() {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT)
      break;
    if (e.type == SDL_KEYDOWN)
        if (!e.key.repeat) {
          if (e.key.keysym.scancode == SDL_SCANCODE_F1)
            PPUDebug();
        }
    IRGamepadEvent(&e);
  }
  PPURender();
  // SDL_Delay(1000);
}

void PPUTick() {
  // simulate some kind of vblank to keep the app happy
  if (curr_line == 800) {
    curr_line = 0;
    if (check_bit(ppu_regs[ppu_irq_control], ppu_irq_vblkstart)) {
      SetIRQState(ppu_intno_vblkstart, true);
      set_bit(ppu_regs[ppu_irq_status], ppu_irq_vblkstart);
    }
  } else if (curr_line == 50) {
    curr_line++;
    PPUUpdate();
    if (check_bit(ppu_regs[ppu_irq_control], ppu_irq_vblkend)) {
      SetIRQState(ppu_intno_vblkend, true);
      set_bit(ppu_regs[ppu_irq_status], ppu_irq_vblkend);
    }

  } else {
    curr_line++;
  }
}

const Peripheral PPUPeripheral = {"PPU", InitPPUDevice, PPUDeviceReadHandler,
                                  PPUDeviceWriteHandler};
}
