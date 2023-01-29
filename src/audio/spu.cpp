#include "spu.h"
#include "okiadpcm.h"

#include "../system.h"

#include <stdio.h>
#include <SDL2/SDL.h>
#include <mutex>
#include <deque>
#include <utility>

namespace Emu293 {

static uint32_t spu_regs[16384];
static uint8_t *memptr = nullptr;

void InitSPUDevice(PeripheralInitInfo initInfo) {
  memptr = get_dma_ptr(0xA0000000);
}

const int chen = 0x0400;
const int chsts = 0x040F;
const int spu_bank = 0x041F;

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
  uint32_t nib_addr;
  uint16_t adpcm36_header, adpcm36_remain;
  int16_t adpcm36_prev[2];
  float iirl, iirr;
  void reset() {
    adpcm32.reset();
    iirl = 0;
    iirr = 0;
    adpcm36_header = 0;
    adpcm36_remain = 0;
    adpcm36_prev[0] = 0;
    adpcm36_prev[1] = 0;
  }
} spu_channels[24];


uint32_t get_startaddr(int ch) {
  int ca = channel_start(ch);
  uint32_t base = spu_regs[ca+chan_wavaddr] & 0xFFFF;
  uint32_t hi = spu_regs[ca+chan_mode] & 0x3F;
  uint32_t xaddr = spu_regs[ca+chan_exaddr] & 0xFF;
  return (xaddr << 23) | (hi << 17) | (base << 1);
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
  a = channel_phase_start(i);
  printf("   phase  %05x\n", spu_regs[a+0]);
  printf("   acc    %05x\n", spu_regs[a+1]);
  printf("   tphase %05x\n", spu_regs[a+2]);
  printf("   pctrl  %05x\n", spu_regs[a+3]);
}

static void start_channel(int ch) {
  spu_channels[ch].reset();
  spu_regs[chsts + ((ch >= 16) ? uoffset : 0)] |= (1 << (ch % 16)); // channel busy
  // nibble address
  spu_channels[ch].nib_addr = (get_startaddr(ch) & 0x01FFFFFF) * 2;
  if (check_bit(spu_regs[channel_start(ch)+chan_mode], 15) &&
        check_bit(spu_regs[channel_start(ch)+chan_adpcm], 15))
    printf("channel %d started in ADPCM36 mode!\n", ch);
}

