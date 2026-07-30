#pragma once
// General API for writing arbitrage-based simulators for AMM This
// header contain data structures and primitives for writing
// simulators but no simulator itself.
#include <stdint.h>
#include <stdlib.h>
#include <cassert>
#include <iosfwd>




using u64   = uint64_t;
using money = long double;

class Curve;
class Trade;


// Price scale in AMM. 
struct Prices {
    static constexpr int N = 2;
    money p[N];

    money operator[](int i) const { return p[i]; }
};

// Amount of tokens in AMM
struct Tokens {
    static constexpr int N = 2;
    money x[N];

    const money& operator[](int i) const { return x[i]; }
    money& operator[](int i) { return x[i]; }
};


// Amount of tokens after conversion by price scale
struct TokensXP {
    static constexpr int N = 2;
    money x[N];

    const money& operator[](int i) const { return x[i]; }
    money& operator[](int i) { return x[i]; }
};

// State of AMM. It's fully described by amount of tokens and price
// scale
struct AMMState {
    AMMState() = default;
    AMMState(money D, const Prices& p) :
        price(p)
    {
        xs.x[0] = D / 2 / price.p[0];
        xs.x[1] = D / 2 / price.p[1];
    }

    void getXP(TokensXP &ret) const {
        for (int i = 0; i < 2; i++) {
            ret[i] = xs[i] * price[i];
            assert(xs[i] > 0);
        }
    }
    
    AMMState applyTrade(const Trade& trade) const;

    Prices price; // Price scale for AMM
    Tokens xs;    // Amount of tokens in AMM
};

// AMM state with some values cached
struct FullAMMState {
    AMMState amm;
    money    xcp;
    money    price;

    void compute(const Curve& curve);

    FullAMMState applyTrade(const Trade& trade, const Curve& curve) const;
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
        return p_2(st) * st.price.p[1];
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

    Trade() = default;

    // Construct trade on a curve.
    Trade(Trade::Dir      trade,  // Whether amount is begin bought or sold by AMM
          money           amount, // Token amount
          int             ibuy,   // Index of token being bought
          int             isell,  // Index of token being sold
          const AMMState& state,  // Initial state of AMM
          const Curve&    curve   // Curve description
        );

    // Apply fee.
    Trade applyFee(money fee) const;
    //
    money amountFor(int i) const;


    money buy;    // Amount of tokens AMM buys
    money sell;   // Amount of tokens AMM sells
    int   i_buy;  // Index of bought token
    int   i_sell; // Index of sold token
};

std::ostream& operator<<(std::ostream&, const Tokens&);
std::ostream& operator<<(std::ostream&, const Prices&);
std::ostream& operator<<(std::ostream&, const AMMState&);
std::ostream& operator<<(std::ostream&, const Trade&);
