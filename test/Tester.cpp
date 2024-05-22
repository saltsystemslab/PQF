#include "Tester.hpp"
#include "Config.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <set>
#include <map>
#include <cmath>
#include <atomic>
#include <thread>
#include <filesystem>
#include <limits>

//returns microseconds
double runTest(std::function<void(void)> t) {
    std::chrono::time_point<chrono::system_clock> startTime, endTime;
    startTime = std::chrono::system_clock::now();
    asm volatile ("" ::: "memory");
    
    t();

    asm volatile ("" ::: "memory");
    endTime = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;
    return duration_cast<std::chrono::microseconds>(elapsed).count();
}

std::vector<size_t> splitRange(size_t start, size_t end, size_t numSegs) {
    // std::cout << start << " " << end << " " << numSegs << std::endl;
    std::vector<size_t> ans(numSegs+1);
    for(size_t i=0; i<=numSegs; i++) {
        ans[i] = start + (end-start) * i / numSegs;
        // std::cout << i << " " << ans[i] << std::endl;
    }
    return ans;
}

std::mt19937_64 createGenerator() {
    return std::mt19937_64(std::random_device()());
}

template<typename FT>
size_t generateKey(const FT& filter, std::mt19937_64& generator) {
    std::uniform_int_distribution dist(0ull, filter.range-1ull);
    return dist(generator);
}

template<typename FT>
std::vector<size_t> generateKeys(const FT& filter, size_t N, size_t NumThreads = 32) {
    if(NumThreads > 1) {
        std::vector<size_t> keys(N);
        std::vector<size_t> threadKeys = splitRange(0, N, NumThreads);
        std::vector<std::thread> threads;
        for(size_t i = 0; i < NumThreads; i++) {
            threads.push_back(std::thread([&, i] {
                auto generator = createGenerator();
                for(size_t j=threadKeys[i]; j < threadKeys[i+1]; j++) {
                    keys[j] = generateKey<FT>(filter, generator);
                }
            }));
        }
        for(auto& th: threads) {
            th.join();
        }
        return keys;
    }
    else {
        std::vector<size_t> keys;
        auto generator = createGenerator();
        for(size_t i=0; i < N; i++) {
            keys.push_back(generateKey<FT>(filter, generator));
        }
        return keys;
    }
}

template<typename FT>
bool insertItems(FT& filter, const std::vector<size_t>& keys, size_t start, size_t end) {
    for(size_t i{start}; i < end; i++) {
        if(!filter.insert(keys[i])) {
            return false;
        }
    }
    return true;
}

template<typename FT>
bool checkQuery(FT& filter, const std::vector<size_t>& keys, size_t start, size_t end) {
    for(size_t i{start}; i < end; i++) {
        if(!filter.query(keys[i])) {
            std::cerr << "zzoo " << i << std::endl;
            return false;
        }
    }
    return true;
}

template<typename FT>
size_t getNumFalsePositives(FT& filter, const std::vector<size_t>& FPRkeys, size_t start, size_t end) {
    size_t fpr = 0;
    for(size_t i{start}; i < end; i++) {
        fpr += filter.query(FPRkeys[i]);
    }
    return fpr;
}

template<typename FT>
bool removeItems(FT& filter, const std::vector<size_t>& keys, size_t start, size_t end) {
    for(size_t i{start}; i < end; i++) {
        if(!filter.remove(keys[i])) {
            return false;
        }
    }
    return true;
}

template<typename FT>
bool checkFunctional(FT& filter, const std::vector<size_t>& keysInFilter, std::mt19937_64& generator) {
    //Make configurable later
    size_t checkQuerySize = 1000;
    double minimumFPR = 0.05; //minimum false positive rate must maintain. should set relatively generously to avoid accidental failures as this is randomized ofc.

    std::uniform_int_distribution indexDist(0ull, keysInFilter.size()-1ull);
    std::vector<size_t> checkKeys(checkQuerySize);
    for(auto& index: checkKeys) {
        index = keysInFilter[indexDist(generator)];
    }

    bool success = checkQuery(filter, checkKeys, 0, checkKeys.size());
    
    if(!success) {
        return false;
    }

    std::vector<size_t> randomKeys = generateKeys(filter, checkQuerySize, 1);
    size_t numFalsePositives = getNumFalsePositives(filter, randomKeys, 0, randomKeys.size());
    double fpr = ((double) numFalsePositives) / randomKeys.size();
    if(fpr < minimumFPR) {
        std::cerr << fpr << std::endl;
        success = false;
    }

    return true;
}

//Assumes filter already filled enough so then it will only insert & delete one key at a time
template<typename FT>
size_t streamingInsertDeleteTest(FT& filter, std::vector<size_t>& keysInFilter, std::mt19937_64& generator, size_t maxKeyCount) {
    size_t checkQueryInterval = 1000; //To confirm the deletions are working properly. Make configurable later?
    
    for(size_t i=0; i < maxKeyCount;  i++) {
        if (i% 10000000 == 0) {
            std::cout << i << "\n";
        }

        if(i % checkQueryInterval == 0) {
            if(!checkFunctional(filter, keysInFilter, generator)) {
                std::cout << "Query failed at " << i << std::endl;
                return i;
            }
        }

        size_t keyToRemove = i % keysInFilter.size();
        if(!filter.remove(keysInFilter[keyToRemove])) {
            std::cout << "Failed to remove at " << i << std::endl;
            return i;
        }
        keysInFilter[keyToRemove] = -1ull; //outside filter.range so we can check this
        size_t key = generateKey(filter, generator);
        if(!filter.insert(key)) {
            std::cout << "Failed to insert at " << i << std::endl;
            return i;
        }
        keysInFilter[keyToRemove] = key;
    }
    return maxKeyCount;
}

