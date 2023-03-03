#include "spu.h"
#include "okiadpcm.h"

#include "../system.h"
#include "../sys/irq_if.h"

#include <stdio.h>
#include <SDL2/SDL.h>
#include <mutex>
#include <deque>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace Emu293 {

static uint32_t spu_regs[16384];
static uint8_t *memptr = nullptr;

bool spu_debug_flag = false;
static int wave_file = -1;
static int wave_samples = 0;

static constexpr int wave_channels = 26;
const int wave_rate = 48000;
const int wave_bytes = 2;

void InitSPUDevice(PeripheralInitInfo initInfo) {
  memptr = get_dma_ptr(0xA0000000);
}

const int chen = 0x0400;
const int chstopsts = 0x040B;
const int chsts = 0x040F;
const int spu_bank = 0x041F;

const int spu_softch_ctrl = 0x0422;
const int spu_softch_compctrl = 0x0419;
const int spu_softch_basel = 0x0420;
const int spu_softch_baseh = 0x0421;
const int spu_softch_ptr = 0x042C;

const int spu_softch_ctrl_irqst = 15;
const int spu_softch_ctrl_irqen = 14;

const int spu_beatbasecnt = 0x0404;
const int spu_beatcnt = 0x0405;

const int spu_ctrl = 0x040D;
const int spu_ctrl_softch_en = 12;

const int spu_softch_irq = 0x3f;
const int spu_beat_irq = 0x3e;

const int uoffset = 0x0100;

const int chan_wavaddr = 0;
const int chan_mode = 1;
const int chan_loopaddr = 2;
const int chan_pan = 3;
const int chan_env0 = 4;
const int chan_envd = 5;
const int chan_env1 = 6;
const int chan_envah = 7;
const int chan_enval = 8;
const int chan_wavd0 = 9;
const int chan_loopct = 10;
const int chan_wavd = 11;
const int chan_adpcm = 13;
const int chan_exaddr = 14;

const int ch_envmode = 0x415;
const int ch_tonerel = 0x416;
const int ch_rampdown = 0x40a;

static const int channel_start(int i) {
  if (i < 16)
    return (0x0000 + 64*i)/4;
  else
    return (0x0400 + 64*(i-16))/4;
}

static const int channel_phase_start(int i) {
  if (i < 16)
    return (0x0800 + 64*i)/4;
  else
    return (0x0c00 + 64*(i-16))/4;
}

struct SPUChState {
  oki_adpcm_state adpcm32;
  uint32_t env_divcnt, env_clk, rampdown_divcnt;
  uint32_t nib_addr, env_addr;
  uint16_t adpcm36_header, adpcm36_remain;
  uint16_t last_samp;
  int8_t curr_env;
  int16_t adpcm36_prev[2];
  float iirl, iirr;
  void reset(bool loop = false) {
    adpcm32.reset();
    adpcm36_header = 0;
    adpcm36_remain = 0;
    adpcm36_prev[0] = 0;
    adpcm36_prev[1] = 0;
    if (!loop) {
      iirl = 0;
      iirr = 0;
      env_divcnt = 0;
      rampdown_divcnt = 0;
      last_samp = 0x8000;
    }
  }
  void state(SaveStater &s) {
    s.tag("SPUCH");
    s.i(nib_addr);
    s.i(env_addr);
    s.i(env_divcnt);
    s.i(rampdown_divcnt);
    s.i(adpcm36_header);
    s.i(adpcm36_remain);
    s.i(adpcm36_prev[0]);
    s.i(adpcm36_prev[1]);
    s.i(last_samp);
    s.i(curr_env);
  }
} spu_channels[24];


inline uint32_t get_startaddr(int ch, bool loop = false) {
  int ca = channel_start(ch);
  uint32_t base = (loop ? spu_regs[ca+chan_loopaddr] : spu_regs[ca+chan_wavaddr]) & 0xFFFF;
  uint32_t hi = (loop ? (spu_regs[ca+chan_mode] >> 6) : spu_regs[ca+chan_mode]) & 0x3F;
  uint32_t xaddr = spu_regs[ca+chan_exaddr] & 0xFF;
  return (xaddr << 23) | (hi << 17) | (base << 1);
}

