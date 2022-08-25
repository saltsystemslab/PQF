#include "DynamicPrefixFilter.hpp"
#include <optional>
#include <iostream>

using namespace DynamicPrefixFilter;

DynamicPrefixFilter8Bit::DynamicPrefixFilter8Bit(std::size_t N): 
    frontyard((N+BucketNumMiniBuckets-1)/BucketNumMiniBuckets),
    backyard((frontyard.size()+FrontyardToBackyardRatio-1)/FrontyardToBackyardRatio),
    overflows(frontyard.size()),
    capacity{N}
{}

std::pair<std::uint64_t, std::uint64_t> DynamicPrefixFilter8Bit::getQRPairFromHash(std::uint64_t hash) {
    return std::make_pair((hash >> 8) % capacity, hash & 255);
}

void DynamicPrefixFilter8Bit::insert(std::uint64_t hash) {
    std::pair<std::uint64_t, std::uint64_t> qrPair = getQRPairFromHash(hash);
    FrontyardQRContainerType frontyardQR(qrPair.first, qrPair.second);
    std::optional<FrontyardQRContainerType> overflow = frontyard[frontyardQR.bucketIndex].insert(frontyardQR);
    if(overflow.has_value()) {
        overflows[frontyardQR.bucketIndex]++;
        BackyardQRContainerType firstBackyardQR(*overflow, 0, frontyard.size());
        BackyardQRContainerType secondBackyardQR(*overflow, 1, frontyard.size());
        std::size_t fillOfFirstBackyardBucket = backyard[firstBackyardQR.bucketIndex].countKeys();
        std::size_t fillOfSecondBackyardBucket = backyard[secondBackyardQR.bucketIndex].countKeys();
        // std::cout << firstBackyardQR.bucketIndex << " " << secondBackyardQR.bucketIndex << " " << fillOfFirstBackyardBucket << " " << fillOfSecondBackyardBucket << std::endl;
        
        if(fillOfFirstBackyardBucket < fillOfSecondBackyardBucket) {
            assert(!backyard[firstBackyardQR.bucketIndex].insert(firstBackyardQR).has_value()); //Failing this would be *really* bad, as it is the main unproven assumption this algo relies on
            // assert(query(hash));
        }
        else {
            assert(!backyard[secondBackyardQR.bucketIndex].insert(secondBackyardQR).has_value());
            // assert(query(hash));
        }
    }
}

std::pair<bool, bool> DynamicPrefixFilter8Bit::query(std::uint64_t hash) {
    std::pair<std::uint64_t, std::uint64_t> qrPair = getQRPairFromHash(hash);
    FrontyardQRContainerType frontyardQR(qrPair.first, qrPair.second);
    std::pair<bool, bool> frontyardQuery = frontyard[frontyardQR.bucketIndex].query(frontyardQR);
    if(frontyardQuery.first) return {true, false}; //found it in the frontyard
    if(!frontyardQuery.second) return {false, false}; //didn't find it in frontyard and don't need to go to backyard, so we done

    BackyardQRContainerType firstBackyardQR(frontyardQR, 0, frontyard.size());
    BackyardQRContainerType secondBackyardQR(frontyardQR, 1, frontyard.size());
    return {backyard[firstBackyardQR.bucketIndex].query(firstBackyardQR).first || backyard[secondBackyardQR.bucketIndex].query(secondBackyardQR).first, true}; //Return true if find it in either of the backyard buckets
}

double DynamicPrefixFilter8Bit::getAverageOverflow() {
    double overflow = 0.0;
    for(size_t o: overflows) overflow+=o;
    return overflow/frontyard.size();
}