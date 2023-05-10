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

template<typename FT>
void insertItems(FT& filter, vector<size_t>& keys, size_t start, size_t end) {
    // std::cout << "start: " << start << ", end: " << end << std::endl;
    for(size_t i{start}; i < end; i++) {
        filter.insert(keys[i]);
    }
}

template<typename FT>
void checkQuery(FT& filter, vector<size_t>& keys, size_t start, size_t end, bool& status) {
    for(size_t i{start}; i < end; i++) {
        if(!filter.query(keys[i])) {
            // cerr << "Query on " << keys[i] << " failed." << endl;
            // exit(EXIT_FAILURE);
            status = false;
            return;
        }
    }
}

template<typename FT>
void getNumFalsePositives(FT& filter, vector<size_t>& FPRkeys, size_t start, size_t end, uint64_t& fpr_res) {
    uint64_t fpr = 0;
    for(size_t i{start}; i < end; i++) {
        fpr += filter.query(FPRkeys[i]);
    }
    // return fpr;
    fpr_res = fpr;
}

template<typename FT>
void removeItems(FT& filter, vector<size_t>& keys, size_t start, size_t end) {
    for(size_t i{start}; i < end; i++) {
        // assert(filter.remove(keys[i]));
        filter.remove(keys[i]);
    }
}