template<typename FT>
size_t randomInsertDeleteTest(FT& filter, std::vector<size_t>& keysInFilter, std::mt19937_64& generator, size_t maxKeyCount) {
    size_t checkQueryInterval = 1000;

    std::uniform_int_distribution removeDist(0ull, keysInFilter.size()-1ull);
    for(size_t i=0; i < maxKeyCount;  i++) {
        if (i% 10000000 == 0) {
            std::cout << i << "\n";
        }

        if(i % checkQueryInterval == 0) {
            if(!checkFunctional(filter, keysInFilter, generator)) {
                std::cout << "Query failed at " << i << std::endl;
                return i;
            }
        }

        size_t keyToRemove = removeDist(generator);
        if(!filter.remove(keysInFilter[keyToRemove])) {
            return i;
        }
        keysInFilter[keyToRemove] = -1ull; //outside filter.range so we can check this
        size_t key = generateKey(filter, generator);
        if(!filter.insert(key)) {
            return i;
        }
        keysInFilter[keyToRemove] = key;
    }
    return maxKeyCount;
}


struct Settings {
    //need a somewhat better way to do settings, since now the setting for every type of thing (filter type, test type, the general test handler) are all in the same struct. Not sure exactly what a better (and still simple) design would be
    static auto SettingTypes() {
        return std::set<std::string>{"NumKeys", "NumTrials", "NumThreads", "NumReplicants", "LoadFactorTicks", "MaxLoadFactor", "MinLoadFactor", "MaxInsertDeleteRatio"};
    }

    std::string TestName;
    std::string FTName;
    size_t N;
    size_t numReplicants = 1; 
    size_t numThreads{1};
    size_t loadFactorTicks = 1;
    std::optional<double> maxLoadFactor; //optional since its a necessary value to set
    double minLoadFactor = 0.0;
    double maxInsertDeleteRatio = 10.0;

    auto getTuple() const {
        return std::tie(TestName, FTName, N, numReplicants, numThreads, loadFactorTicks, maxLoadFactor, minLoadFactor, maxInsertDeleteRatio);
    }

    // bool complete() {
    //     return (Ns.size() > 0) && numTrials.has_value() && loadFactorIncrement.has_value() && maxLoadFactor.has_value();
    // }

    bool operator==(const Settings& rhs) const {
        return getTuple() == rhs.getTuple();
    }

    bool operator<(const Settings& rhs) const {
        return getTuple() < rhs.getTuple();
    }

    //doesn't set testname or ftname here that is done externally. Yeah not great system whatever
    void setval(std::string type, std::vector<double> values) {
        if(SettingTypes().count(type) == 0) {
            std::cerr << "Set an incorrect setting: " << type << std::endl;
            exit(-1);
        }
        if(values.size() != 1) {
            std::cerr << "Can only set one value!" << std::endl;
            exit(-1);
        }
        
        if(type == "NumKeys"s) {
            N = static_cast<size_t>(values[0]);
        }
        else if(type == "NumThreads"s) {
            numThreads = static_cast<size_t>(values[0]);
        }
        else if(type == "NumReplicants"s) {
            numReplicants = static_cast<size_t>(values[0]);
        }
        else if(type == "LoadFactorTicks"s) {
            loadFactorTicks = static_cast<size_t>(values[0]);
        }
        else if (type == "MaxLoadFactor") {
            maxLoadFactor = values[0];
        }
        else if (type == "MinLoadFactor") {
            minLoadFactor = values[0];
        }
        else if (type == "MaxInsertDeleteRatio") {
            maxInsertDeleteRatio = values[0];
        }
    }
};

std::ostream & operator<<(std::ostream &os, const Settings& s) {
    os << "# Settings for a particular run:" << "\n";
    os << s.TestName << "\n";
    os << "NumKeys " << s.N << "\n";
    os << "NumThreads " << s.numThreads << "\n";
    os << "NumReplicants " << s.numReplicants << "\n";
    os << "LoadFactorTicks " << s.loadFactorTicks << "\n";
    if(s.maxLoadFactor)
        os << "MaxLoadFactor " << (*(s.maxLoadFactor)) << "\n";
    os << "MinLoadFactor " << s.minLoadFactor << "\n";
    os << "MaxInsertDeleteRatio " << s.maxInsertDeleteRatio << "\n";
    os << s.FTName << "\n";
    return os;
}

template<typename T, typename LambdaT>
auto transform_vector(std::vector<T>& v, LambdaT lambda) {
    using TElemType = decltype(std::declval<LambdaT>()(std::declval<T>()));
    // std::vector<typename FunctionSignature<LambdaT>::RetT> retv;
    std::vector<TElemType> retv;
    for(T& t: v) {
        retv.push_back(lambda(t));
    }
    return retv;
}

size_t roundDoublePos(double d) {
    long long l = std::llround(d);
    if(l < 0) {
        std::cerr << "Trying to round a double to positive number, but it was negative (probably settings issue)" << std::endl;
        exit(-1);
    }
    return static_cast<size_t>(l);
}

struct CompressedSettings {
    // const static std::set<std::string> TestTypes{"Benchmark", "RandomInsertDelete", "StreamingInsertDelete", "LoadFactor"};

    std::string TestName;
    std::string FTName;
    std::vector<size_t> Ns;
    std::optional<size_t> numTrials;
    size_t numReplicants = 1;
    std::vector<size_t> numThreads{1};
    size_t loadFactorTicks;
    std::optional<double> maxLoadFactor;
    double minLoadFactor = 0.0;
    double maxInsertDeleteRatio = 10.0;

    std::vector<Settings> getSettingsCombos () {
        std::vector<Settings> output;
        for(size_t N: Ns) {
            for(size_t NT: numThreads) {
                assert(numTrials.has_value());
                for(size_t i=0; i < (*numTrials); i++) {
                    output.push_back(Settings{TestName, FTName, N, numReplicants, NT, loadFactorTicks, maxLoadFactor, minLoadFactor, maxInsertDeleteRatio});
                }
            }
        }
        return output;
    }

