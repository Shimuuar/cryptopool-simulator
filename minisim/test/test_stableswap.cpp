// Tests for stableswap curve
#include <gtest/gtest.h>
#include "simulation.hpp"


// Test that we implement correct curve
//
TEST(StableSwap, SolveD) {
    const Stableswap curve(5);
    AMMState st(1, {1,1});
    // 1
    st.xs = {1,1};
    EXPECT_NEAR(curve.computeD(st), 2, 1e-12) << st;
    // 2
    st.xs = {1,2};
    EXPECT_NEAR(curve.computeD(st), 2.96961331212497, 1e-12) << st;
    // 3
    st.xs = {1,3};
    EXPECT_NEAR(curve.computeD(st), 3.89662088422203, 1e-12) << st;
    // 4
    st.xs = {1,4};
    EXPECT_NEAR(curve.computeD(st), 4.79158681183612, 1e-12) << st;
    // 5
    st.xs = {1,5};
    EXPECT_NEAR(curve.computeD(st), 5.65955995546684, 1e-12) << st;
}
