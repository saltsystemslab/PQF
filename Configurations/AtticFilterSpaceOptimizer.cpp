#include <iostream>
#include <cmath>

using namespace std;

double expectedNormalWithCuttoff(double stddev, double cutoff) { //cuttoff is below
    double xpart = stddev * exp(-0.5*(cutoff/stddev)*(cutoff/stddev)) / sqrt(2*M_PI);
    double cpart = cutoff/2.0 * (1.0 - erf(cutoff/(stddev * sqrt(2))));
    return xpart - cpart;
}

void calcMinCost(double cacheLineSize) {
    double maxNumKeysInCacheline = floor(cacheLineSize/10);
    double backyardLineSize = 512;
    double maxBackyardKeys = 64;
    double maxFrontyardBucketsPerBackyardBucket = 8;
    double minBackyardOverhead = 8;

    double minCostPerKey = 1000000.0;
    double bestRealKeysPerBucket = 0;
    double bestVirtualKeysPerBucket = 0;
    double bestMiniBucketCount = 0;

    // double minCostPerBackyardKey = 1000;

    for(double numRealKeysInCacheline = maxNumKeysInCacheline-6; numRealKeysInCacheline<=maxNumKeysInCacheline; numRealKeysInCacheline++) {
        double maxMiniFilterBuckets = cacheLineSize-9*numRealKeysInCacheline; //8 bits for remainder, 1 bit for minifilter real keys
        // cout << numRealKeysInCacheline << " " << maxMiniFilterBuckets << endl;
        for(double miniFilterBuckets = numRealKeysInCacheline-5; miniFilterBuckets <= maxMiniFilterBuckets; miniFilterBuckets++) {
            double maxVirtualKeysPerBucket = maxMiniFilterBuckets - miniFilterBuckets + numRealKeysInCacheline;
            // cout << maxVirtualKeysPerBucket << " " << numRealKeysInCacheline << endl;
            for(double virtualKeysPerBucket = numRealKeysInCacheline+1; virtualKeysPerBucket <= maxVirtualKeysPerBucket; virtualKeysPerBucket++) {
                double stddev = sqrt(miniFilterBuckets);
                double diff = numRealKeysInCacheline - miniFilterBuckets;
                double virtualDiff = virtualKeysPerBucket - miniFilterBuckets;

                double expectedUnderfill = expectedNormalWithCuttoff(stddev, -diff);
                double expectedKeysInFrontyardBucket = numRealKeysInCacheline-expectedUnderfill;
                double costPerFrontyardKey = cacheLineSize/(expectedKeysInFrontyardBucket);
                
                double expectedOverflow = expectedNormalWithCuttoff(stddev, diff);
                double expectedVirtualOverflow = expectedNormalWithCuttoff(stddev, virtualDiff);
                double numFrontyardBucketsToOneBackyardBucket = min(floor((maxBackyardKeys-minBackyardOverhead)/(expectedOverflow-expectedVirtualOverflow)), maxFrontyardBucketsPerBackyardBucket);
                double costPerBackyardKey = backyardLineSize / ((expectedOverflow-expectedVirtualOverflow)*numFrontyardBucketsToOneBackyardBucket);

                constexpr double costPerAtticKey = 64; //Being rather loose here. Can obviously be smaller

                double costPerKey = (costPerFrontyardKey*expectedKeysInFrontyardBucket+costPerBackyardKey*(expectedOverflow-expectedVirtualOverflow) + costPerAtticKey * expectedVirtualOverflow * 1.1)/miniFilterBuckets;

                // if(minCostPerBackyardKey > costPerBackyardKey) {
                //     minCostPerBackyardKey = costPerBackyardKey;
                // }

                if(costPerKey < minCostPerKey && costPerKey > 0) {
                    minCostPerKey = costPerKey;
                    bestRealKeysPerBucket = numRealKeysInCacheline;
                    bestVirtualKeysPerBucket = virtualKeysPerBucket;
                    bestMiniBucketCount = miniFilterBuckets;
                    // cout << costPerKey << " " << numRealKeysInCacheline << " " << virtualKeysPerBucket << " " << miniFilterBuckets << " " << costPerFrontyardKey << " " << costPerBackyardKey << " " << numFrontyardBucketsToOneBackyardBucket << endl;
                    // cout << minCostPerBackyardKey << " " << numFrontyardBucketsToOneBackyardBucket << " " << expectedOverflow << " " << expectedVirtualOverflow << endl;
                }
            }
        }
    }

    cout << minCostPerKey << " " << bestRealKeysPerBucket << " " << bestVirtualKeysPerBucket << " " << bestMiniBucketCount << endl;
    // cout << minCostPerBackyardKey << endl;
}

int main() {
    //Start by optimizing just half cache line

    // cout << expectedNormalWithCuttoff(sqrt(27), -2);

    // double cacheLineSize = 256;
    // double numKeysInCacheline = 25;

    cout << "Optimizing 32 byte frontyard buckets: ";
    calcMinCost(256);

    cout << "Optimizing 64 byte frontyard buckets: ";
    calcMinCost(512);
    
}