    void setval(std::string type, std::vector<double> values) {
        if(Settings::SettingTypes().count(type) == 0) {
            std::cerr << "Set an incorrect setting: " << type << std::endl;
            exit(-1);
        }
        
        if(type == "NumKeys"s) {
            Ns = transform_vector(values, &roundDoublePos);
        }
        else if(type == "NumThreads"s) {
            numThreads = transform_vector(values, &roundDoublePos);
        }
        else {
            if(values.size() != 1) {
                std::cerr << "Can only set one value for " << type << std::endl;
                exit(-1);
            }
            if(type == "NumTrials") {
                numTrials = static_cast<size_t>(values[0]);
            }
            else if(type == "NumReplicants"s) {
                numReplicants = static_cast<size_t>(values[0]);
            }
            else if(type == "LoadFactorTicks"s) {
                loadFactorTicks = static_cast<size_t>(values[0]);
            }
            else if (type == "MaxLoadFactor") {
            maxLoadFactor = values[0];
            }
            else if (type == "MinLoadFactor") {
                minLoadFactor = values[0];
            }
            else if (type == "MaxInsertDeleteRatio") {
                maxInsertDeleteRatio = values[0];
            }
        }
    }
};



struct MergeWrapper {
    static constexpr std::string_view name = "MergeBenchmark";

    template<typename FTWrapper>
    static std::vector<double> run(Settings s) {
        size_t numThreads = s.numThreads;
        if(numThreads == 0) {
            std::cerr << "Cannot have 0 threads!!" << std::endl;
            return {};
        }
        if(numThreads > 1 && !FTWrapper::threaded) {
            std::cerr << "Cannot test multiple threads when the filter does not support it!" << std::endl;
            return {};
        }

        if(!s.maxLoadFactor) {
            std::cerr << "Does not have a max load factor!" << std::endl;
            return std::vector<double>{};
        }
        double maxLoadFactor = *(s.maxLoadFactor);

        using FT = typename FTWrapper::type;
        size_t filterSlots = s.N;
        size_t N = static_cast<size_t>(s.N * maxLoadFactor);
        FT a(filterSlots);
        FT b(filterSlots);

        std::vector<size_t> keys = generateKeys<FT>(a, 2*N);
        insertItems<FT>(a, keys, 0, N);
        if(!checkQuery(a, keys, 0, N)) {
            std::cerr << "Failed to insert into a" << std::endl;
            return std::vector<double>{std::numeric_limits<double>::max()};
        }
        insertItems<FT>(b, keys, N, 2*N);
        if(!checkQuery(b, keys, N, 2*N)) {
            std::cerr << "Failed to insert into b" << std::endl;
            return std::vector<double>{std::numeric_limits<double>::max()};
        }
        auto generator = createGenerator();

        bool success = true;

        double mergeTime = runTest([&] () {
            FT c(a, b);
            // if(!checkQuery(c, keys, 0, 2*N)) {
            //     std::cerr << "Merge failed" << endl;
            //     success = false;
            //     exit(-1);
            // }
            if(!checkFunctional(c, keys, generator)) {
                std::cerr << "Merge failed" << endl;
                success = false;
                exit(-1);
            }
        });

        if(!success) {
            return std::vector<double>{std::numeric_limits<double>::max()};
        }
        
        return std::vector<double>{mergeTime};
    }

    template<typename FTWrapper>
    static void analyze(Settings s, std::filesystem::path outputFolder, std::vector<std::vector<double>> outputs) {
        double avgInsTime = 0;
        for(auto v: outputs) {
            avgInsTime += v[0] / outputs.size();
        }

        if(!s.maxLoadFactor) {
            std::cerr << "Missing max load factor" << std::endl;
            return;
        }
        double maxLoadFactor = *(s.maxLoadFactor);

        double effectiveN = s.N * s.maxLoadFactor.value();
        std::ofstream fout(outputFolder / (std::to_string(s.N) + ".txt"), std::ios_base::app);
        fout << maxLoadFactor << " " << avgInsTime << " " << (effectiveN / avgInsTime) << std::endl;
    }
};

struct MultithreadedWrapper {
    static constexpr std::string_view name = "MultithreadedBenchmark";

    template<typename FTWrapper>
    static std::vector<double> run(Settings s) {
        size_t numThreads = s.numThreads;
        if(numThreads == 0) {
            std::cerr << "Cannot have 0 threads!!" << std::endl;
            return {};
        }
        if(numThreads > 1 && !FTWrapper::threaded) {
            std::cerr << "Cannot test multiple threads when the filter does not support it!" << std::endl;
            return {};
        }

        if(!s.maxLoadFactor) {
            std::cerr << "Does not have a max load factor!" << std::endl;
            return std::vector<double>{};
        }
        double maxLoadFactor = *(s.maxLoadFactor);

        using FT = typename FTWrapper::type;
        size_t filterSlots = s.N;
        size_t N = static_cast<size_t>(s.N * maxLoadFactor);
        FT filter(filterSlots);

        std::vector<size_t> keys = generateKeys<FT>(filter, N);
        auto threadRanges = splitRange(0, N, numThreads);
        // std::vector<size_t> threadResults(numThreads);
        size_t *threadResults = new size_t[numThreads];

        // std::vector<double> insTimes(numThreads);
        // std::vector<std::thread> threads;
        // for(size_t i = 0; i < numThreads; i++) {
        //     threads.push_back(std::thread([&, i] {
        //         insTimes[i] = runTest([&]() {
        //             threadResults[i] = insertItems<FT>(filter, keys, threadRanges[i], threadRanges[i+1]);
        //         });
        //     }));
        // }
        // for(auto& th: threads) {
        //     th.join();
        // }

        double insertTime = runTest([&]() {
            std::vector<std::thread> threads;
            for(size_t i = 0; i < numThreads; i++) {
                threads.push_back(std::thread([&, i] {
                    threadResults[i] = insertItems<FT>(filter, keys, threadRanges[i], threadRanges[i+1]);
                    // if(!insertItems<FT>(filter, keys, threadRanges[i], threadRanges[i+1])) {
                    //     std::cerr << "FAILED" << std::endl;
                    //     exit(1);
                    // }
                }));
            }
            for(auto& th: threads) {
                th.join();
            }
        });

        for(size_t i=0; i < numThreads; i++) {
            if(!threadResults[i]) {
                std::cerr << "FAILED" << std::endl;
                return std::vector<double>{std::numeric_limits<double>::max()};
            }
        }

        delete threadResults;

        if(!checkQuery(filter, keys, 0, N)) {
            std::cerr << "Multithreaded insertion failed" << std::endl;
            return std::vector<double>{std::numeric_limits<double>::max()};
        }

        // for(auto result: threadResults) {
        //     if(!result) {
        //         std::cerr << "FAILED" << std::endl;
        //         break;
        //     }
        // }

        // double insTime = 0;
        // for(double time: insTimes) {
        //     // insTime += time;
        //     insTime = std::max(time, insTime);
        // }
        // // insTime /= numThreads;
        
        // return std::vector<double>{insTime};
        return std::vector<double>{insertTime};
    }

