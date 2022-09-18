#include "DynamicPrefixFilter.hpp"
#include <optional>
#include <iostream>

using namespace DynamicPrefixFilter;

DynamicPrefixFilter8Bit::DynamicPrefixFilter8Bit(std::size_t N): 
    frontyard((N+BucketNumMiniBuckets-1)/BucketNumMiniBuckets),
    backyard((frontyard.size()+FrontyardToBackyardRatio-1)/FrontyardToBackyardRatio),
    // overflows(frontyard.size()),
    capacity{N},
    range{capacity*256}
{}

DynamicPrefixFilter8Bit::FrontyardQRContainerType DynamicPrefixFilter8Bit::getQRPairFromHash(std::uint64_t hash) {
    // return std::make_pair((hash >> 8) % capacity, hash & 255);
    // return std::make_pair(hash >> 8, hash & 255);
    return FrontyardQRContainerType(hash >> 8, hash & 255);
}

void DynamicPrefixFilter8Bit::insertOverflow(FrontyardQRContainerType overflow) {
    BackyardQRContainerType firstBackyardQR(overflow, 0, frontyard.size());
    BackyardQRContainerType secondBackyardQR(overflow, 1, frontyard.size());
    std::size_t fillOfFirstBackyardBucket = backyard[firstBackyardQR.bucketIndex].countKeys();
    std::size_t fillOfSecondBackyardBucket = backyard[secondBackyardQR.bucketIndex].countKeys();
    // std::cout << firstBackyardQR.bucketIndex << " " << secondBackyardQR.bucketIndex << " " << fillOfFirstBackyardBucket << " " << fillOfSecondBackyardBucket << std::endl;
    
    if(fillOfFirstBackyardBucket < fillOfSecondBackyardBucket) {
        if constexpr (DEBUG)
            assert(backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR).miniBucketIndex != -1ull); //Failing this would be *really* bad, as it is the main unproven assumption this algo relies on
        else 
            backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR);
        // assert(query(hash));
    }
    else {
        if constexpr (DEBUG)
            assert(backyard[secondBackyardQR.bucketIndex].insert(secondBackyardQR).miniBucketIndex != -1ull);
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
    return backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR) | backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR);
}

// double DynamicPrefixFilter8Bit::getAverageOverflow() {
//     double overflow = 0.0;
//     for(size_t o: overflows) overflow+=o;
//     return overflow/frontyard.size();
// }