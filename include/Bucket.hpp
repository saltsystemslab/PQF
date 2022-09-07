#ifndef BUCKETS_HPP
#define BUCKETS_HPP

#include <cstddef>
#include <cstdint>
#include "MiniFilter.hpp"

namespace DynamicPrefixFilter {
    //Maybe have a bit set to if the bucket is not overflowed? Cause right now the bucket may send you to the backyard even if there is nothing in the backyard, but the bucket is just full. Not that big a deal, but this slight optimization might be worth a bit?
    //Like maybe have one extra key in the minifilter and then basically account for that or smth? Not sure.
    template<std::size_t NumKeys, std::size_t NumMiniBuckets, template<std::size_t, std::size_t> typename TypeOfRemainderStoreTemplate, template<std::size_t> typename TypeOfQRContainerTemplate, bool optimized=true>
    struct Bucket {
        using TypeOfMiniFilter = MiniFilter<NumKeys, NumMiniBuckets>;
        TypeOfMiniFilter miniFilter;
        using TypeOfRemainderStore = TypeOfRemainderStoreTemplate<NumKeys, TypeOfMiniFilter::Size>;
        TypeOfRemainderStore remainderStore;
        using TypeOfQRContainer = TypeOfQRContainerTemplate<NumMiniBuckets>;
        
        //Returns an overflowed remainder if there was one to be sent to the backyard.
        std::optional<TypeOfQRContainer> insert(TypeOfQRContainer qr) {
            std::optional<uint64_t> miniFilterOverflow;
            std::uint64_t overflowRemainder;
            if constexpr (optimized) {
                std::size_t firstBound = miniFilter.queryMiniBucketBeginning(qr.miniBucketIndex);
                miniFilterOverflow = miniFilter.insert(qr.miniBucketIndex, firstBound);
                overflowRemainder = remainderStore.insertVectorizedUnordered(qr.remainder, firstBound);
            }
            else {
                std::pair<std::size_t, std::size_t> bounds = miniFilter.queryMiniBucketBounds(qr.miniBucketIndex);
                miniFilterOverflow = miniFilter.insert(qr.miniBucketIndex, bounds.first);
                overflowRemainder = remainderStore.insert(qr.remainder, bounds);
            }
            // std::cout << bounds.first << " " << miniFilterOverflow.has_value() << std::endl;
            if (miniFilterOverflow.has_value()) {
                //somewhat janky code. Later make QRContainer do the change, just to encapsulate things properly.
                qr.miniBucketIndex = *miniFilterOverflow;
                qr.remainder = overflowRemainder;
                return {qr};
            }
            return {};
        }

        //First bool is for whether you found the key. If didn't find the key, second bool says whether you need to go to the backyard
        //Obviously for the backyard bucket this doesn't make any sense, since we hope there will never be overflow (although it actually depends somewhat on the filter design!), but just ignore that second bool then
        std::pair<bool, bool> query(TypeOfQRContainer qr) {
            std::pair<size_t, size_t> bounds = miniFilter.queryMiniBucketBounds(qr.miniBucketIndex);
            std::uint64_t inFilter = remainderStore.query(qr.remainder, bounds);
            if(inFilter != 0)
                return std::pair<bool, bool>(true, false);
            else {
                return std::pair<bool, bool>(false, bounds.second == NumKeys && (bounds.first == NumKeys || remainderStore.queryOutOfBounds(qr.remainder)));
            }
        }

        std::size_t countKeys() {
            return miniFilter.countKeys();
        }
    };
}

#endif