inline uint32_t get_envaddr(int ch) {
  int ca = channel_start(ch);
  uint32_t base = spu_regs[ca+chan_enval] & 0xFFFF;
  uint32_t hi = spu_regs[ca+chan_envah] & 0x3F;
  uint32_t xaddr = spu_regs[ca+chan_exaddr] & 0xFF;
  return (xaddr << 23) | (hi << 17) | (base << 1);
}

static void dump_samples(int i) {
  for (int j = 0; j < 2; j++) {
    printf("   %s: \n", j ? "loop" : "wave");
    uint32_t a = get_startaddr(i, j);
    while (true) {
      uint16_t samp = get_uint16le(memptr + (a & 0x03FFFFFE));
      if (samp == 0xFFFF)
        break;
      printf("%d,", int16_t(samp ^ 0x8000));
      a += 2;
    }
    printf("\n");
  }
}

static void dump_channnel(int i) {
  int a = channel_start(i);
  printf("channel %d\n", i);
  printf("   start   %08x\n", get_startaddr(i));
  printf("   waddr   %04x\n", spu_regs[a+0]);
  printf("   mode    %04x\n", spu_regs[a+1]);
  printf("   laddr   %04x\n", spu_regs[a+2]);
  printf("   pan     %04x\n", spu_regs[a+3]);
  printf("   env0    %04x\n", spu_regs[a+4]);
  printf("   envd    %04x\n", spu_regs[a+5]); 
  printf("   env1    %04x\n", spu_regs[a+6]); 
  printf("   eaddr0  %04x\n", spu_regs[a+7]); 
  printf("   eaddr1  %04x\n", spu_regs[a+8]); 
  printf("   wavd0   %04x\n", spu_regs[a+9]); 
  printf("   loopct  %04x\n", spu_regs[a+10]); 
  printf("   wavdat  %04x\n", spu_regs[a+11]);
  printf("   adpcm   %04x\n", spu_regs[a+13]);
  int p = channel_phase_start(i);
  printf("   phase  %05x\n", spu_regs[p+0]);
  printf("   acc    %05x\n", spu_regs[p+1]);
  printf("   tphase %05x\n", spu_regs[p+2]);
  printf("   pctrl  %05x\n", spu_regs[p+3]);
  if(!check_bit(spu_regs[ch_envmode + ((i >= 16) ? uoffset : 0)], i % 16))
    printf("   auto env\n");
  // if ((spu_regs[a+1] & 0xF000) == 0x6000) {
  //   dump_samples(i);
  // }
}

static void start_channel(int ch, bool loop = false) {
  spu_channels[ch].reset(loop);
  spu_regs[chsts + ((ch >= 16) ? uoffset : 0)] |= (1 << (ch % 16)); // channel busy
  // nibble address
  spu_channels[ch].nib_addr = (get_startaddr(ch, loop) & 0x03FFFFFF) * 2;
  if (check_bit(spu_regs[channel_start(ch)+chan_mode], 15) &&
        check_bit(spu_regs[channel_start(ch)+chan_adpcm], 15))
    printf("channel %d started in ADPCM36 mode!\n", ch);
  // envelope divider
  uint16_t env_clk_reg = spu_regs[0x406 + ((ch % 16) / 4) + ((ch >= 16) ? uoffset : 0)];
  uint32_t clk_val = (env_clk_reg >> ((ch % 4) * 4)) & 0xF;
  if (clk_val >= 0b1011)
    clk_val = 0b1011;
  if (!loop) {
    spu_channels[ch].env_clk = 4 * (4 << clk_val);
    spu_channels[ch].env_addr = (get_envaddr(ch) & 0x03FFFFFF);
    spu_channels[ch].curr_env = spu_regs[channel_start(ch)+chan_envd] & 0x7F;
  }
}

static void stop_channel(int ch) {
  spu_channels[ch].reset();
  spu_regs[chsts + ((ch >= 16) ? uoffset : 0)] &= ~(1 << (ch % 16)); // channel not busy
  spu_regs[chsts + ((ch >= 16) ? uoffset : 0)] &= ~(1 << (ch % 16)); // channel stoped
  spu_regs[chen + ((ch >= 16) ? uoffset : 0)] &= ~(1 << (ch % 16)); // channel disabled
  spu_regs[ch_rampdown + ((ch >= 16) ? uoffset : 0)] &= ~(1 << (ch % 16)); // channel not ramping down
  spu_regs[channel_start(ch)+chan_wavd] = 0x8000; // silence
  spu_regs[channel_start(ch)+chan_mode] &= 0x7FFF; // clear ADPCM bit
}

