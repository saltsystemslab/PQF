#include "DynamicPrefixFilter.hpp"
#include <optional>
#include <iostream>
#include <map>
#include <utility>

using namespace DynamicPrefixFilter;

// template class DynamicPrefixFilter8Bit<46, 51, 16, 8, 64, 32>; //These fail for obvious reasons, but I wanted to try anyways
// template class DynamicPrefixFilter8Bit<46, 51, 16, 7, 64, 32>;
template class DynamicPrefixFilter8Bit<46, 51, 35, 8, 64, 64>;
template class DynamicPrefixFilter8Bit<46, 51, 35, 6, 64, 64>;
template class DynamicPrefixFilter8Bit<46, 51, 35, 4, 64, 64>;
template class DynamicPrefixFilter8Bit<48, 51, 35, 8, 64, 64>;
template class DynamicPrefixFilter8Bit<49, 51, 35, 8, 64, 64>;
template class DynamicPrefixFilter8Bit<51, 51, 35, 8, 64, 64>;
template class DynamicPrefixFilter8Bit<51, 51, 35, 6, 64, 64>;
template class DynamicPrefixFilter8Bit<52, 51, 35, 8, 64, 64>;
template class DynamicPrefixFilter8Bit<52, 51, 35, 8, 64, 64, true>;
template class DynamicPrefixFilter8Bit<22, 25, 17, 8, 32, 32>;
template class DynamicPrefixFilter8Bit<22, 25, 17, 8, 32, 32, true>;
template class DynamicPrefixFilter8Bit<22, 25, 17, 6, 32, 32>;
template class DynamicPrefixFilter8Bit<22, 25, 17, 4, 32, 32>;
template class DynamicPrefixFilter8Bit<25, 25, 17, 8, 32, 32>;
template class DynamicPrefixFilter8Bit<25, 25, 17, 6, 32, 32>;
template class DynamicPrefixFilter8Bit<25, 25, 17, 4, 32, 32>;
template class DynamicPrefixFilter8Bit<25, 25, 35, 8, 32, 64>;
template class DynamicPrefixFilter8Bit<25, 25, 16, 8, 32, 32>;
template class DynamicPrefixFilter8Bit<23, 25, 17, 8, 32, 32>;
template class DynamicPrefixFilter8Bit<23, 25, 17, 6, 32, 32>;
template class DynamicPrefixFilter8Bit<23, 25, 17, 4, 32, 32>;
template class DynamicPrefixFilter8Bit<24, 25, 17, 8, 32, 32>;
template class DynamicPrefixFilter8Bit<24, 25, 17, 6, 32, 32>;

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::DynamicPrefixFilter8Bit(std::size_t N, bool Normalize): 
    capacity{Normalize ? static_cast<size_t>(N/NormalizingFactor) : N},
    range{capacity*256},
    frontyard((capacity+BucketNumMiniBuckets-1)/BucketNumMiniBuckets),
    backyard((frontyard.size()+FrontyardToBackyardRatio-1)/FrontyardToBackyardRatio + FrontyardToBackyardRatio*2)
    // overflows(frontyard.size()),
{
    R = frontyard.size() / FrontyardToBackyardRatio / FrontyardToBackyardRatio + 1;
    if(R % (FrontyardToBackyardRatio - 1) == 0) R++;
    // std::cout << NormalizingFactor << " " << frontyard.size() << std::endl;
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
std::uint64_t DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::sizeFilter() {
    return (frontyard.size()*sizeof(FrontyardBucketType)) + (backyard.size()*sizeof(BackyardBucketType));
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::FrontyardQRContainerType DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::getQRPairFromHash(std::uint64_t hash) {
    if constexpr (DEBUG) {
        FrontyardQRContainerType f = FrontyardQRContainerType(hash >> 8, hash & 255);
        assert(f.bucketIndex < frontyard.size());
        BackyardQRContainerType fb1(f, 0, R);
        BackyardQRContainerType fb2(f, 1, R);
        assert(fb1.bucketIndex < backyard.size() && fb2.bucketIndex < backyard.size());
    }
    return FrontyardQRContainerType(hash >> 8, hash & 255);
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
void DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::insertOverflow(FrontyardQRContainerType overflow) {
    BackyardQRContainerType firstBackyardQR(overflow, 0, R);
    BackyardQRContainerType secondBackyardQR(overflow, 1, R);
    if constexpr (DEBUG) {
        if(backyardToFrontyard.count(std::make_pair(firstBackyardQR.bucketIndex, firstBackyardQR.whichFrontyardBucket)) == 0) {
            backyardToFrontyard[std::make_pair(firstBackyardQR.bucketIndex, firstBackyardQR.whichFrontyardBucket)] = overflow.bucketIndex;
        }
        if(backyardToFrontyard.count(std::make_pair(secondBackyardQR.bucketIndex, secondBackyardQR.whichFrontyardBucket)) == 0) {
            backyardToFrontyard[std::make_pair(secondBackyardQR.bucketIndex, secondBackyardQR.whichFrontyardBucket)] = overflow.bucketIndex;
        }
        assert(backyardToFrontyard[std::make_pair(firstBackyardQR.bucketIndex, firstBackyardQR.whichFrontyardBucket)] == overflow.bucketIndex);
        assert(backyardToFrontyard[std::make_pair(secondBackyardQR.bucketIndex, secondBackyardQR.whichFrontyardBucket)] == overflow.bucketIndex);
    }
    std::size_t fillOfFirstBackyardBucket = backyard[firstBackyardQR.bucketIndex].countKeys();
    std::size_t fillOfSecondBackyardBucket = backyard[secondBackyardQR.bucketIndex].countKeys();
    
    if(fillOfFirstBackyardBucket < fillOfSecondBackyardBucket) {
        if constexpr (PARTIAL_DEBUG || DEBUG)
            assert(backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR).miniBucketIndex == -1ull); //Failing this would be *really* bad, as it is the main unproven assumption this algo relies on
        else {
            if constexpr (DIAGNOSTICS) {
                insertFailure = backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR).miniBucketIndex != -1ull;
                failureFB = overflow.bucketIndex;
                failureBucket1 = firstBackyardQR.bucketIndex;
                failureBucket2 = secondBackyardQR.bucketIndex;
                failureWFB = firstBackyardQR.whichFrontyardBucket;
            }
            else {
                backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR);
            }
        }
        // assert(query(hash));
    }
    else {
        if constexpr (PARTIAL_DEBUG || DEBUG)
            assert(backyard[secondBackyardQR.bucketIndex].insert(secondBackyardQR).miniBucketIndex == -1ull);
        else {
            if constexpr (DIAGNOSTICS) {
                insertFailure = backyard[secondBackyardQR.bucketIndex].insert(secondBackyardQR).miniBucketIndex != -1ull;
                failureFB = overflow.bucketIndex;
                failureBucket1 = firstBackyardQR.bucketIndex;
                failureBucket2 = secondBackyardQR.bucketIndex;
                failureWFB = firstBackyardQR.whichFrontyardBucket;
            }
            else {
                backyard[secondBackyardQR.bucketIndex].insert(secondBackyardQR);
            }
        }
        // assert(query(hash));
    }
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
void DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::insert(std::uint64_t hash) {
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    FrontyardQRContainerType overflow = frontyard[frontyardQR.bucketIndex].insert(frontyardQR);
    if constexpr (DEBUG) {
        assert((uint64_t)(&frontyard[frontyardQR.bucketIndex]) % FrontyardBucketSize == 0);
    }
    if(overflow.miniBucketIndex != -1ull) {
        // overflows[frontyardQR.bucketIndex]++;
        insertOverflow(overflow);
    }
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
std::uint64_t DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::queryWhere(std::uint64_t hash) {
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    std::uint64_t frontyardQuery = frontyard[frontyardQR.bucketIndex].query(frontyardQR);
    if(frontyardQuery != 2) return frontyardQuery;

    if constexpr (DIAGNOSTICS) {
        backyardLookupCount ++;
    }

    BackyardQRContainerType firstBackyardQR(frontyardQR, 0, R);
    BackyardQRContainerType secondBackyardQR(frontyardQR, 1, R);
    // return (backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR).first || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR).first) | 2; //Return true if find it in either of the backyard buckets
    return ((backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR) | backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR)) & 1) | 2;
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
bool DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::query(std::uint64_t hash) {
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    std::uint64_t frontyardQuery = frontyard[frontyardQR.bucketIndex].query(frontyardQR);
    if(frontyardQuery != 2) return frontyardQuery;
    
    // return true;

    if constexpr (DIAGNOSTICS) {
        backyardLookupCount ++;
    }

    BackyardQRContainerType firstBackyardQR(frontyardQR, 0, R);
    BackyardQRContainerType secondBackyardQR(frontyardQR, 1, R);
    // return (backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR).first || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR).first) | 2; //Return true if find it in either of the backyard buckets
    return backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR) || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR);
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
bool DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::remove(std::uint64_t hash) {
    if constexpr (DEBUG)
        assert(query(hash));
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    BackyardQRContainerType firstBackyardQR(frontyardQR, 0, R);
    BackyardQRContainerType secondBackyardQR(frontyardQR, 1, R);
    bool frontyardBucketFull = frontyard[frontyardQR.bucketIndex].full();
    bool elementInFrontyard = frontyard[frontyardQR.bucketIndex].remove(frontyardQR);
    if(!frontyardBucketFull) {
        return elementInFrontyard;
    }
    else if (elementInFrontyard) { //In the case we removed it from the frontyard bucket, we need to bring back an element from the backyard (if there is one)
        //Pretty messy since need to figure out who in the backyard has the key with a smaller miniBucket, since we want to bring the key with the smallest miniBucket index back into the frontyard
        std::size_t fillOfFirstBackyardBucket = backyard[firstBackyardQR.bucketIndex].countKeys();
        std::size_t fillOfSecondBackyardBucket = backyard[secondBackyardQR.bucketIndex].countKeys();
        if(fillOfFirstBackyardBucket==0 && fillOfSecondBackyardBucket==0) return true;
        std::uint64_t keysFromFrontyardInFirstBackyard = backyard[firstBackyardQR.bucketIndex].remainderStore.query4BitPartMask(firstBackyardQR.whichFrontyardBucket, (1ull << fillOfFirstBackyardBucket) - 1);
        std::uint64_t keysFromFrontyardInSecondBackyard = backyard[secondBackyardQR.bucketIndex].remainderStore.query4BitPartMask(secondBackyardQR.whichFrontyardBucket, (1ull << fillOfSecondBackyardBucket) - 1);
        if constexpr (DEBUG) {
            assert(firstBackyardQR.whichFrontyardBucket == firstBackyardQR.remainder >> 8);
            assert(secondBackyardQR.whichFrontyardBucket == secondBackyardQR.remainder >> 8);
        }
        if(keysFromFrontyardInFirstBackyard == 0 && keysFromFrontyardInSecondBackyard == 0) return true;
        std::uint64_t firstKeyBackyard = __builtin_ctzll(keysFromFrontyardInFirstBackyard);
        std::uint64_t secondKeyBackyard = __builtin_ctzll(keysFromFrontyardInSecondBackyard);
        std::uint64_t firstMiniBucketBackyard = backyard[firstBackyardQR.bucketIndex].queryWhichMiniBucket(firstKeyBackyard);
        std::uint64_t secondMiniBucketBackyard = backyard[secondBackyardQR.bucketIndex].queryWhichMiniBucket(secondKeyBackyard);
        if constexpr (DEBUG) {
            assert(firstMiniBucketBackyard >= frontyard[frontyardQR.bucketIndex].queryWhichMiniBucket(FrontyardBucketCapacity-2) && secondMiniBucketBackyard >= frontyard[frontyardQR.bucketIndex].queryWhichMiniBucket(FrontyardBucketCapacity-2));
        }
        if(firstMiniBucketBackyard < secondMiniBucketBackyard) {
            frontyardQR.miniBucketIndex = firstMiniBucketBackyard;
            frontyardQR.remainder = backyard[firstBackyardQR.bucketIndex].remainderStoreRemoveReturn(firstKeyBackyard, firstMiniBucketBackyard) & 255;
        }
        else {
            frontyardQR.miniBucketIndex = secondMiniBucketBackyard;
            frontyardQR.remainder = backyard[secondBackyardQR.bucketIndex].remainderStoreRemoveReturn(secondKeyBackyard, secondMiniBucketBackyard) & 255;
        }
        frontyard[frontyardQR.bucketIndex].insert(frontyardQR);
    }
    else {
        if (!backyard[firstBackyardQR.bucketIndex].remove(firstBackyardQR)) {
            return backyard[secondBackyardQR.bucketIndex].remove(secondBackyardQR);
            // if(!backyard[secondBackyardQR.bucketIndex].remove(secondBackyardQR)) {
            //     return false;
            // }
        }
    }
    return true;
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery>
size_t DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>::getNumBuckets() {
    return frontyard.size();
}

// double DynamicPrefixFilter8Bit::getAverageOverflow() {
//     double overflow = 0.0;
//     for(size_t o: overflows) overflow+=o;
//     return overflow/frontyard.size();
// }