#ifndef WRAPPERS_HPP
#define WRAPPERS_HPP

#include "vqf_filter.h"
#include "min_pd256.hpp"
#include "tc-sym.hpp"
#include "TC-shortcut.hpp"
#include "wrappers.hpp"
// #include "cuckoofilter.h"
#include "morton_sample_configs.h"
#include <algorithm>

//The methods in this function were copied from main.cc in VQF
class VQFWrapper {
    size_t nslots;
    vqf_filter *filter;
    static constexpr size_t QUQU_SLOTS_PER_BLOCK = 48; //Defined in vqf_filter.cpp so just copied from here. However there its defined based on remainder size, but here we just assume 8

    public:
        size_t range;
        bool insertFailure;

        VQFWrapper(size_t nslots): nslots{nslots} {
            if ((filter = vqf_init(nslots)) == NULL) {
                fprintf(stderr, "Insertion failed");
                exit(EXIT_FAILURE);
            }
            range = filter->metadata.range;
        }

        void insert(std::uint64_t hash) {
            insertFailure = !vqf_insert(filter, hash);
            // if constexpr (DynamicPrefixFilter::DEBUG || DynamicPrefixFilter::PARTIAL_DEBUG) {
            //     if (!vqf_insert(filter, hash)) {
            //         fprintf(stderr, "Insertion failed");
            //         exit(EXIT_FAILURE);
            //     }
            // }
            // else {
            //     vqf_insert(filter, hash);
            // }
        }

        bool query(std::uint64_t hash) {
            return vqf_is_present(filter, hash);
        }

        std::uint64_t sizeFilter() {
            //Copied from vqf_filter.c
            uint64_t total_blocks = (nslots + QUQU_SLOTS_PER_BLOCK)/QUQU_SLOTS_PER_BLOCK;
            uint64_t total_size_in_bytes = sizeof(vqf_block) * total_blocks;
            return total_size_in_bytes;
        }

        bool remove(std::uint64_t hash) {
            return vqf_remove(filter, hash);
        }

        ~VQFWrapper() {
            free(filter);
        }
};

//Taken from the respective files in prefix filter codes

//From TC_shortcut
size_t sizeTC (size_t N) {
    constexpr float load = .935;
    const size_t single_pd_capacity = tc_sym::MAX_CAP;
    return 64 * TC_shortcut::TC_compute_number_of_PD(N, single_pd_capacity, load);
}

//From stable cuckoo filter and singletable.h
template<size_t bits_per_tag = 12>
size_t sizeCFF(size_t N) {
    static const size_t kTagsPerBucket = 4;
    static const size_t kBytesPerBucket = (bits_per_tag * kTagsPerBucket + 7) >> 3;
    static const size_t kPaddingBuckets = ((((kBytesPerBucket + 7) / 8) * 8) - 1) / kBytesPerBucket;
    size_t assoc = 4;
    // bucket count needs to be even
    constexpr double load = .94;
    size_t bucketCount = (10 + N / load / assoc) / 2 * 2;
    return kBytesPerBucket * (bucketCount + kPaddingBuckets); //I think this is right?
}

//BBF-Flex is SimdBlockFilterFixed?? Seems to be by the main-perf code, so I shall stick with it
size_t sizeBBFF(size_t N) {
    unsigned long long int bits = N; //I am very unsure about this but it appears that is how the code is structured??? Size matches up anyways
    size_t bucketCount = std::max(1ull, bits / 24);
    using Bucket = uint32_t[8];
    return bucketCount * sizeof(Bucket);
}

template<typename SpareType, size_t (*SpareSpaceCalculator)(size_t)>
size_t sizePF (size_t N) {
    // constexpr float loads[2] = {.95, .95};
    constexpr float loads[2] = {1.0, 1.0};
    double frontyardSize =  32 * std::ceil(1.0 * N / (min_pd::MAX_CAP0 * loads[0]));
    static double constexpr overflowing_items_ratio = 0.0586;
    size_t backyardSize = SpareSpaceCalculator(get_l2_slots<SpareType>(N, overflowing_items_ratio, loads));
    return backyardSize+frontyardSize;
}

template<typename FilterType, size_t (*SpaceCalculator)(size_t), bool CanRemove = false>
class PFFilterAPIWrapper {
    // using SpareType = TC_shortcut;
    // using PrefixFilterType = Prefix_Filter<SpareType>;

    size_t N;
    FilterType filter;

    public:
        size_t range;
        bool insertFailure;

        PFFilterAPIWrapper(size_t N): N{N}, filter{FilterAPI<FilterType>::ConstructFromAddCount(N)} {
            range = -1ull;
        }

        void insert(std::uint64_t hash) {
            FilterAPI<FilterType>::Add(hash, &filter);
            // insertFailure = !FilterAPI<FilterType>::Add_attempt(hash, &filter);
        }

        bool query(std::uint64_t hash) {
            return FilterAPI<FilterType>::Contain(hash, &filter);
        }

        std::uint64_t sizeFilter() {
            //Copied from wrappers.hpp and TC-Shortcut.hpp in Prefix-Filter
            //Size of frontyard
            return SpaceCalculator(N);
        }

        bool remove(std::uint64_t hash) {
            if(CanRemove) {
                FilterAPI<FilterType>::Remove(hash, &filter);
                return true; //No indication here at all
            }
            else
                return false;
        }
};

template<typename ItemType, size_t bits_per_item>
class CuckooWrapper{
    size_t N;
    using FT = cuckoofilter::CuckooFilter<ItemType, bits_per_item>;
    using ST = cuckoofilter::Status;
    FT filter;

    public:
        size_t range;
        bool insertFailure;

        CuckooWrapper(size_t N): N{N}, filter{FT(N)} {
            range = -1ull;
        }

        void insert(std::uint64_t hash) {
            ST status = filter.Add(hash);
            insertFailure = status == cuckoofilter::NotEnoughSpace;
            // insertFailure = !FilterAPI<FilterType>::Add_attempt(hash, &filter);
        }

        bool query(std::uint64_t hash) {
            ST status = filter.Contain(hash);
            return status == cuckoofilter::Ok;
        }

        std::uint64_t sizeFilter() {
            return filter.SizeInBytes();
        }

        bool remove(std::uint64_t hash) {
            filter.Delete(hash);
            return true;
        }
};

template<typename FT = CompressedCuckoo::Morton3_12>
class MortonWrapper{
    size_t N;
    // using FT = CompressedCuckoo::Morton3_12;
    FT filter;

    public:
        size_t range;
        bool insertFailure;

        MortonWrapper(size_t N): N{N}, filter{FT(N)} {
            range = -1ull;
        }

        void insert(std::uint64_t hash) {
            insertFailure = filter.insert(hash);
        }

        bool query(std::uint64_t hash) {
            return filter.likely_contains(hash);
        }

        std::uint64_t sizeFilter() {
            return filter._total_blocks*sizeof(*(filter._storage)); //Not actually sure this is correct, as they have no getsize function. This seems to be the main storage hog?
        }

        bool remove(std::uint64_t hash) {
            return filter.delete_item(hash);
        }

        void insertBatch(const std::vector<keys_t>& keys, std::vector<bool>& status, const uint64_t num_keys) {
            filter.insert_many(keys, status, num_keys);
        }

        void queryBatch(const std::vector<keys_t>& keys, std::vector<bool>& status, const uint64_t num_keys) {
            filter.likely_contains_many(keys, status, num_keys);
        }

        void removeBatch(const std::vector<keys_t>& keys, std::vector<bool>& status, const uint64_t num_keys) {
            filter.delete_many(keys, status, num_keys);
        }
};

#endif