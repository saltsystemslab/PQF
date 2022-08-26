#include <iostream>
#include <cassert>
#include <random>
#include <vector>
#include <optional>

#include "DynamicPrefixFilter.hpp"

using namespace DynamicPrefixFilter;
using namespace std;

int main() {
    random_device rd;
    mt19937 generator (rd());

    size_t N = 1e8;
    DynamicPrefixFilter8Bit pf(N);

    vector<size_t> keys(N);

    uniform_int_distribution<size_t> keyDist(0, -1ull);
    for(size_t i{0}; i < N; i++) {
        if(i % 100000 == 0) cout << i << endl;
        // keys[i] = keyDist(generator);
        // pf.insert(keys[i]);
        pf.insert(keyDist(generator));
    }
    // cout << "Average overflow: " << pf.getAverageOverflow() << endl;

    // for(size_t i{0}; i < N; i++) {
    //     assert(pf.query(keys[i]).first);
    // }

    // double fpr = 0.0;
    // double Nf = 0.0;
    // double bpr = 0.0;
    // double Nb = 0.0;
    // for(size_t i{0}; i < N; i++) {
    //     pair<bool, bool> qres = pf.query(keyDist(generator));
    //     if(qres.second) {
    //         Nb++;
    //         bpr += qres.first;
    //     }
    //     else {
    //         Nf++;
    //         fpr+=qres.first;
    //     }
    // }
    // double rfpr = (fpr+bpr)/N;
    // fpr/=Nf;
    // bpr/=Nb;
    // cout << "False positive rate: " << rfpr << ", " << fpr << ", " << bpr << endl;

}