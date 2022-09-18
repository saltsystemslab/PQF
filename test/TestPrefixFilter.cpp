#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <optional>
#include <chrono>

#include "DynamicPrefixFilter.hpp"

using namespace DynamicPrefixFilter;
using namespace std;

int main() {
    random_device rd;
    mt19937 generator (rd());

    size_t N = 1e8;
    DynamicPrefixFilter8Bit pf(N);

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
    cout << ms << " ms for insertion" << endl;
    
    // cout << "Average overflow: " << pf.getAverageOverflow() << endl;

    start = chrono::high_resolution_clock::now(); 
    for(size_t i{0}; i < N; i++) {
        // assert(pf.query(keys[i]).first);
        // assert(pf.querySimple(keys[i]));
        // pf.querySimple(keys[i]);
        [[maybe_unused]] uint64_t x = pf.query(keys[i]);
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    ms = ((double)duration.count())/1000.0;
    cout << ms << " ms for positive queries" << endl;

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
        fpc+=qres & 1;
    }
    end = chrono::high_resolution_clock::now();
    duration = chrono::duration_cast<chrono::microseconds>(end-start);
    ms = ((double)duration.count())/1000.0;
    cout << ms << " ms for random queries" << endl;
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

}