static void stop_channel(int ch) {
  spu_channels[ch].reset();
  spu_regs[chsts + ((ch >= 16) ? uoffset : 0)] &= ~(1 << (ch % 16)); // channel not busy
  spu_regs[chen + ((ch >= 16) ? uoffset : 0)] &= ~(1 << (ch % 16)); // channel disabled
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

static void tick_channel(int ch) {
  if (!check_bit(spu_regs[chen + ((ch >= 16) ? uoffset : 0)], ch % 16))
    return; // channel disabled
  // update phase accumulator
  int pa = channel_phase_start(ch);
  spu_regs[pa+1] += spu_regs[pa+0];
  if (!check_bit(spu_regs[pa+1], 19))
    return; // no tick as no overflow
  spu_regs[pa+1] &= 0x7FFFF;
  int ca = channel_start(ch);
  uint16_t mode = spu_regs[ca+chan_mode];
  bool adpcm = check_bit(mode, 15);
  bool adpcm36 = check_bit(spu_regs[ca+chan_adpcm], 15);
  uint8_t tone_mode = (mode >> 12) & 0x3;
  bool m16 = check_bit(mode, 14);

  if (tone_mode == 0x00) {
    printf("SPU: SW channels not supported!\n");
    stop_channel(ch);
  }

  // update envelope
  uint8_t envinc = spu_regs[ca+chan_env0] & 0x7F;
  int16_t env = spu_regs[ca+chan_envd] & 0x7F;

  if (envinc != 0) {
    bool envsgn = check_bit(spu_regs[ca+chan_env0], 7);
    uint8_t cnt = (spu_regs[ca+chan_envd] >> 8) & 0xFF;
    if (cnt == 0) {
      cnt = (spu_regs[ca+chan_env1] & 0xFF);
      env = envsgn ? (env - envinc) : (env + envinc);
    } else {
      cnt--;
    }
  }

  if (env <= 0) {
    stop_channel(ch);
  } else {
    spu_regs[ca+chan_envd] = (spu_regs[ca+chan_envd] & 0xFF80) | (env & 0x7F);
  }

  int nibs = 1; // more for non-ADPCM modes..
  uint16_t fetch = get_uint16le(memptr + ((spu_channels[ch].nib_addr >> 1) & 0x01FFFFFE));

  if (adpcm) {
    if (adpcm36) {
      if (spu_channels[ch].adpcm36_remain == 0) {
        // fetch new adpcm36 header
        spu_channels[ch].adpcm36_header = fetch;
        spu_channels[ch].nib_addr += 4;
        fetch = get_uint16le(memptr + ((spu_channels[ch].nib_addr >> 1) & 0x01FFFFFE));
        spu_channels[ch].adpcm36_remain = 8;
      } else if ((spu_channels[ch].nib_addr & 0x3) == 0x3) {
        --spu_channels[ch].adpcm36_remain;
      }
      if (fetch == 0xFFFF && get_uint16le(memptr + (((spu_channels[ch].nib_addr >> 1) - 2) & 0x01FFFFFE)) == 0xFFFF)
        goto end_of_data;
      uint16_t nib = (fetch) >> (4 * (spu_channels[ch].nib_addr & 0x3));
      spu_regs[ca+chan_wavd] = decode_adpcm36(ch, nib & 0xF);
    } else {
      if (fetch == 0xFFFF)
        goto end_of_data;
      uint16_t nib = (fetch) >> (4 * (spu_channels[ch].nib_addr & 0x3));
      spu_regs[ca+chan_wavd] = uint16_t(spu_channels[ch].adpcm32.clock(nib & 0xF) << 4) ^ uint16_t(0x8000);
    }
  } else {
    if (m16) {
      // 16-bit PCM
      if (fetch == 0xFFFF)
        goto end_of_data;
      spu_regs[ca+chan_wavd] = fetch;
      nibs = 4;
    } else {
      // 8-bit PCM
      uint16_t byt = ((fetch >> (4 * (spu_channels[ch].nib_addr & 0x3))) & 0xFF);
      if (byt == 0xFF)
        goto end_of_data;
      spu_regs[ca+chan_wavd] = byt << 8;
      nibs = 2;
    }
  }
  spu_channels[ch].nib_addr += nibs;

  if (false) {
end_of_data:
    if (tone_mode == 2)
      start_channel(ch); // repeat
    else
      stop_channel(ch); // stop
  }

  // TODO: envelope, repeat, non-ADPCM, etc
}

static void spu_tick() {
  for (int ch = 0; ch < 24; ch++)
    tick_channel(ch);
}

static void spu_mix_channels(int16_t &l, int16_t &r) {
  int32_t lm = 0, rm = 0;
  for (int ch = 0; ch < 24; ch++) {
    if (!check_bit(spu_regs[chen + ((ch >= 16) ? uoffset : 0)], ch % 16))
      continue; // channel disabled
    int ca = channel_start(ch);
    int32_t samp = int16_t(uint16_t(spu_regs[ca+chan_wavd]) ^ uint16_t(0x8000));
    samp = (samp * int32_t(spu_regs[ca+chan_envd] & 0x7F)) / (1<<7);
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
    // TODO: make vol/pan work
    int32_t lf = (samp * pan_l) / (1<<14);
    int32_t rf = (samp * pan_r) / (1<<14);
    float alpha = 0.33;
    spu_channels[ch].iirl = spu_channels[ch].iirl * alpha + lf * (1.0f-alpha);
    spu_channels[ch].iirr = spu_channels[ch].iirr * alpha + rf * (1.0f-alpha);
    lm += int32_t(spu_channels[ch].iirl);
    rm += int32_t(spu_channels[ch].iirr);
  }
  lm /= 8;
  rm /= 8;
  l = std::min<int32_t>(std::max<int32_t>(-32767, lm), 32767);
  r = std::min<int32_t>(std::max<int32_t>(-32767, rm), 32767);
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
  }
  spu_regs[addr] = val;
}

uint32_t SPUDeviceReadHandler(uint16_t addr) {
  return spu_regs[addr/4];
}

void SPUDeviceResetHandler() {
  for (auto &r : spu_regs)
    r = 0;
  for (auto &ch : spu_channels)
    ch.reset();
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

static float spu_rate_conv = 0;

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
  while (spu_rate_conv > 0) {
    spu_tick();
    spu_rate_conv -= (1.f / 281250.f);
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
    printf("underflow!!\n");
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
}

const Peripheral SPUPeripheral = {"SPU", InitSPUDevice, SPUDeviceReadHandler,
                                  SPUDeviceWriteHandler, SPUDeviceResetHandler};

};