//max ratio is the ratio of how much space you make for filter items to how many items you actually insert
template<typename FT, bool CanDelete = true, bool getBLR = false, bool testBatch=false>
TestResult benchFilter(size_t N, double ratio, size_t numThreads = 1) {
    random_device rd;
    mt19937 generator (rd());
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
    // vector<bool> status(batchSize);
    uniform_int_distribution<size_t> keyDist(0, -1ull);
    for(size_t i{0}; i < N; i++) {
        keys[i] = keyDist(generator) % filter.range;
        FPRkeys[i] = keyDist(generator) % filter.range;
    }
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests)); //Just sleep to try to keep turbo boost to full


    auto start = chrono::high_resolution_clock::now();
    if (numThreads > 1) {
        std::vector<std::thread> threads;
        for(size_t i = 0; i < numThreads; i++) {
            // std::cout << i << std::endl;
            threads.push_back(std::thread([&, i] () -> void {insertItems(filter, keys, (i*N) / numThreads, ((i+1)*N) / numThreads);}));
        }
        for(auto& th: threads) {
            th.join();
        }
    }
    else {
        insertItems(filter, keys, 0, N);
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end-start);
    // cout << "inserted" << endl;
    res.insertTime = (size_t)duration.count();
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    start = chrono::high_resolution_clock::now(); 
    // uint64_t x = 0;
    bool status = true;
    if (numThreads > 1) {
        std::vector<std::thread> threads;
        for(size_t i = 0; i < numThreads; i++) {
            // std::cout << i << std::endl;
            threads.push_back(std::thread([&, i] () -> void {checkQuery(filter, keys, (i*N) / numThreads, ((i+1)*N) / numThreads, status);}));
        }
        for(auto& th: threads) {
            th.join();
        }
    }
    else {
        checkQuery(filter, keys, 0, N, status);
    }
    if(!status) {
        res.successfulQueryTime = -1ull;
        return res;
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
    if (numThreads > 1) {
        std::vector<std::thread> threads;
        std::vector<uint64_t> fpr_res(numThreads);
        for(size_t i = 0; i < numThreads; i++) {
            // std::cout << i << std::endl;
            threads.push_back(std::thread([&, i] () -> void {getNumFalsePositives(filter, FPRkeys, (i*N) / numThreads, ((i+1)*N) / numThreads, fpr_res[i]);}));
        }
        for(size_t i = 0; i < numThreads; i++) {
            threads[i].join();
            fpr+=fpr_res[i];
        }
    }
    else {
        getNumFalsePositives(filter, FPRkeys, 0, N, fpr);
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
        if (numThreads > 1) {
            std::vector<std::thread> threads;
            for(size_t i = 0; i < numThreads; i++) {
                // std::cout << i << std::endl;
                threads.push_back(std::thread([&, i] () -> void {removeItems(filter, keys, (i*N) / numThreads, ((i+1)*N) / numThreads);}));
            }
            for(auto& th: threads) {
                th.join();
            }
        }
        else {
            removeItems(filter, keys, 0, N);
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

template<std::size_t SizeRemainders, std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FQR = false, bool testBatch = false, bool Threaded = false>
TestResult benchPQF(size_t N, double ratio = 1.0, size_t numThreads = 1) {
    using FilterType = DynamicPrefixFilter::PartitionQuotientFilter<SizeRemainders, BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FQR, Threaded>;
    return benchFilter<FilterType, true, true, testBatch>(N, ratio, numThreads);
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

        void runAll(size_t numTests, string ofolder) {
            random_device rd;
            mt19937 generator (rd());
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
                    
                    // cout << test << " " << i << " (" << filterName << ")" << endl;
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
//     return [logN] (size_t N_Filter) -> TestResult {return benchFilter<FT, CanDelete>(N_Filter);}
// }

int main(int argc, char* argv[]) {
    // random_device rd;
    // mt19937 generator (rd());

    // size_t N = 1ull << 30;
    // size_t logN = 28;
    // if(argc > 1) {
    //     logN = atoi(argv[1]);
    // }
    size_t NumTests = 3;
    if(argc > 2){
        NumTests = atoi(argv[2]);
    }
    FilterTester ft;
    string ofolder = "LoadFactorPerformanceTest/7950xTry9";
    // constexpr size_t DelayBetweenTests = 15; //really should be like subtest
    // constexpr size_t DelayBetweenFilters = 0;

    // using PF_TC_Wrapper = PFFilterAPIWrapper<Prefix_Filter<TC_shortcut>, sizePF<TC_shortcut, sizeTC>, false>;
    // ft.addTest("Prefix filter TC", [logN] () -> TestResult {return benchFilter<PF_TC_Wrapper, false>(N, 1.0, DelayBetweenTests);});
    // ft.addTest("Prefix filter TC 95\% Full", [logN] () -> TestResult {return benchFilter<PF_TC_Wrapper, false>(N, 0.95, DelayBetweenTests);});

    // using CF12_Flex = cuckoofilter::CuckooFilterStable<uint64_t, 12>;
    // using PF_CFF12_Wrapper = PFFilterAPIWrapper<Prefix_Filter<CF12_Flex>, sizePF<CF12_Flex, sizeCFF>>;
    // ft.addTest("Prefix filter CF-12-Flex", [logN] () -> TestResult {return benchFilter<PF_CFF12_Wrapper, false>(N, 1.0, DelayBetweenTests);});

    // using PF_BBFF_Wrapper = PFFilterAPIWrapper<Prefix_Filter<SimdBlockFilterFixed<>>, sizePF<SimdBlockFilterFixed<>, sizeBBFF>>;
    // ft.addTest("Prefix filter BBF-Flex", [logN] () -> TestResult {return benchFilter<PF_BBFF_Wrapper, false>(N, 1.0, DelayBetweenTests);});

    // using TC_Wrapper = PFFilterAPIWrapper<TC_shortcut, sizeTC, true>;
    // ft.addTest("TC", [logN] () -> TestResult {return benchFilter<TC_Wrapper, true>(N, 1.0, DelayBetweenTests);});
    // ft.addTest("TC 95\% of max capacity", [logN] () -> TestResult {return benchFilter<TC_Wrapper, true>(N, 0.95, DelayBetweenTests);});
    // ft.addTest("TC 90\% of max capacity", [logN] () -> TestResult {return benchFilter<TC_Wrapper, true>(N, 0.90, DelayBetweenTests);});

    // using CFF12_Wrapper = PFFilterAPIWrapper<CF12_Flex, sizeCFF, true>;
    // ft.addTest("CF-12-Flex", [logN] () -> TestResult {return benchFilter<CFF12_Wrapper, true>(N, 1.0, DelayBetweenTests);});

    // using BBFF_Wrapper = PFFilterAPIWrapper<SimdBlockFilterFixed<>, sizeBBFF>;
    // ft.addTest("BBF-Flex", [logN] () -> TestResult {return benchFilter<BBFF_Wrapper, false>(N, 1.0, DelayBetweenTests);});

    // ft.addTest("PQF Matched to VQF 85 (46, 51, 35, 8, 64, 64)", [logN] () -> TestResult {return benchPQF<46, 51, 35, 8, 64, 64>(N, DelayBetweenTests);});
    // ft.addTest("PQF Matched to VQF 90 (49, 51, 35, 8, 64, 64)", [logN] () -> TestResult {return benchPQF<49, 51, 35, 8, 64, 64>(N, DelayBetweenTests);});
    // ft.addTest("PQF(51, 51, 35, 8, 64, 64)", [logN] () -> TestResult {return benchPQF<51, 51, 35, 8, 64, 64>(N, DelayBetweenTests);});
    // ft.addTest("PQF(22, 25, 17, 8, 32, 32)", [logN] () -> TestResult {return benchPQF<22, 25, 17, 8, 32, 32>(N);});
    // ft.addTest("PQF(23, 25, 17, 8, 32, 32)", [logN] () -> TestResult {return benchPQF<23, 25, 17, 8, 32, 32>(N, DelayBetweenTests);});
    // ft.addTest("PQF(24, 25, 17, 8, 32, 32)", [logN] () -> TestResult {return benchPQF<24, 25, 17, 8, 32, 32>(N, DelayBetweenTests);});
    // ft.addTest("PQF(24, 25, 17, 6, 32, 32)", [logN] () -> TestResult {return benchPQF<24, 25, 17, 6, 32, 32>(N, DelayBetweenTests);});
    // ft.addTest("PQF(25, 25, 17, 4, 32, 32)", [logN] () -> TestResult {return benchPQF<25, 25, 17, 4, 32, 32>(N, DelayBetweenTests);});
    // // ft.addTest("VQF 85\% Full", [logN] () -> TestResult {return benchFilter<VQFWrapper>(N, 0.85, DelayBetweenTests);});
    // ft.addTest("VQF 90\% Full", [logN] () -> TestResult {return benchFilter<VQFWrapper>(N, 0.90, DelayBetweenTests);});

    for(size_t logN = 20; logN <= 28; logN+=2){
        FilterTester ft;

        ft.addLoadFactors("PQF_22-8", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 18, 8, 32, 32, false, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-1T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, false, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-2T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, false, false, true>(1ull << logN, lf, 2);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-4T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, false, false, true>(1ull << logN, lf, 4);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-8T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, false, false, true>(1ull << logN, lf, 8);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-16T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, false, false, true>(1ull << logN, lf, 16);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-30T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, false, false, true>(1ull << logN, lf, 30);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("PQF_22-8-Batch", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 18, 8, 32, 32, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-FQR", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 18, 8, 32, 32, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-FQR-1T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, true, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-FQR-2T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, true, false, true>(1ull << logN, lf, 2);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-FQR-4T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, true, false, true>(1ull << logN, lf, 4);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("PQF_22-8-FQR-8T", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 17, 8, 32, 32, true, false, true>(1ull << logN, lf, 8);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("PQF_22-8-FQR-Batch", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 18, 8, 32, 32, true, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("PQF_22-8BB", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 37, 8, 32, 64>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("PQF_22-8BB-FQR", [logN] (double lf) -> TestResult {return benchPQF<8, 22, 25, 37, 8, 32, 64, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);

        // ft.addLoadFactors("PQF_31-8", [logN] (double lf) -> TestResult {return benchPQF<8, 31, 24, 17, 8, 32, 32>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("PQF_31-8-FQR", [logN] (double lf) -> TestResult {return benchPQF<8, 31, 24, 17, 8, 32, 32, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);

        
        // ft.addLoadFactors("PQF_62-8", [logN] (double lf) -> TestResult {return benchPQF<8, 62, 50, 34, 8, 64, 64>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_62-8-FQR", [logN] (double lf) -> TestResult {return benchPQF<8, 62, 50, 34, 8, 64, 64, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

        // ft.addLoadFactors("PQF_52-8", [logN] (double lf) -> TestResult {return benchPQF<8, 52, 51, 35, 8, 64, 64>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_52-8-FQR", [logN] (double lf) -> TestResult {return benchPQF<8, 52, 51, 35, 8, 64, 64, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_52-8-Batch", [logN] (double lf) -> TestResult {return benchPQF<8, 52, 51, 35, 8, 64, 64, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_52-8-FQR-Batch", [logN] (double lf) -> TestResult {return benchPQF<8, 52, 51, 35, 8, 64, 64, true, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

        ft.addLoadFactors("PQF_53-8", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-1T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, false, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-2T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, false, false, true>(1ull << logN, lf, 2);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-4T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, false, false, true>(1ull << logN, lf, 4);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-8T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, false, false, true>(1ull << logN, lf, 8);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("PQF_53-8-FQR", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-FQR-1T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, true, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-FQR-2T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, true, false, true>(1ull << logN, lf, 2);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-FQR-4T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, true, false, true>(1ull << logN, lf, 4);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-FQR-8T", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, true, false, true>(1ull << logN, lf, 8);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-Batch", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("PQF_53-8-FQR-Batch", [logN] (double lf) -> TestResult {return benchPQF<8, 53, 51, 35, 8, 64, 64, true, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);


        ft.addLoadFactors("PQF16", [logN] (double lf) -> TestResult {return benchPQF<16, 36, 28, 22, 8, 64, 64, false, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-1T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, false, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-2T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, false, false, true>(1ull << logN, lf, 2);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-4T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, false, false, true>(1ull << logN, lf, 4);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-8T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, false, false, true>(1ull << logN, lf, 8);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-FRQ", [logN] (double lf) -> TestResult {return benchPQF<16, 36, 28, 22, 8, 64, 64, true, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-FRQ-1T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, true, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-FRQ-2T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, true, false, true>(1ull << logN, lf, 2);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-FRQ-4T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, true, false, true>(1ull << logN, lf, 4);}, 1ull << logN, 0.05, 0.85, 0.05);
        ft.addLoadFactors("PQF16-FRQ-8T", [logN] (double lf) -> TestResult {return benchPQF<16, 35, 28, 22, 8, 64, 64, true, false, true>(1ull << logN, lf, 8);}, 1ull << logN, 0.05, 0.85, 0.05);
        // ft.addLoadFactors("PQF16-Batch", [logN] (double lf) -> TestResult {return benchPQF<16, 36, 28, 22, 8, 64, 64, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.85, 0.05);
        // ft.addLoadFactors("PQF16-FRQ-Batch", [logN] (double lf) -> TestResult {return benchPQF<16, 36, 28, 22, 8, 64, 64, true, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.85, 0.05);

        ft.addLoadFactors("VQF", [logN] (double lf) -> TestResult {return benchFilter<VQFWrapper, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);


        // using PF_TC_Wrapper = PFFilterAPIWrapper<Prefix_Filter<TC_shortcut>, sizePF<TC_shortcut, sizeTC>, false>;
        // ft.addLoadFactors("PF-TC", [logN] (double lf) -> TestResult {return benchFilter<PF_TC_Wrapper, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        // using CF12_Flex = cuckoofilter::CuckooFilterStable<uint64_t, 12>;
        // using PF_CFF12_Wrapper = PFFilterAPIWrapper<Prefix_Filter<CF12_Flex>, sizePF<CF12_Flex, sizeCFF>>;
        // ft.addLoadFactors("PF-CF12F", [logN] (double lf) -> TestResult {return benchFilter<PF_CFF12_Wrapper, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        // using PF_BBFF_Wrapper = PFFilterAPIWrapper<Prefix_Filter<SimdBlockFilterFixed<>>, sizePF<SimdBlockFilterFixed<>, sizeBBFF>>;
        // ft.addLoadFactors("PF-BBFF", [logN] (double lf) -> TestResult {return benchFilter<PF_BBFF_Wrapper, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        // using TC_Wrapper = PFFilterAPIWrapper<TC_shortcut, sizeTC, true>;
        // ft.addLoadFactors("TC", [logN] (double lf) -> TestResult {return benchFilter<TC_Wrapper, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        // using CFF12_Wrapper = PFFilterAPIWrapper<CF12_Flex, sizeCFF, true>;
        // ft.addLoadFactors("CF-12-Flex", [logN] (double lf) -> TestResult {return benchFilter<CFF12_Wrapper, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        // using BBFF_Wrapper = PFFilterAPIWrapper<SimdBlockFilterFixed<>, sizeBBFF>;
        // ft.addLoadFactors("BBF-Flex", [logN] (double lf) -> TestResult {return benchFilter<BBFF_Wrapper, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 1.0, 0.05);

        using OriginalCF12 = CuckooWrapper<size_t, 12>;
        ft.addLoadFactors("OrigCF12", [logN] (double lf) -> TestResult {return benchFilter<OriginalCF12, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

        using OriginalCF16 = CuckooWrapper<size_t, 16>;
        ft.addLoadFactors("OrigCF16", [logN] (double lf) -> TestResult {return benchFilter<OriginalCF16, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

        ft.addLoadFactors("Morton", [logN] (double lf) -> TestResult {return benchFilter<MortonWrapper<CompressedCuckoo::Morton3_12>, true, false, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("Morton-Batch", [logN] (double lf) -> TestResult {return benchFilter<MortonWrapper<CompressedCuckoo::Morton3_12>, true, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

        ft.addLoadFactors("Morton18", [logN] (double lf) -> TestResult {return benchFilter<MortonWrapper<CompressedCuckoo::Morton3_18>, true, false, false>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("Morton18-Batch", [logN] (double lf) -> TestResult {return benchFilter<MortonWrapper<CompressedCuckoo::Morton3_18>, true, false, true>(1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        
        ft.runAll(NumTests, ofolder);
    }
}