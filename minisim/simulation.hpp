#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <cassert>
#include <iosfwd>


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

class Curve;

// State of AMM. It's fully described by amount of tokens and price
// scale
struct AMMState {
    AMMState() = default; // Hack
    AMMState(money D, const Prices& p) :
        price(p)
    {
        xs.x = D / 2 / price.px;
        xs.y = D / 2 / price.py;
    }

    void getXP(TokensXP &ret) const {
        for (int i = 0; i < 2; i++) {
            ret[i] = xs[i] * price[i];
            assert(xs[i] > 0);
        }
    }
    
    Prices price; // Price scale for AMM
    Tokens xs;    // Amount of tokens in AMM
};

// AMM state with some values cached
struct FullAMMState {
    AMMState amm;
    money    xcp;
    money    price;

    void compute(const Curve& curve);
};


// Definition of curve
class Curve {
public:
    Curve(money _A, money _gamma):
        A(_A), gamma(_gamma)
    {}


    money y_2(const AMMState& st, money x, int i, int j) const;
    money p_2(const AMMState& st) const;
    money price_2(const AMMState& st) const {
        return p_2(st) * st.price.py;
    }

    money get_xcp_2(const AMMState& st) const;
private:
    money D_2(const AMMState& st) const;
public:
    money A;
    money gamma;
};

// Single trade peformed by pool.
struct Trade {
    enum Dir { BUY, SELL };

    // Trade() = default;

    // Construct trade on a curve.
    Trade(Trade::Dir      trade,  // Whether amount is begin bought or sold by AMM
          money           amount, // Token amount
          int             ibuy,   // Index of token being bought
          int             isell,  // Index of token being sold
          const AMMState& state,  // Initial state of AMM
          const Curve&    curve   // Curve description
        );

    money buy;    // Amount of tokens AMM buys
    money sell;   // Amount of tokens AMM sells
    int   i_buy;  // Index of bought token
    int   i_sell; // Index of sold token
};

std::ostream& operator<<(std::ostream&, const Tokens&);
std::ostream& operator<<(std::ostream&, const Prices&);
std::ostream& operator<<(std::ostream&, const AMMState&);