static uint16_t decode_adpcm36(int ch, uint8_t data) {
  // from https://github.com/mamedev/mame/blob/master/src/devices/machine/spg2xx_audio.cpp
  // credits: Ryan Holtz,Jonathan Gevaryahu
  auto &state = spu_channels[ch];
  int32_t shift = state.adpcm36_header & 0xf;
  int16_t filter = (state.adpcm36_header & 0x3f0) >> 4;
  int16_t f0 = filter | ((filter & 0x20) ? ~0x3f : 0); // sign extend
  int32_t f1 = 0;
  int16_t sdata = data << 12;
  sdata = (sdata >> shift) + (((state.adpcm36_prev[0] * f0) + (state.adpcm36_prev[1] * f1) + 32) >> 12);
  state.adpcm36_prev[1] = state.adpcm36_prev[0];
  state.adpcm36_prev[0] = sdata;
  return (uint16_t)sdata ^ 0x8000;
}

static void tick_envelope(int ch) {
  int ca = channel_start(ch);
  int ph = channel_phase_start(ch);
  uint8_t envinc = spu_regs[ca+chan_env0] & 0x7F;
  int16_t env = spu_regs[ca+chan_envd] & 0x7F;
  int16_t env_targ = (spu_regs[ca+chan_env0] >> 8) & 0x7F;
  bool auto_mode = !check_bit(spu_regs[ch_envmode + ((ch >= 16) ? uoffset : 0)], ch % 16);
  if ((envinc != 0) || auto_mode) {
    bool envsgn = check_bit(spu_regs[ca+chan_env0], 7);
    uint8_t cnt = (spu_regs[ca+chan_envd] >> 8) & 0xFF;
    if (cnt == 0) {
      cnt = (spu_regs[ca+chan_env1] & 0xFF);
      env = envsgn ? (env - envinc) : (env + envinc);
      if (env == env_targ && auto_mode) {
        // reload
        spu_regs[ca+chan_env0] = get_uint16le(memptr + (spu_channels[ch].env_addr & 0x03FFFFFE));
        spu_regs[ca+chan_env1] = get_uint16le(memptr + ((spu_channels[ch].env_addr + 2) & 0x03FFFFFE));
        // printf("ch%d env load %04x %04x\n", ch, spu_regs[ca+chan_env0], spu_regs[ca+chan_env1]);
        spu_channels[ch].env_addr += 4;
        // TODO: repeat
      }
    } else {
      cnt--;
    }
    spu_regs[ca+chan_envd] = (spu_regs[ca+chan_envd] & 0xFF) | (uint16_t(cnt & 0xFF) << 8);
  }

  if (env <= 0) {
    stop_channel(ch);
  } else {
    spu_regs[ca+chan_envd] = (spu_regs[ca+chan_envd] & 0xFF80) | (env & 0x7F);
  }
}

