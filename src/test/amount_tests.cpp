// Copyright (c) 2016-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <policy/feerate.h>

#include <limits>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(amount_tests)

BOOST_AUTO_TEST_CASE(MoneyRangeTest)
{
    BOOST_CHECK_EQUAL(MoneyRange(CAmount(-1)), false);
    BOOST_CHECK_EQUAL(MoneyRange(CAmount(0)), true);
    BOOST_CHECK_EQUAL(MoneyRange(CAmount(1)), true);
    BOOST_CHECK_EQUAL(MoneyRange(MAX_MONEY), true);
    BOOST_CHECK_EQUAL(MoneyRange(MAX_MONEY + CAmount(1)), false);
}

BOOST_AUTO_TEST_CASE(GetFeeTest)
{
    CFeeRate feeRate, altFeeRate;

    feeRate = CFeeRate(0);
    // Must always return 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), CAmount(0));
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e5), CAmount(0));

    feeRate = CFeeRate(1000);
    // Must always just return the arg
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), CAmount(0));
    BOOST_CHECK_EQUAL(feeRate.GetFee(1), CAmount(1));
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), CAmount(121));
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), CAmount(999));
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), CAmount(1e3));
    BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), CAmount(9e3));

    feeRate = CFeeRate(-1000);
    // Must always just return -1 * arg
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), CAmount(0));
    BOOST_CHECK_EQUAL(feeRate.GetFee(1), CAmount(-1));
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), CAmount(-121));
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), CAmount(-999));
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), CAmount(-1e3));
    BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), CAmount(-9e3));

    feeRate = CFeeRate(123);
    // Rounds up the result, if not integer
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), CAmount(0));
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), CAmount(1)); // Special case: returns 1 instead of 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(9), CAmount(2));
    BOOST_CHECK_EQUAL(feeRate.GetFee(121), CAmount(15));
    BOOST_CHECK_EQUAL(feeRate.GetFee(122), CAmount(16));
    BOOST_CHECK_EQUAL(feeRate.GetFee(999), CAmount(123));
    BOOST_CHECK_EQUAL(feeRate.GetFee(1e3), CAmount(123));
    BOOST_CHECK_EQUAL(feeRate.GetFee(9e3), CAmount(1107));

    feeRate = CFeeRate(-123);
    // Truncates the result, if not integer
    BOOST_CHECK_EQUAL(feeRate.GetFee(0), CAmount(0));
    BOOST_CHECK_EQUAL(feeRate.GetFee(8), CAmount(-1)); // Special case: returns -1 instead of 0
    BOOST_CHECK_EQUAL(feeRate.GetFee(9), CAmount(-1));

    // check alternate constructor
    feeRate = CFeeRate(1000);
    altFeeRate = CFeeRate(feeRate);
    BOOST_CHECK_EQUAL(feeRate.GetFee(100), altFeeRate.GetFee(100));

    // Check full constructor
    BOOST_CHECK(CFeeRate(CAmount(-1), 0) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(0), 0) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(1), 0) == CFeeRate(0));
    // default value
    BOOST_CHECK(CFeeRate(CAmount(-1), 1000) == CFeeRate(-1));
    BOOST_CHECK(CFeeRate(CAmount(0), 1000) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(1), 1000) == CFeeRate(1));
    // lost precision (can only resolve satoshis per kB)
    BOOST_CHECK(CFeeRate(CAmount(1), 1001) == CFeeRate(0));
    BOOST_CHECK(CFeeRate(CAmount(2), 1001) == CFeeRate(1));
    // some more integer checks
    BOOST_CHECK(CFeeRate(CAmount(26), 789) == CFeeRate(32));
    BOOST_CHECK(CFeeRate(CAmount(27), 789) == CFeeRate(34));
    // Maximum size in bytes, should not crash
    CFeeRate(MAX_MONEY, std::numeric_limits<uint32_t>::max()).GetFeePerK();
}

BOOST_AUTO_TEST_CASE(CFeeRateOverflowTest)
{
    const CAmount amount_max{std::numeric_limits<CAmount>::max()};
    const CAmount amount_min{std::numeric_limits<CAmount>::min()};

    // This valid fee used to overflow before division despite its final rate fitting CAmount.
    constexpr CAmount observed_fee{49'999'999'989'000'000};
    constexpr uint32_t observed_size{171};
    const CFeeRate observed_rate{observed_fee, observed_size};
    BOOST_CHECK_EQUAL(observed_rate.GetFeePerK(), CAmount{292'397'660'754'385'964});
    BOOST_CHECK_EQUAL(observed_rate.GetFee(observed_size), observed_fee);

    // MAX_MONEY can overflow the old intermediate even at a one-kilobyte size.
    BOOST_CHECK(CFeeRate(MAX_MONEY, 1000) == CFeeRate(MAX_MONEY));
    BOOST_CHECK(CFeeRate(-MAX_MONEY, 1000) == CFeeRate(-MAX_MONEY));
    BOOST_CHECK_EQUAL(CFeeRate(MAX_MONEY).GetFee(observed_size), CAmount{15'390'000'000'000'000});

    // Saturate only when the final result cannot be represented.
    BOOST_CHECK(CFeeRate(MAX_MONEY, 1) == CFeeRate(amount_max));
    BOOST_CHECK(CFeeRate(-MAX_MONEY, 1) == CFeeRate(amount_min));
    BOOST_CHECK(CFeeRate(amount_max / 1000, 1) == CFeeRate((amount_max / 1000) * 1000));
    BOOST_CHECK(CFeeRate(amount_max / 1000 + 1, 1) == CFeeRate(amount_max));

    CFeeRate max_rate{amount_max};
    BOOST_CHECK_EQUAL(max_rate.GetFee(1000), amount_max);
    BOOST_CHECK_EQUAL(max_rate.GetFee(1001), amount_max);
    CFeeRate min_rate{amount_min};
    BOOST_CHECK_EQUAL(min_rate.GetFee(1000), amount_min);
    BOOST_CHECK_EQUAL(min_rate.GetFee(1001), amount_min);

    max_rate += CFeeRate{CAmount{1}};
    min_rate += CFeeRate{CAmount{-1}};
    BOOST_CHECK_EQUAL(max_rate.GetFeePerK(), amount_max);
    BOOST_CHECK_EQUAL(min_rate.GetFeePerK(), amount_min);
}

BOOST_AUTO_TEST_CASE(BinaryOperatorTest)
{
    CFeeRate a, b;
    a = CFeeRate(1);
    b = CFeeRate(2);
    BOOST_CHECK(a < b);
    BOOST_CHECK(b > a);
    BOOST_CHECK(a == a);
    BOOST_CHECK(a <= b);
    BOOST_CHECK(a <= a);
    BOOST_CHECK(b >= a);
    BOOST_CHECK(b >= b);
    // a should be 0.00000002 TROLL/kvB now
    a += a;
    BOOST_CHECK(a == b);
}

BOOST_AUTO_TEST_CASE(ToStringTest)
{
    CFeeRate feeRate;
    feeRate = CFeeRate(1);
    BOOST_CHECK_EQUAL(feeRate.ToString(), "0.00000001 TROLL/kvB");
    BOOST_CHECK_EQUAL(feeRate.ToString(FeeEstimateMode::BTC_KVB), "0.00000001 TROLL/kvB");
    BOOST_CHECK_EQUAL(feeRate.ToString(FeeEstimateMode::SAT_VB), "0.001 sat/vB");
}

BOOST_AUTO_TEST_SUITE_END()
