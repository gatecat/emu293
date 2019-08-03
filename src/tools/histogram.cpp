#include <cstdint>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <utility>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <algorithm>

using namespace std;


int main(int argc, char *argv[]) {
    if (argc < 2)
        return 1;
    std::ifstream inf(argv[1], std::ios::binary);
    char c;
    std::vector<uint8_t> bytes;
    while (inf.get(c))
        bytes.push_back((unsigned char) c);
    std::unordered_map<uint32_t, int> histogram;
    for (int i = 0; i < int(bytes.size()) - 3; i += 4) {
        uint32_t x = bytes.at(i) | bytes.at(i + 1) << 8UL | bytes.at(i + 2) << 16UL | bytes.at(i + 3) << 24UL;
        ++histogram[x];
    }
    std::vector<uint32_t> highest;
    for (auto &i : histogram)
        highest.push_back(i.first);
    std::sort(highest.begin(), highest.end(), [&](uint32_t a, uint32_t b) {
        return (histogram[a] > histogram[b]) || (histogram[a] == histogram[b] && a > b); 
    });
    for (size_t i = 0; i < std::max<size_t>(highest.size(), 500); i++) {
        printf("0x%08x | %6d\n", highest.at(i), histogram[highest.at(i)]);
    }
    return 0;
}
