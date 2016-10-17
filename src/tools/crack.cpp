#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>
using namespace std;

struct ValuePair {
  uint32_t enc;
  uint32_t plain;
};

vector<ValuePair> data = {{0xbda2dfab, 0x94fac0d4}, {0xbda29fab, 0x94dac0d4},
                          {0xb27e5040, 0x84d4ac00}, {0xb23ae0bc, 0x80c68870},
                          {0x62224847, 0x76387624}, {0xb9b08f0e, 0x94fa9040},
                          {0xb9b0cf0e, 0x94da9040}, {0xb4beeda8, 0x84d48058},
                          {0xb4beada8, 0x84f48058}, {0xb6baa9b8, 0x84f48060},
                          {0xb5be9da9, 0x84f480d8}, {0xb5bedda8, 0x84d480d8},
                          {0xfe4ec5c1, 0x951bffbe}, {0xfe4e45e1, 0x951bffde},
                          {0xee5c7545, 0x851bff7e}, {0x336c4054, 0x80c69c14},
                          {0x337e4054, 0x80c69c10}, {0xa0fa6d8c, 0xd4c28004},
                          {0xa0fa6f8c, 0xd4c28006}, {0xa4fa6dac, 0xd4c28014},
                          {0x7226cc7f, 0x26687618}, {0x6024dc06, 0x761c2668},
                          {0xbfada485, 0x84cbffff}};

bool applyScramble(uint32_t din, uint32_t circuit, uint32_t circuit2) {
  bool desc = 0;
  while ((circuit != 0) || (circuit2 != 0)) {
    if ((circuit & 0x01) == 0x01) {
      if ((din & 0x01) == 0x01) {
        desc = !desc;
      }
    } else if ((circuit2 & 0x01) == 0x01) {
      if ((din & 0x01) == 0x00) {
        desc = !desc;
      }
    }
    din >>= 1;
    circuit >>= 1;
    circuit2 >>= 1;
  }
  return desc;
}

int main() {
  bool solved[32] = {false};
  uint32_t circuits[32] = {0};
  uint32_t circuits2[32] = {0};

  uint32_t dups[32] = {0};
  for (int bit = 0; bit < 32; bit++) {
    for (int a = 0; a < 32; a++) {
      for (int b = -1; b < 32; b++) {
        for (int c = -1; c < 32; c++) {
          for (int d = -1; d < 32; d++) {
            if ((a == b) || (b == c) || (a == c) || (c == d) || (a == d) ||
                (b == d))
              continue;
            uint32_t circuit = 0;
            uint32_t circuit2 = 0;

            if (a != -1)
              circuit |= (1 << a);
            if (b != -1)
              circuit |= (1 << b);
            if (c != -1)
              circuit2 |= (1 << c);
            if (d != -1)
              circuit2 |= (1 << d);
            if (circuit == 0)
              continue;
            bool success = true;
            for (auto pair : data) {
              bool res = applyScramble(pair.enc, circuit, circuit2);
              if (res != (((pair.plain >> bit) & (1 << bit)) != 0)) {
                success = false;
                break;
              }
            }
            if (success) {
              // cout << "bit " << bit << " solved" << endl;
              if (solved[bit])
                if ((circuits[bit] != circuit) || (circuits2[bit] != circuit2))
                  dups[bit]++;
              solved[bit] = true;
              circuits[bit] = circuit;
              circuits2[bit] = circuit2;
            }
          }
        }
      }
    }
  }
  int scount = 0;
  for (int i = 0; i < 32; i++) {
    if (solved[i])
      scount++;
  }
  cout << "solved " << scount << " bits" << endl;
  for (int i = 0; i < 32; i++) {
    cout << i << ": " << dups[i] << " dups" << endl;
  }
}
