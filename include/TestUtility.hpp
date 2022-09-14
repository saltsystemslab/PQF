#ifndef TEST_UTILITY_HPP
#define TEST_UTILITY_HPP

#include <iostream>
#include <cstddef>
#include <immintrin.h>

namespace DynamicPrefixFilter {
    constexpr bool DEBUG = false;

    struct alignas(64) m512iWrapper {
        static constexpr __m512i zero = {0, 0, 0, 0, 0, 0, 0, 0};
        __m512i m;
        constexpr m512iWrapper(__m512i m = zero) : m(m) {}
        constexpr operator __m512i&() {return m;}
        constexpr operator __m512i() const {return m;}
    };
}

void printBinaryUInt64(uint64_t x, bool newline=false, int divider=64);
void print_vec(__m512i X, bool binary, int divider=64);

#endif