    template<typename FTWrapper>
    static void analyze(Settings s, std::filesystem::path outputFolder, std::vector<std::vector<double>> outputs) {
        double avgInsTime = 0;
        for(auto v: outputs) {
            avgInsTime += v[0] / outputs.size();
        }

        double effectiveN = s.N * s.maxLoadFactor.value();
        if(!s.maxLoadFactor) {
            std::cerr << "Missing max load factor" << std::endl;
            return;
        }
        double maxLoadFactor = *(s.maxLoadFactor);
        size_t maxLoadFactorPct = std::llround(maxLoadFactor * 100);
        std::cout << maxLoadFactorPct << std::endl;
        outputFolder /= std::to_string(maxLoadFactorPct);
        std::filesystem::create_directories(outputFolder);
        std::ofstream fout(outputFolder / (std::to_string(s.N) + ".txt"), std::ios_base::app);
        fout << s.numThreads << " " << avgInsTime << " " << (effectiveN / avgInsTime) << std::endl;
    }
};


struct BenchmarkWrapper {
    static constexpr std::string_view name = "Benchmark";

    template<typename FTWrapper>
    static std::vector<double> run(Settings s) {
        size_t numThreads = s.numThreads;
        if(numThreads == 0) {
            std::cerr << "Cannot have 0 threads!!" << std::endl;
            return {};
        }
        if(numThreads > 1 && !FTWrapper::threaded) {
            std::cerr << "Cannot test multiple threads when the filter does not support it!" << std::endl;
            return {};
        }

        size_t numTicks = s.loadFactorTicks;
        // size_t N = s.N;
        if(!s.maxLoadFactor) {
            std::cerr << "Does not have a max load factor!" << std::endl;
            return std::vector<double>{};
        }
        double maxLoadFactor = *(s.maxLoadFactor);

        using FT = typename FTWrapper::type;

        // size_t filterSlots = static_cast<size_t>(N / maxLoadFactor);
        size_t filterSlots = s.N;
        size_t N = static_cast<size_t>(s.N * maxLoadFactor);
        // std::cout << filterSlots << ", N: " << N << " " << maxLoadFactor << std::endl;
        FT filter(filterSlots);

        std::vector<size_t> tickRanges = splitRange(0, N, numTicks);
        std::vector<size_t> keys = generateKeys<FT>(filter, N);
        std::vector<size_t> fprKeys = generateKeys<FT>(filter, N);

        std::vector<double> results;
        results.push_back(true); //if insertions succeeded
        results.push_back(true); //if check queries succeeded
        results.push_back(true); //if removals succeeded
        for(size_t i=0; i < numTicks; i++) {
            size_t numFalsePositives;
            // std::cout << tickRanges[i] << "\n";

            // auto threadRanges = splitRange(tickRanges[i], tickRanges[i+1], numThreads);
            // std::vector<size_t> threadResults(numThreads);

            // double insTime = runTest([&]() {
            //     std::vector<std::thread> threads;
            //     for(size_t i = 0; i < numThreads; i++) {
            //         threads.push_back(std::thread([&, i] {threadResults[i] = insertItems<FT>(filter, keys, threadRanges[i], threadRanges[i+1]);}));
            //     }
            //     for(auto& th: threads) {
            //         th.join();
            //     }
            // });

            // for(auto result: threadResults) {
            //     if(!result) {
            //         results[0] = false;
            //     }
            // }
            double insTime = runTest([&]() {
                results[0] = insertItems<FT>(filter, keys, tickRanges[i], tickRanges[i+1]);
            });
            if(!results[0]) {
                std::cerr << "FAILED AT " << i << std::endl;
                break;
            }

            // double successfulQueryTime = runTest([&]() {
            //     std::vector<std::thread> threads;
            //     for(size_t i = 0; i < numThreads; i++) {
            //         threads.push_back(std::thread([&, i] {threadResults[i] = checkQuery<FT>(filter, keys, threadRanges[i], threadRanges[i+1]);}));
            //     }
            //     for(auto& th: threads) {
            //         th.join();
            //     }
            // });
            // for(auto result: threadResults) {
            //     if(!result) {
            //         results[1] = false;
            //     }
            // }
            double successfulQueryTime = runTest([&]() {
                results[1] = checkQuery<FT>(filter, keys, tickRanges[i], tickRanges[i+1]);
            });
            if(!results[1]) {
                std::cerr << "BAD QUERY " << i << std::endl;
                break;
            }

            // double randomQueryTime = runTest([&]() {
            //     std::vector<std::thread> threads;
            //     for(size_t i = 0; i < numThreads; i++) {
            //         threads.push_back(std::thread([&, i] {threadResults[i] = getNumFalsePositives<FT>(filter, keys, threadRanges[i], threadRanges[i+1]);}));
            //     }
            //     for(auto& th: threads) {
            //         th.join();
            //     }
            // });
            
            // size_t numFalsePositives = 0;
            // for(auto res: threadResults) {
            //     numFalsePositives += res;
            // }

            double randomQueryTime = runTest([&]() {
                numFalsePositives = getNumFalsePositives<FT>(filter, fprKeys, tickRanges[i], tickRanges[i+1]);
            });

            double falsePositiveRate = ((double) numFalsePositives) / (tickRanges[i+1] - tickRanges[i]);
            // std::cout << "goo " << numFalsePositives << " " << falsePositiveRate << std::endl;

            results.insert(results.end(), {insTime, successfulQueryTime, randomQueryTime, falsePositiveRate, static_cast<double>(filter.sizeFilter())}); //adding size filter just in case decide to ever make size dynamic
        }

        if constexpr (FTWrapper::canDelete) {
            if (results[0] && results[1]) {
                std::vector<double> removalTimes;
                for(size_t i=0; i < numTicks; i++) {
                    // auto threadRanges = splitRange(tickRanges[i], tickRanges[i+1], numThreads);
                    // std::vector<size_t> threadResults(numThreads);

                    // double removalTime = runTest([&]() {
                    //     std::vector<std::thread> threads;
                    //     for(size_t i = 0; i < numThreads; i++) {
                    //         threads.push_back(std::thread([&, i] {threadResults[i] = removeItems<FT>(filter, keys, threadRanges[i], threadRanges[i+1]);}));
                    //     }
                    //     for(auto& th: threads) {
                    //         th.join();
                    //     }
                    // });

                    // for(auto result: threadResults) {
                    //     if(!result) {
                    //         results[2] = false;
                    //     }
                    // }

                    double removalTime = runTest([&] () {
                        results[2] = removeItems<FT>(filter, keys, tickRanges[i], tickRanges[i+1]);
                    });
                    if(!results[2]) {
                        std::cerr << "BAD REMOVE " << i << std::endl;
                        break;
                    }
                    // results.push_back(removalTime);
                    removalTimes.push_back(removalTime);
                }
                results.insert(results.end(), removalTimes.rbegin(), removalTimes.rend()); //reversing order since we delete from full filter first.
            }
        }
        else {
            results[2] = false;
        }

        return results;
    }

