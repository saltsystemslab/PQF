#ifndef MINI_FILTER_HPP
#define MINI_FILTER_HPP

#include <cstdint>
#include <array>
#include <utility>
#include <immintrin.h>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <optional>
#include "TestUtility.hpp"

//Once get this working, turn this into more of an interface by not having this actually store the data
//Rather you pass pointers into the class, and it is a temporary class just to make dealing with stuff convenient
//Cause having the class in charge of its own data seems somewhat inneficient cause you can't avoid initialization nonsense
//But also it feels like the Bucket should be in charge of data handling, since it needs to know the sizes of stuff anyways to fit in a cache line
//Can just have some constexpr static function in this class that tells you the size you need or smth
//This would also enable the dynamicprefixfilter to have a resolution of individual bits rather than bytes in terms of space usage, although that's less of a concern

namespace DynamicPrefixFilter {
    //You have to put the minifilter at the beginning of the bucket! Otherwise it may mess stuff up, since it does something admittedly kinda sus
    //Relies on little endian ordering
    //Definitely not fully optimized, esp given the fact that I'm being generic and allowing any mini filter size rather than basically mini filter has to fit in 2 words (ullongs)
    //TODO: Fix the organization of this (ex make some stuff public, some stuff private etc), and make an interface (this goes for all the things written so far).
    template<std::size_t NumKeys, std::size_t NumMiniBuckets>
    struct alignas(1) MiniFilter {
        static constexpr std::size_t NumBits = NumKeys+NumMiniBuckets;
        static constexpr std::size_t NumBytes = (NumKeys+NumMiniBuckets+7)/8;
        static constexpr std::size_t Size = NumBytes; // Again some naming consistency problems to address later
        static constexpr std::size_t NumUllongs = (NumBytes+7)/8;
        static constexpr std::uint64_t lastSegmentMask = (NumBits%64 == 0) ? -1ull : (1ull << (NumBits%64))-1ull;
        std::array<uint8_t, NumBytes> filterBytes;

        MiniFilter() {
            // Maybe should actually do with manipulating ullongs for efficiency, but for now just taking the easy route. Technically setup cost anyways so who cares amirite?
            int64_t numBitsNeedToSet = NumMiniBuckets;
            for(uint8_t& b: filterBytes) {
                if(numBitsNeedToSet >= 8) {
                    b = -1;
                }
                else if(numBitsNeedToSet > 0) {
                    b = (1 << numBitsNeedToSet) - 1;
                }
                else {
                    b = 0;
                }
                numBitsNeedToSet -= 8;
            }

            // uint64_t* fastCastFilter = reinterpret_cast<uint64_t*> (&filterBytes);
            // size_t numBitsNeedToSet = NumMiniBuckets;
            // size_t bytesLeft
            // for(; numBitsNeedToSet >= 64; numBitsNeedToSet -= 64, fastCastFilter++) {
            //     *fastCastFilter = -1ull;
            // }
            // //Not sure. If I'm setting this first then I can get away with just doing it simple, but I'll keep it like this for now:
            // if(numBitsNeedToSet > 0) {
            //     *fastCastFilter = *
            // }
        }

        std::size_t select(uint64_t filterSegment, uint64_t miniBucketSegmentIndex) {
            uint64_t isolateBit = _pdep_u64(1ull << miniBucketSegmentIndex, filterSegment);
            // std::cout << miniBucketSegmentIndex << " " << isolateBit << std::endl;
            return __builtin_ctzll(isolateBit);
        }

        std::size_t getKeyIndex(uint64_t filterSegment, uint64_t miniBucketSegmentIndex) {
            return select(filterSegment, miniBucketSegmentIndex) - miniBucketSegmentIndex;
        }

        //Returns a pair representing [start, end) of the minibucket. So basically miniBucketIndex to keyIndex conversion
        std::pair<std::size_t, std::size_t> queryMiniBucketBounds(std::size_t miniBucketIndex) {
            //Highly, sus, but whatever
            uint64_t* fastCastFilter = reinterpret_cast<uint64_t*> (&filterBytes);
            std::pair<std::size_t, std::size_t> keyIndices;
            if(miniBucketIndex == 0) {
                keyIndices.first = 0;
            }
            for(size_t remainingBytes{NumBytes}, keysPassed{0}; remainingBytes > 0; remainingBytes-=8, fastCastFilter++) {
                uint64_t filterSegment = (*fastCastFilter);
                // if(remainingBytes < 8) {
                //     filterSegment = filterSegment & (~((1ull << (remainingBytes*8))- 1));
                // } //Should actually be unnecessary! But keeping this in to remind that this is technically accessing memory it may not be supposed to
                uint64_t segmentMiniBucketCount = __builtin_popcountll(filterSegment);
                if (miniBucketIndex != 0 && miniBucketIndex-1 < segmentMiniBucketCount) {
                    keyIndices.first = keysPassed+getKeyIndex(filterSegment, miniBucketIndex-1);
                }
                if(miniBucketIndex < segmentMiniBucketCount) {
                    keyIndices.second = keysPassed+getKeyIndex(filterSegment, miniBucketIndex);
                    return keyIndices;
                }
                miniBucketIndex -= segmentMiniBucketCount;
                keysPassed += 64-segmentMiniBucketCount;
            }

            //Should not get here
            return keyIndices;
        }

