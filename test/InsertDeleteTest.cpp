#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <string>
#include <iostream>
#include <functional>
#include <fstream>
#include <limits>
#include <filesystem>
#include <stdlib.h>
#include <algorithm>
#include <atomic>
#include <thread>

#include "DynamicPrefixFilter.hpp"
#include "TestWrappers.hpp"

#define TEST_FAILURE

using namespace std;

struct TestResult { //Times are all in microseconds for running not one test but all (really just chosen arbitrarily like that)
    size_t N_Failure = -1ull;
    double ratio;
    size_t giveUpRatio;
    size_t failureBucket1;
    size_t failureBucket2;
    size_t sizeFilter;
    size_t N_Filter;
    size_t failureFB;
    size_t failureWFB;
    size_t R;
};

//max ratio is the ratio of how much space you make for filter items to how many items you actually insert
template<typename FT, bool CanDelete = true>
TestResult testInsertDeleteBuffer(size_t N_Filter, double ratio, size_t giveUpRatio) {
    // cout << ratio << endl;
    TestResult res;
    res.N_Filter = N_Filter;
    res.ratio = ratio;
    res.giveUpRatio = giveUpRatio;

    FT filter(N_Filter);
    res.sizeFilter = filter.sizeFilter();

    size_t N = N_Filter*giveUpRatio; //We restrict to failure within 100x of the size of the filter, as any more would be ridiculous.
    size_t N_Ratio = (size_t) (N_Filter * ratio);
    // vector<size_t> keys(N);
    random_device rd;
    uint32_t seed = rd();
    mt19937 generator (seed); //do it here cause we want two generators
    mt19937 generator2 (seed);
    uniform_int_distribution<size_t> keyDist(0, -1ull);
    // for(size_t i{0}; i < N; i++) {
    //     keys[i] = keyDist(generator) % filter.range;
    // }

    for(size_t i{0}; i < N; i++) {
        size_t key = keyDist(generator) % filter.range;
        filter.insert(key);
        if(filter.insertFailure) {
            res.N_Failure = i+1;
            // res.failureBucket1 = filter.failureBucket1;
            // res.failureBucket2 = filter.failureBucket2;
            // res.failureWFB = filter.failureWFB;
            // res.failureFB = filter.failureFB;
            // res.R = filter.getNumBuckets()/8/8 + 1;
            return res;
        }
        if(i >= N_Ratio) {
            size_t old_key = keyDist(generator2) % filter.range; //Should be the same as generator but since started later it is what generator outputted that while ago
            filter.remove(key);
        }
    }

    return res;
}

template<typename FT, bool CanDelete = true>
TestResult testInsertDeleteRandomDeletionsBatch(size_t N_Filter, double ratio, size_t giveUpRatio) {
    // cout << ratio << endl;
    TestResult res;
    res.N_Filter = N_Filter;
    res.ratio = ratio;
    res.giveUpRatio = giveUpRatio;

    FT filter(N_Filter);
    res.sizeFilter = filter.sizeFilter();

    // size_t N = N_Filter*giveUpRatio; //We restrict to failure within 100x of the size of the filter, as any more would be ridiculous.
    size_t N_Ratio = (size_t) (N_Filter * ratio);
    random_device rd;
    uint32_t seed = rd();
    mt19937 generator (seed); //do it here cause we want two generators
    uniform_int_distribution<size_t> keyDist(0, -1ull);

    vector<size_t> batch(N_Ratio);

    for(size_t j{0}; j < N_Ratio; j++) {
        size_t key = keyDist(generator) % filter.range;
        batch[j] = (key);
        filter.insert(key);
        if(filter.insertFailure) {
            res.N_Failure = j+1;
            // res.failureBucket1 = filter.failureBucket1;
            // res.failureBucket2 = filter.failureBucket2;
            // res.failureWFB = filter.failureWFB;
            // res.failureFB = filter.failureFB;
            // res.R = filter.getNumBuckets()/8/8 + 1;
            return res;
        }
    }
    
    for(size_t i{1}; i < giveUpRatio; i++) {
        shuffle(batch.begin(), batch.end(), generator);
        for(size_t j{0}; j < N_Ratio; j++) {
            filter.remove(batch[j]);
            size_t key = keyDist(generator) % filter.range;
            batch[j] = key;
            filter.insert(key);
            if(filter.insertFailure) {
                res.N_Failure = i*N_Ratio+j+1;
                // res.failureBucket1 = filter.failureBucket1;
                // res.failureBucket2 = filter.failureBucket2;
                // res.failureWFB = filter.failureWFB;
                // res.failureFB = filter.failureFB;
                // res.R = filter.getNumBuckets()/8/8 + 1;
                return res;
            }
        }
    }

    return res;
}

