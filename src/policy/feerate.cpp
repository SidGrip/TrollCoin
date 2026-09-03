// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <policy/feerate.h>
#include <tinyformat.h>

#include <limits>

namespace {

CAmount SaturatingMultiply(const CAmount value, const uint32_t multiplier)
{
    if (value == 0 || multiplier == 0) return 0;

    const CAmount multiplier_amount{multiplier};
    if (value > 0 && value > std::numeric_limits<CAmount>::max() / multiplier_amount) {
        return std::numeric_limits<CAmount>::max();
    }
    if (value < 0 && value < std::numeric_limits<CAmount>::min() / multiplier_amount) {
        return std::numeric_limits<CAmount>::min();
    }
    return value * multiplier_amount;
}

} // namespace

CFeeRate::CFeeRate(const CAmount& nFeePaid, uint32_t num_bytes)
{
    const int64_t nSize{num_bytes};

    if (nSize > 0) {
        const CAmount quotient{nFeePaid / nSize};
        const CAmount remainder{nFeePaid % nSize};
        // `remainder` is strictly smaller than uint32_t, so this product is safe.
        nSatoshisPerK = SaturatingAdd(SaturatingMultiply(quotient, 1000), remainder * 1000 / nSize);
    } else {
        nSatoshisPerK = 0;
    }
}

CAmount CFeeRate::GetFee(uint32_t num_bytes) const
{
    const int64_t nSize{num_bytes};

    const CAmount quotient{nSatoshisPerK / 1000};
    const CAmount remainder{nSatoshisPerK % 1000};
    // `remainder` is strictly smaller than 1000 and nSize is uint32_t.
    const CAmount remainder_product{remainder * nSize};
    CAmount nFee{SaturatingAdd(SaturatingMultiply(quotient, num_bytes),
                               (remainder_product + (remainder_product > 0 ? 999 : 0)) / 1000)};

    if (nFee == 0 && nSize != 0) {
        if (nSatoshisPerK > 0) nFee = CAmount(1);
        if (nSatoshisPerK < 0) nFee = CAmount(-1);
    }

    return nFee;
}

std::string CFeeRate::ToString(const FeeEstimateMode& fee_estimate_mode) const
{
    switch (fee_estimate_mode) {
    case FeeEstimateMode::SAT_VB: return strprintf("%d.%03d %s/vB", nSatoshisPerK / 1000, nSatoshisPerK % 1000, CURRENCY_ATOM);
    default:                      return strprintf("%d.%08d %s/kvB", nSatoshisPerK / COIN, nSatoshisPerK % COIN, CURRENCY_UNIT);
    }
}
