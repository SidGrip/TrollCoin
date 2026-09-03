// Copyright (c) 2026 The TrollCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The rolling sync-checkpoint span must match the legacy client.
//
// Legacy TrollCoin uses nCheckpointSpan = 500 (src/checkpoints.cpp) and
// enforces it in AcceptBlock, so it accepts a fork reaching up to 500 blocks
// back. This tree derived the span from nCoinbaseMaturity, which is 77 on
// mainnet, which made it the stricter node: a reorg deeper than ~77 blocks
// would be accepted by the legacy majority and rejected here as
// bad-fork-prior-to-synch-checkpoint -- a hard BLOCK_INVALID_HEADER that also
// scores the peer 100. The legacy nodes are the majority, so the boundary has
// to be theirs.
//
// These cases pin the behaviour rather than the constant, so the span cannot
// quietly drift back to a stricter value.

#include <chain.h>
#include <chainparams.h>
#include <common/args.h>
#include <node/blockstorage.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <validation.h>

#include <vector>

#include <boost/test/unit_test.hpp>

using node::BlockManager;

BOOST_FIXTURE_TEST_SUITE(blk_sync_checkpoint_tests, TestChain100Setup)

//! On a chain shorter than the span the checkpoint must fall back to genesis,
//! which accepts everything above height 0. The old maturity-derived span did
//! not: at 77 it would already be refusing forks on a 100-block chain.
BOOST_AUTO_TEST_CASE(short_chain_checkpoint_falls_back_to_genesis)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    LOCK(cs_main);

    const CBlockIndex* tip{chainman.ActiveChain().Tip()};
    BOOST_REQUIRE(tip != nullptr);
    BOOST_REQUIRE_LT(tip->nHeight, 500);

    const CBlockIndex* checkpoint{chainman.m_blockman.AutoSelectSyncCheckpoint(tip)};
    BOOST_REQUIRE(checkpoint != nullptr);

    BOOST_TEST_MESSAGE("tip height " << tip->nHeight
                       << ", checkpoint height " << checkpoint->nHeight);

    // With a 500 span and a ~100 block chain the walk runs out at genesis.
    BOOST_CHECK_EQUAL(checkpoint->nHeight, 0);

    // Every real block height is therefore still acceptable. Under the old
    // 77-block span the checkpoint would sit near height 23 and heights at or
    // below it would be refused outright.
    for (int height : {1, 24, 50, tip->nHeight}) {
        BOOST_CHECK_MESSAGE(chainman.m_blockman.CheckSyncCheckpoint(height, tip),
                            "height " << height << " was refused by the sync checkpoint");
    }
}

//! The span itself, pinned to the legacy 500 rather than bounded from below.
//!
//! Measuring the span on the fixture's ~100-block chain cannot pin it: there the
//! walk always bottoms out at genesis, so 101, 200 and 499 are indistinguishable
//! from 500. AutoSelectSyncCheckpoint reads only nHeight and pprev, so a
//! synthetic index gives the exact boundary without mining 500+ blocks.
BOOST_AUTO_TEST_CASE(checkpoint_span_is_exactly_the_legacy_500)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);
    LOCK(cs_main);

    constexpr int kTipHeight{600};
    std::vector<CBlockIndex> chain(kTipHeight + 1);
    for (int i = 0; i <= kTipHeight; ++i) {
        chain[i].nHeight = i;
        chain[i].pprev = (i == 0) ? nullptr : &chain[i - 1];
    }
    const CBlockIndex* best{&chain[kTipHeight]};

    const CBlockIndex* checkpoint{chainman.m_blockman.AutoSelectSyncCheckpoint(best)};
    BOOST_REQUIRE(checkpoint != nullptr);
    BOOST_TEST_MESSAGE("tip " << best->nHeight << ", checkpoint " << checkpoint->nHeight);

    BOOST_CHECK_EQUAL(best->nHeight - checkpoint->nHeight, 500);

    // The boundary is what actually decides whether a fork is refused, so assert
    // both sides of it. A span of 499 fails the second check and a span of 501
    // fails the first, so neither can pass by being merely "long enough".
    BOOST_CHECK_MESSAGE(!chainman.m_blockman.CheckSyncCheckpoint(kTipHeight - 500, best),
                        "a fork exactly 500 back must be refused");
    BOOST_CHECK_MESSAGE(chainman.m_blockman.CheckSyncCheckpoint(kTipHeight - 499, best),
                        "a fork 499 back must be accepted, as the legacy client accepts it");
}

//! The max-reorg depth is the same 500 and must stay that way.
//!
//! Two independent gates refuse a deep fork: this one and the sync checkpoint
//! above. They are both 500 on the live chains, so they trigger together. If
//! this one ever drifts lower it fires *first*, and its refusal used to be a
//! 100-point ban -- which is how honest majority peers were being banned while
//! the checkpoint's lighter refusal never got the chance to apply. Keeping the
//! two depths equal is what makes the gentler path reachable, so it is pinned
//! rather than left to coincidence.
BOOST_AUTO_TEST_CASE(max_reorg_depth_matches_the_checkpoint_span)
{
    ArgsManager args;

    for (const auto chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET}) {
        const auto params{CreateChainParams(args, chain)};
        BOOST_CHECK_EQUAL(params->GetConsensus().nMaxReorganizationDepth, 500);
    }

    // Regtest deliberately differs so short-chain tests can exercise the gate,
    // which is also why no unit fixture here can stand in for mainnet reorg
    // behaviour.
    const auto reg{CreateChainParams(args, ChainType::REGTEST)};
    BOOST_CHECK_EQUAL(reg->GetConsensus().nMaxReorganizationDepth, 50);

    // And the span the checkpoint walk actually produces, measured the same way
    // as the case above, so a drift in either constant breaks this.
    ChainstateManager& chainman = *Assert(m_node.chainman);
    LOCK(cs_main);
    constexpr int kTipHeight{600};
    std::vector<CBlockIndex> chain(kTipHeight + 1);
    for (int i = 0; i <= kTipHeight; ++i) {
        chain[i].nHeight = i;
        chain[i].pprev = (i == 0) ? nullptr : &chain[i - 1];
    }
    const CBlockIndex* best{&chain[kTipHeight]};
    const CBlockIndex* checkpoint{chainman.m_blockman.AutoSelectSyncCheckpoint(best)};
    BOOST_REQUIRE(checkpoint != nullptr);

    const auto main{CreateChainParams(args, ChainType::MAIN)};
    BOOST_CHECK_EQUAL(best->nHeight - checkpoint->nHeight,
                      main->GetConsensus().nMaxReorganizationDepth);
}

BOOST_AUTO_TEST_SUITE_END()
