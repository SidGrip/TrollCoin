// Copyright (c) 2018-2023 The Blackcoin More developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/tx_verify.h>
#include <policy/policy.h>
#include <validation.h>

#include <algorithm>
#include <cstdint>
#include <limits>

BOOST_AUTO_TEST_SUITE(minfee_tests)

BOOST_AUTO_TEST_CASE(minfee_test)
{
    SelectParams(ChainType::MAIN);

    // Check minimum fees before V3_1 fork
    BOOST_CHECK_EQUAL(GetMinFee(0, 0), 0);
    BOOST_CHECK_EQUAL(GetMinFee(99, 0), 990);
    BOOST_CHECK_EQUAL(GetMinFee(100, 0), 1000);
    BOOST_CHECK_EQUAL(GetMinFee(101, 0), 1010);
    BOOST_CHECK_EQUAL(GetMinFee(10000, 0), 100000);

    constexpr uint32_t MAX_U32{std::numeric_limits<uint32_t>::max()};
    constexpr CAmount MAX_U32_PRE_V3_FEE{
        static_cast<CAmount>(MAX_U32) * DEFAULT_MIN_RELAY_TX_FEE / 1000};
    BOOST_CHECK_EQUAL(GetMinFee(static_cast<size_t>(MAX_U32), 0), MAX_U32_PRE_V3_FEE);
    if constexpr (std::numeric_limits<size_t>::max() > MAX_U32) {
        BOOST_CHECK_EQUAL(
            GetMinFee(static_cast<size_t>(static_cast<uint64_t>(MAX_U32) + 1), 0),
            MAX_MONEY);
        BOOST_CHECK_EQUAL(GetMinFee(std::numeric_limits<size_t>::max(), 0), MAX_MONEY);
    } else {
        BOOST_CHECK_EQUAL(
            GetMinFee(std::numeric_limits<size_t>::max(), 0), MAX_U32_PRE_V3_FEE);
    }

    // Check minimum fees after V3_1 fork
    const uint32_t activation_time{
        static_cast<uint32_t>(Params().GetConsensus().nProtocolV3_1Time)};
    const uint32_t post_v3_1{
        static_cast<uint32_t>(Params().GetConsensus().nProtocolV3_1Time + 1)};
    BOOST_CHECK_EQUAL(GetMinFee(1000, activation_time), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(0, post_v3_1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(99, post_v3_1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(100, post_v3_1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(101, post_v3_1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(999, post_v3_1), 10000);
    BOOST_CHECK_EQUAL(GetMinFee(1000, post_v3_1), 20000);
    BOOST_CHECK_EQUAL(GetMinFee(1001, post_v3_1), 20000);
    BOOST_CHECK_EQUAL(GetMinFee(10000, post_v3_1), 110000);

    constexpr uint64_t LAST_BELOW_MAX{
        static_cast<uint64_t>((MAX_MONEY / TX_FEE_PER_KB - 2) * 1000 + 999)};
    constexpr uint64_t FIRST_AT_MAX{LAST_BELOW_MAX + 1};
    constexpr uint64_t LAST_AT_MAX{
        static_cast<uint64_t>((MAX_MONEY / TX_FEE_PER_KB - 1) * 1000 + 999)};
    if constexpr (std::numeric_limits<size_t>::max() > LAST_AT_MAX) {
        BOOST_CHECK_EQUAL(
            GetMinFee(static_cast<size_t>(LAST_BELOW_MAX), post_v3_1),
            MAX_MONEY - TX_FEE_PER_KB);
        BOOST_CHECK_EQUAL(
            GetMinFee(static_cast<size_t>(FIRST_AT_MAX), post_v3_1), MAX_MONEY);
        BOOST_CHECK_EQUAL(
            GetMinFee(static_cast<size_t>(LAST_AT_MAX), post_v3_1), MAX_MONEY);
        BOOST_CHECK_EQUAL(
            GetMinFee(static_cast<size_t>(LAST_AT_MAX + 1), post_v3_1), MAX_MONEY);
        BOOST_CHECK_EQUAL(
            GetMinFee(std::numeric_limits<size_t>::max(), post_v3_1), MAX_MONEY);
    } else {
        // Discarded branches of a non-template `if constexpr` are still
        // type-checked, so the unclamped product would constant-fold to an
        // int64 overflow on 64-bit builds. Clamp exactly as GetMinFee does;
        // the clamped product is MAX_MONEY itself and cannot overflow.
        constexpr uint64_t MAX_FEE_UNITS{
            static_cast<uint64_t>(MAX_MONEY / TX_FEE_PER_KB)};
        constexpr uint64_t FEE_UNITS{std::numeric_limits<size_t>::max() / 1000 + 1};
        const CAmount expected{
            static_cast<CAmount>(std::min(FEE_UNITS, MAX_FEE_UNITS)) * TX_FEE_PER_KB};
        BOOST_CHECK_EQUAL(
            GetMinFee(std::numeric_limits<size_t>::max(), post_v3_1), expected);
    }
}

BOOST_AUTO_TEST_SUITE_END()
