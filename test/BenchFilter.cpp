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
#include "vqf_filter.h"
#include "wrappers.hpp"
#include "min_pd256.hpp"
#include "tc-sym.hpp"
#include "TC-shortcut.hpp"

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
template<typename FT, bool CanDelete = true, bool getBLR = false>
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
    uniform_int_distribution<size_t> keyDist(0, -1ull);
    for(size_t i{0}; i < N; i++) {
        keys[i] = keyDist(generator) % filter.range;
        FPRkeys[i] = keyDist(generator) % filter.range;
    }
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests)); //Just sleep to try to keep turbo boost to full


    auto start = chrono::high_resolution_clock::now();
    for(size_t i{0}; i < N; i++) {
        filter.insert(keys[i]);

    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end-start);
    res.insertTime = (size_t)duration.count();
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    start = chrono::high_resolution_clock::now(); 
    // uint64_t x = 0;
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
    // cout << x << endl;
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    res.successfulQueryTime = (size_t)duration.count();
    if constexpr (getBLR) {
        res.sBLR = ((double)filter.backyardLookupCount) / N;
        filter.backyardLookupCount = 0;
    }
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    start = chrono::high_resolution_clock::now();
    uint64_t fpr = 0;
    for(size_t i{0}; i < N; i++) {
        fpr += filter.query(FPRkeys[i]);
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    res.randomQueryTime = (size_t)duration.count();
    res.falsePositiveRate = ((double)fpr) / N;
    if constexpr (getBLR) {
        res.rBLR = ((double)filter.backyardLookupCount) / N;
        filter.backyardLookupCount = 0;
    }
    // this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    if constexpr (CanDelete) {
        start = chrono::high_resolution_clock::now();
        for(size_t i{0}; i < N; i++) {
            // assert(filter.remove(keys[i]));
            filter.remove(keys[i]);
        }
        end = chrono::high_resolution_clock::now();
        duration = chrono::duration_cast<chrono::microseconds>(end-start);
        res.removeTime = (size_t)duration.count();
    }
    else {
        // res.removeTime = numeric_limits<double>::infinity();
        res.removeTime = -1ull;
    }

    return res;
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize, bool FastSQuery = false>
TestResult benchDPF(mt19937& generator, size_t N, double ratio = 1.0) {
    using FilterType = DynamicPrefixFilter::DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize, FastSQuery>;
    return benchFilter<FilterType, true, true>(generator, N, ratio);
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


//The methods in this function were copied from main.cc in VQF
class VQFWrapper {
    size_t nslots;
    vqf_filter *filter;
    static constexpr size_t QUQU_SLOTS_PER_BLOCK = 48; //Defined in vqf_filter.cpp so just copied from here. However there its defined based on remainder size, but here we just assume 8

    public:
        size_t range;

        VQFWrapper(size_t nslots): nslots{nslots} {
            if ((filter = vqf_init(nslots)) == NULL) {
                fprintf(stderr, "Insertion failed");
                exit(EXIT_FAILURE);
            }
            range = filter->metadata.range;
        }

        void insert(std::uint64_t hash) {
            if constexpr (DynamicPrefixFilter::DEBUG || DynamicPrefixFilter::PARTIAL_DEBUG) {
                if (!vqf_insert(filter, hash)) {
                    fprintf(stderr, "Insertion failed");
                    exit(EXIT_FAILURE);
                }
            }
            else {
                vqf_insert(filter, hash);
            }
        }

        bool query(std::uint64_t hash) {
            return vqf_is_present(filter, hash);
        }

        std::uint64_t sizeFilter() {
            //Copied from vqf_filter.c
            uint64_t total_blocks = (nslots + QUQU_SLOTS_PER_BLOCK)/QUQU_SLOTS_PER_BLOCK;
            uint64_t total_size_in_bytes = sizeof(vqf_block) * total_blocks;
            return total_size_in_bytes;
        }

        bool remove(std::uint64_t hash) {
            return vqf_remove(filter, hash);
        }

        ~VQFWrapper() {
            free(filter);
        }
};

//Taken from the respective files in prefix filter codes

//From TC_shortcut
size_t sizeTC (size_t N) {
    constexpr float load = .935;
    const size_t single_pd_capacity = tc_sym::MAX_CAP;
    return 64 * TC_shortcut::TC_compute_number_of_PD(N, single_pd_capacity, load);
}

//From stable cuckoo filter and singletable.h
template<size_t bits_per_tag = 12>
size_t sizeCFF(size_t N) {
    static const size_t kTagsPerBucket = 4;
    static const size_t kBytesPerBucket = (bits_per_tag * kTagsPerBucket + 7) >> 3;
    static const size_t kPaddingBuckets = ((((kBytesPerBucket + 7) / 8) * 8) - 1) / kBytesPerBucket;
    size_t assoc = 4;
    // bucket count needs to be even
    constexpr double load = .94;
    size_t bucketCount = (10 + N / load / assoc) / 2 * 2;
    return kBytesPerBucket * (bucketCount + kPaddingBuckets); //I think this is right?
}

//BBF-Flex is SimdBlockFilterFixed?? Seems to be by the main-perf code, so I shall stick with it
size_t sizeBBFF(size_t N) {
    unsigned long long int bits = N; //I am very unsure about this but it appears that is how the code is structured??? Size matches up anyways
    size_t bucketCount = max(1ull, bits / 24);
    using Bucket = uint32_t[8];
    return bucketCount * sizeof(Bucket);
}

template<typename SpareType, size_t (*SpareSpaceCalculator)(size_t)>
size_t sizePF (size_t N) {
    constexpr float loads[2] = {.95, .95};
    double frontyardSize =  32 * std::ceil(1.0 * N / (min_pd::MAX_CAP0 * loads[0]));
    static double constexpr overflowing_items_ratio = 0.0586;
    size_t backyardSize = SpareSpaceCalculator(get_l2_slots<SpareType>(N, overflowing_items_ratio, loads));
    return backyardSize+frontyardSize;
}

template<typename FilterType, size_t (*SpaceCalculator)(size_t), bool CanRemove = false>
class PFFilterAPIWrapper {
    // using SpareType = TC_shortcut;
    // using PrefixFilterType = Prefix_Filter<SpareType>;

    size_t N;
    FilterType filter;

    public:
        size_t range;

        PFFilterAPIWrapper(size_t N): N{N}, filter{FilterAPI<FilterType>::ConstructFromAddCount(N)} {
            range = -1ull;
        }

        void insert(std::uint64_t hash) {
            FilterAPI<FilterType>::Add(hash, &filter);
        }

        bool query(std::uint64_t hash) {
            return FilterAPI<FilterType>::Contain(hash, &filter);
        }

        std::uint64_t sizeFilter() {
            //Copied from wrappers.hpp and TC-Shortcut.hpp in Prefix-Filter
            //Size of frontyard
            return SpaceCalculator(N);
        }

        bool remove(std::uint64_t hash) {
            if(CanRemove) {
                FilterAPI<FilterType>::Remove(hash, &filter);
                return true; //No indication here at all
            }
            else
                return false;
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
    string ofolder = "LoadFactorPerformanceTest/7950xTry4";
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

    // ft.addTest("DPF Matched to VQF 85 (46, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchDPF<46, 51, 35, 8, 64, 64>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF Matched to VQF 90 (49, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchDPF<49, 51, 35, 8, 64, 64>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(51, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchDPF<51, 51, 35, 8, 64, 64>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(22, 25, 17, 8, 32, 32)", [&] () -> TestResult {return benchDPF<22, 25, 17, 8, 32, 32>(generator, N);});
    // ft.addTest("DPF(23, 25, 17, 8, 32, 32)", [&] () -> TestResult {return benchDPF<23, 25, 17, 8, 32, 32>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(24, 25, 17, 8, 32, 32)", [&] () -> TestResult {return benchDPF<24, 25, 17, 8, 32, 32>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(24, 25, 17, 6, 32, 32)", [&] () -> TestResult {return benchDPF<24, 25, 17, 6, 32, 32>(generator, N, DelayBetweenTests);});
    // ft.addTest("DPF(25, 25, 17, 4, 32, 32)", [&] () -> TestResult {return benchDPF<25, 25, 17, 4, 32, 32>(generator, N, DelayBetweenTests);});
    // // ft.addTest("VQF 85\% Full", [&] () -> TestResult {return benchFilter<VQFWrapper>(generator, N, 0.85, DelayBetweenTests);});
    // ft.addTest("VQF 90\% Full", [&] () -> TestResult {return benchFilter<VQFWrapper>(generator, N, 0.90, DelayBetweenTests);});

    for(size_t logN = 20; logN <= 28; logN+=2){
        FilterTester ft;

        ft.addLoadFactors("DPF_22-8", [&] (double lf) -> TestResult {return benchDPF<22, 25, 17, 8, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_22-8-FastSQuery", [&] (double lf) -> TestResult {return benchDPF<22, 25, 17, 8, 32, 32, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_22-6", [&] (double lf) -> TestResult {return benchDPF<22, 25, 17, 6, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_22-4", [&] (double lf) -> TestResult {return benchDPF<22, 25, 17, 4, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_23-8", [&] (double lf) -> TestResult {return benchDPF<23, 25, 17, 8, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_23-4", [&] (double lf) -> TestResult {return benchDPF<23, 25, 17, 4, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_25-8", [&] (double lf) -> TestResult {return benchDPF<25, 25, 17, 8, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        // ft.addLoadFactors("DPF_25-4", [&] (double lf) -> TestResult {return benchDPF<25, 25, 17, 4, 32, 32>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.8, 0.05);
        ft.addLoadFactors("DPF_46-8", [&] (double lf) -> TestResult {return benchDPF<46, 51, 35, 8, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_46-6", [&] (double lf) -> TestResult {return benchDPF<46, 51, 35, 6, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_46-4", [&] (double lf) -> TestResult {return benchDPF<46, 51, 35, 4, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_51-8", [&] (double lf) -> TestResult {return benchDPF<51, 51, 35, 8, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        // ft.addLoadFactors("DPF_51-6", [&] (double lf) -> TestResult {return benchDPF<51, 51, 35, 6, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("DPF_52-8", [&] (double lf) -> TestResult {return benchDPF<52, 51, 35, 8, 64, 64>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);
        ft.addLoadFactors("DPF_52-8-FastSQuery", [&] (double lf) -> TestResult {return benchDPF<52, 51, 35, 8, 64, 64, true>(generator, 1ull << logN, lf);}, 1ull << logN, 0.05, 0.9, 0.05);

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
        
        ft.runAll(NumTests, generator, ofolder);
    }
}