template<std::size_t SizeRemainders, std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize>
TestResult testPQF(size_t N, double r, size_t g) {
    using FilterType = DynamicPrefixFilter::PartitionQuotientFilter<SizeRemainders, BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize>;
    return testInsertDeleteRandomDeletionsBatch<FilterType, true>(N, r, g);
}

class FilterTester {
    vector<string> filterNames;
    vector<size_t> numsTests;
    vector<function<TestResult(size_t)>> benchFunctions;
    vector<size_t> Ns_Filter;

    public: 
        FilterTester(){}

        void addTest(string filterName, function<TestResult(size_t)> benchFunction, size_t N_Filter, size_t numTests = -1ull) { 
            filterNames.push_back(filterName);
            benchFunctions.push_back(benchFunction);
            Ns_Filter.push_back(N_Filter);
            numsTests.push_back(numTests);
        }

        void runOne(size_t i, size_t nt, string fs, TestResult& worstFailure) {
            // cout << fs << endl;
            ofstream f{fs, std::ios_base::app};
            for(size_t test{0}; test < nt; test++) {
                // cout << test << " " << i << endl;
                TestResult t = benchFunctions[i](Ns_Filter[i]);
                f << t.N_Failure << " " << t.ratio << " " << t.giveUpRatio << " " << oct << t.failureBucket1 << " " << t.failureBucket2 << dec << '\n';
                if(t.N_Failure <= worstFailure.N_Failure) {
                    worstFailure = t;
                }
            }
        }

        void runAll(size_t numTests, size_t numThreads = 1) { //Delay refers to time we wait after calling the bench function for a filter in order to wait for turbo to "reset"
            if(!filesystem::exists("results")) {
                filesystem::create_directory("results");
            }
            
            string folder = "results/InsertDeleteTest/";

            if(!filesystem::exists(folder)) { //Todo: look up how to do this recursively easily (or do it yourself)
                filesystem::create_directory(folder);
            }

            for(size_t i{0}; i < filterNames.size(); i++) {
                if(!filesystem::exists(folder+filterNames[i])) {
                    filesystem::create_directory(folder+filterNames[i]);
                }
            }

            ofstream fout(folder+"WorstFailures.txt");

            for(size_t i{0}; i < benchFunctions.size(); i++) {
                // ofstream f(folder+filterNames[i] + "/"+to_string(Ns_Filter[i]) + ".csv", ios_base::app);
                TestResult worstFailure;
                size_t nt = numsTests[i];
                if(nt == -1ull) {
                    nt = numTests;
                }

                size_t ntPerThread = nt/numThreads;

                vector<thread> threads;
                vector<TestResult> worstFailures(numThreads);
                string thrfolder = folder+filterNames[i] + "/"+to_string(Ns_Filter[i]) + "/";
                if(!filesystem::exists(thrfolder)) {
                    filesystem::create_directory(thrfolder);
                }

                fout << "Benching " << filterNames[i] << " with N=" << Ns_Filter[i] << endl;
                cout << "Benching " << filterNames[i] << " with N=" << Ns_Filter[i] << endl;
                for(size_t j{0}; j < numThreads; j++) {
                    string fs = thrfolder +  "t" + to_string(j) + ".csv";
                    threads.push_back(thread([=, &worstFailures] () -> void {
                        runOne(i, ntPerThread, fs, worstFailures[j]);
                    }));
                }

                for(size_t j{0}; j < numThreads; j++) {
                    threads[j].join();
                    if(worstFailures[j].N_Failure <= worstFailure.N_Failure) {
                        worstFailure = worstFailures[j];
                    }
                }

                if(worstFailure.N_Failure == -1ull) {
                    cout << "PASS with ratio " << worstFailure.ratio << endl;
                    fout << "PASS with ratio " << worstFailure.ratio << endl;
                }
                else {
                    cout << "FAIL with ratio " << worstFailure.ratio << endl;
                    fout << "FAIL with ratio " << worstFailure.ratio << endl;
                }

                // cout << worstFailure.N_Failure << "," << worstFailure.ratio << "," << worstFailure.failureFB << "," <<  worstFailure.failureBucket1 << "," << worstFailure.failureBucket2 << "," << worstFailure.failureWFB << "," << worstFailure.R << endl;
                // fout << worstFailure.N_Failure << "," << worstFailure.ratio << "," << worstFailure.failureFB << "," <<  worstFailure.failureBucket1 << "," << worstFailure.failureBucket2 << "," << worstFailure.failureWFB << "," << worstFailure.R << endl;
            }
            
        }
};

