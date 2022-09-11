#include <iostream>
#include <cassert>
#include <array>
#include <random>
#include <set>
#include <map>
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

    void insert(uint64_t remainder, size_t loc, optional<uint64_t> expectedOverflow) {
        basicFunctionTestWrapper([&] () -> void {
            uint64_t overflow = remainderStore.insert(remainder, loc);
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
};

template<size_t NumKeys, size_t NumMiniBuckets, template<std::size_t NumRemainders, std::size_t Offset> class StoreType, size_t StoreTypeRemainderSize>
void testBucket(mt19937& generator) {
    cout << "Testing with " << NumKeys << " keys." << endl;

    FakeBucket<NumKeys, StoreType> testBucket(generator);
    array<size_t, NumMiniBuckets> sizeEachMiniBucket{};
    // map<pair<size_t, uint64_t>, uint64_t> remToExpectedMask;
    set<pair<pair<size_t, size_t>, uint64_t>> remaindersForUnordered;
    for(size_t i{0}; i < NumKeys*2; i++) {
        // cout << "i is " << i << endl;
        //Generate random data
        uniform_int_distribution<size_t> miniBucketIndexDist(0, NumMiniBuckets-1);
        size_t miniBucketIndex = miniBucketIndexDist(generator);
        uniform_int_distribution<uint64_t> remainderDist(0, (1ull << StoreTypeRemainderSize)-1);
        uint64_t remainder = remainderDist(generator);
        remaindersForUnordered.insert(make_pair(make_pair(miniBucketIndex, NumKeys*2-i), remainder)); //so that its ordered not by remainders in mini bucket but by time inserted in mini bucket

        //Test insertion
        if(i >= NumKeys) {
            // auto it = remToExpectedMask.begin();
            // size_t countSmaller = 0;
            // for(; it->first > make_pair(miniBucketIndex, remainder); it++, countSmaller++);
            size_t insertLoc = 0;
            auto it = remaindersForUnordered.begin();
            auto expectedOverflow = *(remaindersForUnordered.rbegin());
            for(; (it->first).first < miniBucketIndex; it++, insertLoc++);
            testBucket.insert(remainder, insertLoc, {expectedOverflow.second});
            remaindersForUnordered.erase(--remaindersForUnordered.end());
        }
        else {
            size_t insertLoc = 0;
            auto it = remaindersForUnordered.begin();
            for(; (it->first).first < miniBucketIndex; it++, insertLoc++);
            testBucket.insert(remainder, insertLoc, {});
        }
        sizeEachMiniBucket[miniBucketIndex]++;

        map<pair<size_t, uint64_t>, size_t> miniBucketRemainderCounts;
        auto it = remaindersForUnordered.begin();
        for(size_t j{0}; j < NumKeys && it != remaindersForUnordered.end(); j++, it++){
            miniBucketRemainderCounts[make_pair((it->first).first, it->second)] += 1ull << j;
        }
        
        size_t minBound = 0;
        //Just temporary cause well it should be fast enough even for 12 bits but testing 16 bits obv can't test everything, so there just make it like test just the things in the map
        for(size_t j{0}; j < NumMiniBuckets && minBound < NumKeys; j++) {
            for(size_t k{0}; k < (1ull << StoreTypeRemainderSize)-1; k++) {
                testBucket.checkQuery(k, make_pair(minBound, min(minBound+sizeEachMiniBucket[j], NumKeys)), miniBucketRemainderCounts[make_pair(j, k)]);
            }
            minBound += sizeEachMiniBucket[j];
        }
    }
}

template<template<std::size_t NumRemainders, std::size_t Offset> class StoreType, size_t StoreTypeRemainderSize>
void runTests(mt19937& generator) {
    for(size_t i{0}; i < 100; i++) {
        testBucket<15, 1, StoreType, StoreTypeRemainderSize>(generator);
        testBucket<35, 52, StoreType, StoreTypeRemainderSize>(generator);
        testBucket<51, 52, StoreType, StoreTypeRemainderSize>(generator);
        testBucket<25, 26, StoreType, StoreTypeRemainderSize>(generator);
        testBucket<47, 61, StoreType, StoreTypeRemainderSize>(generator);
        testBucket<60, 5, StoreType, StoreTypeRemainderSize>(generator);
    }
}

int main() {
    random_device rd;
    mt19937 generator (rd());

    cout << "Testing 8 bit" << endl;
    runTests<RemainderStore8Bit, 8>(generator);
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