#include <cstdint>
#include <iomanip>
#include <iostream>
#include <utility>

#include <vector>
using namespace std;

struct ValuePair {
  uint32_t enc;
  uint32_t plain;
};

vector<ValuePair> data = {
    {0xbda2dfab, 0x94fac0d4},
    {0xbda29fab, 0x94dac0d4},
    {0xb27e5040, 0x84d4ac00},
    {0xb23ae0bc, 0x80c68870},
    {0x62224847, 0x76387624},
    {0xb9b08f0e, 0x94fa9040},
    {0xb9b0cf0e, 0x94da9040},
    {0xb4beeda8, 0x84d48058},
    {0xb4beada8, 0x84f48058},
    {0xb6baa9b8, 0x84f48060},
    /*{0xb5be9da9, 0x84f480d8},*/
    {0xb5bedda8, 0x84d480d8},
    /*{0xfe4ec5c1, 0x951bffbe},*/ {0xfe4e45e1, 0x951bffde},
    {0xee5c7545, 0x851bff7e},
    {0x336c4054, 0x80c69c14},
    {0x337e4054, 0x80c69c10},
    {0xa0fa6d8c, 0xd4c28004},
    {0xa0fa6f8c, 0xd4c28006},
    {0xa4fa6dac, 0xd4c28014},
    {0x7226cc7f, 0x26687618},
    {0x6024dc06, 0x761c2668},
    {0xbfada485, 0x84cbffff},
    {0x335a005c, 0x80c79810},
    {0x9cecd95c, 0x220e230e}};

bool applyScramble(uint32_t din, uint64_t circuit) {
  bool desc = 0;
  uint64_t long_din = uint64_t(~din) << 32 | din;
  while (circuit != 0) {
    if ((circuit & 0x01) == 0x01) {
      if ((long_din & 0x01) == 0x01) {
        desc = !desc;
      }
    }
    long_din >>= 1;
    circuit >>= 1;
  }
  return desc;
}

int main() {
  bool solved[32] = {false};
  uint64_t circuits[32] = {0};

  for (int bit = 0; bit < 32; bit++) {
    bool success = false;
    uint64_t best_circuit = 0;
    int best_score = 0;
    uint64_t best_failing_pt = 0;
    for (int a = -1; a < 64; a++) {
      for (int b = -1; b < 64; b++) {
        for (int c = -1; c < 64; c++) {
          for (int d = -1; d < 64; d++) {

            uint64_t circuit = 0;

            if (a != -1)
              circuit |= (1ULL << a);
            if (b != -1)
              circuit |= (1ULL << b);
            if (c != -1)
              circuit |= (1ULL << c);
            if (d != -1)
              circuit |= (1ULL << d);

            if (circuit == 0)
              continue;
            int score = 0;
            uint32_t fail = 0;
            for (auto pair : data) {
              bool res = applyScramble(pair.enc, circuit);
              if (res == (((pair.plain >> bit) & (0x01)) != 0)) {
                score++;
              } else {
                fail = pair.enc;
              }
            }
            if (score == data.size()) {
              success = true;
            } else {
              if (score > best_score) {
                best_score = score;
                best_circuit = circuit;
                best_failing_pt = fail;
              }
            }
            if (success) {
              // cout << "bit " << bit << " solved" << endl;
              solved[bit] = true;
              circuits[bit] = circuit;
              break;
            }
          }
          if (success)
            break;
        }
        if (success)
          break;
      }
      if (success)
        break;
    }
    if ((!success) && ((data.size() - best_score) < 3)) {
      cout << "near match for bit " << bit << " (" << (data.size() - best_score)
           << " fails: 0x" << hex << setw(8) << setfill('0') << best_failing_pt
           << "...)" << resetiosflags(ios_base::basefield) << endl;
      circuits[bit] = best_circuit;
    } else if (success) {
      cout << "bit " << bit << " solved" << endl;
    }
  }
  int scount = 0;
  for (int i = 0; i < 32; i++) {
    if (solved[i])
      scount++;
  }
  cout << "solved " << scount << " bits" << endl;
  cout << "uint32_t desc_circuits[32] = {" << endl;
  for (int i = 0; i < 32; i++) {
    cout << "\t\t0x" << hex << setw(8) << setfill('0') << circuits[i] << ", "
         << endl;
  }
}