        //Probably not the most efficient implementation, but this one is at least somewhatish straightforward. Still not great and maybe not even correct
        bool shiftFilterBits(std::size_t in) {
            int64_t index = in;
            uint64_t* fastCastFilter = reinterpret_cast<uint64_t*> (&filterBytes);
            std::size_t endIndex = NumBits;
            uint64_t oldCarryBit = 0;
            uint64_t carryBit = 0;
            for(size_t i{0}; i < NumUllongs; i++, fastCastFilter++, index-=64, endIndex-=64) {
                // printBinaryUInt64(*fastCastFilter, true);
                std::size_t segmentStartIndex = std::max((long long)index, 0ll);
                if(segmentStartIndex >= 64) continue;
                uint64_t shiftBitIndex = 1ull << segmentStartIndex;
                uint64_t shiftMask = -(shiftBitIndex);
                uint64_t shiftedSegment = ((*fastCastFilter) & shiftMask) << 1;
                if(endIndex < 64) {
                    // std::cout << endIndex << std::endl;
                    shiftMask &= (1ull << endIndex) - 1;
                    carryBit = (*fastCastFilter) & (1ull << (endIndex-1));
                    // std::cout << carryBit << std::endl;
                }
                else {
                    carryBit = (*fastCastFilter) & (1ull << 63);
                }
                carryBit = carryBit != 0;
                *fastCastFilter = (*fastCastFilter & (~shiftMask)) | (shiftedSegment & shiftMask) | (oldCarryBit << segmentStartIndex);
                oldCarryBit = carryBit;
            }

            return carryBit;
            // if constexpr (NumUllongs <= 2) {
            //     std::size_t indexOfSegment = index/64;
            //     std::size_t begMaskOffset = index & 63;
            //     if(indexOfSegment == NumUllongs-1) {
            //         constexpr std::size_t endMaskOffset = (NumBytes % 8)*8;
            //         if constexpr (endMaskOffset == 0) {

            //         }
            //     }
            //     else {

            //     }
            // }
            //TODO: for more ullongs
        }

        // void setFilterBit(std::size_t index) {
        //     uint64_t* fastCastFilter = reinterpret_cast<uint64_t*> (&filterBytes);
        //     std::size_t indexOfSegment = index/64;
        //     *(fastCastFilter+indexOfSegment) |= 
        // }

        //TODO: implement & rename to something a bit more descriptive
        //Keys are zeros, so "fix overflow" basically makes it so  that we overflow the key, not the mini bucket. We essentially aim to replace the last zero with a one
        uint64_t fixOverflow() {
            uint64_t* fastCastFilter = (reinterpret_cast<uint64_t*> (&filterBytes)) + NumUllongs-1;
            uint64_t lastSegmentInverse = (~(*fastCastFilter)) & lastSegmentMask;
            uint64_t offsetMiniBuckets = NumBits-(NumUllongs-1)*64; //Again bad name. We want to return the mini bucket index, and to do that we are counting how many mini buckets from the end we have. Originally had this be popcount, but we don't need popcount, since we only continue if everything is ones basically!
            if (lastSegmentInverse != 0) {
                // *fastCastFilter = (*fastCastFilter) | _pdep_u64(1, lastSegmentInverse); //maybe some way to do with pdep but then need to reverse the order of the bits and back again
                size_t skipMiniBuckets = _lzcnt_u64(lastSegmentInverse); //get a better name oops
                *fastCastFilter = (*fastCastFilter) | (1ull << (63-skipMiniBuckets));
                return NumMiniBuckets-(skipMiniBuckets - 64 + offsetMiniBuckets)-1;
            }
            fastCastFilter--;
            if constexpr (DEBUG)
                assert(fastCastFilter >= reinterpret_cast<uint64_t*> (&filterBytes));
            uint64_t segmentInverse = ~(*fastCastFilter); //This gives a warning in -O2 cause it may be out of bounds even though I assert it never happens!
            for(; segmentInverse == 0; offsetMiniBuckets += 64, fastCastFilter--) {
                if constexpr (DEBUG)
                    assert(fastCastFilter >= reinterpret_cast<uint64_t*> (&filterBytes));
                segmentInverse = ~(*fastCastFilter);
            }
            // *fastCastFilter = (*fastCastFilter) | _pdep_u64(1, segmentInverse);
            size_t skipMiniBuckets = _lzcnt_u64(segmentInverse);
            *fastCastFilter = (*fastCastFilter) | (1ull << (63-skipMiniBuckets));
            return NumMiniBuckets-skipMiniBuckets-offsetMiniBuckets-1;
        }
        