    template<typename FTWrapper>
    static void analyze(Settings s, std::filesystem::path outputFolder, std::vector<std::vector<double>> outputs) {
        // std::cout << "Goooooooogoosu " << name << std::endl;
        bool printDeletes = FTWrapper::canDelete;
        for(const auto& v: outputs) {
            if(v.size() < 3 || (!v[0]) || (!v[1])) {
                std::cerr << "A benchmark of " << FTWrapper::name << " failed!!!" << std::endl;
                return;
            }
            if((!v[2]) && FTWrapper::canDelete) {
                std::cerr << "Warning: " << FTWrapper::name << " failed to delete!" << std::endl;
                printDeletes = false;
            }
        }
        
        std::vector<double> averageInsertTimes(s.loadFactorTicks);
        std::vector<double> averageSuccessfulQueryTimes(s.loadFactorTicks);
        std::vector<double> averageRandomQueryTimes(s.loadFactorTicks);
        std::vector<double> averageFalsePositiveRates(s.loadFactorTicks);
        std::vector<double> averageDeleteTimes(s.loadFactorTicks);
        std::vector<double> averageSizes(s.loadFactorTicks); //again just in case, but def kinda pointless size really should be a constant as none of the filters do dynamic resizing

        for(const auto& v: outputs) {
            try {
                size_t i=3;
                for(size_t j=0; j < s.loadFactorTicks; j++, i+=5) {
                    averageInsertTimes[j] += v.at(i) / outputs.size();
                    averageSuccessfulQueryTimes[j] += v.at(i+1) / outputs.size();
                    averageRandomQueryTimes[j] += v.at(i+2) / outputs.size();
                    averageFalsePositiveRates[j] += v.at(i+3) / outputs.size();
                    averageSizes[j] += v.at(i+4) / outputs.size();
                }
                if (v[2] && FTWrapper::canDelete) {
                    for(size_t j=0; j < s.loadFactorTicks; j++, i++) {
                        averageDeleteTimes[j] += v.at(i) / outputs.size();
                    }
                }
            }
            catch (std::out_of_range const& exc){
                std::cerr << "Some problematic output of " << FTWrapper::name << "; " << exc.what() << std::endl;
                return;
            }
        }

        // std::ofstream fout(outputFolder / (std::to_string(s.N) + ".txt"s));
        outputFolder /= std::to_string(s.N);
        outputFolder /= std::to_string(s.numReplicants);
        outputFolder /= std::to_string(s.numThreads);
        std::filesystem::create_directories(outputFolder);
        std::ofstream fins(outputFolder / "insert.txt");
        std::ofstream fsquery(outputFolder / "squery.txt");
        std::ofstream frquery(outputFolder / "rquery.txt");
        std::ofstream ffpr(outputFolder / "fpr.txt");
        std::ofstream fremoval(outputFolder / "removal.txt");
        std::ofstream fefficiency(outputFolder / "efficiency.txt");
        for(size_t i=0; i < s.loadFactorTicks; i++) {
            double effectiveN = s.N * s.maxLoadFactor.value() / s.loadFactorTicks;
            double loadFactor = s.maxLoadFactor.value() * (i+1) / s.loadFactorTicks;
            double idealSize = std::log2(1.0/averageFalsePositiveRates[i]) * s.N * loadFactor;
            double efficiency = idealSize / (averageSizes[i] * 8); //multiply by 8 to get bits from bytes

            fins << efficiency << " " << (effectiveN / averageInsertTimes[i]) << "\n";
            fsquery << efficiency << " " << (effectiveN / averageSuccessfulQueryTimes[i]) << "\n";
            frquery << efficiency << " " << (effectiveN / averageRandomQueryTimes[i]) << "\n";
            ffpr << efficiency << " " << averageFalsePositiveRates[i] << "\n";
            if (printDeletes) {
                fremoval << efficiency << " " << (effectiveN / averageDeleteTimes[i]) << "\n";
            }
            fefficiency << loadFactor << " " << efficiency << "\n";
        }
    }
};

// struct RandomInsertDeleteWrapper {
    // static constexpr std::string_view name = "RandomInsertDelete";
