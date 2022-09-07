#ifndef TEST_UTILITY_HPP
#define TEST_UTILITY_HPP

#include <iostream>
#include <cstddef>
#include <immintrin.h>

namespace DynamicPrefixFilter {
    constexpr bool DEBUG = false;
}

void printBinaryUInt64(uint64_t x, bool newline=false, int divider=64);
void print_vec(__m512i X, bool binary, int divider=64);

#endif