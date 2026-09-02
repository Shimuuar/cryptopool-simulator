
#include "simulation.hpp"
#include <stdexcept>
#include <cmath>
#include <map>
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


static money newton_D_2(money A, const TokensXP &xx, money D0) {
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

static money newton_y(money A, const TokensXP& x, money D, int i) {
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

static money get_p_2(const TokensXP& x, money D, money A) {
    money ANN = A * 2.;
    money Dr = D / 4.;
    for (size_t idx = 0; idx < 2; ++idx) {
        Dr = Dr * D / x[idx];
    }
    money xp0_A = ANN * x[0];
    return
        (xp0_A + Dr * x[0] / x[1]) / (xp0_A + Dr);
}

static money solve_x(money A, const TokensXP& x, money D, int i) {
    return newton_y(A, x, D, i);
}

static money solve_D(money A, const TokensXP &x) {
    auto D0 = 2 * geometric_mean_2(x); //  # <- fuzz to make sure it's ok XXX
    return newton_D_2(A, x, D0);
}


Stableswap::Stableswap(money _A) :
    A(_A)
{}
Stableswap::Stableswap(const JSON::ref& json) :
    A(json["A"])
{}
static Factory<Curve>::Register<Stableswap> reg_stableswap("stableswap");

money Stableswap::computeD(const AMMState& st) const {
    TokensXP xp(st);
    auto ret = solve_D(A, xp);
    return ret;
}

money Stableswap::computeY(const AMMState& st, money x, int i, int j) const {
    TokensXP xp(st);
    xp[i] = x * st.price[i];
    auto yp = solve_x(A, xp, computeD(st), j);
    auto ret = yp / st.price[j];
    return ret;
}

void Stableswap::computeXforP(const AMMState& st, money P, TokensXP& x) const {
    x[0] = 10;
    x[1] = 10;
}

money Stableswap::computeP(const AMMState& st) const {
    TokensXP xp(st);
    auto p = get_p_2(xp, computeD(st), this->A);
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



// ----------------------------------------------------------------
// -- Curve: constant product
// ----------------------------------------------------------------

ConstantProduct::ConstantProduct() = default;
ConstantProduct::ConstantProduct(const JSON::ref& json) {}
static Factory<Curve>::Register<ConstantProduct> reg_constant_product("constant_product");

money ConstantProduct::computeY(const AMMState& st, money x, int i, int j) const {
    const money inv = st.xs[0] * st.xs[1];
    return inv / x;
}

void ConstantProduct::computeXforP(const AMMState& st, money P, TokensXP& x) const {
    money D     = sqrtl(st.xs[0] * st.xs[1]); // Not quite invariant
    money sqrtP = sqrtl(P * st.price[1]);
    x[0] = D * sqrtP;
    x[1] = D / sqrtP;
}

money ConstantProduct::computeP(const AMMState& st) const {
    return st.xs[0] / st.xs[1] / st.price[1];
}

money ConstantProduct::computeXcp(const AMMState& st) const {
    return sqrtl(4 * st.xs[0] * st.xs[1]) / 2;
}
money ConstantProduct::computeD(const AMMState& st) const {
    return sqrtl(4 * st.xs[0] * st.xs[1]);
}


// ----------------------------------------------------------------
// -- AMM state
// ----------------------------------------------------------------

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


// ----------------------------------------------------------------
// -- Trade parameters
// ----------------------------------------------------------------

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

money step_for_price_2(
    const AMMState& state0,
    money p_min,
    money p_max,
    money ext_vol,
    const Curve& curve,
    const Fee&   fee_model,
    money gas_fee,
    money dx
    )
{
    AMMState state = state0;
    Tokens x0 = state.xs;
    money _dx = 0;
    money _dy = 0;
    money x = 0;
    money y = 0;
    money price = 0;
    int _from = 0;
    int _to   = 1;
    if (p_min > 0) {
        _from = 1;
        _to = 0;
    }
    auto step0 = dx / state.price[_from];  // step in units of currency being sold
    auto step = step0;
    money gas = gas_fee / state.price[_from];

    money previous_profit = 0;

    // + (step increases)
    while (true) {
        auto _dx_prev = _dx;
        auto _dy_prev = _dy;

        _dx += step;

        // buy  -> x: first, y: second
        // sell -> x: second, y: first

        x = x0[_from] + _dx;
        y = curve.computeY(state, x, _from, _to);

        state.xs[_from] = x;
        state.xs[_to] = y;
        auto fee_mul = 1.L - fee_model.computeFee(state);

        _dy = (x0[_to] - y) * fee_mul;
        state.xs[_to] = x0[_to] - _dy;

        // price in units d_first / d_second
        if (_from == 0) {
            price = _dx / _dy;
        } else {
            price = _dy / _dx;
        }
        auto v = _dy * state.price[_to];

        state.xs = x0;  // restore the state

        // _from == p.first - buy
        // _from != p.first - sell
        money new_profit;
        if (_from == 0)
            new_profit = (_dx / price - _dx / p_max) * p_max;
        else
            new_profit = (price - p_min) * _dx;

        // printf("*** price=%Lf, min=%Lf, max=%Lf, _dx=%Le, new_p=%Lf, pr_p=%Lf\n", price, p_min, p_max, _dx, new_profit, previous_profit);

        if (new_profit > previous_profit and v <= ext_vol / 2.L) {
            previous_profit = new_profit;
        } else {
            _dx = _dx_prev;
            _dy = _dy_prev;
            break;
        }
        step += step;
    }

    // - (step decreases)
    while (true) {
        auto _dx_prev = _dx;
        auto _dy_prev = _dy;
        if (step < 0) step = -step;
        step /= 2;

        if (step < step0) {
            break;
        }

        for (int ctr=0;ctr<2;ctr++) {
            step = -step;
            _dx = _dx_prev + step;

            x = x0[_from] + _dx;
            y = curve.computeY(state, x, _from, _to);

            state.xs[_from] = x;
            state.xs[_to] = y;
            auto fee_mul = 1.L - fee_model.computeFee(state);

            _dy = (x0[_to] - y) * fee_mul;
            state.xs[_to] = x0[_to] - _dy;

            if (_from == 0) {
                price = _dx / _dy;
            }
            else {
                price = _dy / _dx;
            }
            auto v =  _dy * state.price[_to];
            state.xs = x0;  // restore the state

            // _from == p.first - buy
            // _from != p.first - sell
            money new_profit;
            if (_from == 0)
                new_profit = (_dx / price - _dx / p_max) * p_max;
            else
                new_profit = (price - p_min) * _dx;

            if (new_profit > previous_profit and v <= ext_vol / 2.L) {
                previous_profit = new_profit;
                break;
            } else {
                _dx = _dx_prev;
                _dy = _dy_prev;
            }
        }
    }

    if (_from == 0) {
        price = (_dx + gas) / _dy;  // need to buy higher than without gas
        previous_profit = (_dx / price - _dx / p_max) * p_max;
    }
    else {
        price = _dy / (_dx + gas); // need to sell lower than without gas
        previous_profit = (price - p_min) * _dx;
    }

    if (previous_profit <= 0) _dx = 0;
    return _dx;
}


// ----------------------------------------------------------------
// -- Fee model
// ----------------------------------------------------------------

money Fee::computeTradeFee(const AMMState& state, const Trade& trade) const {
    AMMState st(state, trade);
    return computeFee(st);
}

Fee::~Fee() {}

StdFee::StdFee(money _mid_fee,
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
StdFee::StdFee(const JSON& json) :
    mid_fee(json["mid_fee"]),
    out_fee(json["out_fee"]),
    fee_gamma(json["fee_gamma"]),
    boost_rate((double)json["boost_rate"] / (86400L * 365L)),
    boost_mul(json["boost_mul"])
{}
StdFee::~StdFee() {}
static Factory<Fee>::Register<StdFee> reg_std_fee("std_fee");

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

money StdFee::computeFee(const AMMState& state) const {
    TokensXP xp(state);
    auto f = reduction_coefficient_2(xp, fee_gamma);
    return (mid_fee * f + out_fee * (1.L - f));
}

money StdFee::localBoostRate(const AMMState& state) const {
    auto local_boost_rate = boost_rate;
    if (mid_fee < out_fee) {
        local_boost_rate *= 1 + (computeFee(state) - mid_fee) / (out_fee - mid_fee) * (boost_mul - 1);
    }
    return local_boost_rate;
}


FlatFee::FlatFee(money _fee,
                 money _boost_rate
    ) :
    fee(_fee),
    boost_rate(_boost_rate / (86400L * 365L))
{}
FlatFee::FlatFee(const JSON& json) :
    fee(json["fee"]),
    boost_rate((double)json["boost_rate"] / (86400L * 365L))
{}
FlatFee::~FlatFee() {}
static Factory<Fee>::Register<FlatFee> reg_flat_fee("flat_fee");

money FlatFee::computeFee(const AMMState& state) const {
    return fee;
}
money FlatFee::localBoostRate(const AMMState& state) const {
    return boost_rate;
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
// Helpers
// ----------------------------------------------------------------

std::ostream& operator<<(std::ostream& o, const Tokens& tok) {
    o << '[' << tok[0] << ", " << tok[1] << ']';
    return o;
}
std::ostream& operator<<(std::ostream& o, const TokensXP& tok) {
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
