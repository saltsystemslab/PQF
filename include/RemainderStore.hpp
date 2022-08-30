#ifndef REMAINDER_STORE_HPP
#define REMAINDER_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <immintrin.h>
#include <cassert>
#include "TestUtility.hpp"

//Note: probably change the type of bounds to an in house type where its *explicitly* something like startPos and endPos names or something. Not really necessary but might be a bit nice

namespace DynamicPrefixFilter {
    //Only works for a 64 byte bucket!!
    //Maybe works for <64 byte buckets, but then managing coherency is hard obviously. I think AVX512 instructions in general do not guarantee any consistency that regular instructions do, which might actually be the reason fusion trees were messed up
    //Obviously the offset is a bit cludgy
    //Only for 8 bit remainders for now
    //Keeps remainders sorted by (miniBucket, remainder) lexicographic order
    //Question: should this structure provide bounds checking? Because theoretically the use case of querying mini filter, then inserting/querying here should never be out of bounds
    template<std::size_t NumRemainders, std::size_t Offset, bool vectorized = true>
    struct alignas(1) RemainderStore8Bit {
        static constexpr std::size_t Size = NumRemainders;
        std::array<std::uint8_t, NumRemainders> remainders;

        static constexpr __mmask64 StoreMask = (NumRemainders + Offset < 64) ? ((1ull << (NumRemainders+Offset)) - (1ull << Offset)) : (-(1ull << Offset));

        __m512i* getNonOffsetBucketAddress() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        //Well the abstraction of being able to just insert into both the backyard and the fronyard without trouble I think failed, so separating it out now
        //Reason is backyard for supporting 12 bit store this need to return where it put the item, which seems to be a bit of a pain to do (not too bad, but still like a kmov and an lzcnt and some arithmetic)
        std::uint_fast8_t insertVectorizedFrontyard(std::uint_fast8_t remainder, std::pair<std::size_t, std::size_t> bounds) {
            std::uint_fast8_t retval = remainders[NumRemainders-1];
            if(bounds.first >= NumRemainders || (bounds.second == NumRemainders && remainder > retval)) {
                return remainder;
            }

            __mmask64 insertingLocMask = _cvtu64_mask64(((bounds.second+Offset) < 64 ? (1ull << (bounds.second+Offset)) : 0) - (1ull << (bounds.first+Offset)));

            __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
            // std::cout << "checking" << std::endl;
            // printBinaryUInt64(insertingLocMask, true);
            __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
            __m512i packedRemainders = _mm512_maskz_set1_epi8(_cvtu64_mask64(-1ull), remainder);
            __mmask64 geMask = _mm512_mask_cmpge_epu8_mask(insertingLocMask, packedRemainders, packedStore); //Want a 1 set where remainders are bigger
            // printBinaryUInt64(~geMask, true);
            // geMask = _kadd_mask64(geMask, _cvtu64_mask64(-(1ull << (bounds.second+Offset)) + ((1ull << (bounds.first + Offset))- 1ull)); // We will use vpexpandb for this inneficient version, so we want a 1 everywhere except the insert location
            geMask = _knot_mask64(_kadd_mask64(geMask, 1ull << (bounds.first + Offset))); // want a 1 everywhere except in the spot where we want to insert the item, which is the first spot where it is bigger than other people
            if constexpr (DEBUG) {
                // printBinaryUInt64(~geMask, true);
                // printBinaryUInt64((1ull << (bounds.second+Offset)), true);
                // printBinaryUInt64(insertingLocMask, true);
                assert(__builtin_popcountll(geMask) == 63);
                assert((~geMask) >= (1ull << (bounds.first+Offset)));
                assert((~geMask) <= (1ull << (bounds.second+Offset)));
            }
            packedRemainders = _mm512_mask_expand_epi8(packedRemainders, _cvtu64_mask64(geMask), packedStore);
            _mm512_storeu_si512(nonOffsetAddr, _mm512_mask_blend_epi8(StoreMask, packedStore, packedRemainders)); //the mask blending shouldn't be necessary in our actual use case but whatever
            // _mm512_storeu_si512(nonOffsetAddr, _mm512_mask_expand_epi8(packedRemainders, _cvtu64_mask64(geMask), packedStore)); //the mask blending shouldn't be necessary so let's not
            return retval;
        }

