#pragma once
// General API for writing arbitrage-based simulators for AMM. This
// header contain data structures and primitives for writing
// simulators but no simulator itself.
#include "sim-json.hpp"
#include "sim-factory.hpp"

#include <stdint.h>
#include <stdlib.h>
#include <cassert>
#include <iosfwd>




using u64   = uint64_t;
using money = long double;

class Curve;
class Trade;
class AMMState;
class Fee;

// Price scale in AMM.
struct Prices {
    static constexpr int N = 2;
    money p[N];

    const money& operator[](int i) const { return p[i]; }
    money&       operator[](int i)       { return p[i]; }
};

// Amount of tokens in AMM
struct Tokens {
    static constexpr int N = 2;
    money x[N];

    const money& operator[](int i) const { return x[i]; }
    money&       operator[](int i)       { return x[i]; }
};

// Amount of tokens after conversion by price scale
struct TokensXP {
    TokensXP(const Tokens&, const Prices&);
    TokensXP(const AMMState&);

    static constexpr int N = 2;
    money x[N];

    const money& operator[](int i) const { return x[i]; }
    money&       operator[](int i)       { return x[i]; }
};


// State of AMM. It's fully described by amount of tokens and price
// scale
struct AMMState {
    // Create uninitialized AMM.
    AMMState() = default;
    // Create AMM in equilibrium from invariant D and price scale.
    AMMState(money D, const Prices& p) :
        price(p)
    {
        xs.x[0] = D / 2 / price.p[0];
        xs.x[1] = D / 2 / price.p[1];
    }
    // Create AMM from old state and trade description
    AMMState(const AMMState& old,
             const Trade&    trade);


    Prices price; // Price scale for AMM
    Tokens xs;    // Amount of tokens in AMM
};

// AMM state together with few cached values
struct FullAMMState {
    // Create uninitialized AMM
    FullAMMState() = default;
    // Create AMM from state
    FullAMMState(const AMMState& state,
                 const Curve&    curve);
    // Create AMM from old state and trade
    FullAMMState(const FullAMMState& state,
                 const Trade&        trade,
                 const Curve&        curve);

    AMMState amm;
    money    xcp;    // X[cp]
    money    price;  // Current AMM price

    // Update cached values in place
    void compute(const Curve& curve);
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

money step_for_price_2(
    const AMMState& state0,
    money p_min,
    money p_max,
    money ext_vol,
    const Curve& curve,
    const Fee&   fee_model,
    money gas_fee,
    money dx
    );


// ----------------------------------------------------------------
// -- Pluggable API
// ----------------------------------------------------------------


// Definition of AMM curve
class Curve : public Factory<Curve> {
public:
    virtual ~Curve() = default;
    // Compute value of x[j] for giben x[i].
    virtual money computeY(const AMMState& st, money x, int i, int j) const = 0;
    // Compute relative price for given AMM state. It uses token
    // reduced by price scale.
    virtual money computeP(const AMMState& st) const = 0;
    // Compute relative price for given AMM state. It uses real token
    // prices
    money computePrice(const AMMState& st) const {
        return computeP(st) * st.price.p[1];
    }
    // Compute value of X[cp]
    virtual money computeXcp(const AMMState& st) const = 0;
    // Compute value of invariant D
    virtual money computeD(const AMMState& st) const = 0;
};




class Stableswap : public Curve{
public:
    explicit Stableswap(money _A);
    explicit Stableswap(const JSON::ref& json);
    ~Stableswap() = default;

    virtual money computeY(const AMMState& st, money x, int i, int j) const override;
    virtual money computeP(const AMMState& st)   const override;
    virtual money computeXcp(const AMMState& st) const override;
    virtual money computeD(const AMMState& st)   const override;
public:
    money A;
};

class ConstantProduct : public Curve{
public:
    explicit ConstantProduct();
    explicit ConstantProduct(const JSON::ref& json);
    ~ConstantProduct() = default;

    virtual money computeY(const AMMState& st, money x, int i, int j) const override;
    virtual money computeP(const AMMState& st)   const override;
    virtual money computeXcp(const AMMState& st) const override;
    virtual money computeD(const AMMState& st)   const override;
};


// Interface for computing fee _and_ boost rate. They seems to be
// rather interwined
class Fee: public Factory<Fee> {
public:
    Fee() = default;
    virtual ~Fee();

    // Compute fee for a given state of AMM
    virtual money computeFee(const AMMState& state) const = 0;
    // Compute fee for tentative trade
    money computeTradeFee(const AMMState& state, const Trade& trade) const;
    // Compute local boost rate for donations to a pool
    virtual money localBoostRate(const AMMState& state) const = 0;
};


// Standard fee as used in smart contracts(?)
struct StdFee: public Fee {
    StdFee(money _mid_fee,
           money _out_fee,
           money _fee_gamma,
           money _boost_rate,
           money _boost_mul
        );
    StdFee(const JSON& json);
    virtual ~StdFee();

    virtual money computeFee(const AMMState& state)     const override;
    virtual money localBoostRate(const AMMState& state) const override;

    money mid_fee;
    money out_fee;
    money fee_gamma;
    money boost_rate;
    money boost_mul;
};

// Simple flat fee which useful for testing ideas that don't depend on fee.
struct FlatFee: public Fee {
    FlatFee(money _fee,
            money _boost_rate
        );
    FlatFee(const JSON& json);
    virtual ~FlatFee();

    virtual money computeFee(const AMMState& state)     const override;
    virtual money localBoostRate(const AMMState& state) const override;

    money fee;
    money boost_rate;
};


// Price oracle used by AMM. This struct carry parameter of oracle and
struct PriceOracle {
    struct State {
        void record(u64 t, const Prices& prices);

        money  ma_half_time;
        u64    time;
        Prices price;
    };

    // Initialize oracle with given set of prices and starting time
    State init(u64 t, const Prices& prices) const;

    money ma_half_time;
};



// ----------------------------------------------------------------
// Helper

std::ostream& operator<<(std::ostream&, const Tokens&);
std::ostream& operator<<(std::ostream&, const TokensXP&);
std::ostream& operator<<(std::ostream&, const Prices&);
std::ostream& operator<<(std::ostream&, const AMMState&);
std::ostream& operator<<(std::ostream&, const Trade&);
