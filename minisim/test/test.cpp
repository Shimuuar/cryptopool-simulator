// Dummy test suite skeleton using Boost.Test (header-only unit test framework).
#define BOOST_TEST_MODULE minisim_tests
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(dummy_suite)

BOOST_AUTO_TEST_CASE(dummy_test)
{
    BOOST_CHECK_EQUAL(1 + 1, 2);
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
