#ifndef REMAINDER_STORE_HPP
#define REMAINDER_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <utility>
#include <algorithm>
#include <immintrin.h>
#include <cassert>
#include "TestUtility.hpp"
#include <bit>

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

        static constexpr __mmask64 StoreMask = (NumRemainders + Offset < 64) ? ((1ull << (NumRemainders+Offset)) - (1ull << Offset)) : (-(1ull << Offset));

        __m512i* getNonOffsetBucketAddress() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        //TODO: make insert use the shuffle instruction & precompute all the shuffle vectors. Also do smth with the getNonOffsetBucketAddress? Cause that's an unnecessary instruction.
        static constexpr __m512i getShuffleVector(std::size_t loc) {
            std::array<unsigned char, 64> bytes;
            for(size_t i=0; i < 64; i++) {
                if (i < loc+Offset) {
                    bytes[i] = i;
                }
                else if (i == loc+Offset) {
                    bytes[i] = 0;
                }
                else {
                    bytes[i] = i-1;
                }
            }
            return std::bit_cast<__m512i>(bytes);
        }

        static constexpr std::array<m512iWrapper, 64> getShuffleVectors() {
            std::array<m512iWrapper, 64> masks;
            for(size_t i = 0; i < 64; i++) {
                masks[i] = getShuffleVector(i);
            }
            return masks;
        }

        static constexpr std::array<m512iWrapper, 64> shuffleVectors = getShuffleVectors();

        //loc is equivalent to bounds.first (or really anything in between bounds.first and bounds.second)
        //Much faster to not keep remainders ordered in a mini bucket, even though it slightly increases chance of needing to go to the backyard for a query
        // std::uint_fast8_t insert(std::uint_fast8_t remainder, std::size_t loc) {
        //     if constexpr (DEBUG) {
        //         assert(loc <= NumRemainders);
        //     }
        //     if(loc == NumRemainders) return remainder;
            
        //     std::uint_fast8_t retval = remainders[NumRemainders-1];

        //     __mmask64 insertingLocMask = _cvtu64_mask64(1ull << (loc + Offset));

        //     __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
        //     __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
        //     __m512i packedRemainders = _mm512_maskz_set1_epi8(-1ull, remainder);
        //     packedRemainders = _mm512_mask_expand_epi8(packedRemainders, _knot_mask64(insertingLocMask), packedStore);
        //     // _mm512_storeu_si512(nonOffsetAddr, packedRemainders);
        //     _mm512_storeu_si512(nonOffsetAddr, _mm512_mask_blend_epi8(StoreMask, packedStore, packedRemainders)); //the mask blending shouldn't be necessary in our actual use case but whatever
        //     // _mm512_storeu_si512(nonOffsetAddr, _mm512_mask_expand_epi8(packedRemainders, _cvtu64_mask64(geMask), packedStore)); //the mask blending shouldn't be necessary so let's not
        //     return retval;
        // }

        std::uint_fast8_t insert(std::uint_fast8_t remainder, std::size_t loc) {
            if constexpr (DEBUG) {
                assert(loc < NumRemainders);
            }
            std::uint_fast8_t retval = remainders[NumRemainders-1];
            // if(__builtin_expect(loc == NumRemainders, 0)) return remainder; It is so crazy that commenting out this one line reduced throughput from 70 ns / ins to 57 ns/ins. It really doesn't make sense to me why. Like this wouldn't affect control flow at all (in the even slightly slower version I just changed it to change the value of retval rather than return, so that literally changed nothing about what is executed), and there is no data dependency between retval and stuff until several instructions later, so that should easily fit in the 17 cycle pipeline, but idk. I think it would be a really good exercise in understanding computer architecture to explain this
            // std::uint_fast8_t retval = (loc == NumRemainders) ? remainder : remainders[NumRemainders-1];

            __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
            __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
            __m512i packedStoreWithRemainder = _mm512_mask_set1_epi8(packedStore, 1, remainder);
            packedStore = _mm512_mask_permutexvar_epi8(packedStore, StoreMask, shuffleVectors[loc], packedStoreWithRemainder);
            _mm512_storeu_si512(nonOffsetAddr, packedStore);

            return retval;
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

        std::uint64_t queryVectorizedMask(std::uint_fast8_t remainder, std::uint64_t mask) {
            // __mmask64 queryMask = _cvtu64_mask64(((1ull << bounds.second) - (1ull << bounds.first)) << Offset);
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
            __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
            __m512i remainderVec = _mm512_maskz_set1_epi8(-1ull, remainder);
            return (_cvtmask64_u64(_mm512_mask_cmpeq_epu8_mask(-1ull, packedStore, remainderVec)) >> Offset) & mask;
        }

        std::uint64_t queryVectorized(std::uint_fast8_t remainder, std::pair<std::size_t, std::size_t> bounds) {
            __mmask64 queryMask = _cvtu64_mask64(((1ull << bounds.second) - (1ull << bounds.first)) << Offset);
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
            __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
            __m512i remainderVec = _mm512_maskz_set1_epi8(-1ull, remainder);
            return _cvtmask64_u64(_mm512_mask_cmpeq_epu8_mask(queryMask, packedStore, remainderVec)) >> Offset;
        }

        // Returns a bitmask of which remainders match within the bounds. Maybe this should return not a uint64_t but a mask type? Cause we should be able to do everything with them
        std::uint64_t query(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(bounds.second <= NumRemainders);
            }
            // return queryNonVectorized(remainder, bounds);
            return queryVectorized(remainder, bounds);
        }
    };


    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore4Bit {
        static constexpr std::size_t Size = (NumRemainders+1)/2;
        std::array<std::uint8_t, Size> remainders;

        static constexpr __mmask64 StoreMask = (Size + Offset < 64) ? ((1ull << (Size+Offset)) - (1ull << Offset)) : (-(1ull << Offset));

        __m512i* getNonOffsetBucketAddress() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        //bitGroup = 0 if lower order, 1 if higher order
        static std::uint_fast8_t get4Bits(std::uint_fast8_t byte, std::uint_fast8_t bitGroup) {
            if constexpr (DEBUG) assert(bitGroup <= 1 && (byte & (0b1111 << (bitGroup*4))) >> (bitGroup*4) <16);
            return (byte & (0b1111 << (bitGroup*4))) >> (bitGroup*4);
        }

        static constexpr __m512i getRemainderStoreMask(std::size_t loc) {
            // return _mm512_maskz_set1_epi8(_cvtu64_mask64(1ull << ((loc >> 1) + Offset)), 15ull << ((loc & 1)*4));
            std::array<unsigned char, 64> bytes;
            for(size_t i=0; i < 64; i++) {
                if(i == ((loc >> 1) + Offset)) {
                    bytes[i] = 15ull << ((loc & 1)*4);
                }
                else {
                    bytes[i] = 0;
                }
            }
            return std::bit_cast<__m512i>(bytes);
        }

        static constexpr std::array<m512iWrapper, 64> getRemainderStoreMasks() {
            std::array<m512iWrapper, 64> masks;
            for(size_t i = 0; i < 64; i++) {
                masks[i] = getRemainderStoreMask(i);
            }
            return masks;
        }

        static constexpr std::array<m512iWrapper, 64> remainderStoreMasks = getRemainderStoreMasks();

        // static const __m512i getPackedStoreMask(std::size_t loc) {
        //     __m512i allOnes = _mm512_maskz_set1_epi8(_cvtu64_mask64((1ull << ((loc >> 1) + Offset)) - 1), (unsigned char)-1ull);
        //     if (loc % 2 == 1){
        //         __m512i fourBits = _mm512_maskz_set1_epi8(_cvtu64_mask64(1ull << ((loc >> 1) + Offset)), 15);
        //         return (__m512i)_mm512_or_ps((__m512)allOnes, (__m512)fourBits);
        //     }
        //     return allOnes;
        // }

        static constexpr __m512i getPackedStoreMask(std::size_t loc) {
            // __m512i allOnes = _mm512_maskz_set1_epi8(_cvtu64_mask64((1ull << ((loc >> 1) + Offset)) - 1), (unsigned char)-1ull);
            // if (loc % 2 == 1){
            //     __m512i fourBits = _mm512_maskz_set1_epi8(_cvtu64_mask64(1ull << ((loc >> 1) + Offset)), 15);
            //     return (__m512i)_mm512_or_ps((__m512)allOnes, (__m512)fourBits);
            // }
            // return allOnes;
            std::array<unsigned char, 64> bytes;
            for(size_t i=0; i < 64; i++) {
                if(i < ((loc >> 1) + Offset)) {
                    bytes[i] = 255;
                }
                else if(i == ((loc >> 1) + Offset) && loc % 2 == 1) {
                    bytes[i] = 15;
                }
                else {
                    bytes[i] = 0;
                }
            }
            return std::bit_cast<__m512i>(bytes);
        }

        static constexpr std::array<m512iWrapper, 64> getPackedStoreMasks() {
            std::array<m512iWrapper, 64> masks;
            for(size_t i = 0; i < 64; i++) {
                masks[i] = getPackedStoreMask(i);
            }
            return masks;
        }

        static constexpr std::array<m512iWrapper, 64> packedStoreMasks = getPackedStoreMasks();

        static void set4Bits(std::uint_fast8_t& byte, std::uint_fast8_t bitGroup, std::uint_fast8_t bits) {
            if constexpr (DEBUG) assert(bitGroup <= 1 && bits < 16);
            // std::cout << "Setting 4 bits: " << (int)byte << " " << (int)bitGroup << " " << (int)bits << " ";
            byte = (byte & (0b1111 << ((1-bitGroup)*4))) + (bits << (bitGroup*4));
        }

        //Seems quite inneficient honestly
        std::uint_fast8_t insert(std::uint_fast8_t remainder, std::size_t loc) {
            if constexpr (DEBUG) {
                assert(loc <= NumRemainders);
            }

            if(loc == NumRemainders) return remainder;

            std::uint_fast8_t retval = get4Bits(remainders[(NumRemainders-1)/2], (NumRemainders-1)%2);
            std::uint_fast8_t remainderDoubledToFullByte = remainder * 17;
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
            __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
            __m512i packedRemainders = _mm512_maskz_set1_epi8(_cvtu64_mask64(-1ull), remainderDoubledToFullByte);
            __m512i shuffleMoveRight = {7, 0, 1, 2, 3, 4, 5, 6};
            __m512i packedStoreShiftedRight = _mm512_permutexvar_epi64(shuffleMoveRight, packedStore);
            __m512i packedStoreShiftedRight4Bits = _mm512_shldi_epi64(packedStore, packedStoreShiftedRight, 4);
            // __m512i newPackedStore = _mm512_ternarylogic_epi32(packedStoreShiftedRight4Bits, packedStore, getPackedStoreMask(loc), 0b11011000);
            __m512i newPackedStore = _mm512_ternarylogic_epi32(packedStoreShiftedRight4Bits, packedStore, packedStoreMasks[loc], 0b11011000);
            // newPackedStore = _mm512_ternarylogic_epi32(newPackedStore, packedRemainders, getRemainderStoreMask(loc), 0b11011000);
            newPackedStore = _mm512_ternarylogic_epi32(newPackedStore, packedRemainders, remainderStoreMasks[loc], 0b11011000);
            _mm512_storeu_si512(nonOffsetAddr, _mm512_mask_blend_epi8(StoreMask, packedStore, newPackedStore));
            return retval;
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

        //Resulting vector would have doubled elements of the array of remainders, so that we could then mask off one for the first 4 bit remainder and one for the second
        static constexpr __m512i get4BitExpanderShuffle() {
            std::array<unsigned char, 64> bytes;
            for(size_t i=0; i < 64; i++) {
                bytes[i] = (i/2) + Offset;
            }
            return std::bit_cast<__m512i>(bytes);
        }
        
        static constexpr __m512i getDoublePackedStoreTernaryMask() {
            std::array<unsigned char, 64> bytes;
            for(size_t i=0; i < 64; i++) {
                bytes[i] = 15 << ((i%2) * 4);
            }
            return std::bit_cast<__m512i>(bytes);
        }

        std::uint64_t queryVectorizedMask(std::uint_fast8_t remainder, std::uint64_t mask) {
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
            __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
            // std::cout << "PackedStore:" <<std::endl;
            // print_vec(packedStore, true, 8);
            __m512i packedRemainder = _mm512_maskz_set1_epi8(-1ull, remainder*17);
            // std::cout << "PackedRemainder:" <<std::endl;
            // print_vec(packedRemainder, true, 8);
            static constexpr __m512i expanderShuffle = get4BitExpanderShuffle();
            // std::cout << "ExpanderShuffle:" <<std::endl;
            // print_vec(expanderShuffle, true, 8);
            __m512i doubledPackedStore = _mm512_permutexvar_epi8(expanderShuffle, packedStore);
            // std::cout << "DoubledPackedStore:" <<std::endl;
            // print_vec(doubledPackedStore, true, 8);
            __m512i maskedXNORedPackedStore = _mm512_ternarylogic_epi32(doubledPackedStore, packedRemainder, getDoublePackedStoreTernaryMask(), 0b00101000);
            // std::cout << "DoublePackedStoreTernaryMask:" <<std::endl;
            // print_vec(getDoublePackedStoreTernaryMask(), true, 8);
            // std::cout << "maskedXNORedPackedStore:" <<std::endl;
            // print_vec(maskedXNORedPackedStore, true, 8);
            __mmask64 compared = _knot_mask64(_mm512_test_epi8_mask(maskedXNORedPackedStore, maskedXNORedPackedStore));
            return _cvtmask64_u64(compared) & mask;
        }

        std::uint64_t queryVectorized(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress();
            __m512i packedStore = _mm512_loadu_si512(nonOffsetAddr);
            // std::cout << "PackedStore:" <<std::endl;
            // print_vec(packedStore, true, 8);
            __m512i packedRemainder = _mm512_maskz_set1_epi8(_cvtu64_mask64(-1ull), remainder*17);
            // std::cout << "PackedRemainder:" <<std::endl;
            // print_vec(packedRemainder, true, 8);
            static constexpr __m512i expanderShuffle = get4BitExpanderShuffle();
            // std::cout << "ExpanderShuffle:" <<std::endl;
            // print_vec(expanderShuffle, true, 8);
            __m512i doubledPackedStore = _mm512_permutexvar_epi8(expanderShuffle, packedStore);
            // std::cout << "DoubledPackedStore:" <<std::endl;
            // print_vec(doubledPackedStore, true, 8);
            __m512i maskedXNORedPackedStore = _mm512_ternarylogic_epi32(doubledPackedStore, packedRemainder, getDoublePackedStoreTernaryMask(), 0b00101000);
            // std::cout << "DoublePackedStoreTernaryMask:" <<std::endl;
            // print_vec(getDoublePackedStoreTernaryMask(), true, 8);
            // std::cout << "maskedXNORedPackedStore:" <<std::endl;
            // print_vec(maskedXNORedPackedStore, true, 8);
            __mmask64 compared = _knot_mask64(_mm512_test_epi8_mask(maskedXNORedPackedStore, maskedXNORedPackedStore));
            return _cvtmask64_u64(compared) & ((1ull << bounds.second) - (1ull << bounds.first));
        }

        // Returns a bitmask of which remainders match within the bounds. Maybe this should return not a uint64_t but a mask type? Cause we should be able to do everything with them
        std::uint64_t query(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(remainder < 16);
                assert(bounds.second <= NumRemainders);
            }
            // return queryNonVectorized(remainder, bounds);
            return queryVectorized(remainder, bounds);
        }
    };

    //Oooooops I forgot about ordering so making a 12bit store out of 4 and 8 bit will be hard :(
    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore12Bit {
        using Store4BitType = RemainderStore4Bit<NumRemainders, Offset>;
        static constexpr std::size_t Size4BitPart = Store4BitType::Size;
        using Store8BitType = RemainderStore8Bit<NumRemainders, Offset+Size4BitPart>;
        static constexpr std::size_t Size8BitPart = Store8BitType::Size;
        static constexpr std::size_t Size = Size8BitPart+Size4BitPart;

        Store8BitType store8BitPart;
        Store4BitType store4BitPart;

        std::uint_fast16_t insert(std::uint_fast16_t remainder, std::size_t loc) {
            if constexpr (DEBUG) {
                assert(remainder <= 4095);
                assert(loc <= NumRemainders);
            }

            uint_fast8_t remainder8BitPart = remainder >> 4;
            uint_fast8_t remainder4BitPart = remainder & 15;
            uint_fast16_t overflow = store8BitPart.insert(remainder8BitPart, loc) << 4ull;
            overflow |= store4BitPart.insert(remainder4BitPart, loc);
            return overflow;
        }

        std::uint64_t query(std::uint_fast16_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(remainder <= 4095);
                assert(bounds.second <= NumRemainders);
            }

            uint_fast8_t remainder8BitPart = remainder >> 4;
            uint_fast8_t remainder4BitPart = remainder & 15;

            return store8BitPart.query(remainder8BitPart, bounds) & store4BitPart.query(remainder4BitPart, bounds);
        }

        std::uint64_t queryVectorizedMask(std::uint_fast16_t remainder, std::uint64_t mask) {
            // if constexpr (DEBUG) {
            //     assert(remainder <= 4095);
            //     assert(bounds.second <= NumRemainders);
            // }

            uint_fast8_t remainder8BitPart = remainder >> 4;
            uint_fast8_t remainder4BitPart = remainder & 15;

            return store8BitPart.queryVectorizedMask(remainder8BitPart, mask) & store4BitPart.queryVectorizedMask(remainder4BitPart, mask);
        }
    };
    
}

#endif