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
    {0x9cecd95c, 0x220e230e},
    {0x15a60de6, 0x230a0f34},
    {0xb0b6af88, 0x0023220a}};

vector<int> get_diff_bits(uint32_t a, uint32_t b) {
    vector<int> out;
    for(int i = 0; i < 32; i++) {
        if((a & 0x01) != (b & 0x01)) {
            out.push_back(i);
        }
        a >>= 1;
        b >>= 1;
    }
    return out;
}
int delta[32][32] = {{0}};

int main() {
    for(int i = 0; i < data.size(); i++) {
        for(int j = i + 1; j < data.size(); j++) {
            vector<int> diff_enc = get_diff_bits(data[i].enc, data[j].enc);
            vector<int> diff_plain = get_diff_bits(data[i].plain, data[j].plain);
            for(auto de : diff_enc) {
                for(auto dp : diff_plain) {
                    delta[de][dp]++;
                }
            }
        }
    }
    for(int i = 0; i < 32; i++){
        for(int j = 0; j < 32; j++)
            cout << delta[i][j] << ", ";
        cout << endl;
    }
    return 0;
}
