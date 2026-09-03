// Copyright (c) 2026 The TrollCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// A block that arrives ahead of our clock is early, not invalid: the same header
// becomes acceptable once the clock catches up. Legacy 2.1.x refuses it with a bare
// error() and no DoS score at all (main.cpp:2056), and it is not in that client's DoS
// list. On a network where 2.1.x is the majority, scoring the sender for a few minutes
// of clock drift evicts honest peers, so CheckBlockHeader reports BLOCK_TIME_FUTURE --
// the same result the MAX_FUTURE_BLOCK_TIME gate in ContextualCheckBlockHeader already
// uses for the identical rule -- and net_processing scores that zero.
//
// The second case guards the path that made this expensive. Headers from a pre-70016
// peer carry no PoS marker, so AcceptBlockHeader guesses work-or-stake and retries the
// other way when CheckBlockHeader rejects the guess. That retry must be limited to the
// guess itself. It used to fire on any failure, so an early but otherwise valid
// proof-of-work header was reclassified as proof-of-stake, measured against the stake
// target, and refused as "bad-diffbits, incorrect proof-of-stake" -- scoring the peer
// 100 and hiding the real reason. CheckBlockHeader leaves state untouched for the
// unproven guess and sets it for every genuine failure, which is what separates them.

#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <util/time.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(blk_time_gate_tests, TestChain100Setup)

namespace {

//! Regtest widens FutureDrift to a day (validation.cpp:145); mainnet and testnet use
//! the 640s legacy drift. Read it off the chain rather than hardcoding either.
constexpr int64_t REGTEST_DRIFT{24 * 60 * 60};

//! A work header on top of `prev`, stamped at `ntime`, ground until it meets the
//! target. Version stays 1: above 6 GetHash() switches to sha256d while validation
//! hashes with scrypt. nFlags stays 0, which is what a pre-70016 peer sends.
bool MakeSolvedWorkHeader(CBlockHeader& header, const CBlockIndex* prev, int64_t ntime,
                          const Consensus::Params& consensus)
{
    header.nVersion = 1;
    header.hashPrevBlock = prev->GetBlockHash();
    header.hashMerkleRoot = uint256{};
    header.nTime = static_cast<uint32_t>(ntime);
    header.nBits = GetNextTargetRequired(prev, consensus, /*fProofOfStake=*/false);
    header.nFlags = 0;

    // The regtest target is trivial, so this lands almost immediately.
    for (uint32_t nonce = 0; nonce < (1u << 22); ++nonce) {
        header.nNonce = nonce;
        header.InvalidateCache();
        if (CheckProofOfWork(header.GetPoWHash(), header.nBits, consensus)) return true;
    }
    return false;
}

struct Verdict {
    bool accepted{false};
    std::string reason;
    BlockValidationResult result{BlockValidationResult::BLOCK_RESULT_UNSET};
};

Verdict Offer(ChainstateManager& chainman, const CBlockHeader& header, bool old_client)
{
    BlockValidationState state;
    const CBlockIndex* index{nullptr};
    Verdict v;
    v.accepted = chainman.ProcessNewBlockHeaders({header}, /*min_pow_checked=*/true, state,
                                                 old_client, &index);
    v.reason = state.GetRejectReason();
    v.result = state.GetResult();
    return v;
}

} // namespace

//! An early header is refused, but the sender is not charged for it.
BOOST_AUTO_TEST_CASE(early_header_is_refused_without_scoring_the_peer)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    const Consensus::Params& consensus{Params().GetConsensus()};

    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    BOOST_REQUIRE(tip != nullptr);

    const int64_t now{tip->GetBlockTime() + 100};
    SetMockTime(now);

    // Comfortably past the drift allowance, so the gate under test is the one that fires.
    CBlockHeader early;
    BOOST_REQUIRE(MakeSolvedWorkHeader(early, tip, now + REGTEST_DRIFT + 3600, consensus));

    for (const bool old_client : {false, true}) {
        const Verdict v{Offer(chainman, early, old_client)};
        BOOST_TEST_MESSAGE("old_client=" << old_client << " accepted=" << v.accepted
                           << " reason='" << v.reason << "'");

        BOOST_CHECK(!v.accepted);
        // The honest reason, not a difficulty complaint invented by the retry.
        BOOST_CHECK_EQUAL(v.reason, "time-too-new");
        // BLOCK_INVALID_HEADER is Misbehaving(100) at net_processing.cpp:2028.
        BOOST_CHECK(v.result != BlockValidationResult::BLOCK_INVALID_HEADER);
        BOOST_CHECK(v.result == BlockValidationResult::BLOCK_TIME_FUTURE);
    }

    SetMockTime(0);
}

