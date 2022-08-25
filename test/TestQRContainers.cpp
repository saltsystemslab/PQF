#include <iostream>
#include <cassert>
#include <random>
#include "QRContainers.hpp"

using namespace std;
using namespace DynamicPrefixFilter;

template<std::size_t NumMiniBuckets, bool HashNum, size_t ConsolidationFactor>
void testQRContainers (mt19937& generator){
    constexpr size_t RemainderBits = 8;
    using FrontyardQRType = FrontyardQRContainer<NumMiniBuckets>;
    using BackyardQRType = BackyardQRContainer<NumMiniBuckets, RemainderBits, ConsolidationFactor>;
    constexpr size_t cfactor = ConsolidationFactor;

    //We are "building up" the quotient from the backyard and stuff
    uniform_int_distribution<size_t> backBucketIndexDist(1000, 10000000000ull);
    size_t backBucketIndex = backBucketIndexDist(generator);
    uniform_int_distribution<size_t> miniBucketIndexDist(0, NumMiniBuckets-1);
    size_t miniBucketIndex = miniBucketIndexDist(generator);
    uniform_int_distribution<size_t> frontyardBucketDist(0, cfactor-1);
    size_t firstFrontyardBucket = frontyardBucketDist(generator);
    size_t secondFrontyardBucket = frontyardBucketDist(generator);
    
    size_t frontBucketIndex = backBucketIndex*cfactor*cfactor + firstFrontyardBucket*cfactor + secondFrontyardBucket;
    size_t quotient = frontBucketIndex*NumMiniBuckets + miniBucketIndex;
    uniform_int_distribution<size_t> remainderDist(0, (1ull << RemainderBits)-1);
    size_t remainder = remainderDist(generator);

    size_t firstBackBucketIndex = backBucketIndex*cfactor + secondFrontyardBucket;
    size_t firstBackBucketRemainder = (firstFrontyardBucket << RemainderBits) + remainder;

    size_t secondBackBucketIndex = backBucketIndex*cfactor + firstFrontyardBucket;
    size_t secondBackBucketRemainder = (secondFrontyardBucket << RemainderBits) + remainder;


    FrontyardQRType fqr(quotient, remainder);
    assert(fqr.bucketIndex == frontBucketIndex);
    assert(fqr.miniBucketIndex == miniBucketIndex);
    // assert(fqr.quotient == quotient); //these here seem overly ridiculous but whatever
    assert(fqr.remainder == remainder);

    BackyardQRType bqr1(quotient, remainder, 0);
    assert(bqr1.bucketIndex == firstBackBucketIndex);
    assert(bqr1.miniBucketIndex == miniBucketIndex);
    // assert(bqr1.quotient == quotient);
    assert(bqr1.realRemainder == remainder);
    assert(bqr1.remainder == firstBackBucketRemainder);

    bqr1 = BackyardQRType(fqr, 0);
    assert(bqr1.bucketIndex == firstBackBucketIndex);
    assert(bqr1.miniBucketIndex == miniBucketIndex);
    // assert(bqr1.quotient == quotient);
    assert(bqr1.realRemainder == remainder);
    assert(bqr1.remainder == firstBackBucketRemainder);

    BackyardQRType bqr2(quotient, remainder, 1);
    assert(bqr2.bucketIndex == secondBackBucketIndex);
    assert(bqr2.miniBucketIndex == miniBucketIndex);
    // assert(bqr2.quotient == quotient);
    assert(bqr2.realRemainder == remainder);
    assert(bqr2.remainder == secondBackBucketRemainder);

    bqr2 = BackyardQRType(fqr, 1);
    assert(bqr2.bucketIndex == secondBackBucketIndex);
    assert(bqr2.miniBucketIndex == miniBucketIndex);
    // assert(bqr2.quotient == quotient);
    assert(bqr2.realRemainder == remainder);
    assert(bqr2.remainder == secondBackBucketRemainder);

}

int main() {
    random_device rd;
    mt19937 generator (rd());

    for(size_t i{0}; i < 1000; i++) {
        testQRContainers<52, 0, 14>(generator);
        testQRContainers<52, 1, 14>(generator);
        testQRContainers<26, 0, 14>(generator);
        testQRContainers<26, 1, 16>(generator);
        testQRContainers<47, 0, 16>(generator);
        testQRContainers<47, 1, 16>(generator);
    }
}