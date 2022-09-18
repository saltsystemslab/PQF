#ifndef DYNAMIC_PREFIX_FILTER_HPP
#define DYNAMIC_PREFIX_FILTER_HPP

#include <vector>
#include <cstddef>
#include <cstdint>
#include "Bucket.hpp"
#include "QRContainers.hpp"
#include "RemainderStore.hpp"

namespace DynamicPrefixFilter {
    class DynamicPrefixFilter8Bit {
        constexpr static std::size_t FrontyardBucketSize = 64;
        //Change the following two names? they're not consistent with the rest of everything, although I kind of want it to be different to differentiate global config from the local template param, so idk.
        constexpr static std::size_t FrontyardBucketCapacity = 51; //number of actual remainders stored before overflow to backyard
        constexpr static std::size_t BucketNumMiniBuckets = 51; //the average number of keys that we expect to actually go into the bucket

        constexpr static std::size_t BackyardBucketSize = 64;
        constexpr static std::size_t BackyardBucketCapacity = 35;
        constexpr static std::size_t FrontyardToBackyardRatio = 8; //Max possible = 8
        //seems like even 8 works, so switch to 8?

        private:
            using FrontyardQRContainerType = FrontyardQRContainer<BucketNumMiniBuckets>;
            using FrontyardBucketType = Bucket<FrontyardBucketCapacity, BucketNumMiniBuckets, RemainderStore8Bit, FrontyardQRContainer>;
            using BackyardQRContainerType = BackyardQRContainer<BucketNumMiniBuckets, 8, FrontyardToBackyardRatio>;
            template<size_t NumMiniBuckets>
            using WrappedBackyardQRContainerType = BackyardQRContainer<NumMiniBuckets, 8, FrontyardToBackyardRatio>;
            using BackyardBucketType = Bucket<BackyardBucketCapacity, BucketNumMiniBuckets, RemainderStore12Bit, WrappedBackyardQRContainerType>;
            
            std::vector<FrontyardBucketType> frontyard;
            std::vector<BackyardBucketType> backyard;
            // std::vector<size_t> overflows;
            FrontyardQRContainerType getQRPairFromHash(std::uint64_t hash);
            void insertOverflow(FrontyardQRContainerType overflow);
        
        public:
            std::size_t capacity;
            std::size_t range;
            DynamicPrefixFilter8Bit(std::size_t N);
            void insert(std::uint64_t hash);
            std::uint64_t query(std::uint64_t hash);
            bool querySimple(std::uint64_t hash);
            // double getAverageOverflow();

    };
}

#endif