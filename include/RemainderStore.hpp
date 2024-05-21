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
    template<std::size_t RemainderSize, std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore {
        __m512i loadRemainders();
        void storeRemainders(__m512i remainders);

        std::uint64_t insert(std::uint64_t remainder, std::size_t loc);

        void remove(std::size_t loc);

        //Removes and returns the value that was there
        std::uint64_t removeReturn(std::size_t loc);

        std::uint64_t removeFirst();

        std::uint64_t get(std::size_t loc) const;

        // Original q: Should this even be vectorized really? Cause that would be more consistent, but probably slower on average since maxPossible-minPossible should be p small? Then again already overhead of working with bytes
        // Original plan was: Returns 0 if can definitely say this is not in the filter, 1 if definitely is, 2 if need to go to backyard
        // Feels like def a good idea to vectorize now
        std::uint64_t queryNonVectorized(std::uint64_t remainder, std::pair<std::size_t, std::size_t> bounds);

        std::uint64_t queryVectorizedMask(std::uint64_t remainder, std::uint64_t mask);

        std::uint64_t queryVectorized(std::uint64_t remainder, std::pair<std::size_t, std::size_t> bounds);

        // Returns a bitmask of which remainders match within the bounds. Maybe this should return not a uint64_t but a mask type? Cause we should be able to do everything with them
        std::uint64_t query(std::uint64_t remainder, std::pair<size_t, size_t> bounds);
    };

    //Only works for a 64 byte bucket!!
    //Maybe works for <64 byte buckets, but then managing coherency is hard obviously. I think AVX512 instructions in general do not guarantee any consistency that regular instructions do, which might actually be the reason fusion trees were messed up
    //Obviously the offset is a bit cludgy
    //Only for 8 bit remainders for now
    //Keeps remainders sorted by (miniBucket, remainder) lexicographic order
    //Question: should this structure provide bounds checking? Because theoretically the use case of querying mini filter, then inserting/querying here should never be out of bounds
    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore<8, NumRemainders, Offset> {
        static constexpr std::size_t Size = NumRemainders;
        std::array<std::uint8_t, NumRemainders> remainders;

        static constexpr __mmask64 StoreMask = (NumRemainders + Offset < 64) ? ((1ull << (NumRemainders+Offset)) - (1ull << Offset)) : (-(1ull << Offset));

        __m512i* getNonOffsetBucketAddress2() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        __m512i loadRemainders() { //Stopgap measure to at least avoid loading and storing across cache lines
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress2();
            if(Size + Offset <= 32) {
                return _mm512_castsi256_si512(_mm256_load_si256((__m256i*)nonOffsetAddr));
            }
            else{ 
                return _mm512_load_si512(nonOffsetAddr);
            }
        }

        void storeRemainders(__m512i remainders) {
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress2();
            if(Size + Offset <= 32) {
                _mm256_store_si256((__m256i*)nonOffsetAddr, _mm512_castsi512_si256(remainders));
            }
            else{ 
                _mm512_store_si512(nonOffsetAddr, remainders);
            }
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
                else if (i < Size+Offset){
                    bytes[i] = i-1;
                }
                else {
                    bytes[i] = i;
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

        static constexpr __m512i getRemoveShuffleVector(std::size_t loc) {
            std::array<unsigned char, 64> bytes;
            for(size_t i=0; i < 64; i++) {
                if (i < loc+Offset) {
                    bytes[i] = i;
                }
                else if (i < Size+Offset){
                    bytes[i] = i+1;
                }
                else {
                    bytes[i] = i;
                }
            }
            return std::bit_cast<__m512i>(bytes);
        }

        static constexpr std::array<m512iWrapper, 64> getRemoveShuffleVectors() {
            std::array<m512iWrapper, 64> masks;
            for(size_t i = 0; i < 64; i++) {
                masks[i] = getRemoveShuffleVector(i);
            }
            return masks;
        }

        static constexpr std::array<m512iWrapper, 64> shuffleVectors = getShuffleVectors();
        static constexpr std::array<m512iWrapper, 64> removeShuffleVectors = getRemoveShuffleVectors();

        std::uint_fast8_t insert(std::uint_fast8_t remainder, std::size_t loc) {
            if constexpr (DEBUG) {
                assert(loc < NumRemainders);
            }
            std::uint_fast8_t retval = remainders[NumRemainders-1];

            __m512i packedStore = loadRemainders();
            __m512i packedStoreWithRemainder = _mm512_mask_set1_epi8(packedStore, 1, remainder);
            packedStore = _mm512_mask_permutexvar_epi8(packedStore, StoreMask, shuffleVectors[loc], packedStoreWithRemainder);
            storeRemainders(packedStore);

            return retval;
        }

        void remove(std::size_t loc) {
            __m512i packedStore = loadRemainders();
            packedStore = _mm512_mask_permutexvar_epi8(packedStore, StoreMask, removeShuffleVectors[loc], packedStore);
            storeRemainders(packedStore);
        }

        //Removes and returns the value that was there
        std::uint_fast8_t removeReturn(std::size_t loc) {
            std::uint_fast8_t retval = remainders[loc];
            remove(loc);
            return retval;
        }

        std::uint_fast8_t removeFirst() {
            std::uint_fast8_t first = remainders[0];
            remove(0);
            return first;
        }

        std::uint64_t get(std::size_t loc) const {
            return remainders[loc];
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
            __m512i packedStore = loadRemainders();
            __m512i remainderVec = _mm512_maskz_set1_epi8(-1ull, remainder);
            return (_cvtmask64_u64(_mm512_mask_cmpeq_epu8_mask(-1ull, packedStore, remainderVec)) >> Offset) & mask;
        }

        std::uint64_t queryVectorized(std::uint_fast8_t remainder, std::pair<std::size_t, std::size_t> bounds) {
            __mmask64 queryMask = _cvtu64_mask64(((1ull << bounds.second) - (1ull << bounds.first)) << Offset);
            __m512i packedStore = loadRemainders();
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
    struct alignas(1) RemainderStore<4, NumRemainders, Offset> {
        static constexpr std::size_t Size = (NumRemainders+1)/2;
        std::array<std::uint8_t, Size> remainders;

        static constexpr __mmask64 StoreMask = (Size + Offset < 64) ? ((1ull << (Size+Offset)) - (1ull << Offset)) : (-(1ull << Offset));

        __m512i* getNonOffsetBucketAddress2() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        __m512i loadRemainders() { //Stopgap measure to at least avoid loading and storing across cache lines
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress2();
            if(Size + Offset <= 32) {
                return _mm512_castsi256_si512(_mm256_load_si256((__m256i*)nonOffsetAddr));
            }
            else{ 
                return _mm512_load_si512(nonOffsetAddr);
            }
        }

        void storeRemainders(__m512i remainders) {
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress2();
            if(Size + Offset <= 32) {
                _mm256_store_si256((__m256i*)nonOffsetAddr, _mm512_castsi512_si256(remainders));
            }
            else{ 
                _mm512_store_si512(nonOffsetAddr, remainders);
            }
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

        static constexpr __m512i getPackedStoreMask(std::size_t loc) {
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
            __m512i packedStore = loadRemainders();
            __m512i packedRemainders = _mm512_maskz_set1_epi8(_cvtu64_mask64(-1ull), remainderDoubledToFullByte);
            __m512i shuffleMoveRight = {7, 0, 1, 2, 3, 4, 5, 6};
            __m512i packedStoreShiftedRight = _mm512_permutexvar_epi64(shuffleMoveRight, packedStore);
            __m512i packedStoreShiftedRight4Bits = _mm512_shldi_epi64(packedStore, packedStoreShiftedRight, 4); //Yes this says shldi which is left shift and well we are doing a left shift but that's in big endian, and well when I work with intrinsics I start thinking little endian.
            __m512i newPackedStore = _mm512_ternarylogic_epi32(packedStoreShiftedRight4Bits, packedStore, packedStoreMasks[loc], 0b11011000);
            newPackedStore = _mm512_ternarylogic_epi32(newPackedStore, packedRemainders, remainderStoreMasks[loc], 0b11011000);
            storeRemainders(_mm512_mask_blend_epi8(StoreMask, packedStore, newPackedStore));
            return retval;
        }

        void remove(std::size_t loc) {
            __m512i packedStore = loadRemainders();
            __m512i shuffleMoveLeft = {1, 2, 3, 4, 5, 6, 7, 0};
            __m512i packedStoreShiftedLeft = _mm512_permutexvar_epi64(shuffleMoveLeft, packedStore);
            __m512i packedStoreShiftedLeft4Bits = _mm512_shrdi_epi64(packedStore, packedStoreShiftedLeft, 4);
            __m512i newPackedStore = _mm512_ternarylogic_epi32(packedStoreShiftedLeft4Bits, packedStore, packedStoreMasks[loc], 0b11011000);
            storeRemainders(_mm512_mask_blend_epi8(StoreMask, packedStore, newPackedStore));
        }

        std::uint_fast8_t removeReturn(std::size_t loc) {
            std::uint_fast8_t retval = get4Bits(remainders[loc/2], loc%2);
            remove(loc);
            return retval;
        }

        std::uint64_t get(std::size_t loc) const {
            return get4Bits(remainders[loc/2], loc % 2);
        }

        std::uint_fast8_t removeFirst() {
            std::uint_fast8_t first = get4Bits(remainders[0], 0);
            remove(0);
            return first;
        }

        // Original q: Should this even be vectorized really? Cause that would be more consistent, but probably slower on average since maxPossible-minPossible should be p small? Then again already overhead of working with bytes
        // Original plan was: Returns 0 if can definitely say this is not in the filter, 1 if definitely is, 2 if need to go to backyard
        // Feels like def a good idea to vectorize now
        std::uint64_t queryNonVectorized(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            std::uint64_t retMask = 0;
            for(size_t i{bounds.first}; i < bounds.second; i++) {
                if(get4Bits(remainders[i/2], i%2) == remainder) {
                    retMask |= 1ull << i;
                }assert(remainder < 16);
            }
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
            __m512i packedStore = loadRemainders();
            __m512i packedRemainder = _mm512_maskz_set1_epi8(-1ull, remainder*17);
            static constexpr __m512i expanderShuffle = get4BitExpanderShuffle();
            __m512i doubledPackedStore = _mm512_permutexvar_epi8(expanderShuffle, packedStore);
            __m512i maskedXNORedPackedStore = _mm512_ternarylogic_epi32(doubledPackedStore, packedRemainder, getDoublePackedStoreTernaryMask(), 0b00101000);
            __mmask64 compared = _knot_mask64(_mm512_test_epi8_mask(maskedXNORedPackedStore, maskedXNORedPackedStore));
            return _cvtmask64_u64(compared) & mask;
        }

        std::uint64_t queryVectorized(std::uint_fast8_t remainder, std::pair<size_t, size_t> bounds) {
            __m512i packedStore = loadRemainders();
            __m512i packedRemainder = _mm512_maskz_set1_epi8(_cvtu64_mask64(-1ull), remainder*17);
            static constexpr __m512i expanderShuffle = get4BitExpanderShuffle();
            __m512i doubledPackedStore = _mm512_permutexvar_epi8(expanderShuffle, packedStore);
            __m512i maskedXNORedPackedStore = _mm512_ternarylogic_epi32(doubledPackedStore, packedRemainder, getDoublePackedStoreTernaryMask(), 0b00101000);
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

    //This filter stores 12 bits by using an 8 bit store and 4 bit store. It stores the lower 8 bits in the 8 bit store, and the higher order 4 bits in the 4 bit store.
    // template<std::size_t NumRemainders, std::size_t Offset>
    // struct alignas(1) RemainderStore<12, NumRemainders, Offset> {
    //     using Store4BitType = RemainderStore<4, NumRemainders, Offset>;
    //     static constexpr std::size_t Size4BitPart = Store4BitType::Size;
    //     using Store8BitType = RemainderStore<8, NumRemainders, Offset+Size4BitPart>;
    //     static constexpr std::size_t Size8BitPart = Store8BitType::Size;
    //     static constexpr std::size_t Size = Size8BitPart+Size4BitPart;

    //     Store8BitType store8BitPart;
    //     Store4BitType store4BitPart;

    //     std::uint_fast16_t insert(std::uint_fast16_t remainder, std::size_t loc) {
    //         if constexpr (DEBUG) {
    //             assert(remainder <= 4095);
    //             assert(loc <= NumRemainders);
    //         }

    //         uint_fast8_t remainder8BitPart = remainder & 255;
    //         uint_fast8_t remainder4BitPart = remainder >> 8;
    //         uint_fast16_t overflow = store8BitPart.insert(remainder8BitPart, loc) << 4ull;
    //         overflow |= store4BitPart.insert(remainder4BitPart, loc);
    //         return overflow;
    //     }

    //     void remove(std::size_t loc) {
    //         store8BitPart.remove(loc);
    //         store4BitPart.remove(loc);
    //     }

    //     std::uint_fast16_t removeReturn(std::size_t loc) {
    //         uint_fast16_t retval8BitPart = store8BitPart.removeReturn(loc);
    //         uint_fast16_t retval4BitPart = store4BitPart.removeReturn(loc);
    //         return retval8BitPart + (retval4BitPart << 8);
    //     }

    //     std::uint64_t query(std::uint_fast16_t remainder, std::pair<size_t, size_t> bounds) {
    //         if constexpr (DEBUG) {
    //             assert(remainder <= 4095);
    //             assert(bounds.second <= NumRemainders);
    //         }

    //         uint_fast8_t remainder8BitPart = remainder & 255;
    //         uint_fast8_t remainder4BitPart = remainder >> 8;

    //         return store8BitPart.query(remainder8BitPart, bounds) & store4BitPart.query(remainder4BitPart, bounds);
    //     }

    //     //This is really just adhoc stuff to get the deletions to work, so that we find where the keys are that match the bucket we're coming from
    //     std::uint64_t query4BitPartMask(std::uint_fast8_t bits, std::uint64_t mask) {
    //         return store4BitPart.queryVectorizedMask(bits, mask);
    //     }

    //     std::uint64_t queryVectorizedMask(std::uint_fast16_t remainder, std::uint64_t mask) {
    //         if constexpr (DEBUG) {
    //             assert(remainder <= 4095);
    //         }

    //         uint_fast8_t remainder8BitPart = remainder & 255;
    //         uint_fast8_t remainder4BitPart = remainder >> 8;

    //         return store8BitPart.queryVectorizedMask(remainder8BitPart, mask) & store4BitPart.queryVectorizedMask(remainder4BitPart, mask);
    //     }
    // };

    template<std::size_t SizeFirst, std::size_t SizeSecond, std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStoreTwoPieces {
        using StoreFirstType = RemainderStore<SizeFirst, NumRemainders, Offset>;
        static constexpr std::size_t TotalSizeFirst = StoreFirstType::Size;
        using StoreSecondType = RemainderStore<SizeSecond, NumRemainders, Offset+TotalSizeFirst>;
        static constexpr std::size_t TotalSizeSecond = StoreSecondType::Size;
        static constexpr std::size_t Size = TotalSizeFirst+TotalSizeSecond;

        StoreFirstType storeFirstPart;
        StoreSecondType storeSecondPart;

        std::uint64_t insert(std::uint64_t remainder, std::size_t loc) {
            if constexpr (DEBUG) {
                assert(remainder <= (1ull << (SizeFirst + SizeSecond)) - 1);
                assert(loc <= NumRemainders);
            }

            uint64_t remainderFirstPart = remainder & ((1ull << SizeFirst) - 1);
            uint64_t remainderSecondPart = remainder >> SizeFirst;
            uint64_t overflow = storeFirstPart.insert(remainderFirstPart, loc) << 4ull;
            overflow |= storeSecondPart.insert(remainderSecondPart, loc);
            return overflow;
        }

        void remove(std::size_t loc) {
            storeFirstPart.remove(loc);
            storeSecondPart.remove(loc);
        }

        std::uint64_t removeReturn(std::size_t loc) {
            uint64_t retvalFirstPart = storeFirstPart.removeReturn(loc);
            uint64_t retvalSecondPart = storeSecondPart.removeReturn(loc);
            return retvalFirstPart + (retvalSecondPart << SizeFirst);
        }

        std::uint64_t get(std::size_t loc) const{
            uint64_t retvalFirstPart = storeFirstPart.get(loc);
            uint64_t retvalSecondPart = storeSecondPart.get(loc);
            return retvalFirstPart + (retvalSecondPart << SizeFirst);
        }

        std::uint64_t query(std::uint64_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(remainder <= (1ull << (SizeFirst + SizeSecond)) - 1);
                assert(bounds.second <= NumRemainders);
            }

            uint64_t remainderFirstPart = remainder & ((1ull << SizeFirst) - 1);
            uint64_t remainderSecondPart = remainder >> SizeFirst;

            return storeFirstPart.query(remainderFirstPart, bounds) & storeSecondPart.query(remainderSecondPart, bounds);
        }

        //This is really just adhoc stuff to get the deletions to work, so that we find where the keys are that match the bucket we're coming from
        std::uint64_t query4BitPartMask(std::uint_fast8_t bits, std::uint64_t mask) {
            return storeSecondPart.queryVectorizedMask(bits, mask);
        }

        std::uint64_t queryVectorizedMask(std::uint64_t remainder, std::uint64_t mask) {
            if constexpr (DEBUG) {
                assert(remainder <= (1ull << (SizeFirst + SizeSecond)) - 1);
            }

            uint64_t remainderFirstPart = remainder & ((1ull << SizeFirst) - 1);
            uint64_t remainderSecondPart = remainder >> SizeFirst;

            return storeFirstPart.queryVectorizedMask(remainderFirstPart, mask) & storeSecondPart.queryVectorizedMask(remainderSecondPart, mask);
        }
    };

    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore<12, NumRemainders, Offset> : RemainderStoreTwoPieces<8, 4, NumRemainders, Offset>{};

    //Requires the remainder to be aligned to two byte boundary, as otherwise could not use the 2-byte AVX512 instruction and would be inneficient
    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore<16, NumRemainders, Offset> {
        static constexpr std::size_t Size = NumRemainders * 2;
        std::array<std::uint16_t, NumRemainders> remainders;

        static_assert(Offset % 2 == 0);

        static constexpr size_t WordOffset = Offset/2;

        static constexpr __mmask32 StoreMask = (NumRemainders + WordOffset < 32) ? ((1u << (NumRemainders+WordOffset)) - (1u << WordOffset)) : (-(1u << WordOffset));

        __m512i* getNonOffsetBucketAddress2() {
            return reinterpret_cast<__m512i*>(reinterpret_cast<std::uint8_t*>(&remainders) - Offset);
        }

        __m512i loadRemainders() { //Stopgap measure to at least avoid loading and storing across cache lines
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress2();
            if(Size + Offset <= 32) {
                return _mm512_castsi256_si512(_mm256_load_si256((__m256i*)nonOffsetAddr));
            }
            else{ 
                return _mm512_load_si512(nonOffsetAddr);
            }
        }

        void storeRemainders(__m512i remainders) {
            __m512i* nonOffsetAddr = getNonOffsetBucketAddress2();
            if(Size + Offset <= 32) {
                _mm256_store_si256((__m256i*)nonOffsetAddr, _mm512_castsi512_si256(remainders));
            }
            else{ 
                _mm512_store_si512(nonOffsetAddr, remainders);
            }
        }

        static constexpr __m512i getShuffleVector(std::size_t loc) {
            std::array<std::uint16_t, 32> words;
            for(size_t i=0; i < 32; i++) {
                if (i < loc+WordOffset) {
                    words[i] = i;
                }
                else if (i == loc+WordOffset) {
                    words[i] = 0;
                }
                else if (i < NumRemainders+WordOffset){
                    words[i] = i-1;
                }
                else {
                    words[i] = i;
                }
            }
            return std::bit_cast<__m512i>(words);
        }

        static constexpr std::array<m512iWrapper, 32> getShuffleVectors() {
            std::array<m512iWrapper, 32> masks;
            for(size_t i = 0; i < 32; i++) {
                masks[i] = getShuffleVector(i);
            }
            return masks;
        }

        static constexpr __m512i getRemoveShuffleVector(std::size_t loc) {
            std::array<uint16_t, 32> words;
            for(size_t i=0; i < 32; i++) {
                if (i < loc+WordOffset) {
                    words[i] = i;
                }
                else if (i < NumRemainders+WordOffset){
                    words[i] = i+1;
                }
                else {
                    words[i] = i;
                }
            }
            return std::bit_cast<__m512i>(words);
        }

        static constexpr std::array<m512iWrapper, 32> getRemoveShuffleVectors() {
            std::array<m512iWrapper, 32> masks;
            for(size_t i = 0; i < 32; i++) {
                masks[i] = getRemoveShuffleVector(i);
            }
            return masks;
        }

        static constexpr std::array<m512iWrapper, 32> shuffleVectors = getShuffleVectors();
        static constexpr std::array<m512iWrapper, 32> removeShuffleVectors = getRemoveShuffleVectors();

        std::uint_fast16_t insert(std::uint_fast16_t remainder, std::size_t loc) {
            if constexpr (DEBUG) {
                assert(loc < NumRemainders);
            }
            std::uint_fast16_t retval = remainders[NumRemainders-1];

            __m512i packedStore = loadRemainders();
            __m512i packedStoreWithRemainder = _mm512_mask_set1_epi16(packedStore, 1, remainder);
            packedStore = _mm512_mask_permutexvar_epi16(packedStore, StoreMask, shuffleVectors[loc], packedStoreWithRemainder);
            storeRemainders(packedStore);

            return retval;
        }

        void remove(std::size_t loc) {
            __m512i packedStore = loadRemainders();
            packedStore = _mm512_mask_permutexvar_epi16(packedStore, StoreMask, removeShuffleVectors[loc], packedStore);
            storeRemainders(packedStore);
        }

        //Removes and returns the value that was there
        std::uint_fast16_t removeReturn(std::size_t loc) {
            std::uint_fast16_t retval = remainders[loc];
            remove(loc);
            return retval;
        }

        std::uint64_t get(std::size_t loc) const {
            return remainders[loc];
        }

        std::uint_fast16_t removeFirst() {
            std::uint_fast16_t first = remainders[0];
            remove(0);
            return first;
        }

        // Original q: Should this even be vectorized really? Cause that would be more consistent, but probably slower on average since maxPossible-minPossible should be p small? Then again already overhead of working with bytes
        // Original plan was: Returns 0 if can definitely say this is not in the filter, 1 if definitely is, 2 if need to go to backyard
        // Feels like def a good idea to vectorize now
        std::uint64_t queryNonVectorized(std::uint_fast16_t remainder, std::pair<std::size_t, std::size_t> bounds) {
            std::uint64_t retMask = 0;
            for(size_t i{bounds.first}; i < bounds.second; i++) {
                if(remainders[i] == remainder) {
                    retMask |= 1ull << i;
                }
            }
            return retMask;
        }

        std::uint64_t queryVectorizedMask(std::uint_fast16_t remainder, std::uint64_t mask) {
            // __mmask64 queryMask = _cvtu64_mask64(((1ull << bounds.second) - (1ull << bounds.first)) << WordOffset);
            __m512i packedStore = loadRemainders();
            __m512i remainderVec = _mm512_maskz_set1_epi16(-1u, remainder);
            return (_cvtmask32_u32(_mm512_mask_cmpeq_epu16_mask(-1u, packedStore, remainderVec)) >> WordOffset) & mask;
        }

        std::uint64_t queryVectorized(std::uint_fast16_t remainder, std::pair<std::size_t, std::size_t> bounds) {
            __mmask32 queryMask = _cvtu32_mask32(((1u << bounds.second) - (1u << bounds.first)) << WordOffset);
            __m512i packedStore = loadRemainders();
            __m512i remainderVec = _mm512_maskz_set1_epi16(-1, remainder);
            return _cvtmask32_u32(_mm512_mask_cmpeq_epu16_mask(queryMask, packedStore, remainderVec)) >> WordOffset;
        }

        // Returns a bitmask of which remainders match within the bounds. Maybe this should return not a uint64_t but a mask type? Cause we should be able to do everything with them
        std::uint64_t query(std::uint_fast16_t remainder, std::pair<size_t, size_t> bounds) {
            if constexpr (DEBUG) {
                assert(bounds.second <= NumRemainders);
            }
            // return queryNonVectorized(remainder, bounds);
            return queryVectorized(remainder, bounds);
        }
    };

    template<std::size_t NumRemainders, std::size_t Offset>
    struct alignas(1) RemainderStore<20, NumRemainders, Offset> : RemainderStoreTwoPieces<16, 4, NumRemainders, Offset>{};

}

#endif