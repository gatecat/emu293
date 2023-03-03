#include "ir_gamepad.h"
#include "../system.h"
#include "../sys/gpio.h"

#include <cstdio>
#include <map>
#include <queue>

using namespace std;

namespace Emu293 {
  static int count = 0;

  // ratio between IRGamepadTick call frequency and timer frequency
  static int tick_ratio = 10;

  bool softreset_flag = false;
  bool zone3d_pad_mode = false;

  // we only care the time between falling edge IRQs
  static int t_start = 2815 / tick_ratio;
  static int t_zero = 1023 / tick_ratio;
  static int t_one = 1919 / tick_ratio;
  static int t_stop = 5503 / tick_ratio;

  static int timer = 0;
  static int bits = 0;
  static uint16_t sr = 0;
  static bool active = false;

  static void ir_fire_irq() {
    FireInterrupt(GPIO_PORT_I, 2, GPIO_FALLING);
  }

  static std::queue<uint16_t> packets;

  enum IRButtons : uint16_t {
    BTN_M = 0x0080,
    BTN_B = 0x0100,
    BTN_A = 0x0200,
    BTN_START = 0x0400,
    BTN_SELECT = 0x0800,
    BTN_RIGHT = 0x1000,
    BTN_LEFT = 0x2000,
    BTN_DOWN = 0x4000,
    BTN_UP = 0x8000,
  };
  static uint16_t player_state[2] = {0};

  static uint16_t zone3d_get_pad(int i) {
    // TODO: remap
    uint16_t s = player_state[i];
    uint16_t result = 0;
    if (s & BTN_A) result |= (1U<<0);
    if (s & BTN_B) result |= (1U<<1);
    if (s & BTN_SELECT) result |= (1U<<2);
    if (s & BTN_START) result |= (1U<<3);
    if (s & BTN_RIGHT) result |= (1U<<7);
    if (s & BTN_LEFT) result |= (1U<<6);
    if (s & BTN_DOWN) result |= (1U<<5);
    if (s & BTN_UP) result |= (1U<<4);
    return ~result;
  }

  static void tick_zone3d() {
    static bool last_clk = false;
    static bool last_latch = false;
    static uint16_t shiftreg[2] = {0};
    bool curr_latch = GetGPIOState(GPIO_PORT_I, 1) == GPIO_HIGH;
    bool curr_clk = GetGPIOState(GPIO_PORT_I, 0) == GPIO_HIGH;

    if (curr_latch && !last_latch) {
      for (int i = 0; i < 2; i++)
        shiftreg[i] = zone3d_get_pad(i);
    }

    if (curr_clk && !last_clk) {
      for (int i = 0; i < 2; i++)
        shiftreg[i] >>= 1;
    }

    SetGPIOInputState(GPIO_PORT_S, 6, shiftreg[0]&0x1);
    SetGPIOInputState(GPIO_PORT_S, 7, shiftreg[1]&0x1);
    SetGPIOInputState(GPIO_PORT_R, 0, shiftreg[1]&0x1);

    SetGPIOInputState(GPIO_PORT_G, 0, player_state[0] & BTN_SELECT);

    last_clk = curr_clk;
    last_latch = curr_latch;
  }

  static void tick_subor() {
    if (!active) {
      if (!packets.empty()) {
        // schedule next
        active = true;
        bits = 16;
        sr = packets.front();
        packets.pop();
        timer = t_start;
        ir_fire_irq();
      }
      return;
    }
    if (timer > 0) {
      --timer;
      return;
    }
    // timer expired, fire IRQ
    ir_fire_irq();
    if (bits > 0) {
      // send bit
      if (check_bit(sr, 15)) // 16 bit SR, MSB first
        timer = t_one;
      else
        timer = t_zero;
      --bits;
      sr <<= 1U;
    } else if (bits == 0) {
      // send stop
      timer = t_stop;
      --bits;
    }  else if (bits == -1) {
      active = false;
    }
  }

  void IRGamepadTick() {
    if (zone3d_pad_mode) {
      tick_zone3d();
    } else {
      tick_subor();
    }
  }

  static uint16_t add_checksum(uint16_t send) {
    uint8_t cksum = 0;
    for (int i = 1; i < 4; i++) { // upper 4 nibbles
      uint8_t nib = (send >> (i * 4)) & 0xF;
      cksum ^= nib;
    }
    return (send | cksum);
  }

  static void do_send(uint16_t data) {
    packets.push(add_checksum(data));
  }

  void IRGamepadUpdate(int player, uint16_t state) {
    if (!zone3d_pad_mode) {
      uint16_t data = state;
      if (player == 1)
        data |= (1U << 6);
      do_send(data);
    }
  }



  static const map<SDL_Scancode, int> player_keys[2] = {
    { // player 1
      {SDL_SCANCODE_X, BTN_B}, {SDL_SCANCODE_Z, BTN_A}, {SDL_SCANCODE_C, BTN_M},
      {SDL_SCANCODE_RSHIFT, BTN_SELECT},
      {SDL_SCANCODE_RETURN, BTN_START}, {SDL_SCANCODE_UP, BTN_UP},   {SDL_SCANCODE_DOWN, BTN_DOWN},
      {SDL_SCANCODE_LEFT, BTN_LEFT},   {SDL_SCANCODE_RIGHT, BTN_RIGHT}
    }, { // player 2
      {SDL_SCANCODE_A, BTN_B}, {SDL_SCANCODE_S, BTN_A}, {SDL_SCANCODE_D, BTN_M},
      {SDL_SCANCODE_RIGHTBRACKET, BTN_SELECT},
      {SDL_SCANCODE_LEFTBRACKET, BTN_START}, {SDL_SCANCODE_I, BTN_UP},   {SDL_SCANCODE_K, BTN_DOWN},
      {SDL_SCANCODE_J, BTN_LEFT},   {SDL_SCANCODE_L, BTN_RIGHT}
    }
  };


  void IRGamepadEvent(SDL_Event *ev) {
    switch (ev->type) {
      case SDL_KEYDOWN:
        if (!ev->key.repeat) {
          for (int p = 0; p < 2; p++)
            if (player_keys[p].count(ev->key.keysym.scancode)) {
              // cout << "keydown " << keys.at(ev->key.keysym.scancode) << endl;
              player_state[p] |= uint16_t(player_keys[p].at(ev->key.keysym.scancode));
              IRGamepadUpdate(p+1, player_state[p]);
            }
          if (ev->key.keysym.scancode == SDL_SCANCODE_F9)
            softreset_flag = true;
        }
        break;
      case SDL_KEYUP:
        if (!ev->key.repeat) {
          for (int p = 0; p < 2; p++)
            if (player_keys[p].count(ev->key.keysym.scancode)) {
              // cout << "keyup " << keys.at(ev->key.keysym.scancode) << endl;
              player_state[p] &= ~uint16_t(player_keys[p].at(ev->key.keysym.scancode));
              IRGamepadUpdate(p+1, player_state[p]);
            }
        }
        break;
      }
  }
}
