#include <iostream>
#include <cassert>
#include <array>
#include <random>
#include <set>
#include <optional>
#include "RemainderStore.hpp"

using namespace std;
using namespace DynamicPrefixFilter;

template<size_t NumKeys>
struct alignas(64) FakeBucket {
    static constexpr size_t frontOffsetSize = (64-NumKeys)/2;
    static constexpr size_t backOffsetSize = 64-NumKeys-frontOffsetSize;
    
    std::array<uint8_t, frontOffsetSize> frontOffset;
    RemainderStore8Bit<NumKeys, frontOffsetSize> remainderStore;
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

    void insert(uint_fast8_t remainder, pair<size_t, size_t> bounds, optional<uint_fast8_t> expectedOverflow) {
        // cout << "Inserting " << (int)remainder << " into [" << bounds.first << ", " << bounds.second << ")" << endl;
        basicFunctionTestWrapper([&] () -> void {
            uint_fast8_t overflow = remainderStore.insert(remainder, bounds);
            // cout << ((int)overflow) << " " << ((int)(*expectedOverflow)) << endl;
            assert(!expectedOverflow.has_value() || overflow == *expectedOverflow);
        });
    }

    void checkQuery(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds, std::uint64_t expectedMask) {
        basicFunctionTestWrapper([&] () -> void {
            // cout << "Querying " << (int)remainder << " in [" << bounds.first << ", " << bounds.second << ")" << endl;
            uint64_t mask = remainderStore.query(remainder, bounds);
            // printBinaryUInt64(mask, true);
            // printBinaryUInt64(expectedMask, true);
            assert(mask == expectedMask);
        });
    }
};

template<size_t NumKeys, size_t NumMiniBuckets>
void testBucket(mt19937 generator) {
    cout << "Testing with " << NumKeys << " keys." << endl;

    FakeBucket<NumKeys> testBucket(generator);
    array<size_t, NumMiniBuckets> sizeEachMiniBucket{};
    multiset<pair<size_t, uint_fast8_t>> remainders{};
    for(size_t i{0}; i < NumKeys*2; i++) {
        // cout << i << endl;
        uniform_int_distribution<size_t> miniBucketIndexDist(0, NumMiniBuckets-1);
        size_t miniBucketIndex = miniBucketIndexDist(generator);
        uniform_int_distribution<uint_fast8_t> remainderDist(0, 255);
        uint_fast8_t remainder = remainderDist(generator);
        remainders.insert(make_pair(miniBucketIndex, remainder));
        size_t minBound = 0;
        for(size_t j{0}; j < miniBucketIndex; j++) {
            minBound += sizeEachMiniBucket[j];
        }
        size_t maxBound = min(minBound + sizeEachMiniBucket[miniBucketIndex], NumKeys);
        if(i >= NumKeys) {
            pair<size_t, uint_fast8_t> expectedOverflow = *(remainders.rbegin());
            testBucket.insert(remainder, make_pair(minBound, maxBound), {expectedOverflow.second});
            remainders.erase(--remainders.end());
        }
        else {
            testBucket.insert(remainder, make_pair(minBound, maxBound), {});
        }
        sizeEachMiniBucket[miniBucketIndex]++;

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
    }
}

int main() {
    random_device rd;
    mt19937 generator (rd());

    for(size_t i{0}; i < 100; i++) {
        testBucket<15, 1>(generator);
        testBucket<51, 52>(generator);
        testBucket<25, 26>(generator);
        testBucket<47, 61>(generator);
        testBucket<60, 5>(generator);
    }
}