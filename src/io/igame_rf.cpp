#include "igame_rf.h"
#include "../system.h"
#include "../sys/gpio.h"

#include <cstdio>
#include <map>
#include <queue>

using namespace std;

namespace Emu293 {

static uint64_t ioi_time = 0;
static GPIOState ioi_last_clock;
static uint8_t ioi_buf;
static int ioi_byte_count;
static int ioi_bit_count;

static uint8_t command = 0;

static std::vector<uint8_t> tx_packet, rx_packet, rx_buf;
static uint64_t tx_timestamp = 0;
static int tx_seq = 0;

int16_t accel_x, accel_y, accel_z;
uint16_t buttons;

static void fill_rx_packet() {
    rx_packet.resize(24);
    for (int i = 0; i < 24; i++)
        rx_packet.at(i) = 0;
    rx_packet[0] = 0x56;
    rx_packet[6] = accel_x & 0xFF;
    rx_packet[7] = (accel_x >> 8) & 0xFF;
    rx_packet[8] = accel_y & 0xFF;
    rx_packet[9] = (accel_y >> 8) & 0xFF;
    rx_packet[10] = accel_z & 0xFF;
    rx_packet[11] = (accel_z >> 8) & 0xFF;
    rx_packet[12] = buttons & 0xFF;
    rx_packet[13] = (buttons >> 8) & 0xFF;
}

static void handle_command_byte() {
    command = ioi_buf;
    rx_buf.clear();
    // printf("%8lld command: %02x\n", ioi_time, command);

    if (command == 0x05) { // write tx packet buffer
        tx_packet.clear();
    } else if (command == 0xd0) { // do tx
        tx_timestamp = ioi_time;
        tx_seq = 1;
        // printf("%8lld TX packet:", tx_timestamp);
        // for (auto b : tx_packet) {
        //     printf(" %02x", b);
        // }
        // printf("\n");
    } else if (command == 0xc0) { // do rx?
        tx_timestamp = ioi_time;
        if (tx_seq == 2)
            SetGPIOInputState(GPIO_PORT_I, 2, 1); // status high
    } else if (command == 0x45) { // read rx packet buffer
        rx_buf = rx_packet;
        // printf("%8lld read RX packet:", ioi_time);
        // for (auto b : rx_buf) {
        //     printf(" %02x", b);
        // }
        // printf("\n");
        SetGPIOInputState(GPIO_PORT_I, 2, 1); // status high
    }
}
static void handle_data_byte() {
    if (command == 0x05) {
        tx_packet.push_back(ioi_buf);
    }
}

static void ioi_listener(GPIOPort port, uint8_t pin, GPIOState newState) {
  if (pin == 1 && newState == GPIO_HIGH) {
    if (ioi_bit_count > 0) {
        ioi_buf <<= (8 - ioi_bit_count);
        handle_command_byte();
    }
    command = 0;
    ioi_bit_count = 0;
    ioi_byte_count = 0;
    ioi_buf = 0;
  } else if (pin == 4 && newState == GPIO_HIGH && ioi_last_clock == GPIO_LOW) {
    if ((command & 0xc0) != 0x40) { // write
        bool bit = GetGPIOState(GPIO_PORT_I, 5) == GPIO_HIGH;
        ioi_buf = ((ioi_buf << 1) | (bit ? 0x1 : 0x0)) & 0xFF;
    }
    ++ioi_bit_count;
    if (ioi_bit_count >= 8) {
      if (ioi_byte_count == 0) {
        handle_command_byte();
      } else {
        handle_data_byte();
      }
      ioi_buf = 0;
      ioi_bit_count = 0;
      ++ioi_byte_count;
    }

    if (ioi_byte_count > 0 && (command & 0xc0) == 0x40) { // read
        auto byte = ((ioi_byte_count-1) < int(rx_buf.size())) ? rx_buf.at(ioi_byte_count-1) : 0x00;
        SetGPIOInputState(GPIO_PORT_I, 5, (byte >> (7 - ioi_bit_count)) & 0x1);
    }

  }
  ioi_last_clock = GetGPIOState(GPIO_PORT_I, 4);
}

void IGameRFInit() {
  AttachGPIOListener(GPIO_PORT_I, 1, ioi_listener);
  AttachGPIOListener(GPIO_PORT_I, 4, ioi_listener);
}

static bool motion_active = false;

void IGameRFTick() {
    ++ioi_time;

    if (tx_timestamp != 0 && (tx_seq == 1) && ioi_time >= (tx_timestamp + 5)) { // simulate a reply some time after tx
        // printf("Simulating end of TX\n");
        SetGPIOInputState(GPIO_PORT_I, 2, 0); // status low, fire IRQ
        FireInterrupt(GPIO_PORT_I, 2, GPIO_FALLING);
        tx_seq = 2;
    }

    if (tx_timestamp != 0 && (tx_seq == 2) && ioi_time >= (tx_timestamp + 30)) { // simulate a reply some time after tx
        // printf("Sending RF packet\n");
        fill_rx_packet();
        SetGPIOInputState(GPIO_PORT_I, 2, 0); // status low, fire IRQ
        FireInterrupt(GPIO_PORT_I, 2, GPIO_FALLING);
        tx_seq = 3;
    }

    if (motion_active) {
        accel_x = std::min(accel_x + 1000, 40000);
        accel_y = std::min(accel_y + 1000, 40000);
        accel_z = std::min(accel_z + 1000, 40000);
    } else {
        accel_x = std::max(accel_x - 1000, 0);
        accel_y = std::max(accel_y - 1000, 0);
        accel_z = std::max(accel_z - 1000, 0);
    }

}

// TODO : these are in arbitrary order, no choice beyond trial and error
 enum IGameButttons : uint16_t {
    IGAME_RIGHT = 0x0100,
    IGAME_LEFT = 0x0200,
    IGAME_DOWN = 0x0040,
    IGAME_UP = 0x0080,

