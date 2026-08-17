// Dummy test suite skeleton using Boost.Test (header-only unit test framework).
#define BOOST_TEST_MODULE minisim_tests
#include <boost/test/unit_test.hpp>
#include "simulation.hpp"

BOOST_AUTO_TEST_SUITE(minisim)

BOOST_AUTO_TEST_CASE(curve) {
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
    BOOST_CHECK_EQUAL(D0, D1);
}

BOOST_AUTO_TEST_SUITE_END()
