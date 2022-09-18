#ifndef BUCKETS_HPP
#define BUCKETS_HPP

#include <cstddef>
#include <cstdint>
#include "MiniFilter.hpp"

namespace DynamicPrefixFilter {
    //Maybe have a bit set to if the bucket is not overflowed? Cause right now the bucket may send you to the backyard even if there is nothing in the backyard, but the bucket is just full. Not that big a deal, but this slight optimization might be worth a bit?
    //Like maybe have one extra key in the minifilter and then basically account for that or smth? Not sure.
    template<std::size_t NumKeys, std::size_t NumMiniBuckets, template<std::size_t, std::size_t> typename TypeOfRemainderStoreTemplate, template<std::size_t> typename TypeOfQRContainerTemplate>
    struct alignas(64) Bucket {
        using TypeOfMiniFilter = MiniFilter<NumKeys, NumMiniBuckets>;
        TypeOfMiniFilter miniFilter;
        using TypeOfRemainderStore = TypeOfRemainderStoreTemplate<NumKeys, TypeOfMiniFilter::Size>;
        TypeOfRemainderStore remainderStore;
        using TypeOfQRContainer = TypeOfQRContainerTemplate<NumMiniBuckets>;
        
        //Returns an overflowed remainder if there was one to be sent to the backyard.
        TypeOfQRContainer insert(TypeOfQRContainer qr) {
            std::size_t loc = miniFilter.queryMiniBucketBeginning(qr.miniBucketIndex);
            // std::size_t loc = 0;
            if(__builtin_expect(loc == NumKeys, 0)) return qr; //Find a way to remove this if statement!!! That would shave off an entire second!
            qr.miniBucketIndex = miniFilter.insert(qr.miniBucketIndex, loc);
            std::uint64_t overflowRemainder = remainderStore.insert(qr.remainder, loc);
            qr.remainder = overflowRemainder;
            return qr;
        }

        //First bool is for whether you found the key. If didn't find the key, second bool says whether you need to go to the backyard
        //Obviously for the backyard bucket this doesn't make any sense, since we hope there will never be overflow (although it actually depends somewhat on the filter design!), but just ignore that second bool then
        // std::pair<bool, bool> query(TypeOfQRContainer qr) {
        //     std::pair<size_t, size_t> bounds = miniFilter.queryMiniBucketBounds(qr.miniBucketIndex);
        //     std::uint64_t inFilter = remainderStore.query(qr.remainder, bounds);
        //     if(inFilter != 0)
        //         return std::pair<bool, bool>(true, false);
        //     else {
        //         // return std::pair<bool, bool>(false, bounds.second == NumKeys && (bounds.first == NumKeys || remainderStore.queryOutOfBounds(qr.remainder)));
        //         return std::pair<bool, bool>(false, bounds.second == NumKeys);
        //     }
        // }
        std::uint64_t query(TypeOfQRContainer qr) {
            std::pair<size_t, size_t> bounds = miniFilter.queryMiniBucketBounds(qr.miniBucketIndex);
            // std::pair<size_t, size_t> bounds = std::make_pair(0, 5);
            std::uint64_t inFilter = remainderStore.query(qr.remainder, bounds);
            if(inFilter != 0)
                return 1;
            else if (bounds.second == NumKeys) {
                return 2;
            }
            else {
                return 0;
            }
            // else {
            //     // return std::pair<bool, bool>(false, bounds.second == NumKeys && (bounds.first == NumKeys || remainderStore.queryOutOfBounds(qr.remainder)));
            //     return (bounds.second == NumKeys) << 1;
            // }
        }

        std::size_t countKeys() {
            return miniFilter.countKeys();
        }
    };
}

#endif