//! The work-or-stake retry must not fire on a failure that has nothing to do with the
//! guess. Before the guard, this header came back as "bad-diffbits" with a 100 score.
BOOST_AUTO_TEST_CASE(old_client_retry_does_not_reclassify_an_early_work_header)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    const Consensus::Params& consensus{Params().GetConsensus()};

    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    const int64_t now{tip->GetBlockTime() + 100};
    SetMockTime(now);

    CBlockHeader early;
    BOOST_REQUIRE(MakeSolvedWorkHeader(early, tip, now + REGTEST_DRIFT + 3600, consensus));

    const Verdict v{Offer(chainman, early, /*old_client=*/true)};
    BOOST_CHECK(!v.accepted);
    BOOST_CHECK_NE(v.reason, "bad-diffbits");
    BOOST_CHECK(v.result != BlockValidationResult::BLOCK_INVALID_HEADER);

    SetMockTime(0);
}

//! The retry itself must survive. Every legitimate stake header from a 2.1.x peer
//! arrives with nFlags == 0, so it is guessed as work, fails the work check, and is
//! only accepted because the guess is retried as stake. Narrowing the retry to the
//! unproven guess must not narrow it to nothing: without this case, deleting the retry
//! outright would still leave the two tests above passing while every 2.1.x stake
//! header died as high-hash.
BOOST_AUTO_TEST_CASE(an_old_client_stake_header_is_still_retried_as_stake)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    const Consensus::Params& consensus{Params().GetConsensus()};

    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    const int64_t now{tip->GetBlockTime() + 100};
    SetMockTime(now);

    // What a 2.1.x peer sends for a stake block: no PoS marker, carrying the stake
    // target, and with no work behind it. The guess must fail and be corrected.
    CBlockHeader stake_header;
    stake_header.nVersion = 1;
    stake_header.hashPrevBlock = tip->GetBlockHash();
    stake_header.hashMerkleRoot = uint256{};
    stake_header.nTime = static_cast<uint32_t>(now + 60);
    stake_header.nBits = GetNextTargetRequired(tip, consensus, /*fProofOfStake=*/true);
    stake_header.nNonce = 0;
    stake_header.nFlags = 0;
    stake_header.InvalidateCache();

    // Precondition: it really does fail as work, so the retry is what is being measured.
    BOOST_REQUIRE(!CheckProofOfWork(stake_header.GetPoWHash(), stake_header.nBits, consensus));

    const Verdict v{Offer(chainman, stake_header, /*old_client=*/true)};
    BOOST_TEST_MESSAGE("stake retry accepted=" << v.accepted << " reason='" << v.reason << "'");
    BOOST_CHECK(v.accepted);
    BOOST_CHECK_NE(v.reason, "high-hash");

    SetMockTime(0);
}

//! Control: the identical construction inside the allowance is accepted, so the two
//! cases above are measuring the timestamp and nothing else.
BOOST_AUTO_TEST_CASE(a_header_inside_the_drift_allowance_is_accepted)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    const Consensus::Params& consensus{Params().GetConsensus()};

    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    const int64_t now{tip->GetBlockTime() + 100};
    SetMockTime(now);

    CBlockHeader ok_header;
    BOOST_REQUIRE(MakeSolvedWorkHeader(ok_header, tip, now + 60, consensus));

    const Verdict v{Offer(chainman, ok_header, /*old_client=*/true)};
    BOOST_TEST_MESSAGE("control accepted=" << v.accepted << " reason='" << v.reason << "'");
    BOOST_CHECK(v.accepted);

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
