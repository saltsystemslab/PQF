#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <optional>
#include <chrono>

#include "DynamicPrefixFilter.hpp"

using namespace DynamicPrefixFilter;
using namespace std;

int main(int argc, char* argv[]) {
    random_device rd;
    mt19937 generator (rd());

    // size_t N = 1e8;
    size_t N = (1ull << 26)*90/100;
    if(argc > 1) {
        N = (1ull << atoi(argv[1]));
    }
    if(argc > 2) {
        N = N * atoi(argv[2]) / 100;
    }
    DynamicPrefixFilter8Bit pf(N);
    std::cout << "Realized N: " << N << ", size of filter: " << pf.sizeFilter() << endl;

    vector<size_t> keys(N);
    vector<size_t> FPRkeys(N);
    uniform_int_distribution<size_t> keyDist(0, -1ull);
    for(size_t i{0}; i < N; i++) {
        keys[i] = keyDist(generator) % pf.range;
        FPRkeys[i] = keyDist(generator) % pf.range;
    }

    auto start = chrono::high_resolution_clock::now();    
    for(size_t i{0}; i < N; i++) {
        // if(i % 100000 == 0) cout << i << endl;
        pf.insert(keys[i]);
        // pf.insert(keyDist(generator));
    }
    auto end = chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::microseconds>(end-start);
    double ms = ((double)duration.count())/1000.0;
    cout << ms << " ms for insertion, or " << (ms * 1e6/N) << "ns per insertion" << endl;
    
    // cout << "Average overflow: " << pf.getAverageOverflow() << endl;

    start = chrono::high_resolution_clock::now(); 
    for(size_t i{0}; i < N; i++) {
        // assert(pf.query(keys[i]).first);
        assert(pf.querySimple(keys[i]));
        // pf.querySimple(keys[i]);
        // [[maybe_unused]] uint64_t x = pf.querySimple(keys[i]);
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    ms = ((double)duration.count())/1000.0;
    cout << ms << " ms for positive queries, or " << (ms *1e6 / N) << "ns per query."<< endl;

    start = chrono::high_resolution_clock::now();
    uint64_t fpc = 0;
    for(size_t i{0}; i < N; i++) {
        // pair<bool, bool> qres = pf.query(keyDist(generator) % pf.range);
        // if(qres.second) {
        //     Nb++;
        //     bpr += qres.first;
        // }
        // else {
        //     Nf++;
        //     fpr+=qres.first;
        // }
        [[maybe_unused]] uint64_t qres = pf.querySimple(FPRkeys[i]);
        fpc+=qres;
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    ms = ((double)duration.count())/1000.0;
    cout << ms << " ms for random queries, or " << (ms * 1e6 / N) << "ns per query." << endl;
    cout << "False positive rate: " << (((double)fpc)/N) << endl;

    start = chrono::high_resolution_clock::now();
    fpc = 0.0;
    double Nf = 0.0;
    uint64_t bpc = 0.0;
    double Nb = 0.0;
    for(size_t i{0}; i < N; i++) {
        // pair<bool, bool> qres = pf.query(keyDist(generator) % pf.range);
        // if(qres.second) {
        //     Nb++;
        //     bpr += qres.first;
        // }
        // else {
        //     Nf++;
        //     fpr+=qres.first;
        // }
        [[maybe_unused]] uint64_t qres = pf.query(FPRkeys[i]);
        if(qres & 2) {
            Nb++;
            bpc += qres & 1;
        }
        else {
            Nf++;
            fpc+=qres & 1;
        }
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    ms = ((double)duration.count())/1000.0;
    cout << ms << " ms for random queries" << endl;
    double rfpr = ((double)(fpc+bpc))/N;
    double fpr = fpc / Nf;
    double bpr = bpc / Nb;
    cout << "False positive rate: " << rfpr << ", " << fpr << ", " << bpr << endl;

    cout << "Deleting first half of keys, then querying to see if it works" << endl;
    for(size_t i{0}; i < N/2; i++) {
        // cout << i << endl;
        assert(pf.remove(keys[i]));
        if(N < 10000) {
            for(size_t j{i+1}; j < N; j++) {
                // if(!pf.querySimple(keys[j])) {
                //     cout << "Fail " << (keys[j] & 255) << endl;
                // }
                assert(pf.querySimple(keys[j]));
            }
        }
    }
    for(size_t i{N/2}; i < N; i++) {
        assert(pf.querySimple(keys[i]));
    }
    fpc = 0;
    for(size_t i{0}; i < N; i++) {
        [[maybe_unused]] uint64_t qres = pf.querySimple(FPRkeys[i]);
        fpc+=qres;
    }
    cout << "False positive rate after removal: " << (((double)fpc)/N) << endl;

    cout << "Inserting back a new random half of keys as a simple test" << endl;
    for(size_t i{0}; i < N/2; i++) {
        keys[i] = keyDist(generator) % pf.range;
    }
    for(size_t i{0}; i < N/2; i++) {
        pf.insert(keys[i]);
    }
    cout << "Checking queries" << endl;
    for(size_t i{0}; i < N; i++) {
        assert(pf.querySimple(keys[i]));
    }
    start = chrono::high_resolution_clock::now();
    for(size_t i{0}; i < N; i++) {
        assert(pf.remove(keys[i]));
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    ms = ((double)duration.count())/1000.0;
    cout << ms << "ms for removing all the keys, or " << (ms * 1e6 / N) << "ns per removal" << endl;

}