struct InsertDeleteWrapper {
    static constexpr std::string_view name = "InsertDelete";

    template<typename FTWrapper>
    static std::vector<double> run(Settings s) {
        size_t numTicks = s.loadFactorTicks;
        if(!s.maxLoadFactor) {
            std::cerr << "Does not have a max load factor!" << std::endl;
            return std::vector<double>{};
        }
        double maxLoadFactor = *(s.maxLoadFactor);
        double minLoadFactor = s.minLoadFactor;
        double maxInsertDeleteRatio = s.maxInsertDeleteRatio;

        using FT = typename FTWrapper::type;
        
        size_t filterSlots = s.N;
        size_t minN = static_cast<size_t>(filterSlots * minLoadFactor);
        // std::cout << "minlf: " << minLoadFactor << " " << minN << std::endl;
        size_t maxN = static_cast<size_t>(filterSlots * maxLoadFactor);
        size_t maxInsDeleteKeys = static_cast<size_t>(filterSlots * maxInsertDeleteRatio);

        std::vector<size_t> tickRanges = splitRange(minN, maxN, numTicks-1); //normally represents ranges, but here we represent exact values so that's why numTicks-1
        std::vector<double> results;
        
        //Random test
        for(size_t N: tickRanges) {
            std::cout << N << std::endl;
            FT filter(filterSlots);
            std::vector<size_t> keys = generateKeys<FT>(filter, N);
            insertItems<FT>(filter, keys, 0, N);
            auto generator = createGenerator();
            results.push_back(static_cast<double>(randomInsertDeleteTest(filter, keys, generator, maxInsDeleteKeys)));
        }
        
        //Streaming test
        for(size_t N: tickRanges) {
            std::cout << N << std::endl;
            FT filter(filterSlots);
            std::vector<size_t> keys = generateKeys<FT>(filter, N);
            insertItems<FT>(filter, keys, 0, N);
            auto generator = createGenerator();
            results.push_back(static_cast<double>(streamingInsertDeleteTest(filter, keys, generator, maxInsDeleteKeys)));
        }

        return results;
    }

    template<typename FTWrapper>
    static void analyze(Settings s, std::filesystem::path outputFolder, std::vector<std::vector<double>> outputs) {
        size_t numTicks = s.loadFactorTicks;
        if(!s.maxLoadFactor) {
            std::cerr << "Does not have a max load factor!" << std::endl;
            return;
        }
        double maxLoadFactor = *(s.maxLoadFactor);
        double minLoadFactor = s.minLoadFactor;
        
        std::vector<double> averageRandomFailureRatios(numTicks);
        std::vector<double> averageStreamingFailureRatios(numTicks);
        for(auto v: outputs) {
            size_t i = 0;
            for(size_t j=0; j <= numTicks-1; j++, i++) {
                double Nfailure = v[i];
                double ratio = Nfailure / ((double)s.N);
                averageRandomFailureRatios[j] += ratio / outputs.size();
            }
            for(size_t j=0; j <= numTicks-1; j++, i++) {
                double Nfailure = v[i];
                double ratio = Nfailure / ((double)s.N);
                averageStreamingFailureRatios[j] += ratio / outputs.size();
            }
        }

        std::ofstream frandom(outputFolder / "randomRatios.txt", std::ios_base::app);
        std::ofstream fstreaming(outputFolder / "streamingRatios.txt", std::ios_base::app);
        for(size_t i = 0; i <= numTicks-1; i++) {
            double lf = minLoadFactor + (maxLoadFactor-minLoadFactor) * i / (numTicks-1);
            frandom << lf << " " << averageRandomFailureRatios[i] << "\n";
            fstreaming << lf << " " << averageStreamingFailureRatios[i] << "\n";
        }
    }
};

// struct StreamingInsertDeleteWrapper {
//     static constexpr std::string_view name = "StreamingInsertDelete";

//     template<typename FTWrapper>
//     static std::vector<double> run(Settings s) {
//         std::cout << name << std::endl;
//         return std::vector<double>{};
//     }

//     template<typename FTWrapper>
//     static void analyze(Settings s, std::filesystem::path outputFolder, std::vector<std::vector<double>> outputs) {
//         // std::cout << "Goooooooogoosu " << name << std::endl;
//     }
// };

struct LoadFactorWrapper {
    static constexpr std::string_view name = "LoadFactor";

    template<typename FTWrapper>
    static std::vector<double> run(Settings s) {
        using FT = typename FTWrapper::type;
        size_t filterSlots = s.N;
        FT filter(filterSlots);

        auto generator = createGenerator();
        
        size_t failureN = 0;

        size_t key = generateKey(filter, generator);
        for(; ; failureN++) {
            try {
                if(!filter.insert(key)) {
                    break;
                }
            }
            catch(...) {
                break;
            }
            if(!filter.query(key)) {
                break;
            }
            key = generateKey(filter, generator);
        }

        return std::vector<double>{static_cast<double>(failureN)};
    }

    template<typename FTWrapper>
    static void analyze(Settings s, std::filesystem::path outputFolder, std::vector<std::vector<double>> outputsDoubleVec) {
        std::vector<double> outputs;
        for(auto v: outputsDoubleVec) {
            if(v.size() != 1) {
                std::cerr << "Something wrong with load factor tester output for:\n" << s << std::endl;
                return;
            }
            outputs.push_back(v[0]);
        }
        std::sort(outputs.begin(), outputs.end());
        double medianLF = ((double)outputs[outputs.size() / 2]) / s.N;
        double pct10LF = ((double)outputs[outputs.size() / 10]) / s.N;
        double pct1LF = ((double)outputs[outputs.size() / 100]) / s.N;
        double minLF = ((double)outputs[0]) / s.N;
        std::ofstream fmedian(outputFolder / "medians.txt", std::ios_base::app);
        std::ofstream fpct10(outputFolder / "10pct.txt", std::ios_base::app);
        std::ofstream fpct1(outputFolder / "1pct.txt", std::ios_base::app);
        std::ofstream fmin(outputFolder / "min.txt", std::ios_base::app);
        fmedian << std::log2(s.N) << " " << medianLF << "\n";
        fpct10 << std::log2(s.N) << " " << pct10LF << "\n";
        fpct1 << std::log2(s.N) << " " << pct1LF << "\n";
        fmin << std::log2(s.N) << " " << minLF << "\n";
    }
};



