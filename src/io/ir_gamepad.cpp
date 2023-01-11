#include "ir_gamepad.h"
#include "../system.h"
#include "../sys/gpio.h"

#include <cstdio>
#include <map>

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

  void IRGamepadTick() {
    if (!active)
        return;
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
    active = true;
    bits = 16;
    sr = add_checksum(data);
    timer = t_start;
    ir_fire_irq();
    printf("do_send %04x\n", sr);
  }

  void IRGamepadUpdate(int player, uint8_t state) {
    uint16_t data = (uint16_t(state) << 8);
    if (player == 1)
      data |= (1U << 6);
    do_send(data);
  }

  enum IRButtons : uint8_t {
    BTN_B = 0x01,
    BTN_A = 0x02,
    BTN_START = 0x04,
    BTN_SELECT = 0x08,
    BTN_RIGHT = 0x10,
    BTN_LEFT = 0x20,
    BTN_DOWN = 0x40,
    BTN_UP = 0x80,
  };

  static const map<SDL_Scancode, int> p1_keys = {
    {SDL_SCANCODE_X, BTN_B},  {SDL_SCANCODE_Z, BTN_A},    {SDL_SCANCODE_RSHIFT, BTN_SELECT},
    {SDL_SCANCODE_RETURN, BTN_START}, {SDL_SCANCODE_UP, BTN_UP},   {SDL_SCANCODE_DOWN, BTN_DOWN},
    {SDL_SCANCODE_LEFT, BTN_LEFT},   {SDL_SCANCODE_RIGHT, BTN_RIGHT}};

  static uint8_t p1_state = 0;

  void IRGamepadEvent(SDL_Event *ev) {
    switch (ev->type) {
      case SDL_KEYDOWN:
        if (!ev->key.repeat) {
          if (p1_keys.count(ev->key.keysym.scancode)) {
            // cout << "keydown " << keys.at(ev->key.keysym.scancode) << endl;
            p1_state |= uint8_t(p1_keys.at(ev->key.keysym.scancode));
            IRGamepadUpdate(1, p1_state);
          }
        }
        break;
      case SDL_KEYUP:
        if (!ev->key.repeat) {
          if (p1_keys.count(ev->key.keysym.scancode)) {
            // cout << "keyup " << keys.at(ev->key.keysym.scancode) << endl;
            p1_state &= ~uint8_t(p1_keys.at(ev->key.keysym.scancode));
            IRGamepadUpdate(1, p1_state);
          }
        }
        break;
      }
  }
}
