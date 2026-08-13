
#include "simulation.hpp"
#include <stdexcept>
#include <cmath>
#include <iostream>


TokensXP::TokensXP(const Tokens& xs, const Prices& price) {
    for (int i = 0; i < 2; i++) {
        this->x[i] = xs[i] * price[i];
        assert(xs[i] > 0);
    }
}

TokensXP::TokensXP(const AMMState& st) :
    TokensXP(st.xs, st.price)
{}

static inline money mabs(money val) noexcept {
    return val >= 0 ? val : -val;
}

// ----------------------------------------------------------------
// -- Curve
// ----------------------------------------------------------------

static money geometric_mean_2(money const *x) {
    return sqrtl(x[0] * x[1]);
}
static money geometric_mean_2(const TokensXP &x) {
    return sqrtl(x[0] * x[1]);
}


static money newton_D_2(money A, money gamma, const TokensXP &xx, money D0) {
    // ***
    // This now uses stableswap invariant (because invariants are pluggable)
    // ***
    money S = 0;
    const size_t N = 2;
    money x[2];
    for (size_t i = 0; i < N; i++) {
        S += x[i] = xx[i];
    }
    money D = D0;
    A *= N;  // A is already A * N**(N-1)

    for (int i=0; i < 255; i++) {
        money D_P = D;
        for (auto const &_x: x) {
            D_P = D_P * D / (N * _x);
        }
        money Dprev = D;
        D = (A * S + D_P * N) * D / ((A - 1) * D + (N + 1) * D_P);
        if (mabs(D - Dprev) <= 1e-6) {
            return D;
        }
    }
    return D; // we ignore convergence error in simulation
    throw std::logic_error("Newton_D: did not converge");
}

static money newton_y(money A, money gamma, const TokensXP& x, money D, int i) {
    // ***
    // This now uses stableswap invariant (because invariants are pluggable)
    // ***
    constexpr int N = 2;
    A = A * N;
    int other = 1 - i;
    money c = D*D / (x[other] * N);
    c = c * D / (A * N);
    money b = x[other] + D / A - D;
    money y_prev = 0;
    money y = D;
    for (size_t k = 0; k < 255; k++) {
        y_prev = y;
        y = (y*y + c) / (2 * y + b);
        if (mabs(y - y_prev) <= 1e-12L) {
            return y;
        }
    }
    return y;  // XXX
    throw std::logic_error("Did not converge");
}

static money get_p_2(const TokensXP& x, money D, money A, money gamma) {
    money ANN = A * 2.;
    money Dr = D / 4.;
    for (size_t idx = 0; idx < 2; ++idx) {
        Dr = Dr * D / x[idx];
    }
    money xp0_A = ANN * x[0];
    return
        (xp0_A + Dr * x[0] / x[1]) / (xp0_A + Dr);
}

static money solve_x(money A, money gamma, const TokensXP& x, money D, int i) {
    return newton_y(A, gamma, x, D, i);
}

static money solve_D(money A, money gamma, const TokensXP &x) {
    auto D0 = 2 * geometric_mean_2(x); //  # <- fuzz to make sure it's ok XXX
    return newton_D_2(A, gamma, x, D0);
}


Stableswap::Stableswap(money _A, money _gamma) :
    A(_A),
    gamma(_gamma)
{}
Stableswap::Stableswap(const JSON& json) :
    A(json["A"]),
    gamma(json["gamma"])
{}
    

money Stableswap::computeD(const AMMState& st) const {
    TokensXP xp(st);
    auto ret = solve_D(A, gamma, xp);
    return ret;
}

money Stableswap::computeY(const AMMState& st, money x, int i, int j) const {
    TokensXP xp(st);
    xp[i] = x * st.price[i];
    auto yp = solve_x(A, gamma, xp, computeD(st), j);
    auto ret = yp / st.price[j];
    return ret;
}

money Stableswap::computeP(const AMMState& st) const {
    TokensXP xp(st);
    auto p = get_p_2(xp, computeD(st), this->A, this->gamma);
    return p;
}

money Stableswap::computeXcp(const AMMState& st) const {
    // First calculate the ideal balance
    //  Then calculate, what the constant-product would be
    auto D = computeD(st);
    money X[2];
    for (size_t i = 0; i < 2; i++) {
        X[i] = D  / (2 * st.price[i]);
    }
    return geometric_mean_2(X);
}


AMMState::AMMState(const AMMState& old,
                   const Trade&    trade)
{
    *this = old;
    xs[trade.i_buy]  += trade.buy;
    xs[trade.i_sell] -= trade.sell;
}

void FullAMMState::compute(const Curve& curve) {
    xcp   = curve.computeXcp(amm);
    price = curve.computePrice(amm);
}

FullAMMState::FullAMMState(const AMMState &state,
                           const Curve    &curve) :
    amm(state)
{
    compute(curve);
}

