// Test suite uses Google Test (main() is provided by gtest_main).
#include <gtest/gtest.h>
#include "simulation.hpp"


class CurveTest : public ::testing::TestWithParam<Curve*> {};

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
    Stableswap stableswap_1(5);
    Stableswap stableswap_2(50);
}

INSTANTIATE_TEST_SUITE_P(Minisim, CurveTest,
                         ::testing::Values(&stableswap_1,
                                           &stableswap_2
                             ));