static void tick_channel(int ch) {
  if (!check_bit(spu_regs[chen + ((ch >= 16) ? uoffset : 0)], ch % 16))
    return; // channel disabled

  int ca = channel_start(ch);
  int pa = channel_phase_start(ch);

  bool ramp_down = check_bit(spu_regs[ch_rampdown + ((ch >= 16) ? uoffset : 0)], ch % 16);
  if (ramp_down) {
    // ramp down
    ++spu_channels[ch].rampdown_divcnt;
    uint32_t rampdown_sel = (spu_regs[pa+3] >> 16) & 0x7;
    int32_t rampdown_div = 4 * 13 * std::min((4U << (2*rampdown_sel)), 8192U);
    if (spu_channels[ch].rampdown_divcnt >= rampdown_div) {
      spu_channels[ch].rampdown_divcnt = 0;
      int16_t env = spu_regs[ca+chan_envd] & 0x7F;
      int16_t delta = ((spu_regs[ca+chan_loopct] >> 9) & 0x3F);
      env -= delta;
      if (env <= 0) {
        stop_channel(ch);
      } else {
        spu_regs[ca+chan_envd] = (spu_regs[ca+chan_envd] & 0xFF80) | (env & 0x7F);
      }
    }
  }
  // update envelope
  ++spu_channels[ch].env_divcnt;
  if (spu_channels[ch].env_divcnt >= spu_channels[ch].env_clk) {
    spu_channels[ch].env_divcnt = 0;
    tick_envelope(ch);
  }

  // update phase accumulator
  spu_regs[pa+1] += spu_regs[pa+0];
  if (!check_bit(spu_regs[pa+1], 19))
    return; // no tick as no overflow
  spu_regs[pa+1] &= 0x7FFFF;
  uint16_t mode = spu_regs[ca+chan_mode];
  bool adpcm = check_bit(mode, 15);
  bool adpcm36 = check_bit(spu_regs[ca+chan_adpcm], 15);
  uint8_t tone_mode = (mode >> 12) & 0x3;
  bool m16 = check_bit(mode, 14);

  if (tone_mode == 0x00) {
    printf("SPU: SW channels not supported!\n");
    stop_channel(ch);
  }
  spu_channels[ch].last_samp = spu_regs[ca+chan_wavd];

  int nibs = 1; // more for non-ADPCM modes..
  auto get_sample = [&]() {
    uint16_t fetch = get_uint16le(memptr + ((spu_channels[ch].nib_addr >> 1) & 0x03FFFFFE));
    if (adpcm) {
      if (adpcm36) {
        if (spu_channels[ch].adpcm36_remain == 0) {
          // fetch new adpcm36 header
          spu_channels[ch].adpcm36_header = fetch;
          spu_channels[ch].nib_addr += 4;
          fetch = get_uint16le(memptr + ((spu_channels[ch].nib_addr >> 1) & 0x03FFFFFE));
          spu_channels[ch].adpcm36_remain = 8;
        } else if ((spu_channels[ch].nib_addr & 0x3) == 0x3) {
          --spu_channels[ch].adpcm36_remain;
        }
        if (fetch == 0xFFFF && get_uint16le(memptr + (((spu_channels[ch].nib_addr >> 1) - 2) & 0x03FFFFFE)) == 0xFFFF)
          return false;
        uint16_t nib = (fetch) >> (4 * (spu_channels[ch].nib_addr & 0x3));
        spu_regs[ca+chan_wavd] = decode_adpcm36(ch, nib & 0xF);
      } else {
        if (fetch == 0xFFFF)
          return false;
        uint16_t nib = (fetch) >> (4 * (spu_channels[ch].nib_addr & 0x3));
        spu_regs[ca+chan_wavd] = uint16_t(spu_channels[ch].adpcm32.clock(nib & 0xF) << 4) ^ uint16_t(0x8000);
      }
    } else {
      if (m16) {
        // 16-bit PCM
        nibs = 4;
        if (fetch == 0xFFFF)
          return false;
        spu_regs[ca+chan_wavd] = fetch;
      } else {
        // 8-bit PCM
        nibs = 2;
        uint16_t byt = ((fetch >> (4 * (spu_channels[ch].nib_addr & 0x3))) & 0xFF);
        if (byt == 0xFF)
          return false;
        spu_regs[ca+chan_wavd] = byt << 8;
      }
    } 
    // zero crossing
    if ((spu_regs[ca+chan_wavd] ^ spu_channels[ch].last_samp) & 0x8000) {
      spu_channels[ch].curr_env = spu_regs[ca+chan_envd] & 0x7F;
    }
    spu_channels[ch].nib_addr += nibs;
    return true;
  };

  if (!get_sample()) { // end of data
    if (check_bit(spu_regs[ch_tonerel + ((ch >= 16) ? uoffset : 0)], ch % 16)) {
      printf("tone release %d!\n", ch);
      // tone release - play release tone then stop
      clear_bit(spu_regs[ch_tonerel + ((ch >= 16) ? uoffset : 0)], ch % 16);
      spu_channels[ch].nib_addr += nibs;
    } else if (tone_mode == 2) {
      start_channel(ch, true); // repeat
      get_sample(); // compensate for the 'FFFF' we swallowed...
    } else {
      stop_channel(ch); // stop
    }
  }

  // TODO: envelope, repeat, non-ADPCM, etc
}

static float spu_rate_conv = 0;
static float softch_phase = 0;
static int16_t softch_l, softch_r;