template<typename FTTuple, typename TestWrapperTuple>
struct TemplatedTester{
    static_assert(!std::is_same_v<FTTuple, FTTuple>, "Tester needs two tuples, one for the filter types and one for the test wrappers.");
};


template<template<typename...> typename FTTuple, template <typename...> typename TestWrapperTuple, typename ...FTWrappers, typename ...TestWrappers>
struct TemplatedTester<FTTuple<FTWrappers...>, TestWrapperTuple<TestWrappers...>> {
    private:
        template<typename TestWrapper>
        static auto getRunFuncs() {
            // // std::vector<std::string> FTNames{FTWrappers::name...};
            // std::vector<std::pair<std::string, std::function<std::vector<double>(Settings)>>> s{{std::string{FTWrappers::name}, std::function<std::vector<double>(Settings)>{[](Settings s) -> std::vector<double>{return TestWrapper::template run<FTWrappers>(s);}}}...};
            
            // std::map<std::string, std::function<std::vector<double>(Settings)>> ans;
            // for(auto [x, y]: s) {
            //     ans[x] = y;
            // }
            // return ans;

            return std::map<std::string, std::function<std::vector<double>(Settings)>> {{std::string{FTWrappers::name}, std::function<std::vector<double>(Settings)>{[](Settings s) -> std::vector<double>{return TestWrapper::template run<FTWrappers>(s);}}}...};
        }

        template<typename TestWrapper>
        static auto getAnalyzeFuncs() {
            return std::map<std::string, std::function<void(Settings, std::filesystem::path, std::vector<std::vector<double>>)>>{
                {std::string{FTWrappers::name}, std::function<void(Settings, std::filesystem::path, std::vector<std::vector<double>>)>{[](Settings s, std::filesystem::path p, std::vector<std::vector<double>> d) {return TestWrapper::template analyze<FTWrappers>(s, p, d);}}}...
            };
        }

        // template<typename TestWrapper>
        // std::map<std::string, std::function<std::vector<double>(Settings)>> getAnalyzeFuncs() {
        //     // std::vector<std::string> FTNames{FTWrappers::name...};
        //     return std::map{{FTWrappers::name, [](Settings s) -> {return TestWrapper::run<FTWrappers>;}}...};
        // }

        static auto getTestResults(const char* outputFilepath, std::set<std::string> keywords, std::multiset<Settings> testsToRun) {
            keywords.insert("TestResult");

            auto testsCompleted = readConfig(outputFilepath, keywords);
            std::map<Settings, std::vector<std::vector<double>>> testResults;
            std::map<Settings, size_t> replicantCounts;

            Settings curSetting;
            for(auto [keyword, vals]: testsCompleted) {
                if(TestNames.count(keyword) == 1) {
                    curSetting.TestName = keyword;
                }
                else if(settingTypes.count(keyword) == 1) {
                    curSetting.setval(keyword, vals);
                }
                else if(FTNames.count(keyword) == 1){
                    curSetting.FTName = keyword;
                }
                else if (keyword == "TestResult"s) { ///Potential minor issue---technically could print one testresult within replicants but not others, in which case we print extra testresults at the end (keeping this one and then doing it again)
                    if(testsToRun.count(curSetting) >= 1) {
                        replicantCounts[curSetting]++;
                        if(replicantCounts[curSetting] >= curSetting.numReplicants) {
                            // std::cout << "Removing setting: " << std::endl;
                            // std::cout << curSetting << std::endl;
                            testsToRun.extract(curSetting);
                            testResults[curSetting].push_back(vals);
                            // std::cout << "New testsToRunSize: " << testsToRun.size() << std::endl;
                            replicantCounts[curSetting] = 0;
                        }
                    }
                }
                else {
                    std::cerr << "Bug in the code" << std::endl;
                    exit(-1);
                }
            }
            
            return std::pair{testResults, testsToRun};
        }
        