        //Returns true if the filter was full and had to kick somebody to make room.
        //Since we assume that keyIndex was obtained with a query or is at least valid, we have an implicit assertion that keyIndex <= NumKeys (so can essentially be the key bigger than all the other keys in the filter & it becomes the overflow)
        std::optional<uint64_t> insert(std::size_t miniBucketIndex, std::size_t keyIndex) {
            std::size_t bitIndex = miniBucketIndex + keyIndex;
            bool overflow = shiftFilterBits(bitIndex);
            if (overflow) {
                return fixOverflow();
            }
            return {};
        }

        //We implement this by counting where the last bucket cutoff is, and then the number of keys is just that minus the number of buckets. So p similar to fixOverflow()
        std::size_t countKeys() {
            uint64_t* fastCastFilter = (reinterpret_cast<uint64_t*> (&filterBytes)) + NumUllongs-1;
            uint64_t segment = *fastCastFilter & lastSegmentMask;
            size_t offset = (NumUllongs-1) * 64;
            for(; segment == 0; fastCastFilter--, segment = *fastCastFilter, offset -= 64);
            return (64 - _lzcnt_u64(segment)) + offset - NumMiniBuckets;
        }


        //Functions for testing below
        static void printMiniFilter(std::array<uint8_t, NumBytes> filterBytes, bool withExtraBytes = false) {
            int64_t bitsLeftInFilter = withExtraBytes ? NumUllongs*64 : NumBits;
            for(std::size_t i{0}; i < NumBytes; i++) {
                uint8_t byte = filterBytes[i];
                for(size_t j{0}; j < 8 && j < bitsLeftInFilter; j++, byte >>= 1) {
                    std::cout << (byte & 1);
                }
                bitsLeftInFilter -= 8;
            }
        }

        std::optional<uint64_t> testInsert(std::size_t miniBucketIndex, std::size_t keyIndex) {
            // std::cout << "Trying to insert " << miniBucketIndex << " " << keyIndex << std::endl;
            // printMiniFilter(filterBytes, true);
            // std::cout << std::endl;
            std::array<uint8_t, NumBytes> expectedFilterBytes;
            bool expectedOverflowBit = 0;
            std::optional<uint64_t> expectedOverflow = {};
            // bool startedShifting = false;
            int64_t bitsNeedToSet = miniBucketIndex+keyIndex;
            int64_t bitsLeftInFilter = NumBits;
            size_t lastZeroPos = -1;
            for(std::size_t i{0}; i < NumBytes; i++) {
                uint8_t byte = filterBytes[i];
                uint8_t shiftedByte = 0;
                for(int64_t j{0}; j < 8 && j < bitsLeftInFilter; j++, byte >>= 1, bitsNeedToSet--) {
                    if(bitsNeedToSet <= 0) {
                        shiftedByte += ((uint8_t)expectedOverflowBit) << j;
                        if(!expectedOverflowBit) {
                            lastZeroPos = i*8 + j;
                        }
                        expectedOverflowBit = byte & 1;
                    }
                    else {
                        shiftedByte += (byte & 1) << j;
                        if((byte & 1) == 0) {
                            lastZeroPos = i*8 + j;
                        }
                    }
                }
                expectedFilterBytes[i] = shiftedByte;
                bitsLeftInFilter -= 8;
            }
            if(expectedOverflowBit) {
                uint8_t* byteToChange = &expectedFilterBytes[0];
                expectedOverflow = lastZeroPos - NumKeys;
                for(size_t i{1}; lastZeroPos >= 8; lastZeroPos-=8, i++) {
                    byteToChange = &expectedFilterBytes[i];
                }
                *byteToChange |= 1 << lastZeroPos;
            }
            std::optional<uint64_t> overflow = insert(miniBucketIndex, keyIndex);
            // std::cout << std::endl;
            // printMiniFilter(filterBytes);
            // std::cout << std::endl;
            // std::cout << std::endl;
            // printMiniFilter(expectedFilterBytes, true);
            // std::cout << std::endl;
            // std::cout << overflow << " " << expectedOverflow << std::endl;
            assert(expectedFilterBytes == filterBytes);
            assert(overflow.has_value() == expectedOverflow.has_value());
            if(overflow.has_value()) {
                // std::cout << (*overflow) << " " << (*expectedOverflow) << std::endl;
                assert(*overflow == *expectedOverflow);
            }
            return overflow;
        }

        void checkCorrectPopCount() {
            uint64_t totalPopcount = 0;
            for(uint8_t byte: filterBytes) {
                totalPopcount += __builtin_popcountll(byte);
            }
            assert(totalPopcount == NumMiniBuckets);
        }
    };
}

#endif