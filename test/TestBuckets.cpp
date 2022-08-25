#include <iostream>
#include <cassert>
#include <array>
#include <random>
#include <set>
#include <optional>
#include "RemainderStore.hpp"
#include "QRContainers.hpp"
#include "Bucket.hpp"

using namespace std;
using namespace DynamicPrefixFilter;

template<std::size_t NumKeys, std::size_t NumMiniBuckets, size_t SizeOfRemainderInBits, template<std::size_t, std::size_t> typename TypeOfRemainderStoreTemplate, template<std::size_t> typename TypeOfQRContainerTemplate>
void testRealBucket(mt19937& generator) {
    using QRContainerType = TypeOfQRContainerTemplate<NumMiniBuckets>;
    using BucketType = Bucket<NumKeys, NumMiniBuckets, TypeOfRemainderStoreTemplate, TypeOfQRContainerTemplate>;
    constexpr size_t RemainderRange = 1ull << SizeOfRemainderInBits;

    multiset<pair<uint64_t, uint64_t>> miniBucketRemainderPairs;
    BucketType bucket;
    for(size_t i{0}; i < NumKeys*2; i++) {
        // cout << i << endl;
        uniform_int_distribution<size_t> miniBucketIndexDist(0, NumMiniBuckets-1);
        size_t miniBucketIndex = miniBucketIndexDist(generator);
        uniform_int_distribution<uint64_t> remainderDist(0, RemainderRange-1);
        uint64_t remainder = remainderDist(generator);
        miniBucketRemainderPairs.insert(make_pair(miniBucketIndex, remainder));

        QRContainerType qr(miniBucketIndex, remainder); //we don't care about the actual qr rn, we just care about its minibucket and remainder, and we obviously independently verified it works so this is ok
        // qr.miniBucketIndex = miniBucketIndex;
        // qr.remainder = remainder;

        //testing insert
        std::optional<QRContainerType> overflowQR = bucket.insert(qr);
        assert(overflowQR.has_value() == (i >= NumKeys));
        if (i >= NumKeys) {
            auto it = prev(miniBucketRemainderPairs.end());
            assert(overflowQR->miniBucketIndex == it->first && overflowQR->remainder == it->second);
            miniBucketRemainderPairs.erase(it);
        }

        //testing "positive" queries
        for(auto it = miniBucketRemainderPairs.begin(); it != miniBucketRemainderPairs.end(); it++) {
            QRContainerType qr(it->first, it->second);
            assert(bucket.query(qr).first);
        }

        //testing "negative" queries
        for(size_t j=0; j < NumKeys*2; j++) {
            uniform_int_distribution<size_t> miniBucketIndexDist(0, NumMiniBuckets-1);
            size_t miniBucketIndex = miniBucketIndexDist(generator);
            uniform_int_distribution<uint64_t> remainderDist(0, RemainderRange-1);
            uint64_t remainder = remainderDist(generator);
            pair<uint64_t, uint64_t> miniBucketRemainderPair = make_pair(miniBucketIndex, remainder);
            if(miniBucketRemainderPairs.count(miniBucketRemainderPair) > 0) continue;
            QRContainerType qr(miniBucketIndex, remainder);
            pair<bool, bool> queryResult = bucket.query(qr);
            assert(queryResult.first == false);
            //Yeah rare test for the case where needs to go to backyard so should make this its own test but for now whatever. Given how many times its run should still check it decently
            // cout << i << " " << queryResult.second << " " << (miniBucketRemainderPairs.lower_bound(miniBucketRemainderPair) == miniBucketRemainderPairs.end()) << endl;
            if(i < NumKeys-1) assert(queryResult.second == false);
            else assert(queryResult.second == (miniBucketRemainderPairs.lower_bound(miniBucketRemainderPair) == miniBucketRemainderPairs.end()));
        }
    }
}

template<size_t NumMiniBuckets>
using BackyardQRContainer1 = BackyardQRContainer<NumMiniBuckets, 0>;

int main() {
    random_device rd;
    mt19937 generator (rd());

    //Honestly this is running a little slow even for unoptimized hm.
    for(size_t i{0}; i < 1000; i++) {
        cout << i << endl;
        //Test possible frontyard buckets
        testRealBucket<51, 52, 8, RemainderStore8Bit, FrontyardQRContainer>(generator);
        testRealBucket<25, 26, 8, RemainderStore8Bit, FrontyardQRContainer>(generator);
        testRealBucket<75, 61, 8, RemainderStore8Bit, FrontyardQRContainer>(generator);

        //Test possible backyard buckets
        testRealBucket<35, 52, 12, RemainderStore12Bit, BackyardQRContainer1>(generator);
        testRealBucket<37, 26, 12, RemainderStore12Bit, BackyardQRContainer1>(generator);
    }
}