static int softch_buf_size(int size) {
  int lo = size & (0x3);
  int hi = (size >> 3) & 0x1;
  return 0x100 * (1<<(lo|(hi << 2)));
}

void tick_softch() {
  if (!check_bit(spu_regs[spu_ctrl], spu_ctrl_softch_en))
    return;
  // softch freq is in 1/27MHz units; we run at 281.25kHz
  softch_phase += 96.f / float(spu_regs[spu_softch_compctrl] & 0xFFFF);
  if (softch_phase < 1.f)
    return;
  // an actual tick....
  softch_phase -= 1.f;
  int ctrl = spu_regs[spu_softch_ctrl];
  uint32_t base = (spu_regs[spu_softch_baseh] << 16) | (spu_regs[spu_softch_basel] & 0xFFFF);
  int half_size = softch_buf_size(ctrl & 0xF) / 2;
  bool stereo = (ctrl & 0x4);
  // TODO: what unit is buf_size in, probably samples...
  uint32_t ptr = spu_regs[spu_softch_ptr];
  uint32_t idx = base + ptr * (stereo ? 4 : 2);
  softch_l = int16_t(get_uint16le(memptr + (idx & 0x03FFFFFE)) ^ 0x8000);
  if (stereo) {
    softch_r = int16_t(get_uint16le(memptr + ((idx + 2) & 0x03FFFFFE)) ^ 0x8000);
  } else {
    softch_r = softch_l;
  }
  // TODO: actual fetch and play...
  uint32_t next_ptr = (ptr + 1) % (2*half_size);
  // trigger fiq per half size?
  if ((next_ptr ^ ptr) & (half_size)) {
    // IRQ (called a FIQ in some places but how does it differ?)
    if (check_bit(ctrl, spu_softch_ctrl_irqen)) {
      SetIRQState(spu_softch_irq, true);
      set_bit(spu_regs[spu_softch_ctrl], spu_softch_ctrl_irqst);
    }
  }
  spu_regs[spu_softch_ptr] = next_ptr;
}

static void spu_tick() {
  for (int ch = 0; ch < 24; ch++)
    tick_channel(ch);
  tick_softch();
}

static void spu_mix_channels(int16_t &l, int16_t &r) {
  int32_t lm = 0, rm = 0;
  uint16_t wave_chunk[wave_channels];
  std::fill(wave_chunk, wave_chunk+wave_channels, 0x0);
  for (int ch = 0; ch < 24; ch++) {
    if (!check_bit(spu_regs[chen + ((ch >= 16) ? uoffset : 0)], ch % 16))
      continue; // channel disabled
    int ca = channel_start(ch);
    int pa = channel_phase_start(ch);
    int phase = spu_regs[pa+1];
    float lerp_factor = float(phase) / float(1<<19);
    int32_t last_samp = int16_t(spu_channels[ch].last_samp ^ uint16_t(0x8000));
    int32_t samp = int16_t(uint16_t(spu_regs[ca+chan_wavd]) ^ uint16_t(0x8000));
    int32_t lerp_samp = int32_t(samp * lerp_factor + last_samp * (1.f-lerp_factor));
    samp = (lerp_samp * int32_t(spu_channels[ch].curr_env & 0x7F)) / (1<<7);
    wave_chunk[ch + 2] = lerp_samp; 
    int32_t vol = int32_t(spu_regs[ca+chan_pan] & 0x7F);
    int32_t pan = int32_t((spu_regs[ca+chan_pan] >> 8) & 0x7F);
    int32_t pan_l = 0, pan_r = 0;
    if (pan < 0x40) {
      pan_l = 0x7f * vol;
      pan_r = pan * 2 * vol;
    } else {
      pan_l = (0x7f - pan) * 2 * vol;
      pan_r = 0x7f * vol;
    }
    int32_t lf = (samp * pan_l) / (1<<14);
    int32_t rf = (samp * pan_r) / (1<<14);
    float alpha = 0.33;
    spu_channels[ch].iirl = spu_channels[ch].iirl * alpha + lf * (1.0f-alpha);
    spu_channels[ch].iirr = spu_channels[ch].iirr * alpha + rf * (1.0f-alpha);
    lm += int32_t(spu_channels[ch].iirl);
    rm += int32_t(spu_channels[ch].iirr);
  }
  if (check_bit(spu_regs[spu_ctrl], spu_ctrl_softch_en)) {
    lm += int32_t(softch_l);
    rm += int32_t(softch_r);
  }
  lm /= 8;
  rm /= 8;
  l = std::min<int32_t>(std::max<int32_t>(-32767, lm), 32767);
  r = std::min<int32_t>(std::max<int32_t>(-32767, rm), 32767);
  wave_chunk[0] = l;
  wave_chunk[1] = r;
  if (wave_file > 0) {
    write(wave_file, reinterpret_cast<const void*>(wave_chunk), 2*wave_channels);
    wave_samples += 1;
  }
}

