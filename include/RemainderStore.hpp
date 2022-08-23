#ifndef REMAINDER_STORE_HPP
#define REMAINDER_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <immintrin.h>
#include "TestUtility.hpp"

//Note: probably change the type of bounds to an in house type where its *explicitly* something like startPos and endPos names or something. Not really necessary but might be a bit nice

namespace DynamicPrefixFilter {
    //Only works for a 64 byte bucket!!
    //Maybe works for <64 byte buckets, but then managing coherency is hard obviously. I think AVX512 instructions in general do not guarantee any consistency that regular instructions do, which might actually be the reason fusion trees were messed up
    //Obviously the offset is a bit cludgy
    //Only for 8 bit remainders for now
    //Keeps remainders sorted by (miniBucket, remainder) lexicographic order
    //Question: should this structure provide bounds checking? Because theoretically the use case of querying mini filter, then inserting/querying here should never be out of bounds
    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore8Bit {
        static constexpr std::size_t Size = NumRemainders;
        std::array<std::uint8_t, NumRemainders> remainders;

        __m512i* getNonOffsetBucketAddress() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        //returns the remainder that overflowed. This struct doesn't actually know if there was any overflow, so this might just be a random value.
        std::uint_fast8_t insertNonVectorized(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            std::uint_fast8_t overflow = remainder;
            // std::cout << (int)overflow << " ";
            for(size_t i{bounds.first}; i < bounds.second; i++) {
                std::uint_fast8_t tmp = remainders[i];
                remainders[i] = std::min(overflow, tmp);
                overflow = std::max(overflow, tmp);
                // std::cout << (int)overflow << " ";
            }
            for(size_t i{bounds.second}; i < NumRemainders; i++) {
                std::uint_fast8_t tmp = remainders[i];
                remainders[i] = overflow;
                overflow = tmp;
                // std::cout << (int)overflow << " ";
            }
            // std::cout << std::endl;
            // for(uint8_t b : remainders){
            //     std::cout << (int) b << ' ';
            // }
            // std::cout << "o" << (int) overflow << std::endl;
            // std::cout << std::endl;
            return overflow;
        }

        // Original q: Should this even be vectorized really? Cause that would be more consistent, but probably slower on average since maxPossible-minPossible should be p small? Then again already overhead of working with bytes
        // Original plan was: Returns 0 if can definitely say this is not in the filter, 1 if definitely is, 2 if need to go to backyard
        // Feels like def a good idea to vectorize now
        std::uint64_t queryNonVectorized(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            std::uint64_t retMask = 0;
            for(size_t i{bounds.first}; i < bounds.second; i++) {
                if(remainders[i] == remainder) {
                    retMask |= 1ull << i;
                }
            }
            return retMask;
        }

        //This is the query to see if we need to go to the backyard. Basically only run this if our mini filter says that there are keys in the mini bucket and the mini bucket is the last one to have keys.
        bool queryOutOfBounds(std::uint_fast8_t remainder) {
            return remainder > remainders[NumRemainders-1];
        }

        //Todo: vectorize this
        std::uint_fast8_t insert(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(bounds.second <= NumRemainders);
            }
            return insertNonVectorized(remainder, bounds);
        }

        // Returns a bitmask of which remainders match within the bounds. Maybe this should return not a uint64_t but a mask type? Cause we should be able to do everything with them
        std::uint64_t query(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(bounds.second <= NumRemainders);
            }
            return queryNonVectorized(remainder, bounds);
        }
    };
    
}

#endif