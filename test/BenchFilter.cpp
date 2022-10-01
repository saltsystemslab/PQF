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

#include "DynamicPrefixFilter.hpp"

using namespace std;

struct TestResult { //Times are all in microseconds for running not one test but all (really just chosen arbitrarily like that)
    double insertTime;
    double successfulQueryTime;
    double randomQueryTime;
    double falsePositiveRate;
    double removeTime;
    double ratio;
    size_t sizeFilter;
    size_t N;
};

//max ratio is the ratio of how much space you make for filter items to how many items you actually insert
template<typename FT, bool CanDelete = true>
TestResult benchFilter(mt19937 generator, size_t N, double ratio = 1.0, size_t delayBetweenTests = 30) {
    TestResult res;
    res.N = N;
    res.ratio = ratio;

    FT filter(N / ratio);
    res.sizeFilter = filter.sizeFilter();

    vector<size_t> keys(N);
    vector<size_t> FPRkeys(N);
    uniform_int_distribution<size_t> keyDist(0, -1ull);
    for(size_t i{0}; i < N; i++) {
        keys[i] = keyDist(generator) % filter.range;
        FPRkeys[i] = keyDist(generator) % filter.range;
    }
    this_thread::sleep_for(chrono::seconds(delayBetweenTests)); //Just sleep to try to keep turbo boost to full


    auto start = chrono::high_resolution_clock::now();
    for(size_t i{0}; i < N; i++) {
        filter.insert(keys[i]);
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end-start);
    res.insertTime = (double)duration.count();
    this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    start = chrono::high_resolution_clock::now(); 
    for(size_t i{0}; i < N; i++) {
        assert(filter.query(keys[i]));
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    res.successfulQueryTime = (double)duration.count();
    this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    start = chrono::high_resolution_clock::now();
    uint64_t fpr = 0;
    for(size_t i{0}; i < N; i++) {
        fpr += filter.query(FPRkeys[i]);
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    res.randomQueryTime = (double)duration.count();
    res.falsePositiveRate = ((double)fpr) / N;
    this_thread::sleep_for(chrono::seconds(delayBetweenTests));

    if constexpr (CanDelete) {
        start = chrono::high_resolution_clock::now();
        for(size_t i{0}; i < N; i++) {
            assert(filter.remove(keys[i]));
        }
        end = chrono::high_resolution_clock::now();
        duration = chrono::duration_cast<chrono::microseconds>(end-start);
        res.removeTime = (double)duration.count();
    }
    else {
        res.removeTime = numeric_limits<double>::infinity();
    }

    return res;
}

template<std::size_t BucketNumMiniBuckets, std::size_t FrontyardBucketCapacity, std::size_t BackyardBucketCapacity, std::size_t FrontyardToBackyardRatio, std::size_t FrontyardBucketSize, std::size_t BackyardBucketSize>
TestResult benchDPF(mt19937 generator, size_t N, size_t delayBetweenTests = 30) {
    using FilterType = DynamicPrefixFilter::DynamicPrefixFilter8Bit<BucketNumMiniBuckets, FrontyardBucketCapacity, BackyardBucketCapacity, FrontyardToBackyardRatio, FrontyardBucketSize, BackyardBucketSize>;
    return benchFilter<FilterType, true>(generator, N, 1.0, delayBetweenTests);
}

class FilterTester {
    vector<string> filterNames;
    vector<function<TestResult()>> benchFunctions;

    vector<vector<TestResult>> testResults;
    string individualFilterHeader = "N, \"Size of Filter\", \"Insert Time\", \"Positive Query Time\", \"Random Query Time\", \"False Positive Rate\", \"Remove Time\""; //Idk why doing it this way but here we report how long total to insert, and in summary we say how much per item

    string summaryHeader = "\"Filter Name\", N, \"Insert(ns)\", \"Positive Query(ns)\", \"Random Query(ns)\", \"False Positive Rate\", \"Remove Time\", \"Bits Per Key\"";

    public: 
        FilterTester(){}

        void addTest(string filterName, function<TestResult()> benchFunction) {
            filterNames.push_back("\"" + filterName + "\"");
            benchFunctions.push_back(benchFunction);
            testResults.push_back(vector<TestResult>());
        }

        void runAll(size_t numTests, size_t delayBetweenFilters = 120) { //Delay refers to time we wait after calling the bench function for a filter in order to wait for turbo to "reset"
            if(!filesystem::exists("results")) {
                filesystem::create_directory("results");
            }

            if(!filesystem::exists("results/all-data")) {
                filesystem::create_directory("results/all-data");
            }

            vector<ofstream> fileOutputs;
            for(string filterName: filterNames) {
                fileOutputs.push_back(ofstream("results/all-data/" + filterName + ".csv"));
                fileOutputs[fileOutputs.size()-1] << individualFilterHeader;
            }

            this_thread::sleep_for(chrono::seconds(delayBetweenFilters));

            for(size_t test{0}; test < numTests; test++) {
                for(size_t i{0}; i < benchFunctions.size(); i++) {
                    cout << test << " " << i << endl;
                    testResults[i].push_back(benchFunctions[i]());
                    fileOutputs[i] << testResults[i][test].N << "," << testResults[i][test].sizeFilter << "," << testResults[i][test].insertTime << "," << testResults[i][test].successfulQueryTime << "," << testResults[i][test].randomQueryTime << "," << testResults[i][test].falsePositiveRate << "," << testResults[i][test].removeTime << endl;
                    this_thread::sleep_for(chrono::seconds(delayBetweenFilters));
                }
            }

            ofstream summaryOut("results/summary.csv");
            summaryOut << summaryHeader << endl;
            for(size_t i{0}; i < filterNames.size(); i++) {
                TestResult avg = {0};
                summaryOut << filterNames[i] << ",";
                avg.N = testResults[i][0].N;
                summaryOut << avg.N << ",";
                avg.sizeFilter = testResults[i][0].sizeFilter;
                for(size_t test{0}; test < numTests; test++) {
                    avg.insertTime += testResults[i][test].insertTime * 1000.0; //convert to ns from us
                    avg.successfulQueryTime += testResults[i][test].successfulQueryTime * 1000.0;
                    avg.randomQueryTime += testResults[i][test].randomQueryTime * 1000.0;
                    avg.falsePositiveRate += testResults[i][test].falsePositiveRate;
                    avg.removeTime += testResults[i][test].removeTime * 1000.0;
                }
                avg.insertTime /= numTests * avg.N;
                summaryOut << avg.insertTime << ",";
                avg.successfulQueryTime /= numTests * avg.N;
                summaryOut << avg.successfulQueryTime << ",";
                avg.randomQueryTime /= numTests * avg.N;
                summaryOut << avg.randomQueryTime << ",";
                avg.falsePositiveRate /= numTests;
                summaryOut << avg.falsePositiveRate << ",";
                avg.removeTime /= numTests * avg.N;
                summaryOut << avg.removeTime << ",";
                summaryOut << ((double)avg.sizeFilter * 8.0 / avg.N) << endl;
            }
            
        }

};

int main(int argc, char* argv[]) {
    random_device rd;
    mt19937 generator (rd());

    size_t N = 1ull << 26;
    if(argc > 1) {
        N = (1ull << atoi(argv[1]));
    }
    FilterTester ft;
    ft.addTest("DPF(46, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchDPF<46, 51, 35, 8, 64, 64>(generator, N);});
    ft.addTest("DPF(48, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchDPF<48, 51, 35, 8, 64, 64>(generator, N);});
    ft.addTest("DPF(51, 51, 35, 8, 64, 64)", [&] () -> TestResult {return benchDPF<51, 51, 35, 8, 64, 64>(generator, N);});
    ft.addTest("DPF(22, 25, 17, 8, 32, 32)", [&] () -> TestResult {return benchDPF<22, 25, 17, 8, 32, 32>(generator, N);});

    ft.runAll(1);
}