// Test suite uses Google Test (main() is provided by gtest_main).
#include <gtest/gtest.h>
#include "simulation.hpp"


class CurveTest : public ::testing::TestWithParam<Curve*> {};


// Number on logarifmic grid from 1/100 to 100. Used in tests
static money logspace_100[] = {
    1.00000000e-02, 1.20679264e-02, 1.45634848e-02, 1.75751062e-02,
    2.12095089e-02, 2.55954792e-02, 3.08884360e-02, 3.72759372e-02,
    4.49843267e-02, 5.42867544e-02, 6.55128557e-02, 7.90604321e-02,
    9.54095476e-02, 1.15139540e-01, 1.38949549e-01, 1.67683294e-01,
    2.02358965e-01, 2.44205309e-01, 2.94705170e-01, 3.55648031e-01,
    4.29193426e-01, 5.17947468e-01, 6.25055193e-01, 7.54312006e-01,
    9.10298178e-01, 1.09854114e+00, 1.32571137e+00, 1.59985872e+00,
    1.93069773e+00, 2.32995181e+00, 2.81176870e+00, 3.39322177e+00,
    4.09491506e+00, 4.94171336e+00, 5.96362332e+00, 7.19685673e+00,
    8.68511374e+00, 1.04811313e+01, 1.26485522e+01, 1.52641797e+01,
    1.84206997e+01, 2.22299648e+01, 2.68269580e+01, 3.23745754e+01,
    3.90693994e+01, 4.71486636e+01, 5.68986603e+01, 6.86648845e+01,
    8.28642773e+01, 1.00000000e+02
};



static money finiteDiffPrice(const Curve& curve, const AMMState& st, const money eps) {
    const money h = eps * st.xs[0];
    AMMState st_lo = st;
    st_lo.xs[0] -= h;
    st_lo.xs[1]  = curve.computeY(st, st_lo.xs[0], 0, 1);
    AMMState st_hi = st;
    st_hi.xs[0] += h;
    st_hi.xs[1]  = curve.computeY(st, st_hi.xs[0], 0, 1);
    return (2*h) / (st_lo.xs[1] - st_hi.xs[1]);
}


// Test that we correctly solve y(D,x), e.g. on-curve trade.
TEST_P(CurveTest, OnCurveTrade) {
    const Curve& curve = *GetParam();
    // Initial state
    const AMMState st0(1e6, Prices({1,100}));
    const money    D0 = curve.computeD(st0);
    const int n1 = 0;
    const int n2 = 1;
    // Solve y(x,D) and check that invariant is preserved
    for(auto scale: logspace_100) {
        AMMState st1 = st0;
        st1.xs[n1] *= scale;
        st1.xs[n2] =  curve.computeY(st0, st1.xs[n1], n1, n2);
        const money D1 = curve.computeD(st1);
        EXPECT_NEAR(D0, D1, 1e-12L * D0);
    }
    // Solve x(y,D) and check that invariant is preserved
    for(auto scale: logspace_100) {
        AMMState st1 = st0;
        st1.xs[n2] *= scale;
        st1.xs[n1] =  curve.computeY(st0, st1.xs[n2], n2, n1);
        const money D1 = curve.computeD(st1);
        EXPECT_NEAR(D0, D1, 1e-12L * D0);
    }
}

// X[cp] is correctly related to D
TEST_P(CurveTest, Xcp) {
    const Curve& curve = *GetParam();
    //
    const AMMState st_xcp(1e6, Prices({0.1, 10}));
    const money D   = curve.computeD(st_xcp);
    const money Xcp = curve.computeXcp(st_xcp);
    EXPECT_NEAR(D, 2*Xcp, 1e-12L * D);
}

