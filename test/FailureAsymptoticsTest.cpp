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

#define TEST_FAILURE

using namespace std;

struct TestResult { //Times are all in microseconds for running not one test but all (really just chosen arbitrarily like that)
    size_t N_Failure = -1ull;
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
TestResult testInsertsUntilFull(mt19937& generator, size_t N_Filter) {
    TestResult res;
    res.N_Filter = N_Filter;

    FT filter(N_Filter);
    res.sizeFilter = filter.sizeFilter();

    size_t N = N_Filter*2; //if doesn't fail within 2x then idk what to say.
    // vector<size_t> keys(N);
    uniform_int_distribution<size_t> keyDist(0, -1ull);
    // for(size_t i{0}; i < N; i++) {
    //     keys[i] = keyDist(generator) % filter.range;
    // }

    for(size_t i{0}; i < N; i++) {
        size_t key = keyDist(generator) % filter.range;
        filter.insert(key);
        if(filter.insertFailure) {
            res.N_Failure = i+1;
            res.failureBucket1 = filter.failureBucket1;
            res.failureBucket2 = filter.failureBucket2;
            res.failureWFB = filter.failureWFB;
            res.failureFB = filter.failureFB;
            res.R = filter.getNumBuckets()/8/8 + 1;
            return res;
        }
    }

    return res;
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize>
TestResult testDPF(mt19937& generator, size_t N) {
    using FilterType = DynamicPrefixFilter::DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize>;
    return testInsertsUntilFull<FilterType, true>(generator, N);
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
            cout << fs << endl;
            ofstream f{fs};
            for(size_t test{0}; test < nt; test++) {
                // cout << test << " " << i << endl;
                TestResult t = benchFunctions[i](Ns_Filter[i]);
                f << t.N_Failure << " " << oct << t.failureBucket1 << " " << t.failureBucket2 << dec << '\n';
                if(t.N_Failure < worstFailure.N_Failure) {
                    worstFailure = t;
                }
            }
        }

        void runAll(size_t numTests, size_t numThreads = 1) { //Delay refers to time we wait after calling the bench function for a filter in order to wait for turbo to "reset"
            if(!filesystem::exists("results")) {
                filesystem::create_directory("results");
            }
            
            string folder = "results/FailureTest5/";

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
                    threads.push_back(thread([=, &worstFailures, this] () -> void {
                        runOne(i, ntPerThread, fs, worstFailures[j]);
                    }));
                }

                for(size_t j{0}; j < numThreads; j++) {
                    threads[j].join();
                    if(worstFailures[j].N_Failure < worstFailure.N_Failure) {
                        worstFailure = worstFailures[j];
                    }
                }

                cout << worstFailure.N_Failure << "," << worstFailure.failureFB << "," <<  worstFailure.failureBucket1 << "," << worstFailure.failureBucket2 << "," << worstFailure.failureWFB << "," << worstFailure.R << endl;
                fout << worstFailure.N_Failure << "," << worstFailure.failureFB << "," <<  worstFailure.failureBucket1 << "," << worstFailure.failureBucket2 << "," << worstFailure.failureWFB << "," << worstFailure.R << endl;
            }
            
        }
};

int main(int argc, char* argv[]) {
    random_device rd;
    mt19937 generator (rd());

    // size_t N_Filter = 1ull << 26;
    // if(argc > 1) {
    //     N_Filter = (1ull << atoi(argv[1]));
    // }
    size_t NumTests = 1000;
    // if(argc > 2){
    //     NumTests = atoi(argv[2]);
    // }
    FilterTester ft;
    
    for(size_t logN = 15; logN <= 26; logN++) {
        ft.addTest("DPF_22_25_17_8_32_32", [&] (size_t N_Filter) -> TestResult {return testDPF<22, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN, 10000);
        // ft.addTest("DPF_23_25_17_8_32_32", [&] (size_t N_Filter) -> TestResult {return testDPF<23, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("Matched_VQF_85_46_51_35_8_64_64",[&] (size_t N_Filter) -> TestResult {return testDPF<46, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("Matched_VQF_90_49_51_35_8_64_64",[&] (size_t N_Filter) -> TestResult {return testDPF<49, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);
    }
    for(size_t logN = 27; logN <= 30; logN++) {
        ft.addTest("DPF_22_25_17_8_32_32", [&] (size_t N_Filter) -> TestResult {return testDPF<22, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN, 1000);
    }

    ft.runAll(NumTests, 16);
}