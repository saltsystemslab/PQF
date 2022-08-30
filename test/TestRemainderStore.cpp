#include <iostream>
#include <cassert>
#include <array>
#include <random>
#include <set>
#include <optional>
#include "RemainderStore.hpp"

using namespace std;
using namespace DynamicPrefixFilter;

template<size_t NumKeys, template<std::size_t NumRemainders, std::size_t Offset> class StoreType>
struct alignas(64) FakeBucket {
    static constexpr size_t frontOffsetSize = (64-StoreType<NumKeys, 0>::Size)/2;
    static constexpr size_t backOffsetSize = 64-StoreType<NumKeys, 0>::Size-frontOffsetSize;
    
    std::array<uint8_t, frontOffsetSize> frontOffset;
    StoreType<NumKeys, frontOffsetSize> remainderStore;
    std::array<uint8_t, backOffsetSize> backOffset;

    void basicFunctionTestWrapper(std::function<void(void)> func) {
        std::array<uint8_t, frontOffsetSize> frontOffsetCopy = frontOffset;
        std::array<uint8_t, backOffsetSize> backOffsetCopy = backOffset;
        func();
        assert(frontOffsetCopy == frontOffset);
        assert(backOffsetCopy == backOffset);
    }

    FakeBucket(mt19937 generator) {
        assert(sizeof(*this) == 64);

        uniform_int_distribution<uint8_t> uint8_dist(0, 255);
        for(uint8_t& byte: frontOffset) {
            byte = uint8_dist(generator);
        }
        for(uint8_t& byte: backOffset) {
            byte = uint8_dist(generator);
        }
    }

    void insert(uint64_t remainder, pair<size_t, size_t> bounds, optional<uint64_t> expectedOverflow) {
        // cout << "Inserting " << (int)remainder << " into [" << bounds.first << ", " << bounds.second << ")" << endl;
        basicFunctionTestWrapper([&] () -> void {
            assert(bounds.first <= bounds.second || bounds.first >= NumKeys);
            uint64_t overflow = remainderStore.insert(remainder, bounds);
            // if(expectedOverflow.has_value())
            //     cout << ((int)overflow) << " " << ((int)(*expectedOverflow)) << endl;
            assert(!expectedOverflow.has_value() || overflow == *expectedOverflow);
        });
    }

    void insertVectorized(uint64_t remainder, pair<size_t, size_t> bounds, optional<uint64_t> expectedOverflow) {
        basicFunctionTestWrapper([&] () -> void {
            assert(bounds.first <= bounds.second || bounds.first >= NumKeys);
            uint64_t overflow = remainderStore.insertVectorizedFrontyard(remainder, bounds);
            // if(expectedOverflow.has_value())
            //     cout << ((int)overflow) << " " << ((int)(*expectedOverflow)) << endl;
            assert(!expectedOverflow.has_value() || overflow == *expectedOverflow);
        });
    }

    void checkQuery(std::uint64_t remainder, std::pair<size_t, size_t> bounds, std::uint64_t expectedMask) {
        basicFunctionTestWrapper([&] () -> void {
            uint64_t mask = remainderStore.query(remainder, bounds);
            // cout << "Querying " << (int)remainder << " in [" << bounds.first << ", " << bounds.second << ")" << endl;
            // printBinaryUInt64(mask, true);
            // printBinaryUInt64(expectedMask, true);
            assert(mask == expectedMask);
        });
    }

    void checkQueryOutOfBounds(std::uint64_t remainder, bool expected) {
        basicFunctionTestWrapper([&] () -> void {
            assert(expected == remainderStore.queryOutOfBounds(remainder));
        });
    }
};