// Check that price is estimated correctly using finite differences
TEST_P(CurveTest, Price) {
    const Curve& curve = *GetParam();
    // In-equilibrium
    {
        const AMMState st(1e6, Prices({1,100}));
        const money p = curve.computeP(st);
        EXPECT_NEAR(p, 1, 1e-12) << "In equilibrium";
    }
    // Out-of equilibrium
    {
        AMMState st;
        st.xs    = {1e6, 1.5e6};
        st.price = {1,1};
        const money p = curve.computeP(st);
        EXPECT_NEAR(p, finiteDiffPrice(curve, st, 1e-6), 1e-9) << "Out of equilibrium 1";
    }
    {
        AMMState st;
        st.xs    = {3e6, 1.5e6};
        st.price = {1,1};
        const money p = curve.computeP(st);
        EXPECT_NEAR(p, finiteDiffPrice(curve, st, 1e-6), 1e-9) << "Out of equilibrium 2";
    }
    {
        AMMState st;
        st.xs    = {1e6, 1e6};
        st.price = {1,2};
        const money p = curve.computePrice(st);
        EXPECT_NEAR(p, finiteDiffPrice(curve, st, 1e-6), 1e-9) << "With price scale";
    }
}

// Check that computation of state at given price is correct
TEST_P(CurveTest, XForPrice) {
    const Curve& curve = *GetParam();
    //
    auto test_price = [&](const AMMState& st0, money P) {
        TokensXP x;
        curve.computeXforP(st0, P, x);
        AMMState st = st0;
        st.xs[0] = x[0] / st.price[0];
        st.xs[1] = x[1] / st.price[1];
        money D0 = curve.computeD(st0);
        money D  = curve.computeD(st);
        EXPECT_NEAR(D0, D, 1e-12*D0)
            << "D is conserved" << std::endl
            << "st0 = " << st0 << std::endl
            << "st  = " << st  << std::endl
            << "P = " << P;
        EXPECT_NEAR(curve.computeP(st), P, 1e-12*P)
            << "P is correct" << std::endl
            << "st0 = " << st0 << std::endl
            << "st  = " << st  << std::endl
            << "P = " << P;
    };
    // Trivial price scale
    const AMMState st1(1e6, Prices({1, 1} ));
    for(auto price: logspace_100) {
        test_price(st1, price);
    }
    // Nontrivial price scale
    const AMMState st2(1e6, Prices({1, 10}));
    for(auto price: logspace_100) {
        test_price(st2, price);
    }
}

// When we solve for price with zero fee computeXforPFee works
// identically to computeXforP
TEST_P(CurveTest, XForPrice_ZeroFee) {
    const Curve& curve = *GetParam();
    //
    FlatFee zero_fee(0, 0);
    auto test_price = [&](const AMMState& st0, money P) {
        TokensXP x;
        curve.computeXforPFee(st0, P, zero_fee, x);
        AMMState st = st0;
        st.xs[0] = x[0] / st.price[0];
        st.xs[1] = x[1] / st.price[1];
        money D0 = curve.computeD(st0);
        money D  = curve.computeD(st);
        EXPECT_NEAR(D0, D, 1e-12*D0)
            << "D is conserved" << std::endl
            << "st0 = " << st0 << std::endl
            << "st  = " << st  << std::endl
            << "P = " << P;
        EXPECT_NEAR(curve.computeP(st), P, 1e-12*P)
            << "P is correct" << std::endl
            << "st0 = " << st0 << std::endl
            << "st  = " << st  << std::endl
            << "P = " << P;
    };
    // Trivial price scale
    const AMMState st1(1e6, Prices({1, 1} ));
    for(auto price: logspace_100) {
        test_price(st1, price);
    }
    // Nontrivial price scale
    const AMMState st2(1e6, Prices({1, 10}));
    for(auto price: logspace_100) {
        test_price(st2, price);
    }
}


// D is linear in token amount
TEST_P(CurveTest, DIsLinear) {
    const Curve& curve = *GetParam();
    //
    const AMMState st1(1e6, Prices({1, 100}));
    AMMState st2 = st1;
    st2.xs[0] *= 2;
    st2.xs[1] *= 2;
    //
    const money D1 = curve.computeD(st1);
    const money D2 = curve.computeD(st2);
    EXPECT_NEAR(2*D1, D2, 1e-12L * D1);
}