        //This solution of overflowLocToStore to make 12bit remainder store work is pretty ugly and wouldn't directly work in AVX512, so try to come up with something better!
        //returns the remainder that overflowed. This struct doesn't actually know if there was any overflow, so this might just be a random value.
        std::uint_fast8_t insertNonVectorized(std::uint_fast8_t remainder, std::pair<std::size_t, std::size_t> bounds, std::pair<std::size_t, std::size_t>* insertLoc = NULL) {
            std::uint_fast8_t overflow = remainder;
            // std::cout << (int)overflow << " ";
            if(insertLoc != NULL) *insertLoc = std::make_pair(-1ull, -1ull);
            for(size_t i{bounds.first}; i < bounds.second; i++) {
                std::uint_fast8_t tmp = remainders[i];
                if(insertLoc == NULL) {
                    remainders[i] = std::min(overflow, tmp);
                    overflow = std::max(overflow, tmp);
                }
                else {
                    if(overflow <= tmp) {
                        if(insertLoc->first == -1ull) {
                            insertLoc->first = i;
                            insertLoc->second = i;
                            // *insertLoc.second = i;
                            //made the awful thing even more awful but whatever
                            // for(; *insertLoc.second < NumRemainders && *insertLoc.second <= bounds.second && remainders; insertLoc->second++);
                        }
                        else if (overflow == remainder) {
                            insertLoc->second = i;
                        }
                        remainders[i] = overflow;
                        overflow = tmp;
                    }
                    else {
                        remainders[i] = tmp;
                    }
                }
                // std::cout << (int)overflow << " ";
            }
            if(insertLoc != NULL) {
                if(insertLoc->first == -1ull) {
                    insertLoc->first = bounds.second;
                    insertLoc->second = bounds.second;
                    // *insertLoc.second = i;
                    //most awful thing but whatever
                    // for(; *insertLoc.second < NumRemainders && *insertLoc.second <= bounds.second && remainders; insertLoc->second++);
                }
                else if (overflow == remainder) {
                    insertLoc->second = bounds.second;
                }
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
        std::uint64_t queryNonVectorized(std::uint_fast8_t remainder, std::pair<std::size_t, std::size_t> bounds) {
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
            // std::cout << (int)remainder << " " << (int)remainders[NumRemainders-1] << std::endl;
            return remainder > remainders[NumRemainders-1];
        }

        //A second very ugly solution to the 12 bit remainder problem.
        bool queryMightBeOutOfBounds(std::uint_fast8_t remainder) {
            return remainder == remainders[NumRemainders-1];
        }

        //Todo: vectorize this
        std::uint_fast8_t insert(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds, std::pair<size_t, size_t>* insertLoc = NULL) {
            if constexpr (DEBUG) {
                assert(bounds.second <= NumRemainders);
            }
            if constexpr (!vectorized) {
                return insertNonVectorized(remainder, bounds, insertLoc);
            }
            else {
                return insertVectorizedFrontyard(remainder, bounds);
            }
        }

        // Returns a bitmask of which remainders match within the bounds. Maybe this should return not a uint64_t but a mask type? Cause we should be able to do everything with them
        std::uint64_t query(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(bounds.second <= NumRemainders);
            }
            return queryNonVectorized(remainder, bounds);
        }
    };


    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore4Bit {
        static constexpr std::size_t Size = (NumRemainders+1)/2;
        std::array<std::uint8_t, Size> remainders;

        __m512i* getNonOffsetBucketAddress() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        //bitGroup = 0 if lower order, 1 if higher order
        static std::uint_fast8_t get4Bits(std::uint_fast8_t byte, std::uint_fast8_t bitGroup) {
            if constexpr (DEBUG) assert(bitGroup <= 1 && (byte & (0b1111 << (bitGroup*4))) >> (bitGroup*4) <16);
            return (byte & (0b1111 << (bitGroup*4))) >> (bitGroup*4);
        }

        static void set4Bits(std::uint_fast8_t& byte, std::uint_fast8_t bitGroup, std::uint_fast8_t bits) {
            if constexpr (DEBUG) assert(bitGroup <= 1 && bits < 16);
            // std::cout << "Setting 4 bits: " << (int)byte << " " << (int)bitGroup << " " << (int)bits << " ";
            byte = (byte & (0b1111 << ((1-bitGroup)*4))) + (bits << (bitGroup*4));
            // std::cout << (int)byte << std::endl;
        }

        //returns the remainder that overflowed. This struct doesn't actually know if there was any overflow, so this might just be a random value.
        std::uint_fast8_t insertNonVectorized(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            std::uint_fast8_t overflow = remainder;
            // std::cout << "Inserting: " << bounds.first << " " << bounds.second << " " << (int)overflow << std::endl;
            for(size_t i{bounds.first}; i < bounds.second; i++) {
                std::uint_fast8_t tmp = get4Bits(remainders[i/2], i%2);
                set4Bits(remainders[i/2], i%2, std::min(overflow, tmp));
                overflow = std::max(overflow, tmp);
                // std::cout << (int)overflow << " ";
            }
            for(size_t i{bounds.second}; i < NumRemainders; i++) {
                std::uint_fast8_t tmp = get4Bits(remainders[i/2], i%2);
                set4Bits(remainders[i/2], i%2, overflow);
                overflow = tmp;
                // std::cout << (int)overflow << " ";
            }
            // std::cout << std::endl;
            return overflow;
        }

        // Original q: Should this even be vectorized really? Cause that would be more consistent, but probably slower on average since maxPossible-minPossible should be p small? Then again already overhead of working with bytes
        // Original plan was: Returns 0 if can definitely say this is not in the filter, 1 if definitely is, 2 if need to go to backyard
        // Feels like def a good idea to vectorize now
        std::uint64_t queryNonVectorized(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            std::uint64_t retMask = 0;
            // std::cout << "Querying " << remainder << std::endl;
            for(size_t i{bounds.first}; i < bounds.second; i++) {
                // std::cout << (int)get4Bits(remainders[i/2], i%2) << " ";
                if(get4Bits(remainders[i/2], i%2) == remainder) {
                    retMask |= 1ull << i;
                }assert(remainder < 16);
            }
            // std::cout << std::endl;
            return retMask;
        }

        //This is the query to see if we need to go to the backyard. Basically only run this if our mini filter says that there are keys in the mini bucket and the mini bucket is the last one to have keys.
        bool queryOutOfBounds(std::uint_fast8_t remainder) {
            return remainder > get4Bits(remainders[(NumRemainders-1)/2], (NumRemainders-1)%2);
        }

        //Todo: vectorize this
        std::uint_fast8_t insert(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(remainder < 16); //make sure is actually within 4 bits
                assert(bounds.second <= NumRemainders);
            }
            return insertNonVectorized(remainder, bounds);
        }

