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

#include "DynamicPrefixFilter.hpp"
#include "TestWrappers.hpp"

using namespace std;

struct TestResult { //Times are all in microseconds for running not one test but all (really just chosen arbitrarily like that)
    size_t insertTime;
    size_t successfulQueryTime;
    size_t randomQueryTime;
    double falsePositiveRate;
    size_t removeTime;
    double lf;
    size_t sizeFilter;
    size_t N;
    size_t NFilter;
    double sBLR; //successful backyard lookup rate
    double rBLR; //random backyard lookup rate
};

//max ratio is the ratio of how much space you make for filter items to how many items you actually insert
template<typename FT, bool CanDelete = true, bool getBLR = false, bool testBatch=false>
TestResult benchFilter(mt19937& generator, size_t N, double ratio) {
    TestResult res;
    res.N = static_cast<size_t>(N*ratio);
    res.NFilter = N;
    res.lf = ratio;

    FT filter(res.NFilter);
    res.sizeFilter = filter.sizeFilter();

    N = res.N;

    vector<size_t> keys(N);
    vector<size_t> FPRkeys(N);
    constexpr size_t batchSize = 128;
    vector<size_t> batch(batchSize);
    vector<bool> status(batchSize);
    uniform_int_distribution<size_t> keyDist(0, -1ull);
    for(size_t i{0}; i < N; i++) {
        keys[i] = keyDist(generator) % filter.range;
        FPRkeys[i] = keyDist(generator) % filter.range;
    }
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests)); //Just sleep to try to keep turbo boost to full


    auto start = chrono::high_resolution_clock::now();
    if constexpr (testBatch) {
        for(size_t i{0}; i < N/batchSize; i++) {
            copy(keys.begin() + (i*batchSize), keys.begin() + ((i+1)*batchSize), batch.begin());
            filter.insertBatch(batch, status, batchSize);
        }
    }
    else {
        for(size_t i{0}; i < N; i++) {
            filter.insert(keys[i]);
        }
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end-start);
    // cout << "inserted" << endl;
    res.insertTime = (size_t)duration.count();
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    start = chrono::high_resolution_clock::now(); 
    // uint64_t x = 0;
    if constexpr (testBatch) {
        for(size_t i{0}; i < N/batchSize; i++) {
            copy(keys.begin() + (i*batchSize), keys.begin() + ((i+1)*batchSize), batch.begin());
            filter.queryBatch(batch, status, batchSize);
            for(bool s: status) {
                if(!s){
                    cout << ratio << endl;
                    res.successfulQueryTime = -1ull;
                    return res;
                }
            }
        }
    }
    else{
        for(size_t i{0}; i < N; i++) {
            if(!filter.query(keys[i])) {
                // cerr << "Query on " << keys[i] << " failed." << endl;
                // exit(EXIT_FAILURE);
                cout << ratio << endl;
                res.successfulQueryTime = -1ull;
                return res;
            }
            // x += filter.query(keys[i]);
        }
    }
    // cout << x << endl;
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    // cout << "queried" << endl;
    res.successfulQueryTime = (size_t)duration.count();
    if constexpr (getBLR) {
        res.sBLR = ((double)filter.backyardLookupCount) / N;
        filter.backyardLookupCount = 0;
    }
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    start = chrono::high_resolution_clock::now();
    uint64_t fpr = 0;
    if constexpr (testBatch) {
        for(size_t i{0}; i < N/batchSize; i++) {
            copy(FPRkeys.begin() + (i*batchSize), FPRkeys.begin() + ((i+1)*batchSize), batch.begin());
            filter.queryBatch(batch, status, batchSize);
            for(bool s: status) {
                fpr += s;
            }
        }
    }
    else {
        for(size_t i{0}; i < N; i++) {
            fpr += filter.query(FPRkeys[i]);
        }
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    // cout << "fpred" << endl;
    res.randomQueryTime = (size_t)duration.count();
    res.falsePositiveRate = ((double)fpr) / N;
    if constexpr (getBLR) {
        res.rBLR = ((double)filter.backyardLookupCount) / N;
        filter.backyardLookupCount = 0;
    }
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    if constexpr (CanDelete) {
        start = chrono::high_resolution_clock::now();
        if constexpr (testBatch) {
            for(size_t i{0}; i < N/batchSize; i++) {
                copy(keys.begin() + (i*batchSize), keys.begin() + ((i+1)*batchSize), batch.begin());
                filter.removeBatch(batch, status, batchSize);
            }
        }
        else {
            for(size_t i{0}; i < N; i++) {
                // assert(filter.remove(keys[i]));
                filter.remove(keys[i]);
            }
        }
        end = chrono::high_resolution_clock::now();
        duration = chrono::duration_cast<chrono::microseconds>(end-start);
        res.removeTime = (size_t)duration.count();
        // cout << "removed" << endl;
    }
    else {
        // res.removeTime = numeric_limits<double>::infinity();
        res.removeTime = -1ull;
    }

    return res;
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery = false, bool testBatch = false>
TestResult benchPQF(mt19937& generator, size_t N, double ratio = 1.0) {
    using FilterType = DynamicPrefixFilter::PartitionQuotientFilter<8, BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>;
    return benchFilter<FilterType, true, true, testBatch>(generator, N, ratio);
}

class FilterTester {
    vector<string> filterNames;
    vector<function<TestResult()>> benchFunctions;
    vector<size_t> Ns;

    vector<vector<TestResult>> testResults;
    string individualFilterHeader = "N, \"Size of Filter\", \"Insert Time\", \"Positive Query Time\", \"Random Query Time\", \"False Positive Rate\", \"Remove Time\""; //Idk why doing it this way but here we report how long total to insert, and in summary we say how much per item

    string summaryHeader = "\"Filter Name\", N, \"Insert(ns)\", \"Positive Query(ns)\", \"Random Query(ns)\", \"Inverse False Positive Rate\", \"Remove Time\", \"Bits Per Key\"";

    public: 
        FilterTester(){}

        void addTest(string filterName, function<TestResult()> benchFunction, size_t N) {
            filterNames.push_back(filterName);
            benchFunctions.push_back(benchFunction);
            testResults.push_back(vector<TestResult>());
            Ns.push_back(N);
        }

        void addLoadFactors(string filterName, function<TestResult(double)> f, size_t N, double minLF, double maxLF, double step) {
            for(double lf = minLF; lf <= maxLF+1e-9; lf+=step) {
                // filterNames.push_back(filterName);
                // benchFunctions.push_back([=]() -> TestResult {return f(lf);});
                // testResults.push_back(vector<TestResult>());
                addTest(filterName, [=]() -> TestResult {return f(lf);}, N);
            }
        }

        void runAll(size_t numTests, mt19937 generator, string ofolder) {
            if(!filesystem::exists("results")) {
                filesystem::create_directory("results");
            }

            string folder = "results/" + ofolder;

            if(!filesystem::exists(folder)) {
                filesystem::create_directory(folder);
            }

            // vector<ofstream> fileOutputs;
            // for(string filterName: filterNames) {
            for(size_t i=0; i < filterNames.size(); i++){
                string filterName = filterNames[i];
                string bfolder = folder + "/" + filterName+"/";
                if(!filesystem::exists(bfolder)) {
                    filesystem::create_directory(bfolder);
                }
                // fileOutputs.push_back(ofstream(bfolder+to_string(Ns[i])));
                // fileOutputs[fileOutputs.size()-1] << individualFilterHeader << endl;
                ofstream fout(bfolder+to_string(Ns[i]));//lazy way to empty the file
            }

            // this_thread::sleep_for(chrono::seconds(delayBetweenFilters));

            for(size_t test{0}; test < numTests; test++) {
                
                //Randomize order just in case that does something
                vector<size_t> order(benchFunctions.size());
                for(size_t i{0}; i < order.size(); i++) order[i] = i;
                shuffle(order.begin(), order.end(), generator);

                for(size_t c{0}; c < benchFunctions.size(); c++) {
                    size_t i = order[c];

                    string filterName = filterNames[i];
                    string bfolder = folder + "/" + filterName+"/";
                    ofstream fout(bfolder+to_string(Ns[i]), std::ios_base::app);
                    
                    cout << test << " " << i << endl;
                    TestResult t = benchFunctions[i]();
                    // testResults[i].push_back();
                    fout << t.lf << " " << t.insertTime << " " << t.successfulQueryTime << " " << t.randomQueryTime << " " << t.removeTime << " " << t.falsePositiveRate << " " << t.sizeFilter << " " << t.sBLR << " " << t.rBLR << "\n";
                    // fileOutputs[i] << testResults[i][test].N << "," << testResults[i][test].sizeFilter << "," << testResults[i][test].insertTime << "," << testResults[i][test].successfulQueryTime << "," << testResults[i][test].randomQueryTime << "," << testResults[i][test].falsePositiveRate << "," << testResults[i][test].removeTime << endl;
                    // this_thread::sleep_for(chrono::seconds(delayBetweenFilters));
                }
            }

            // ofstream summaryOut("results/summary.csv");
            // summaryOut << summaryHeader << endl;
            // // cout << "Writing results" << endl;
            // for(size_t i{0}; i < filterNames.size(); i++) {
            //     TestResult avg = {0};
            //     summaryOut << filterNames[i] << ",";
            //     avg.N = testResults[i][0].N;
            //     summaryOut << avg.N << ",";
            //     avg.sizeFilter = testResults[i][0].sizeFilter;
            //     for(size_t test{0}; test < numTests; test++) {
            //         avg.insertTime += testResults[i][test].insertTime * 1000.0; //convert to ns from us
            //         avg.successfulQueryTime += testResults[i][test].successfulQueryTime * 1000.0;
            //         avg.randomQueryTime += testResults[i][test].randomQueryTime * 1000.0;
            //         avg.falsePositiveRate += testResults[i][test].falsePositiveRate;
            //         avg.removeTime += testResults[i][test].removeTime * 1000.0;
            //     }
            //     avg.insertTime /= numTests * avg.N;
            //     summaryOut << avg.insertTime << ",";
            //     avg.successfulQueryTime /= numTests * avg.N;
            //     summaryOut << avg.successfulQueryTime << ",";
            //     avg.randomQueryTime /= numTests * avg.N;
            //     summaryOut << avg.randomQueryTime << ",";
            //     avg.falsePositiveRate /= numTests;
            //     summaryOut << (1.0/avg.falsePositiveRate) << ",";
            //     avg.removeTime /= numTests * avg.N;
            //     summaryOut << avg.removeTime << ",";
            //     summaryOut << ((double)avg.sizeFilter * 8.0 / avg.N) << endl;
            // }
            
        }

};



// template<typename FT, bool CanDelete = true>
// function<TestResult(double)> glf(mt19937& generator) {
//     return [&] (size_t N_Filter) -> TestResult {return benchFilter<FT, CanDelete>(generator, N_Filter);}
// }

int main(int argc, char* argv[]) {
    random_device rd;
    mt19937 generator (rd());

    // size_t N = 1ull << 30;
    // size_t logN = 28;
    // if(argc > 1) {
    //     logN = atoi(argv[1]);
    // }
    size_t NumTests = 5;
    if(argc > 2){
        NumTests = atoi(argv[2]);
    }
    FilterTester ft;
    string ofolder = "LoadFactorPerformanceTest/7950xTry6";
    // constexpr size_t DelayBetweenTests = 15; //really should be like subtest
    // constexpr size_t DelayBetweenFilters = 0;

    // using PF_TC_Wrapper = PFFilterAPIWrapper<Prefix_Filter<TC_shortcut>, sizePF<TC_shortcut, sizeTC>, false>;
    // ft.addTest("Prefix filter TC", [&] () -> TestResult {return benchFilter<PF_TC_Wrapper, false>(generator, N, 1.0, DelayBetweenTests);});
    // ft.addTest("Prefix filter TC 95\% Full", [&] () -> TestResult {return benchFilter<PF_TC_Wrapper, false>(generator, N, 0.95, DelayBetweenTests);});

    // using CF12_Flex = cuckoofilter::CuckooFilterStable<uint64_t, 12>;
    // using PF_CFF12_Wrapper = PFFilterAPIWrapper<Prefix_Filter<CF12_Flex>, sizePF<CF12_Flex, sizeCFF>>;
    // ft.addTest("Prefix filter CF-12-Flex", [&] () -> TestResult {return benchFilter<PF_CFF12_Wrapper, false>(generator, N, 1.0, DelayBetweenTests);});

    // using PF_BBFF_Wrapper = PFFilterAPIWrapper<Prefix_Filter<SimdBlockFilterFixed<>>, sizePF<SimdBlockFilterFixed<>, sizeBBFF>>;
    // ft.addTest("Prefix filter BBF-Flex", [&] () -> TestResult {return benchFilter<PF_BBFF_Wrapper, false>(generator, N, 1.0, DelayBetweenTests);});

    // using TC_Wrapper = PFFilterAPIWrapper<TC_shortcut, sizeTC, true>;
    // ft.addTest("TC", [&] () -> TestResult {return benchFilter<TC_Wrapper, true>(generator, N, 1.0, DelayBetweenTests);});
    // ft.addTest("TC 95\% of max capacity", [&] () -> TestResult {return benchFilter<TC_Wrapper, true>(generator, N, 0.95, DelayBetweenTests);});
    // ft.addTest("TC 90\% of max capacity", [&] () -> TestResult {return benchFilter<TC_Wrapper, true>(generator, N, 0.90, DelayBetweenTests);});

    // using CFF12_Wrapper = PFFilterAPIWrapper<CF12_Flex, sizeCFF, true>;
    // ft.addTest("CF-12-Flex", [&] () -> TestResult {return benchFilter<CFF12_Wrapper, true>(generator, N, 1.0, DelayBetweenTests);});

    // using BBFF_Wrapper = PFFilterAPIWrapper<SimdBlockFilterFixed<>, sizeBBFF>;
    // ft.addTest("BBF-Flex", [&] () -> TestResult {return benchFilter<BBFF_Wrapper, false>(generator, N, 1.0, DelayBetweenTests);});

    // ft.addTest("DPF Matched to VQF 85 (46, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchPQF<46, 51, 35, 8, 64, 64>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF Matched to VQF 90 (49, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchPQF<49, 51, 35, 8, 64, 64>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(51, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchPQF<51, 51, 35, 8, 64, 64>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(22, 25, 17, 8, 32, 32)", [&] () -> TestResult {return benchPQF<22, 25, 17, 8, 32, 32>(generator, N);});
    // ft.addTest("DPF(23, 25, 17, 8, 32, 32)", [&] () -> TestResult {return benchPQF<23, 25, 17, 8, 32, 32>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(24, 25, 17, 8, 32, 32)", [&] () -> TestResult {return benchPQF<24, 25, 17, 8, 32, 32>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(24, 25, 17, 6, 32, 32)", [&] () -> TestResult {return benchPQF<24, 25, 17, 6, 32, 32>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(25, 25, 17, 4, 32, 32)", [&] () -> TestResult {return benchPQF<25, 25, 17, 4, 32, 32>(generator, N, DelayBetweenTests);});
    // // ft.addTest("VQF 85\% Full", [&] () -> TestResult {return benchFilter<VQFWrapper>(generator, N, 0.85, DelayBetweenTests);});
    // ft.addTest("VQF 90\% Full", [&] () -> TestResult {return benchFilter<VQFWrapper>(generator, N, 0.90, DelayBetweenTests);});

    for(size_t logN = 20; logN <= 28; logN+=2){
        FilterTester ft;

        ft.addLoadFactors("DPF_22-8", [&] (double lf) -> TestResult {return benchPQF<22, 25, 17, 8, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_22-8-Batch", [&] (double lf) -> TestResult {return benchPQF<22, 25, 17, 8, 32, 32, false, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_22-8-FastSQuery", [&] (double lf) -> TestResult {return benchPQF<22, 25, 17, 8, 32, 32, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_22-8-FastSQuery-Batch", [&] (double lf) -> TestResult {return benchPQF<22, 25, 17, 8, 32, 32, true, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_22-6", [&] (double lf) -> TestResult {return benchPQF<22, 25, 17, 6, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_22-4", [&] (double lf) -> TestResult {return benchPQF<22, 25, 17, 4, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_23-8", [&] (double lf) -> TestResult {return benchPQF<23, 25, 17, 8, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_23-4", [&] (double lf) -> TestResult {return benchPQF<23, 25, 17, 4, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_25-8", [&] (double lf) -> TestResult {return benchPQF<25, 25, 17, 8, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_25-4", [&] (double lf) -> TestResult {return benchPQF<25, 25, 17, 4, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_46-8", [&] (double lf) -> TestResult {return benchPQF<46, 51, 35, 8, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_46-6", [&] (double lf) -> TestResult {return benchPQF<46, 51, 35, 6, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_46-4", [&] (double lf) -> TestResult {return benchPQF<46, 51, 35, 4, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_51-8", [&] (double lf) -> TestResult {return benchPQF<51, 51, 35, 8, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_51-6", [&] (double lf) -> TestResult {return benchPQF<51, 51, 35, 6, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("DPF_52-8", [&] (double lf) -> TestResult {return benchPQF<52, 51, 35, 8, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("DPF_52-8-FastSQuery", [&] (double lf) -> TestResult {return benchPQF<52, 51, 35, 8, 64, 64, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("DPF_52-8-Batch", [&] (double lf) -> TestResult {return benchPQF<52, 51, 35, 8, 64, 64, false, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("DPF_52-8-FastSQuery-Batch", [&] (double lf) -> TestResult {return benchPQF<52, 51, 35, 8, 64, 64, true, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

        ft.addLoadFactors("VQF", [&] (double lf) -> TestResult {return benchFilter<VQFWrapper, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);


        using PF_TC_Wrapper = PFFilterAPIWrapper<Prefix_Filter<TC_shortcut>, sizePF<TC_shortcut, sizeTC>, false>;
        ft.addLoadFactors("PF-TC", [&] (double lf) -> TestResult {return benchFilter<PF_TC_Wrapper, false>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        using CF12_Flex = cuckoofilter::CuckooFilterStable<uint64_t, 12>;
        using PF_CFF12_Wrapper = PFFilterAPIWrapper<Prefix_Filter<CF12_Flex>, sizePF<CF12_Flex, sizeCFF>>;
        ft.addLoadFactors("PF-CF12F", [&] (double lf) -> TestResult {return benchFilter<PF_CFF12_Wrapper, false>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        using PF_BBFF_Wrapper = PFFilterAPIWrapper<Prefix_Filter<SimdBlockFilterFixed<>>, sizePF<SimdBlockFilterFixed<>, sizeBBFF>>;
        ft.addLoadFactors("PF-BBFF", [&] (double lf) -> TestResult {return benchFilter<PF_BBFF_Wrapper, false>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        using TC_Wrapper = PFFilterAPIWrapper<TC_shortcut, sizeTC, true>;
        ft.addLoadFactors("TC", [&] (double lf) -> TestResult {return benchFilter<TC_Wrapper, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        using CFF12_Wrapper = PFFilterAPIWrapper<CF12_Flex, sizeCFF, true>;
        ft.addLoadFactors("CF-12-Flex", [&] (double lf) -> TestResult {return benchFilter<CFF12_Wrapper, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        using BBFF_Wrapper = PFFilterAPIWrapper<SimdBlockFilterFixed<>, sizeBBFF>;
        ft.addLoadFactors("BBF-Flex", [&] (double lf) -> TestResult {return benchFilter<BBFF_Wrapper, false>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        using OriginalCF12 = CuckooWrapper<size_t, 12>;
        ft.addLoadFactors("OrigCF12", [&] (double lf) -> TestResult {return benchFilter<OriginalCF12, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

        ft.addLoadFactors("Morton", [&] (double lf) -> TestResult {return benchFilter<MortonWrapper, true, false, false>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("Morton-Batch", [&] (double lf) -> TestResult {return benchFilter<MortonWrapper, true, false, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        
        ft.runAll(NumTests, generator, ofolder);
    }
}