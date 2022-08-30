#ifndef TEST_UTILITY_HPP
#define TEST_UTILITY_HPP

#include <iostream>
#include <cstddef>

namespace DynamicPrefixFilter {
    constexpr bool DEBUG = false;
}

void printBinaryUInt64(uint64_t x, bool newline=false, int divider=64);

#endif