// D(x,y) = D(y,x)
TEST_P(CurveTest, DIsSymmetric) {
    const Curve& curve = *GetParam();
    // We use price scale 1,1 to make symmetry explicit
    const AMMState st1(Tokens({3e6, 1e6}), Prices({1, 1}));
    const AMMState st2(Tokens({1e6, 3e6}), Prices({1, 1}));
    //
    const money D1 = curve.computeD(st1);
    const money D2 = curve.computeD(st2);
    EXPECT_NEAR(D1, D2, 1e-12L * D1);
    
}

namespace {
    Stableswap      stableswap_1(5);
    Stableswap      stableswap_2(50);
    ConstantProduct constant_prod;
}

INSTANTIATE_TEST_SUITE_P(
    Minisim,
    CurveTest,
    ::testing::Values(&stableswap_1,
                      &stableswap_2,
                      &constant_prod
        ));



// ----------------------------------------------------------------
// Step calculations
// ----------------------------------------------------------------

// These tests determine sematics of
TEST(StepCalculation, Simple) {
    // We use uniswap as simple reference for veryfying semantics of
    // step_for_price_2
    ConstantProduct curve;
    const money     fee_amount = 0.01;
    FlatFee         fee(fee_amount, 0.0);
    FlatFee         zero_fee(0.0, 0.0);
    AMMState        st0(2e6, Prices({1,10}));
    const money     P = st0.price[1];
    EXPECT_NEAR(P, curve.computePrice(st0), 1e-12) << "Initial price";
    // Zero fee case. It should provide same answer as simple on-curve
    // trade
    {
        const money dP = 0.1 * P;
        money step_x = step_for_price_2(
            st0,
            0, P + dP,
            1e100,
            curve, zero_fee, 0, 1e-8*2e6
            );
        AMMState st = st0;
        st.xs[0] += step_x;
        st.xs[1]  = curve.computeY(st0, st.xs[0], 0, 1);
        EXPECT_NEAR( P+dP, curve.computePrice(st), 1e-6) << "After trade (0 fee) dP>0";
    }
    {
        const money dP = -0.1 * P;
        money step_x = step_for_price_2(
            st0,
            P + dP, 0,
            1e100,
            curve, zero_fee, 0, 1e-8*2e6
            );
        AMMState st = st0;
        st.xs[1] += step_x;
        st.xs[0]  = curve.computeY(st0, st.xs[1], 1, 0);
        EXPECT_NEAR( P+dP, curve.computePrice(st), 1e-6) << "After trade (0 fee) dP<0";
    }
    // Nonzero fee case. It should provide same answer as simple on-curve
    // deal
    //
    // {
    //     const money dP = 0.1 * P;
    //     money step_x = step_for_price_2(
    //         st0,
    //         0, P + dP,
    //         1e100,
    //         curve, zero_fee, 0, 1e-8*2e6
    //         );
    //     // AMMState st = st0;
    //     // st.xs[0] += step_x;
    //     // st.xs[1]  = curve.computeY(st0, st.xs[0], 0, 1);

    //     Trade trade     = Trade(Trade::BUY, step_x, 0, 1, st0, curve);
    //     Trade trade_fee = trade.applyFee(fee.computeTradeFee(st0, trade));
    //     AMMState st(st0, trade_fee);
    //     EXPECT_NEAR( (P + dP), curve.computePrice(st), 1e-6)
    //         << "After trade (with fee) dP>0"
    //         << std::endl << step_x
    //         << std::endl << trade
    //         << std::endl << trade_fee
    //         << std::endl << st0
    //         << std::endl << st
    //         << std::endl << P + dP
    //         << std::endl << curve.computePrice(st)
    //         ;
    // }
    // {
    //     const money dP = -0.1;
    //     money step_x = step_for_price_2(
    //         st0,
    //         P + dP, 0,
    //         1e100,
    //         curve, fee, 0, 1e-8*2e6
    //         );
    //     AMMState st = st0;
    //     st.xs[1] += step_x;
    //     st.xs[0]  = curve.computeY(st0, st.xs[1], 1, 0);
    //     EXPECT_NEAR( P+dP, curve.computePrice(st), 1e-6) << "After trade (with fee) dP<0";
    // }
}
