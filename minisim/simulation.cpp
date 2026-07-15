
#include "simulation.hpp"
#include <stdexcept>
#include <cmath>

static inline money mabs(money val) noexcept {
    return val >= 0 ? val : -val;
}

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



money Curve::D_2(const AMMState& st) const {
    TokensXP xp;
    st.getXP(xp);
    auto ret = solve_D(A, gamma, xp);
    return ret;
}

money Curve::y_2(const AMMState& st, money x, int i, int j) const {
    TokensXP xp;
    st.getXP(xp);
    xp[i] = x * st.price[i];
    auto yp = solve_x(A, gamma, xp, this->D_2(st), j);
    auto ret = yp / st.price[j];
    return ret;
}

money Curve::p_2(const AMMState& st) const {
    TokensXP xp;
    st.getXP(xp);
    auto p = get_p_2(xp, this->D_2(st), this->A, this->gamma);
    return p;
}

money Curve::get_xcp_2(const AMMState& st) const {
    // First calculate the ideal balance
    //  Then calculate, what the constant-product would be
    auto D = D_2(st);
    money X[2];
    for (size_t i = 0; i < 2; i++) {
        X[i] = D  / (2 * st.price[i]);
    }
    return geometric_mean_2(X);
}

void FullAMMState::compute(const Curve& curve) {
    xcp = curve.get_xcp_2(amm);
}