        inline static const std::set<std::string> settingTypes = Settings::SettingTypes();
        inline static const std::set<std::string> FTNames{std::string{FTWrappers::name}...};
        inline static const std::set<std::string> TestNames{std::string{TestWrappers::name}...};
    public:
    static void runTests(const char* configFilepath, const char* outputFilepath, const char* analysisFolderPath = nullptr, bool rerun = false) {

        std::set<std::string> keywords;
        keywords.insert(FTNames.begin(), FTNames.end());
        keywords.insert(TestNames.begin(), TestNames.end());
        keywords.insert(settingTypes.begin(), settingTypes.end());

        auto config = readConfig(configFilepath, keywords);

        // std::cout << "Read config!" << std::endl;

        std::multiset<Settings> testsToRun;
        {
        CompressedSettings curSettings;
        for(auto [keyword, vals]: config) {
            if(TestNames.count(keyword) == 1) {
                curSettings.TestName = keyword;
            }
            else if(settingTypes.count(keyword) == 1) {
                curSettings.setval(keyword, vals);
            }
            else if(FTNames.count(keyword) == 1){
                // std::cout << "HI" << std::endl;
                curSettings.FTName = keyword;
                auto settingCombos = curSettings.getSettingsCombos();
                for(auto setting: settingCombos) {
                    testsToRun.insert(setting);
                    // std::cout << setting << std::endl;
                }
            }
            else {
                std::cerr << "Bug in the code" << std::endl;
                exit(-1);
            }
        }
        }

        // std::cout << "testsToRun size: " << testsToRun.size() << std::endl;
        
        // auto testsCompleted = readConfig(outputFilepath, keywords);

        auto remainingTestsToRun = getTestResults(outputFilepath, keywords, testsToRun).second;

        std::vector<Settings> testsToRunRandomized(remainingTestsToRun.begin(), remainingTestsToRun.end());
        auto generator = createGenerator();
        std::shuffle(testsToRunRandomized.begin(), testsToRunRandomized.end(), generator);

        std::ofstream outputStream (outputFilepath, std::ofstream::app);

        std::map<std::string, std::map<std::string, std::function<std::vector<double>(Settings)>>> runFuncs{{std::string{TestWrappers::name}, getRunFuncs<TestWrappers>()}...};
        size_t testOn=0;
        std::chrono::time_point<chrono::system_clock> startTime, curTime;
        startTime = std::chrono::system_clock::now();
        for(Settings s: testsToRunRandomized) {
            cout << s;
            std::vector<std::vector<double>> results(s.numReplicants);
            std::vector<std::thread> threads;
            auto funcToRun = runFuncs[s.TestName][s.FTName];
            for(size_t i = 0; i < s.numReplicants; i++) {
                threads.push_back(std::thread([funcToRun, i, &results, s] () {results[i] = funcToRun(s);}));
            }
            for(auto& th: threads) {
                th.join();
            }
            outputStream << s;
            for(const auto& res: results) {
                outputStream << "TestResult ";
                cout << "TestResult ";
                for(const auto& val: res) {
                    outputStream << val << " ";
                    cout << val << " ";
                }
                outputStream << "\n";
                cout << "\n";
            }
            outputStream << std::endl;
            cout << std::endl;
            double portionDone = ((double) (testOn+1)) / testsToRunRandomized.size();
            long long pctDone = std::llround(portionDone * 100);
            cout << "Finished test " << (++testOn) << " of " << testsToRunRandomized.size() <<  " (" << pctDone << "\% completed)" << std::endl;
            curTime = std::chrono::system_clock::now();
            auto elapsed = duration_cast<std::chrono::seconds>(curTime - startTime);
            cout << "Elapsed time: " << std::llround(elapsed.count()) << " seconds. Expected total time: " << std::llround(elapsed.count() / portionDone) << " seconds." << std::endl << std::endl << std::endl;
        }

        if(analysisFolderPath) {

            std::map<std::string, std::map<std::string, std::function<void(Settings, std::filesystem::path, std::vector<std::vector<double>>)>>> analyzeFuncs{{std::string{TestWrappers::name}, getAnalyzeFuncs<TestWrappers>()}...};
            
            auto allResults = getTestResults(outputFilepath, keywords, testsToRun).first;

            for(auto [setting, _]: allResults) {
                auto outputFolder = std::filesystem::path(analysisFolderPath) / setting.TestName / setting.FTName;
                std::filesystem::create_directories(outputFolder);
                std::filesystem::remove_all(outputFolder);
                std::filesystem::create_directory(outputFolder);
            }

            for(auto [setting, results]: allResults) {
                auto outputFolder = std::filesystem::path(analysisFolderPath) / setting.TestName / setting.FTName;
                analyzeFuncs[setting.TestName][setting.FTName](setting, outputFolder, results);
            }
        }
    }
};

using FTTuple = std::tuple<PQF_8_22_Wrapper, PQF_8_22_FRQ_Wrapper, PQF_8_22BB_Wrapper, PQF_8_22BB_FRQ_Wrapper,
        PQF_8_31_Wrapper, PQF_8_31_FRQ_Wrapper, PQF_8_62_Wrapper, PQF_8_62_FRQ_Wrapper,
        PQF_8_53_Wrapper, PQF_8_53_FRQ_Wrapper, PQF_16_36_Wrapper, PQF_16_36_FRQ_Wrapper,
        PQF_8_21_T_Wrapper, PQF_8_21_FRQ_T_Wrapper, PQF_8_52_T_Wrapper, PQF_8_52_FRQ_T_Wrapper,
        PQF_16_35_T_Wrapper, PQF_16_35_FRQ_T_Wrapper,
        PF_TC_Wrapper, 
        PF_CFF12_Wrapper, PF_BBFF_Wrapper,
        TC_Wrapper, CFF12_Wrapper, BBFF_Wrapper, 
        OriginalCF12_Wrapper, OriginalCF16_Wrapper,
        Morton3_12_Wrapper, Morton3_18_Wrapper,
        VQF_Wrapper, VQFT_Wrapper>;

using TestWrapperTuple = std::tuple<BenchmarkWrapper, 
MultithreadedWrapper, 
// RandomInsertDeleteWrapper, StreamingInsertDeleteWrapper, 
InsertDeleteWrapper,
LoadFactorWrapper>;

using PQFTuple = std::tuple<PQF_8_22_Wrapper, PQF_8_22_FRQ_Wrapper, PQF_8_22BB_Wrapper, PQF_8_22BB_FRQ_Wrapper,
        PQF_8_31_Wrapper, PQF_8_31_FRQ_Wrapper, PQF_8_62_Wrapper, PQF_8_62_FRQ_Wrapper,
        PQF_8_53_Wrapper, PQF_8_53_FRQ_Wrapper, PQF_16_36_Wrapper, PQF_16_36_FRQ_Wrapper,
        PQF_8_21_T_Wrapper, PQF_8_21_FRQ_T_Wrapper, PQF_8_52_T_Wrapper, PQF_8_52_FRQ_T_Wrapper,
        PQF_16_35_T_Wrapper, PQF_16_35_FRQ_T_Wrapper>;

using AllTester = TemplatedTester<FTTuple, TestWrapperTuple>;

using MergeTester = TemplatedTester<PQFTuple, std::tuple<MergeWrapper>>;

int main(int argc, char* argv[]) {
    if(argc < 3) {
        std::cerr << "Missing config file" << std::endl;
        exit(-1);
    }

    if(argc == 3) {
        AllTester::runTests(argv[1], argv[2]);
    }
    else if (argc == 4) {
        AllTester::runTests(argv[1], argv[2], argv[3]);
    }
    else if (argc == 5) {
        MergeTester::runTests(argv[1], argv[2], argv[3]);
    }
}