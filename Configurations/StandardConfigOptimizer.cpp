#include <iostream>
#include <cmath>

using namespace std;

double expectedNormalWithCuttoff(double stddev, double cutoff) { //cuttoff is below
    double xpart = stddev * exp(-0.5*(cutoff/stddev)*(cutoff/stddev)) / sqrt(2*M_PI);
    double cpart = cutoff/2.0 * (1.0 - erf(cutoff/(stddev * sqrt(2))));
    return xpart - cpart;
}

void calcMinCost(double cacheLineSize, double numKeysInCacheline) {
    double backyardLineSize = 512;
    double maxBackyardKeys = 37;
    double maxFrontyardBucketsPerBackyardBucket = 8;

    double maxMiniFilterBuckets = cacheLineSize-9*numKeysInCacheline; //8 bits for remainder, 1 bit for minifilter
    
    double minCostPerKey = 1000000.0;
    double bestBackyardKeys = 0;
    double bestMiniFilterBuckets = 0;
    double bestNumFrontyardToBackyard = 0;

    for(double maxBackyardKeys = 32; maxBackyardKeys <= 37; maxBackyardKeys++) {
        maxMiniFilterBuckets = min(maxMiniFilterBuckets, (double)512-maxBackyardKeys*9-(maxBackyardKeys+((size_t)maxBackyardKeys%2))*4);
        for(double miniFilterBuckets = numKeysInCacheline-5; miniFilterBuckets <= maxMiniFilterBuckets; miniFilterBuckets++) {
            double stddev = sqrt(miniFilterBuckets);
            double diff = numKeysInCacheline - miniFilterBuckets;
            double expectedOverflow = expectedNormalWithCuttoff(stddev, diff);
            double expectedUnderfill = expectedNormalWithCuttoff(stddev, -diff);
            double expectedKeysInFrontyardBucket = numKeysInCacheline-expectedUnderfill;
            double costPerFrontyardKey = cacheLineSize/(expectedKeysInFrontyardBucket);
            double numFrontyardBucketsToOneBackyardBucket = min(floor((maxBackyardKeys-8)/expectedOverflow), maxFrontyardBucketsPerBackyardBucket);
            double costPerBackyardKey = backyardLineSize / (expectedOverflow*numFrontyardBucketsToOneBackyardBucket);
            double costPerKey = (costPerFrontyardKey*expectedKeysInFrontyardBucket+costPerBackyardKey*expectedOverflow)/miniFilterBuckets;

            if(costPerKey <= minCostPerKey) {
                minCostPerKey = costPerKey;
                bestMiniFilterBuckets = miniFilterBuckets;
                bestBackyardKeys = maxBackyardKeys;
                bestNumFrontyardToBackyard = numFrontyardBucketsToOneBackyardBucket;
                // cout << costPerKey << " " << miniFilterBuckets << " " << maxBackyardKeys << " " << expectedOverflow << " " << numFrontyardBucketsToOneBackyardBucket << endl;
            }
        }
    }

    cout << minCostPerKey << " " << bestMiniFilterBuckets << " " << bestBackyardKeys << " " << bestNumFrontyardToBackyard << endl;
}

int main() {
    //Start by optimizing just half cache line

    // cout << expectedNormalWithCuttoff(sqrt(27), -2);

    // double cacheLineSize = 256;
    // double numKeysInCacheline = 25;
    cout << "Optimizing 32 byte frontyard buckets with only 24 keys: ";
    calcMinCost(256, 24);

    cout << "Optimizing 32 byte frontyard buckets: ";
    calcMinCost(256, 25);

    cout << "Optimizing 64 byte frontyard buckets: ";
    calcMinCost(512, 51);
    
}