FullAMMState::FullAMMState(const FullAMMState &state,
                           const Trade        &trade,
                           const Curve        &curve) :
    amm(state.amm, trade)
{
    compute(curve);
}



Trade::Trade(Trade::Dir      trade,
             money           amount,
             int             ibuy,
             int             isell,
             const AMMState& amm,
             const Curve&    curve
    )
{
    i_buy  = ibuy;
    i_sell = isell;
    if( trade == Trade::BUY ) {
        buy  = amount;
        sell = amm.xs[i_sell] - curve.computeY(amm, amm.xs[i_buy] + buy, i_buy, i_sell);
    } else {
        sell = -amount;
        buy  = amm.xs[i_buy] - curve.computeY(amm, amm.xs[i_sell] + sell, i_sell, i_buy);
    }
}

Trade Trade::applyFee(money fee) const {
    Trade t(*this);
    t.sell *= 1 - fee;
    return t;
}

money Trade::amountFor(int i) const {
    if( i_buy == i )
        return buy;
    if( i_sell == i )
        return sell;
    return 0;
}


Fee::Fee(money _mid_fee,
         money _out_fee,
         money _fee_gamma,
         money _boost_rate,
         money _boost_mul
    ) :
    mid_fee(_mid_fee),
    out_fee(_out_fee),
    fee_gamma(_fee_gamma),
    boost_rate(_boost_rate / (86400L * 365L)),
    boost_mul(_boost_mul)
{}
Fee::Fee(const JSON& json) :
    mid_fee(json["mid_fee"]),
    out_fee(json["out_fee"]),
    fee_gamma(json["fee_gamma"]),
    boost_rate((double)json["boost_rate"] / (86400L * 365L)),
    boost_mul(json["boost_mul"])
{}

static money reduction_coefficient_2(const TokensXP &x, money gamma) {
    money K = 1.L;
    money S = 0.L;
    for (size_t i = 0; i < 2; i++) S += x[i]; // = sum(x)
    for (size_t i = 0; i < 2; i++)  {
        K *= 2 * x[i] / S;
    }
    if (gamma > 0) {
        K = gamma * K / (gamma * K + 1.L - K);
    }
    return K;
}

money Fee::computeFee(const AMMState& state) const {
    TokensXP xp(state);
    auto f = reduction_coefficient_2(xp, fee_gamma);
    return (mid_fee * f + out_fee * (1.L - f));
}

money Fee::computeFee(const AMMState& state, const Trade& trade) const {
    AMMState st(state, trade);
    return computeFee(st);
}

money Fee::localBoostRate(const AMMState& state) const {
    auto local_boost_rate = boost_rate;
    if (mid_fee < out_fee) {
        local_boost_rate *= 1 + (computeFee(state) - mid_fee) / (out_fee - mid_fee) * (boost_mul - 1);
    }
    return local_boost_rate;
}



PriceOracle::State PriceOracle::init(u64 t, const Prices& prices) const {
    PriceOracle::State o;
    o.ma_half_time = ma_half_time;
    o.time         = t;
    o.price        = prices;
    return o;
}

void PriceOracle::State::record(u64 t, const Prices& trade_price) {
    if (t > time) {
        money alpha = powl(0.5, ((money)(t - time) / ma_half_time));
        alpha = std::min(alpha, 1.L);
        const size_t k = 1;
        price.p[1] = trade_price[k] * (1 - alpha) + price.p[1] * alpha;
        time = t;
    }
}

// ----------------------------------------------------------------
// Factories
// ----------------------------------------------------------------

static std::vector<Curve* (*)(const JSON::ref&)> maker_function;

Curve* makeCurve(const JSON& json) {
    return makeCurve(json.as_ref());
}
Curve* makeCurve(const JSON::ref& json) {
    for(auto f: maker_function) {
        Curve *c = (*f)(json);
        if( c ) {
            return c;
        }
    }
    return 0;
}
void register_curve_factory(Curve* (*fun)(const JSON::ref&)) {
    maker_function.push_back(fun);
}


static Curve* make_stableswap(const JSON::ref& json) {
    return new Stableswap(json);
}

namespace {
    struct Init {
        Init() {
            register_curve_factory(make_stableswap);
        }
    };
    Init _ini;
}


std::ostream& operator<<(std::ostream& o, const Tokens& tok) {
    o << '[' << tok[0] << ", " << tok[1] << ']';
    return o;
}
std::ostream& operator<<(std::ostream& o, const Prices& p) {
    o << '[' << p[0] << ", " << p[1] << ']';
    return o;
}

std::ostream& operator<<(std::ostream& o, const AMMState& amm) {
    o << "AMM{p="<<amm.price<< ", x="<<amm.xs<<"}";
    return o;
}
std::ostream& operator<<(std::ostream& o, const Trade& t) {
    o << "Trade{buy="<<t.buy<< ", sell="<<t.sell<<", i_buy="<<t.i_buy<<", i_sell="<<t.i_sell<<"}";
    return o;
}