void start_softch() {
  spu_regs[spu_softch_ptr] = 0; // reset pointer
  softch_phase = 0;
  softch_l = 0;
  softch_r = 0;
}


void SPUDeviceWriteHandler(uint16_t addr, uint32_t val) {
  // printf("SPU write %04x %08x\n", addr, val);
  addr /= 4;
  if(addr == chen || addr == (chen+uoffset)) {
    // channel enable
    for (int i = 0; i < 16; i++) {
      if (!(spu_regs[addr] & (1<<i)) && (val & (1 << i))) {
        // dump_channnel(i + ((addr & uoffset) ? 16 : 0));
        // set channel busy
        start_channel(i + ((addr & uoffset) ? 16 : 0));
      } else if ((spu_regs[addr] & (1<<i)) && !(val & (1 << i))) {
        stop_channel(i + ((addr & uoffset) ? 16 : 0));
      }
    }
  } else if (addr == spu_ctrl) {
    if (check_bit(val, spu_ctrl_softch_en) && !check_bit(spu_regs[addr], spu_ctrl_softch_en)) {
      start_softch();
    }
  }
  spu_regs[addr] = val;
  if (addr == spu_softch_ctrl) {
    if (check_bit(val, spu_softch_ctrl_irqst)) {
      clear_bit(spu_regs[addr], spu_softch_ctrl_irqst);
      SetIRQState(spu_softch_irq, false);
    }
  } else if (addr == spu_beatcnt) {
    if (check_bit(val, 14)) {
      clear_bit(spu_regs[addr], 14);
      SetIRQState(spu_beat_irq, false);
    }
  }
}

uint32_t SPUDeviceReadHandler(uint16_t addr) {
  // printf("SPU read %04x %08x\n", addr, spu_regs[addr/4]);
  return spu_regs[addr/4];
}

void SPUDeviceResetHandler() {
  for (auto &r : spu_regs)
    r = 0;
  for (auto &ch : spu_channels)
    ch.reset();
}

void SPUDeviceStateHandler(SaveStater &s) {
  s.tag("SPU");
  s.a(spu_regs);
  for (auto &ch : spu_channels)
    ch.state(s);
}

static SDL_AudioDeviceID audio_dev;

std::mutex spu_buf_mutex;

static int64_t spu_t0 = 0;
static int64_t samp_t0 = 0;

static int64_t spu_time() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int64_t samp_period = 20833;
static int64_t min_speriod = 20700;
static int64_t max_speriod = 20920;

static constexpr int16_t max_buf_size = 4096;
std::deque<std::pair<int16_t, int16_t>> audio_buf;

static int64_t update_t = 0;
static int ticks = 0;
static int samps = 0;
static int beat_base_count = 0;

