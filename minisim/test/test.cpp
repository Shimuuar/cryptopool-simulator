// Test suite uses Google Test (main() is provided by gtest_main).
#include <gtest/gtest.h>
#include "simulation.hpp"


class CurveTest : public ::testing::TestWithParam<Curve*> {};

TEST_P(CurveTest, OnCurveTradePreservesInvariant) {
    const Curve& curve = *GetParam();
    // Initial state
    const AMMState st0(1e6, Prices({1,10}));
    const money    D0 = curve.computeD(st0);
    // ----------------------------------------
    // On-curve trade must preserve invariant
    const int n1 = 0;
    const int n2 = 1;
    AMMState st1 = st0;
    st1.xs[n1] *= 1.01;
    st1.xs[n2] =  curve.computeY(st0, st1.xs[n1], n1, n2);
    const money D1 = curve.computeD(st1);
    EXPECT_NEAR(D0, D1, 1e-12L * D0);
}

namespace {
    Stableswap stableswap_0(5, 0);
    Stableswap stableswap_1(50, 0.1);
}

INSTANTIATE_TEST_SUITE_P(Minisim, CurveTest,
                         ::testing::Values(&stableswap_0,
                                           &stableswap_1));