        // Returns a bitmask of which remainders match within the bounds. Maybe this should return not a uint64_t but a mask type? Cause we should be able to do everything with them
        std::uint64_t query(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(remainder < 16);
                assert(bounds.second <= NumRemainders);
            }
            return queryNonVectorized(remainder, bounds);
        }
    };

    //Oooooops I forgot about ordering so making a 12bit store out of 4 and 8 bit will be hard :(
    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore12Bit {
        using Store8BitType = RemainderStore8Bit<NumRemainders, Offset, false>;
        static constexpr std::size_t Size8BitPart = Store8BitType::Size;
        using Store4BitType = RemainderStore4Bit<NumRemainders, Offset+Size8BitPart>;
        static constexpr std::size_t Size4BitPart = Store4BitType::Size;
        static constexpr std::size_t Size = Size8BitPart+Size4BitPart;

        Store8BitType store8BitPart;
        Store4BitType store4BitPart;

        bool queryOutOfBounds(std::uint_fast16_t remainder) {
            uint_fast8_t remainder8BitPart = remainder >> 4;
            uint_fast8_t remainder4BitPart = remainder & 15;
            
            return store8BitPart.queryOutOfBounds(remainder8BitPart) || (store8BitPart.queryMightBeOutOfBounds(remainder8BitPart) && store4BitPart.queryOutOfBounds(remainder4BitPart));
        }

        std::uint_fast16_t insert(std::uint_fast16_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(remainder <= 4095);
                assert(bounds.second <= NumRemainders);
            }

            uint_fast8_t remainder8BitPart = remainder >> 4;
            uint_fast8_t remainder4BitPart = remainder & 15;

            std::pair<std::size_t, std::size_t> insertLoc8BitPart;
            uint_fast16_t overflow = store8BitPart.insert(remainder8BitPart, bounds, &insertLoc8BitPart) << 4ull;
            // std::cout << "Inserted " << remainder << " to " << insertLoc8BitPart.first << ", " << insertLoc8BitPart.second << "; " << (int) remainder8BitPart << " " << (int) remainder4BitPart << ", " << bounds.first << " " << bounds.second << std::endl;
            overflow |= store4BitPart.insert(remainder4BitPart, insertLoc8BitPart);
            // if constexpr (DEBUG)
            //     assert(insertLoc8BitPart == NumRemainders || (query(remainder, std::make_pair(bounds.first, std::min(bounds.second+1, NumRemainders))) & (1ull << insertLoc8BitPart)) != 0);
            return overflow;
        }

        std::uint64_t query(std::uint_fast16_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(remainder <= 4095);
                assert(bounds.second <= NumRemainders);
            }

            uint_fast8_t remainder8BitPart = remainder >> 4;
            uint_fast8_t remainder4BitPart = remainder & 15;

            // std::cout << "Querying " << remainder << " " << (int) remainder8BitPart << " " << (int) remainder4BitPart << std::endl;
            // printBinaryUInt64(store8BitPart.query(remainder8BitPart, bounds), true);
            // printBinaryUInt64(store4BitPart.query(remainder4BitPart, bounds), true);

            return store8BitPart.query(remainder8BitPart, bounds) & store4BitPart.query(remainder4BitPart, bounds);
        }
    };
    
}

#endif