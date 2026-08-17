// Test suite uses Google Test (main() is provided by gtest_main).
#include <gtest/gtest.h>
#include "simulation.hpp"

TEST(minisim, curve) {
    Stableswap curve(5, 0);
    // Initial state
    AMMState   st0(1e6, Prices({1,10}));
    money D0 = curve.computeD(st0);
    // On-curve trade
    const int n1 = 0;
    const int n2 = 1;
    AMMState st1 = st0;
    st1.xs[n1] *= 1.01;
    st1.xs[n2] = curve.computeY(st0, st1.xs[n1], n1, n2);
    const money D1 = curve.computeD(st1);
    //
    EXPECT_EQ(D0, D1);
}