int main(int argc, char* argv[]) {
    // random_device rd;
    // mt19937 generator (rd());

    // size_t N_Filter = 1ull << 26;
    // if(argc > 1) {
    //     N_Filter = (1ull << atoi(argv[1]));
    // }
    size_t NumTests = 8;
    // if(argc > 2){
    //     NumTests = atoi(argv[2]);
    // }
    FilterTester ft;

    
    
    for(size_t logN = 18; logN <= 26; logN++) {
        {
            vector<double> ratios{0.87, 0.88, 0.89, 0.90, 0.91, 0.92, 0.93, 0.94};
            for(double r: ratios){
                using OriginalCF12 = CuckooWrapper<size_t, 12>;
                ft.addTest("Cuckoo", [=] (size_t N_Filter) -> TestResult {return testInsertDeleteRandomDeletionsBatch<OriginalCF12, true>(N_Filter, r, 50);}, 1ull << logN, 8);
            }
        }
        {
            vector<double> ratios{0.86, 0.87, 0.88, 0.89, 0.90, 0.91, 0.92, 0.93};
            for(double r: ratios){
                ft.addTest("VQF", [=] (size_t N_Filter) -> TestResult {return testInsertDeleteRandomDeletionsBatch<VQFWrapper, true>(N_Filter, r, 50);}, 1ull << logN, 8);
            }
        }
        {
            vector<double> ratios{0.8, 0.81, 0.82, 0.83, 0.84, 0.85, 0.86};
            for(double r: ratios)
                ft.addTest("PQF_22-8", [=] (size_t N_Filter) -> TestResult {return testPQF<8, 22, 26, 18, 8, 32, 32>(N_Filter, r, 50);}, 1ull << logN, 8);
        }
        {
            vector<double> ratios{0.86, 0.87, 0.88, 0.89, 0.90, 0.91, 0.92};
            for(double r: ratios)
                ft.addTest("PQF_53-8", [=] (size_t N_Filter) -> TestResult {return testPQF<8, 53, 51, 35, 8, 64, 64>(N_Filter, r, 50);}, 1ull << logN, 8);
        }
    }

    for(size_t logN = 18; logN <= 20; logN++) {
        {
            vector<double> ratios{0.8, 0.81, 0.82, 0.83, 0.84, 0.85, 0.86};
            for(double r: ratios)
                ft.addTest("PQF_22-8", [=] (size_t N_Filter) -> TestResult {return testPQF<8, 22, 26, 18, 8, 32, 32>(N_Filter, r, 100000);}, 1ull << logN, 8);
        }
        {
            vector<double> ratios{0.86, 0.87, 0.88, 0.89, 0.90, 0.91, 0.92};
            for(double r: ratios)
                ft.addTest("PQF_53-8", [=] (size_t N_Filter) -> TestResult {return testPQF<8, 53, 51, 35, 8, 64, 64>(N_Filter, r, 100000);}, 1ull << logN, 8);
        }
    }

    ft.runAll(NumTests, 8);
}