#ifndef QRTRACKER_HPP
#define QRTRACKER_HPP

#include <cstddef>
#include <cstdint>

namespace DynamicPrefixFilter {
    //Just a data structure to store bucket index, mini bucket index, and remainder trios
    //And also then allow you to get the same for the backyard/attic
    //Just use the global settings in Configuration.hpp for NumMiniBuckets?
    template<std::size_t NumMiniBuckets>
    struct FrontyardQRContainer {
        // std::size_t quotient;
        std::size_t bucketIndex;
        std::size_t miniBucketIndex;
        std::uint64_t remainder;

        //Here hash is assumed to already be in the range supported
        FrontyardQRContainer(std::size_t quotient, std::uint64_t remainder): /*quotient{quotient},*/ bucketIndex{quotient/NumMiniBuckets}, miniBucketIndex{quotient%NumMiniBuckets}, remainder{remainder} {
        }
    };

    //TODO: Figure out some better naming here. HashNum refers to whether we are doing the first or second (0 or 1) "hash function" (so first or second bucket)
    //Consolidation bits is how many bits we use to refer to which bucket an item came from, one bit of which is used to specify the hashnum.
    template<std::size_t NumMiniBuckets, std::size_t RemainderBits, std::size_t ConsolidationFactor>
    struct BackyardQRContainer {
        // static constexpr std::size_t ConsolidationFactor = 1ull << ConsolidationBits;
        // std::size_t quotient; // Do we really need to store this?
        std::size_t realRemainder; //We imbue the remainder with the hash to make a bigger remainder, so this is just to store what was originally the remainder before the addition. Not used but just to be there
        std::size_t bucketIndex;
        std::size_t miniBucketIndex;
        std::uint64_t remainder;

        void finishInit(std::uint64_t frontyardBucketIndex, bool hashNum) {
            if (hashNum) {
                remainder += (frontyardBucketIndex % ConsolidationFactor) << RemainderBits; //Division and remainder *should* be optimized to bit shift, but maybe just do that manually in the future to be sure
                bucketIndex = frontyardBucketIndex / ConsolidationFactor;
            }
            else {
                std::uint64_t fbiMinusLowBits = frontyardBucketIndex / ConsolidationFactor;
                std::uint64_t lowBits = frontyardBucketIndex % ConsolidationFactor;
                bucketIndex = (fbiMinusLowBits/ConsolidationFactor) * ConsolidationFactor + lowBits;
                remainder += (fbiMinusLowBits % ConsolidationFactor) << RemainderBits;
            }
        }

        BackyardQRContainer(std::size_t quotient, std::uint64_t remainder, bool hashNum): /*quotient{quotient},*/ realRemainder{remainder}, miniBucketIndex{quotient%NumMiniBuckets}, remainder(remainder) {
            std::uint64_t frontyardBucketIndex = quotient/NumMiniBuckets;
            finishInit(frontyardBucketIndex, hashNum);
        }

        BackyardQRContainer(FrontyardQRContainer<NumMiniBuckets> frontQR, bool hashNum): /*quotient{frontQR.quotient},*/ realRemainder{frontQR.remainder}, miniBucketIndex{frontQR.miniBucketIndex}, remainder{frontQR.remainder} {
            finishInit(frontQR.bucketIndex, hashNum);
        }
    };

}

#endif