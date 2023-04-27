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
    // size_t failureBucket1;
    // size_t failureBucket2;
    size_t sizeFilter;
    size_t N_Filter;
    // size_t failureFB;
    // size_t failureWFB;
    // size_t R;
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
            // res.failureBucket1 = filter.failureBucket1;
            // res.failureBucket2 = filter.failureBucket2;
            // res.failureWFB = filter.failureWFB;
            // res.failureFB = filter.failureFB;
            // res.R = filter.getNumBuckets()/8/8 + 1;
            return res;
        }
    }

    return res;
}

template<std::size_t SizeRemainder, std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize>
TestResult testPQF(mt19937& generator, size_t N) {
    using FilterType = DynamicPrefixFilter::PartitionQuotientFilter<SizeRemainder, BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize>;
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
                // f << t.N_Failure << " " << oct << t.failureBucket1 << " " << t.failureBucket2 << dec << '\n';
                f << t.N_Failure << '\n';
                if(t.N_Failure < worstFailure.N_Failure) {
                    worstFailure = t;
                }
            }
        }

        void runAll(size_t numTests, size_t numThreads = 1) { //Delay refers to time we wait after calling the bench function for a filter in order to wait for turbo to "reset"
            if(!filesystem::exists("results")) {
                filesystem::create_directory("results");
            }
            
            string folder = "results/FailureTestAll/Test4NormalizedOldHash/";

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

                // cout << worstFailure.N_Failure << "," << worstFailure.failureFB << "," <<  worstFailure.failureBucket1 << "," << worstFailure.failureBucket2 << "," << worstFailure.failureWFB << "," << worstFailure.R << endl;
                // fout << worstFailure.N_Failure << "," << worstFailure.failureFB << "," <<  worstFailure.failureBucket1 << "," << worstFailure.failureBucket2 << "," << worstFailure.failureWFB << "," << worstFailure.R << endl;
                fout << worstFailure.N_Failure << endl;
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
    // for(size_t logN = 28; logN <= 28; logN++) {
        ft.addTest("PQF_22-8", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 22, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_22-6", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 22, 25, 17, 6, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_22-4", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 22, 25, 17, 4, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_23-8", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 23, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_23-4", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 23, 25, 17, 4, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_25-8", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 25, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_25-4", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 25, 25, 17, 4, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_46-8", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 46, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_46-6", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 46, 51, 35, 6, 64, 64>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_46-4", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 46, 51, 35, 4, 64, 64>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_51-8", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 51, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_51-6", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 51, 51, 35, 6, 64, 64>(generator, N_Filter);}, 1ull << logN);
        ft.addTest("PQF_52-8", [&] (size_t N_Filter) -> TestResult {return testPQF<8, 52, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);
        ft.addTest("PQF16", [&] (size_t N_Filter) -> TestResult {return testPQF<16, 36, 28, 22, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);

        using OriginalCF12 = CuckooWrapper<size_t, 12>;
        ft.addTest("OrigCF12", [&] (size_t N_Filter) -> TestResult {return testInsertsUntilFull<OriginalCF12>(generator, N_Filter);}, 1ull << logN);
        // using PF_BBFF_Wrapper = PFFilterAPIWrapper<Prefix_Filter<SimdBlockFilterFixed<>>, sizePF<SimdBlockFilterFixed<>, sizeBBFF>>; //Can't do these cause they don't have add_attempt
        // ft.addTest("Prefix filter BBF-Flex", [&] (size_t N_Filter) -> TestResult {return testInsertsUntilFull<PF_BBFF_Wrapper, false>(generator, N_Filter);}, 1ull << logN);
        // using PF_TC_Wrapper = PFFilterAPIWrapper<Prefix_Filter<TC_shortcut>, sizePF<TC_shortcut, sizeTC>, false>;
        // ft.addTest("Prefix filter TC", [&] (size_t N_Filter) -> TestResult {return testInsertsUntilFull<PF_TC_Wrapper, false>(generator, N_Filter);}, 1ull << logN);

        ft.addTest("VQF", [&] (size_t N_Filter) -> TestResult {return testInsertsUntilFull<VQFWrapper>(generator, N_Filter);}, 1ull << logN);
        // using CF12_Flex = cuckoofilter::CuckooFilterStable<uint64_t, 12>;
        // using CFF12_Wrapper = PFFilterAPIWrapper<CF12_Flex, sizeCFF, true>;
        // ft.addTest("CF-12-Flex", [&] (size_t N_Filter) -> TestResult {return testInsertsUntilFull<CFF12_Wrapper, true>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("PQF_23_25_17_8_32_32", [&] (size_t N_Filter) -> TestResult {return testPQF<23, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("Matched_VQF_85_46_51_35_8_64_64",[&] (size_t N_Filter) -> TestResult {return testPQF<46, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);
        // ft.addTest("Matched_VQF_90_49_51_35_8_64_64",[&] (size_t N_Filter) -> TestResult {return testPQF<49, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN);
    }
    // for(size_t logN = 27; logN <= 30; logN++) {
    //     ft.addTest("PQF_22_25_17_8_32_32", [&] (size_t N_Filter) -> TestResult {return testPQF<22, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN, 1000);
    //     ft.addTest("Matched_VQF_85_46_51_35_8_64_64", [&] (size_t N_Filter) -> TestResult {return testPQF<46, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN, 1000);
    // }

    // for(size_t logN = 31; logN <= 33; logN++) {
    //     // ft.addTest("PQF_22_25_17_8_32_32", [&] (size_t N_Filter) -> TestResult {return testPQF<22, 25, 17, 8, 32, 32>(generator, N_Filter);}, 1ull << logN, 50);
    //     ft.addTest("Matched_VQF_85_46_51_35_8_64_64", [&] (size_t N_Filter) -> TestResult {return testPQF<46, 51, 35, 8, 64, 64>(generator, N_Filter);}, 1ull << logN, 50);
    // }

    ft.runAll(NumTests, 16);
}