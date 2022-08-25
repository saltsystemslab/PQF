#include "TestUtility.hpp"

void printBinaryUInt64(uint64_t x, bool newline, int divider) {
    for(int i=0; i < 64; i++) {
        if(i>0 && i%divider == 0) std::cout << ' ';
        std::cout << (x%2);
        x/=2;
    }
    if(newline) std::cout << std::endl;
}