    IGAME_A = 0x0002,
    IGAME_B= 0x0004,
    IGAME_MENU = 0x0400,
    IGAME_POWER = 0x0001,

    IGAME_TRI = 0x0800,
    IGAME_RED= 0x0008,
    IGAME_GREEN = 0x0020,
    IGAME_BLUE = 0x0010,

    IGAME_UNK = 0x1000,

};


static const map<SDL_Scancode, int> player_keys = {

  {SDL_SCANCODE_UP, IGAME_UP},  
  {SDL_SCANCODE_DOWN, IGAME_DOWN},
  {SDL_SCANCODE_LEFT, IGAME_LEFT},  
  {SDL_SCANCODE_RIGHT, IGAME_RIGHT},

  {SDL_SCANCODE_Z, IGAME_A},
  {SDL_SCANCODE_X, IGAME_B},
  {SDL_SCANCODE_RSHIFT, IGAME_MENU},
  {SDL_SCANCODE_RETURN, IGAME_POWER},

  {SDL_SCANCODE_D, IGAME_TRI},
  {SDL_SCANCODE_F, IGAME_RED},
  {SDL_SCANCODE_C, IGAME_GREEN},
  {SDL_SCANCODE_V, IGAME_BLUE},

  {SDL_SCANCODE_A, IGAME_UNK},
};

void IGameRFEvent(SDL_Event *ev) {
    switch (ev->type) {
      case SDL_KEYDOWN:
        if (!ev->key.repeat) {
            if (player_keys.count(ev->key.keysym.scancode)) {
              buttons |= uint16_t(player_keys.at(ev->key.keysym.scancode));
            }
            if (ev->key.keysym.scancode == SDL_SCANCODE_SPACE) {
                motion_active = true;
            }
        }
        break;
      case SDL_KEYUP:
        if (!ev->key.repeat) {
            if (player_keys.count(ev->key.keysym.scancode)) {
              buttons &= ~uint16_t(player_keys.at(ev->key.keysym.scancode));
            }
            if (ev->key.keysym.scancode == SDL_SCANCODE_SPACE) {
                motion_active = false;
            }
        }
        break;
      }
}

}