void SPUUpdate() {
  int64_t curr_time = spu_time();
  if ((curr_time - samp_t0) > samp_period) {
    std::lock_guard<std::mutex> spu_lock(spu_buf_mutex);
    // we need to provide audio
    spu_rate_conv += (1.f / 48000.f);
    int16_t l, r;
    spu_mix_channels(l, r);
    audio_buf.emplace_back(l, r);
    if (audio_buf.size() >= max_buf_size) {
      // overflow
      while (audio_buf.size() >= (max_buf_size-100))
        audio_buf.pop_front();
      if (samp_period < max_speriod)
        samp_period += 20;
    }
    samp_t0 += samp_period;
    ++samps;
  }
  bool beat_en = check_bit(spu_regs[spu_beatcnt], 15);
  int beat_period = 4 * (spu_regs[spu_beatbasecnt] & 0x3ff);
  while (spu_rate_conv > 0) {
    spu_tick();
    spu_rate_conv -= (1.f / 281250.f);
    if (beat_en) {
      ++beat_base_count;
      if (beat_base_count >= beat_period) {
        int beat_cnt = spu_regs[spu_beatcnt] & 0x3fff;
        if (beat_cnt > 0) {
          --beat_cnt;
          if (beat_cnt == 0) {
            SetIRQState(spu_beat_irq, true);
            set_bit(spu_regs[spu_beatcnt], 14);
          }
        }
        spu_regs[spu_beatcnt] = (spu_regs[spu_beatcnt] & 0xc000) | (beat_cnt & 0x3fff);
        beat_base_count = 0;
      }
    } else {
      beat_base_count = 0;
    }
    ++ticks;
  }
#if 0
  if ((curr_time - update_t) > 1000000000) {
    printf("%d %d %d\n", ticks, samps, samp_period);
    update_t = curr_time;
    ticks = 0;
    samps = 0;
  }
#endif
}

void audio_callback(void *userdata, uint8_t* stream, int len) {
  bool underflow = false;
  int16_t l = 0, r = 0;
  for (int i = 0; i < len; i += 4) {
    std::lock_guard<std::mutex> spu_lock(spu_buf_mutex);

    if (audio_buf.size() > 8) {
      l = audio_buf.front().first;
      r = audio_buf.front().second;

      audio_buf.pop_front();
    } else {
      underflow = true;
    }

    stream[i+0] = (l & 0xFF);
    stream[i+1] = ((l >> 8) & 0xFF);
    stream[i+2] = (r & 0xFF);
    stream[i+3] = ((r >> 8) & 0xFF);
  }
  if (underflow) {
    // printf("underflow!!\n");
    if (samp_period > min_speriod)
      samp_period -= 20;
  }
}

void SPUInitSound() {
  SDL_AudioSpec want, have;

  spu_t0 = spu_time();
  samp_t0 = spu_time();

  SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
  want.freq = 48000;
  want.format = AUDIO_S16LSB;
  want.channels = 2;
  want.samples = 256;
  want.callback = audio_callback; // because we use SDL_QueueAudio
  audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (audio_dev == 0) {
    printf("failed to open audio device: %s!\n", SDL_GetError());
    exit(1);
  }
  SDL_PauseAudioDevice(audio_dev, 0);
  if (spu_debug_flag) {
    wave_file = creat("../../test/ppudebug/spu_wave.wav", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (wave_file < 0) {
      printf("Failed to open debug wave file.\n");
      exit(1);
    }
    // header will be filled in later
    static constexpr int hdr_size = 0x2c;
    uint8_t padding[hdr_size];
    std::fill(padding, padding+hdr_size, 0x00);
    write(wave_file, padding, hdr_size);
  }
}

void ShutdownSPU() {
  if (wave_file > 0) {
    // append wave header
    static constexpr int hdr_size = 0x2c;
    int data_size = wave_samples * wave_channels * 2;
    int file_size = data_size + hdr_size;
    uint8_t header[hdr_size];
    memcpy(reinterpret_cast<void*>(header+0), reinterpret_cast<const void*>("RIFF"), 4);
    set_uint32le(header+4, data_size-8);
    memcpy(reinterpret_cast<void*>(header+8), reinterpret_cast<const void*>("WAVE"), 4);
    memcpy(reinterpret_cast<void*>(header+12), reinterpret_cast<const void*>("fmt "), 4);
    set_uint32le(header+16, 16);
    set_uint16le(header+20, 0x0001); // PCM
    set_uint16le(header+22, wave_channels);
    set_uint32le(header+24, wave_rate);
    set_uint32le(header+28, wave_rate*wave_channels*2);
    set_uint16le(header+32, wave_channels*2);
    set_uint16le(header+34, 16);
    memcpy(reinterpret_cast<void*>(header+36), reinterpret_cast<const void*>("data"), 4);
    set_uint32le(header+40, data_size);
    pwrite(wave_file, reinterpret_cast<void*>(header), hdr_size, 0);
    close(wave_file);
  }
}

const Peripheral SPUPeripheral = {"SPU", InitSPUDevice, SPUDeviceReadHandler,
                                  SPUDeviceWriteHandler, SPUDeviceResetHandler,
                                  SPUDeviceStateHandler};

};