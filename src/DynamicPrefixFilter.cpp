#include "DynamicPrefixFilter.hpp"
#include <optional>
#include <iostream>
#include <map>
#include <utility>

using namespace DynamicPrefixFilter;

DynamicPrefixFilter8Bit::DynamicPrefixFilter8Bit(std::size_t N): 
    frontyard((N+BucketNumMiniBuckets-1)/BucketNumMiniBuckets),
    backyard((frontyard.size()+FrontyardToBackyardRatio-1)/FrontyardToBackyardRatio + FrontyardToBackyardRatio),
    // overflows(frontyard.size()),
    capacity{N},
    range{capacity*256}
{}

std::uint64_t DynamicPrefixFilter8Bit::sizeFilter() {
    return (frontyard.size() + backyard.size()) * 64;
}

DynamicPrefixFilter8Bit::FrontyardQRContainerType DynamicPrefixFilter8Bit::getQRPairFromHash(std::uint64_t hash) {
    // return std::make_pair((hash >> 8) % capacity, hash & 255);
    // return std::make_pair(hash >> 8, hash & 255);
    if constexpr (DEBUG) {
        FrontyardQRContainerType f = FrontyardQRContainerType(hash >> 8, hash & 255);
        assert(f.bucketIndex < frontyard.size());
        BackyardQRContainerType fb1(f, 0, frontyard.size());
        BackyardQRContainerType fb2(f, 1, frontyard.size());
        assert(fb1.bucketIndex < backyard.size() && fb2.bucketIndex < backyard.size());
    }
    return FrontyardQRContainerType(hash >> 8, hash & 255);
}

static std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> backyardToFrontyard;

void DynamicPrefixFilter8Bit::insertOverflow(FrontyardQRContainerType overflow) {
    BackyardQRContainerType firstBackyardQR(overflow, 0, frontyard.size());
    BackyardQRContainerType secondBackyardQR(overflow, 1, frontyard.size());
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
    // std::cout << firstBackyardQR.bucketIndex << " " << secondBackyardQR.bucketIndex << " " << fillOfFirstBackyardBucket << " " << fillOfSecondBackyardBucket << std::endl;
    
    if(fillOfFirstBackyardBucket < fillOfSecondBackyardBucket) {
        if constexpr (DEBUG)
            assert(backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR).miniBucketIndex == -1ull); //Failing this would be *really* bad, as it is the main unproven assumption this algo relies on
        else 
            backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR);
        // assert(query(hash));
    }
    else {
        if constexpr (DEBUG)
            assert(backyard[secondBackyardQR.bucketIndex].insert(secondBackyardQR).miniBucketIndex == -1ull);
        else
            backyard[secondBackyardQR.bucketIndex].insert(secondBackyardQR);
        // assert(query(hash));
    }
}

void DynamicPrefixFilter8Bit::insert(std::uint64_t hash) {
    // std::pair<std::uint64_t, std::uint64_t> qrPair = getQRPairFromHash(hash);
    // FrontyardQRContainerType frontyardQR(qrPair.first, qrPair.second);
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    FrontyardQRContainerType overflow = frontyard[frontyardQR.bucketIndex].insert(frontyardQR);
    if constexpr (DEBUG) {
        assert((uint64_t)(&frontyard[frontyardQR.bucketIndex]) % 64 == 0);
        assert(sizeof(frontyard[frontyardQR.bucketIndex]) == 64);
    }
    if(overflow.miniBucketIndex != -1ull) {
        // overflows[frontyardQR.bucketIndex]++;
        insertOverflow(overflow);
    }
}

// std::pair<bool, bool> DynamicPrefixFilter8Bit::query(std::uint64_t hash) {
//     // std::pair<std::uint64_t, std::uint64_t> qrPair = getQRPairFromHash(hash);
//     // FrontyardQRContainerType frontyardQR(qrPair.first, qrPair.second);
//     FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
//     std::pair<bool, bool> frontyardQuery = frontyard[frontyardQR.bucketIndex].query(frontyardQR);
//     if(frontyardQuery.first) return {true, false}; //found it in the frontyard
//     if(!frontyardQuery.second) return {false, false}; //didn't find it in frontyard and don't need to go to backyard, so we done

//     BackyardQRContainerType firstBackyardQR(frontyardQR, 0, frontyard.size());
//     BackyardQRContainerType secondBackyardQR(frontyardQR, 1, frontyard.size());
//     return {backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR).first || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR).first, true}; //Return true if find it in either of the backyard buckets
// }

