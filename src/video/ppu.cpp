#include "ppu.h"
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
const int ppu_layer_height[4] = {256, 512, 512, 512};

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
static uint32_t textLayers[3][512][1024];
uint16_t rendered[480][640];

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
    if ((data & 0x40000000) == 0) {
      surface = data & 0xFFFF;
    } else {
      uint16_t r0, g0, b0, r1, g1, b1, r, g, b;
      r0 = data & 0x1F;
      r1 = surface & 0x1F;
      g0 = (data >> 5) & 0x3F;
      g1 = (surface >> 5) & 0x3F;
      b0 = (data >> 11) & 0x1F;
      b1 = (surface >> 11) & 0x1F;
      uint8_t beta = 63 - alpha;
      r = (alpha * r0 + beta * r1) >> 6;
      g = (alpha * g0 + beta * g1) >> 6;
      b = (alpha * b0 + beta * b1) >> 6;
      surface = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
    }
  }
}

static void MergeTextLayer(int layerNo) {
  int swidth = ppu_screen_width[ppu_regs[ppu_control] & 0x03];
  int sheight = ppu_screen_height[ppu_regs[ppu_control] & 0x03];
  int lwidth = ppu_layer_width[ppu_regs[ppu_control] & 0x03];
  int lheight = ppu_layer_height[ppu_regs[ppu_control] & 0x03];

  if (check_bit(ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl],
                ppu_tctrl_enable)) {
    int offX = sign_extend(
        ppu_regs[ppu_text_begin[layerNo] + ppu_text_xpos] & 0x3FF, 10);
    int offY = sign_extend(
        ppu_regs[ppu_text_begin[layerNo] + ppu_text_ypos] & 0x1FF, 9);
    bool hmve = check_bit(ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl],
                          ppu_tctrl_hmoveen);
    int alpha = ppu_regs[ppu_text_begin[layerNo] + ppu_text_blendlevel] & 0x3F;
    bool blnden = check_bit(ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl],
                            ppu_tctrl_blenden);
    // TODO: HCMP/VCMP support
    for (int y = 0; y < sheight; y++) {
      int ly = (offY + y) % lheight;
      int mvx = offX;
      if (hmve) {
        mvx += (ppu_regs[ppu_text_hmve_start + ly] & 0x1FF);
      }
      for (int x = 0; x < swidth; x++) {
        if (blnden) {
          BlendCustomFormatToSurface(
              textLayers[layerNo][ly][(x + mvx) % lwidth], alpha,
              rendered[y][x]);

        } else {
          BlendCustomFormatToSurface(
              textLayers[layerNo][ly][(x + mvx) % lwidth], 63, rendered[y][x]);
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
        val |= get_bits_msbfirst(in[inIndex], 0, inBitIndex - 2);
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
                                        int bank, int bpp) {
  int offset = 0;
  if (bpp == 2)
    offset = bank * 16; // datasheet says this, but dubious myself
  else
    offset = bank * (1 << bpp);
  for (int i = 0; i < count; i++) {
    out[i] = Argb1555ToCustomFormat(
        uint16_t(ppu_regs[ppu_palette_begin + offset + in[i]] & 0xFFFF));
  }
}

static inline void RAMToCustomFormat(uint8_t *ram, uint32_t *out, int count,
                                     int pbank, bool argb1555, bool rgb565,
                                     int bpp) {
  if ((!argb1555) & (!rgb565)) { // palette encoded
    uint8_t *temp = new uint8_t[count];
    UnpackByteArray(ram, temp, count, bpp);
    DepalettizeByteArray(temp, out, count, pbank, bpp);
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
  // uint32_t r_attr = ppu_regs[ppu_text_begin[layerNo] + ppu_text_attr];
  int lwidth = ppu_layer_width[ppu_regs[ppu_control] & 0x03];
  int lheight = ppu_layer_height[ppu_regs[ppu_control] & 0x03];

  uint8_t *ramBuf =
      memptr +
      (ppu_regs[ppu_text_begin[layerNo] + ppu_text_chnumarray] & 0x01FFFFFF);
  // always use attribute array in bitmap mode???
  uint16_t attr = ramBuf[lheight * 4 + line * 2] |
                  (uint16_t(ramBuf[lheight * 4 + line * 2 + 1]) << 8);
  uint32_t lineBegin = get_uint32le(&(ramBuf[line * 4]));
  uint8_t *linebuf = memptr + (lineBegin & 0x01FFFFFF);
  int bpp;
  if (rgb565 | argb1555) {
    bpp = 16;
  } else {
    bpp = ppu_bpp_values[attr & 0x03];
  }
  int bank = get_bits(attr, 8, 5);
  RAMToCustomFormat(linebuf, out, lwidth, bank, argb1555, rgb565, bpp);
}

static void RenderTextChar(uint8_t *chbuf, uint16_t attr, uint32_t chno,
                           int chwidth, int chheight, int posx, int posy,
                           int layerNo) {
  bool hflip = check_bit(attr, ppu_tattr_hflip);
  bool vflip = check_bit(attr, ppu_tattr_vflip);
  int bank = get_bits(attr, 8, 5);
  int bpp = bpp = ppu_bpp_values[attr & 0x03];

  // convert char to a format we like
  uint32_t *chfmtd = new uint32_t[chwidth * chheight];
  RAMToCustomFormat(chbuf + (chno * (chwidth * chheight * bpp) / 8), chfmtd,
                    chwidth * chheight, bank, false, false, bpp);

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
      textLayers[layerNo][outy][outx] = chfmtd[y * chwidth + x];
    }
  }
  delete[] chfmtd;
}

static void RenderTextLayer(int layerNo) {
  uint32_t attr = ppu_regs[ppu_text_begin[layerNo] + ppu_text_attr];
  uint32_t ctrl = ppu_regs[ppu_text_begin[layerNo] + ppu_text_ctrl];
  int lwidth = ppu_layer_width[ppu_regs[ppu_control] & 0x03];
  int lheight = ppu_layer_height[ppu_regs[ppu_control] & 0x03];

  bool rgb565 = false, argb1555 = false;
  if (check_bit(ctrl, ppu_tctrl_rgb555)) {
    argb1555 = true;
  } else if (check_bit(ctrl, ppu_tctrl_rgb555)) {
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
    int chwidth = ppu_text_sizes[get_bits(attr, 2, 2)];
    int chheight = ppu_text_sizes[get_bits(attr, 4, 2)];
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
        uint32_t chnum = get_uint32le(&(numbuf[(gridwidth * y + x) * 4]));
        uint16_t chattr;
        if (reg_mode) {
          chattr = attr;
        } else {
          uint32_t attr_offs =
              gridwidth * gridheight * 4 + (gridwidth * y + x) * 2;
          chattr = numbuf[attr_offs] + (uint16_t(numbuf[attr_offs + 1]));
        }
        RenderTextChar(datbuf, chattr, chnum, chwidth, chheight, x * chwidth,
                       y * chheight, layerNo);
      }
    }
  }
}
}
