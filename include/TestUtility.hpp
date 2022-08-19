#ifndef TEST_UTILITY_HPP
#define TEST_UTILITY_HPP

#include <iostream>
#include <cstddef>

void printBinaryUInt64(uint64_t x, bool newline=false, int divider=64) {
    for(int i=0; i < 64; i++) {
        if(i>0 && i%divider == 0) std::cout << ' ';
        std::cout << (x%2);
        x/=2;
    }
    if(newline) std::cout << std::endl;
}

#endif