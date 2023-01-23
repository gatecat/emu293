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

  std::queue<uint16_t> packets;

  void IRGamepadTick() {
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
    uint16_t data = state;
    if (player == 1)
      data |= (1U << 6);
    do_send(data);
  }

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

  static const map<SDL_Scancode, int> player_keys[2] = {
    { // player 1
      {SDL_SCANCODE_X, BTN_B}, {SDL_SCANCODE_Z, BTN_A}, {SDL_SCANCODE_C, BTN_M},
      {SDL_SCANCODE_RSHIFT, BTN_SELECT},
      {SDL_SCANCODE_RETURN, BTN_START}, {SDL_SCANCODE_UP, BTN_UP},   {SDL_SCANCODE_DOWN, BTN_DOWN},
      {SDL_SCANCODE_LEFT, BTN_LEFT},   {SDL_SCANCODE_RIGHT, BTN_RIGHT}
    }, { // player 2
      {SDL_SCANCODE_A, BTN_B}, {SDL_SCANCODE_S, BTN_A}, {SDL_SCANCODE_D, BTN_M},
      {SDL_SCANCODE_RIGHTBRACKET, BTN_SELECT},
      {SDL_SCANCODE_LEFTBRACKET, BTN_START},
    }
  };

  static uint16_t player_state[2] = {0};

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
