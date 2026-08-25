// Test suite uses Google Test (main() is provided by gtest_main).
#include <gtest/gtest.h>
#include "simulation.hpp"


class CurveTest : public ::testing::TestWithParam<Curve*> {};

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

TEST_P(CurveTest, OnCurveTrade) {
    const Curve& curve = *GetParam();
    // Initial state
    const AMMState st0(1e6, Prices({1,100}));
    const money    D0 = curve.computeD(st0);
    const int n1 = 0;
    const int n2 = 1;
    // ----------------------------------------
    // On-curve trade must preserve invariant
    {
        AMMState st1 = st0;
        st1.xs[n1] *= 1.01;
        st1.xs[n2] =  curve.computeY(st0, st1.xs[n1], n1, n2);
        const money D1 = curve.computeD(st1);
        EXPECT_NEAR(D0, D1, 1e-12L * D0);
    }
    {
        AMMState st1 = st0;
        st1.xs[n1] *= 0.99;
        st1.xs[n2] =  curve.computeY(st0, st1.xs[n1], n1, n2);
        const money D1 = curve.computeD(st1);
        EXPECT_NEAR(D0, D1, 1e-12L * D0);
    }
    {
        AMMState st1 = st0;
        st1.xs[n2] *= 1.01;
        st1.xs[n1] =  curve.computeY(st0, st1.xs[n2], n2, n1);
        const money D1 = curve.computeD(st1);
        EXPECT_NEAR(D0, D1, 1e-12L * D0);
    }
    {
        AMMState st1 = st0;
        st1.xs[n2] *= 0.99;
        st1.xs[n1] =  curve.computeY(st0, st1.xs[n2], n2, n1);
        const money D1 = curve.computeD(st1);
        EXPECT_NEAR(D0, D1, 1e-12L * D0);
    }
}

TEST_P(CurveTest, Xcp) {
    // X[cp] is correctly related to D
    const Curve& curve = *GetParam();
    //
    const AMMState st_xcp(1e6, Prices({0.1, 10}));
    const money D   = curve.computeD(st_xcp);
    const money Xcp = curve.computeXcp(st_xcp);
    EXPECT_NEAR(D, 2*Xcp, 1e-12L * D);
}

TEST_P(CurveTest, Price) {
    // Check that price is estimated correctly using finite differences
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

TEST_P(CurveTest, DisLinear) {
    // D is linear in token amount
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


namespace {
    Stableswap      stableswap_1(5);
    Stableswap      stableswap_2(50);
    ConstantProduct constant_prod;
}

INSTANTIATE_TEST_SUITE_P(Minisim, CurveTest,
                         ::testing::Values(&stableswap_1,
                                           &stableswap_2,
                                           &constant_prod
                             ));
