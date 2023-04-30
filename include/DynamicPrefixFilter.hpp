#ifndef DYNAMIC_PREFIX_FILTER_HPP
#define DYNAMIC_PREFIX_FILTER_HPP

#include <vector>
#include <cstddef>
#include <cstdint>
#include <map>
#include "Bucket.hpp"
#include "QRContainers.hpp"
#include "RemainderStore.hpp"

namespace DynamicPrefixFilter {
    template<std::size_t SizeRemainders, std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity = 51, std::size_t BackyardBucketCapacity = 35, std::size_t FrontyardToBackyardRatio = 8, std::size_t FrontyardBucketSize = 64, std::size_t BackyardBucketSize = 64, bool FastSQuery = false>
    class PartitionQuotientFilter {
        static_assert(FrontyardBucketSize == 32 || FrontyardBucketSize == 64);
        static_assert(BackyardBucketSize == 32 || BackyardBucketSize == 64);
        // constexpr static std::size_t FrontyardBucketSize = 64;
        // //Change the following two names? they're not consistent with the rest of everything, although I kind of want it to be different to differentiate global config from the local template param, so idk.
        // constexpr static std::size_t FrontyardBucketCapacity = 51; //number of actual remainders stored before overflow to backyard
        // constexpr static std::size_t BucketNumMiniBuckets = 48; //the average number of keys that we expect to actually go into the bucket
        // //Set BucketNumMiniBuckets to:
        //     //51 to be space efficient (even 52 but that's even slower and cutting it a bit close with space overallocation to not fail)
        //     //49 to match 90% capacity VQF size -- roughly same speed for queries, faster for inserts
        //     //46 to match 85% capacity VQF size -- faster for everything
        //     //22 and FrontyardBucketCapacity to 25 for the really fast filter that needs smaller bucket sizes to work. Probably backyard to size 32 with capacity 17

        // constexpr static std::size_t BackyardBucketSize = 64;
        // constexpr static std::size_t BackyardBucketCapacity = 35;
        // constexpr static std::size_t FrontyardToBackyardRatio = 8; //Max possible = 8
        //seems like even 8 works, so switch to 8?

        private:
            using FrontyardQRContainerType = FrontyardQRContainer<BucketNumMiniBuckets>;
            using FrontyardBucketType = Bucket<SizeRemainders, FrontyardBucketCapacity, BucketNumMiniBuckets, FrontyardQRContainer, FrontyardBucketSize, FastSQuery>;
            static_assert(sizeof(FrontyardBucketType) == FrontyardBucketSize);
            using BackyardQRContainerType = BackyardQRContainer<BucketNumMiniBuckets, SizeRemainders, FrontyardToBackyardRatio>;
            template<size_t NumMiniBuckets>
            using WrappedBackyardQRContainerType = BackyardQRContainer<NumMiniBuckets, SizeRemainders, FrontyardToBackyardRatio>;
            using BackyardBucketType = Bucket<SizeRemainders + 4, BackyardBucketCapacity, BucketNumMiniBuckets, WrappedBackyardQRContainerType, BackyardBucketSize, FastSQuery>;
            static_assert(sizeof(BackyardBucketType) == BackyardBucketSize);

            static constexpr double NormalizingFactor = (double)FrontyardBucketCapacity / (double) BucketNumMiniBuckets * (double)(1+FrontyardToBackyardRatio)/(FrontyardToBackyardRatio);

            static constexpr uint64_t HashMask = (1ull << SizeRemainders) - 1;
            
            std::uint64_t R;
            std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> backyardToFrontyard; //Comment this out when done with testing I guess?
            // std::vector<size_t> overflows;
            FrontyardQRContainerType getQRPairFromHash(std::uint64_t hash);
            bool insertOverflow(FrontyardQRContainerType overflow);
        
        public:
            bool insertFailure = false;
            std::size_t failureFB = 0;
            std::size_t failureBucket1 = 0;
            std::size_t failureBucket2 = 0;
            std::size_t failureWFB = 0;
            std::size_t backyardLookupCount = 0;

            // std::size_t normalizedCapacity;
            std::size_t capacity;
            std::size_t range;
            PartitionQuotientFilter(std::size_t N, bool Normalize = true);
            bool insert(std::uint64_t hash);
            void insertBatch(const std::vector<size_t>& hashes, std::vector<bool>& status, const uint64_t num_keys); //Really lazy and not super optimized implementation just to show that morton filters can easily be beaten
            std::uint64_t queryWhere(std::uint64_t hash); //also queries where the item is (backyard or frontyard)
            bool query(std::uint64_t hash);
            void queryBatch(const std::vector<size_t>& hashes, std::vector<bool>& status, const uint64_t num_keys);
            std::uint64_t sizeFilter();
            bool remove(std::uint64_t hash);
            void removeBatch(const std::vector<size_t>& hashes, std::vector<bool>& status, const uint64_t num_keys);

            size_t getNumBuckets();
            // double getAverageOverflow();
        
        private:
            std::vector<FrontyardBucketType> frontyard;
            std::vector<BackyardBucketType> backyard;

    };
}

#endif