template<size_t NumKeys, size_t NumMiniBuckets, template<std::size_t NumRemainders, std::size_t Offset> class StoreType, size_t StoreTypeRemainderSize, bool vectorized=false>
void testBucket(mt19937& generator) {
    cout << "Testing with " << NumKeys << " keys." << endl;

    FakeBucket<NumKeys, StoreType> testBucket(generator);
    array<size_t, NumMiniBuckets> sizeEachMiniBucket{};
    multiset<pair<size_t, uint64_t>> remainders{};
    for(size_t i{0}; i < NumKeys*2; i++) {
        // cout << "i is " << i << endl;
        //Generate random data
        uniform_int_distribution<size_t> miniBucketIndexDist(0, NumMiniBuckets-1);
        size_t miniBucketIndex = miniBucketIndexDist(generator);
        uniform_int_distribution<uint64_t> remainderDist(0, (1ull << StoreTypeRemainderSize)-1);
        uint64_t remainder = remainderDist(generator);
        remainders.insert(make_pair(miniBucketIndex, remainder));

        //Test insertion
        size_t minBound = 0;
        for(size_t j{0}; j < miniBucketIndex; j++) {
            minBound += sizeEachMiniBucket[j];
        }
        size_t maxBound = min(minBound + sizeEachMiniBucket[miniBucketIndex], NumKeys);
        if(i >= NumKeys) {
            pair<size_t, uint64_t> expectedOverflow = *(remainders.rbegin());
            if constexpr (vectorized) {
                testBucket.insertVectorized(remainder, make_pair(minBound, maxBound), {expectedOverflow.second});    
            }
            else {
                testBucket.insert(remainder, make_pair(minBound, maxBound), {expectedOverflow.second});
            }
            remainders.erase(--remainders.end());
        }
        else {
            if constexpr (vectorized) {
                testBucket.insertVectorized(remainder, make_pair(minBound, maxBound), {});
            }
            else {
                testBucket.insert(remainder, make_pair(minBound, maxBound), {});
            }
        }
        sizeEachMiniBucket[miniBucketIndex]++;

        //Test queries
        minBound = 0;
        auto it = remainders.begin();
        uint64_t shift = 0;
        for(size_t size: sizeEachMiniBucket) {
            uint64_t expectedMask = 0;
            for(size_t j{0}; it != remainders.end() && j < size; j++, it++, shift++) {
                if(shift >= NumKeys) break;
                expectedMask += 1ull << shift;
                if(next(it) != remainders.end() && *next(it) == *it) continue;
                testBucket.checkQuery(it->second, make_pair(minBound, min(minBound+size, NumKeys)), expectedMask);
                expectedMask = 0;
            }
            minBound += size;
        }

        //Test query out of bounds.
        if(shift == NumKeys) {
            uint64_t overflowRemainder = remainderDist(generator);
            // cout << i << " " << prev(it)->second << " " << overflowRemainder << " " << ((prev(it)->second)>>4) << " " << (overflowRemainder >> 4) << endl;
            testBucket.checkQueryOutOfBounds(overflowRemainder, overflowRemainder > prev(it)->second);
        }
    }
}

template<template<std::size_t NumRemainders, std::size_t Offset> class StoreType, size_t StoreTypeRemainderSize, bool vectorized = false>
void runTests(mt19937& generator) {
    for(size_t i{0}; i < 100; i++) {
        testBucket<15, 1, StoreType, StoreTypeRemainderSize, vectorized>(generator);
        testBucket<35, 52, StoreType, StoreTypeRemainderSize, vectorized>(generator);
        testBucket<51, 52, StoreType, StoreTypeRemainderSize, vectorized>(generator);
        testBucket<25, 26, StoreType, StoreTypeRemainderSize, vectorized>(generator);
        testBucket<47, 61, StoreType, StoreTypeRemainderSize, vectorized>(generator);
        testBucket<60, 5, StoreType, StoreTypeRemainderSize, vectorized>(generator);
    }
}

int main() {
    random_device rd;
    mt19937 generator (rd());

    cout << "Testing 8 bit" << endl;
    runTests<RemainderStore8Bit, 8>(generator);
    cout << "Testing 8 bit vectorized" << endl;
    runTests<RemainderStore8Bit, 8, true>(generator);
    cout << "Testing 4 bit" << endl;
    runTests<RemainderStore4Bit, 4>(generator);
    cout << "Testing 12 bit (composite of 4 & 8)" << endl;
    for(size_t i{0}; i < 100; i++) {
        testBucket<35, 52, RemainderStore12Bit, 12>(generator);
        testBucket<15, 1, RemainderStore12Bit, 12>(generator);
        testBucket<25, 26, RemainderStore12Bit, 12>(generator);
        testBucket<37, 26, RemainderStore12Bit, 12>(generator);
    }
}