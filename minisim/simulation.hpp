#pragma once

#include <stdint.h>
#include <stdlib.h>

using u64   = uint64_t;
using money = long double;


// Price scale in AMM
struct Prices {
    money px;
    money py;

    money operator[](int i) const {
        if( 0 == i ) return px;
        if( 1 == i ) return py;
        abort();
    }
};

// Amount of tokens in AMM
struct Tokens {
    money x;
    money y;

    const money& operator[](int i) const {
        if( 0 == i ) return x;
        if( 1 == i ) return y;
        abort();
    }
    money& operator[](int i) {
        if( 0 == i ) return x;
        if( 1 == i ) return y;
        abort();
    }
};


// Amount of tokens after conversion by price scale
struct TokensXP {
    money x;
    money y;

    const money& operator[](int i) const {
        if( 0 == i ) return x;
        if( 1 == i ) return y;
        abort();
    }
    money& operator[](int i) {
        if( 0 == i ) return x;
        if( 1 == i ) return y;
        abort();
    }
};

// State of AMM. It's fully described by amount of tokens and price
// scale
struct AMMState {
    Prices price;
    Tokens xs;
};