// static uint64_t XYZ = 0;

std::uint64_t DynamicPrefixFilter8Bit::query(std::uint64_t hash) {
    // std::pair<std::uint64_t, std::uint64_t> qrPair = getQRPairFromHash(hash);
    // FrontyardQRContainerType frontyardQR(qrPair.first, qrPair.second);
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    std::uint64_t frontyardQuery = frontyard[frontyardQR.bucketIndex].query(frontyardQR);
    // std::uint64_t frontyardQuery = (XYZ++) & 16;
    // if(frontyardQuery.first) return 1; //found it in the frontyard
    // if(!frontyardQuery.second) return 0; //didn't find it in frontyard and don't need to go to backyard, so we done
    if(frontyardQuery != 2) return frontyardQuery;

    BackyardQRContainerType firstBackyardQR(frontyardQR, 0, frontyard.size());
    BackyardQRContainerType secondBackyardQR(frontyardQR, 1, frontyard.size());
    // return (backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR).first || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR).first) | 2; //Return true if find it in either of the backyard buckets
    return ((backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR) | backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR)) & 1) | 2;
}

bool DynamicPrefixFilter8Bit::querySimple(std::uint64_t hash) {
    // std::pair<std::uint64_t, std::uint64_t> qrPair = getQRPairFromHash(hash);
    // FrontyardQRContainerType frontyardQR(qrPair.first, qrPair.second);
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    std::uint64_t frontyardQuery = frontyard[frontyardQR.bucketIndex].query(frontyardQR);
    // std::uint64_t frontyardQuery = (XYZ++) & 16;
    // if(frontyardQuery.first) return 1; //found it in the frontyard
    // if(!frontyardQuery.second) return 0; //didn't find it in frontyard and don't need to go to backyard, so we done
    if(frontyardQuery != 2) return frontyardQuery;

    BackyardQRContainerType firstBackyardQR(frontyardQR, 0, frontyard.size());
    BackyardQRContainerType secondBackyardQR(frontyardQR, 1, frontyard.size());
    // return (backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR).first || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR).first) | 2; //Return true if find it in either of the backyard buckets
    return backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR) || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR);
}

bool DynamicPrefixFilter8Bit::remove(std::uint64_t hash) {
    assert(querySimple(hash));
    FrontyardQRContainerType frontyardQR = getQRPairFromHash(hash);
    BackyardQRContainerType firstBackyardQR(frontyardQR, 0, frontyard.size());
    BackyardQRContainerType secondBackyardQR(frontyardQR, 1, frontyard.size());
    bool frontyardBucketFull = frontyard[frontyardQR.bucketIndex].full();
    // std::cout << frontyardBucketFull << " c " << frontyard[frontyardQR.bucketIndex].countKeys() << std::endl;
    bool elementInFrontyard = frontyard[frontyardQR.bucketIndex].remove(frontyardQR);
    if(!frontyardBucketFull) {
        // if(!elementInFrontyard) {
        //     std::cout << "what?" << std::endl;
        // }
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
            // std::cout << fillOfFirstBackyardBucket << " " << fillOfSecondBackyardBucket << std::endl;
            // std::cout << frontyardQR.remainder << " " << frontyardQR.miniBucketIndex << std::endl;
            // std::cout << "(" << firstKeyBackyard << ", " << firstMiniBucketBackyard << "); (" << secondKeyBackyard << ", " << secondMiniBucketBackyard << ")" << "; " << ((int)frontyard[frontyardQR.bucketIndex].queryWhichMiniBucket(FrontyardBucketCapacity-2)) << std::endl;
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
        if constexpr (DEBUG) {
            // std::cout << frontyardQR.remainder << " " << frontyardQR.miniBucketIndex << std::endl;
            // std::cout << "here" << std::endl;
        }
        frontyard[frontyardQR.bucketIndex].insert(frontyardQR);
    }
    else {
        if (!backyard[firstBackyardQR.bucketIndex].remove(firstBackyardQR)) {
            // return backyard[secondBackyardQR.bucketIndex].remove(secondBackyardQR);
            if(!backyard[secondBackyardQR.bucketIndex].remove(secondBackyardQR)) {
                // std::cout << "WAH" << std::endl;
                return false;
            }
        }
    }
    return true;
}

// double DynamicPrefixFilter8Bit::getAverageOverflow() {
//     double overflow = 0.0;
//     for(size_t o: overflows) overflow+=o;
//     return overflow/frontyard.size();
// }