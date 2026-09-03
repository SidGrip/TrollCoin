// Copyright (c) 2026 The TrollCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Adversarial coverage for security finding H-02: a peer that owns no stake can
// mint proof-of-stake headers. The proof of stake lives in the coinstake inside
// the full block, so a bare header carries nothing that can be verified, and
// the peer-supplied nFlags marker claiming stake is not covered by the block
// hash. These cases pin down how far such a chain gets, so that any later
// hardening is measurable rather than asserted.

#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <pow.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(blk_pos_header_dos_tests, TestChain100Setup)

namespace {
struct AttackResult {
    //! Headers the node took into its block index.
    int accepted{0};
    //! Deepest index entry the attacker reached, if any.
    const CBlockIndex* tip{nullptr};
    //! Why the chain stopped, when it did.
    std::string stop_reason;
};

//! Feed a chain of proofless proof-of-stake headers one at a time, exactly as a
//! peer would. Each header is given the difficulty the chain demands of it at
//! that moment, so the run measures the stake claim itself rather than tripping
//! over a wrong target. Nothing is mined and no block bodies are ever supplied.
AttackResult FeedProoflessPoSHeaders(ChainstateManager& chainman, const CBlockIndex* start, size_t count)
{
    const Consensus::Params& consensus{Params().GetConsensus()};
    AttackResult result;
    const CBlockIndex* prev{start};

    for (size_t i = 0; i < count; ++i) {
        CBlockHeader header;
        {
            LOCK(cs_main);
            // Version must stay 1: above 6 GetHash() switches to sha256d while
            // validation hashes with scrypt.
            header.nVersion = 1;
            header.hashPrevBlock = prev->GetBlockHash();
            header.hashMerkleRoot = uint256{};
            // One second past the parent, which is this chain's median time past.
            header.nTime = static_cast<uint32_t>(prev->GetBlockTime()) + 1;
            // The stake target, because the header claims stake.
            header.nBits = GetNextTargetRequired(prev, consensus, /*fProofOfStake=*/true);
            header.nNonce = 0;
            // The attacker's entire claim. Not covered by the block hash.
            header.nFlags = CBlockIndex::BLOCK_PROOF_OF_STAKE;
            header.InvalidateCache();
        }

        BlockValidationState state;
        const CBlockIndex* accepted{nullptr};
        const bool ok{chainman.ProcessNewBlockHeaders(
            {header}, /*min_pow_checked=*/true, state, /*old_client=*/false, &accepted)};
        if (!ok || accepted == nullptr) {
            result.stop_reason = state.IsValid() ? "not accepted" : state.GetRejectReason();
            break;
        }
        result.accepted++;
        result.tip = accepted;
        prev = accepted;
    }
    return result;
}
} // namespace

//! A chain that proves nothing must not displace the validated chain, and must
//! not take over the best-header state that drives sync and block download.
BOOST_AUTO_TEST_CASE(proofless_pos_headers_cannot_outrank_validated_chain)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);

    const CBlockIndex* validated_tip{nullptr};
    const CBlockIndex* best_header_before{nullptr};
    arith_uint256 validated_work;
    {
        LOCK(cs_main);
        validated_tip = chainman.ActiveChain().Tip();
        best_header_before = chainman.m_best_header;
        validated_work = validated_tip->nChainWork;
    }
    BOOST_REQUIRE(validated_tip != nullptr);

    static constexpr size_t attack_length{64};
    const AttackResult attack{FeedProoflessPoSHeaders(chainman, validated_tip, attack_length)};

    LOCK(cs_main);
    const CBlockIndex* best_header_after{chainman.m_best_header};

    BOOST_TEST_MESSAGE("proofless headers accepted=" << attack.accepted << "/" << attack_length
                       << " stop_reason='" << attack.stop_reason << "'"
                       << " attacker_height=" << (attack.tip ? attack.tip->nHeight : -1)
                       << " validated_tip_height=" << validated_tip->nHeight
                       << " best_header_height=" << (best_header_after ? best_header_after->nHeight : -1));

    // No block bodies exist, so the validated chain cannot move whatever the
    // headers did.
    BOOST_CHECK_EQUAL(chainman.ActiveChain().Tip(), validated_tip);
    BOOST_CHECK(chainman.ActiveChain().Tip()->nChainWork == validated_work);

    // Note on what is *not* asserted here. An honest peer announcing a new
    // block also extends the validated tip, also carries more work than it, and
    // also moves best-header state before the block arrives; sync depends on
    // that. So "never leads" and "never becomes best header" are not properties
    // any correct node can have. What a node can require is that a chain
    // proving nothing cannot run away, which is what is checked below.
    BOOST_CHECK_MESSAGE(attack.accepted < static_cast<int>(attack_length),
                        "a proofless proof-of-stake header chain ran the full "
                        << attack_length << " headers unchecked");
    BOOST_CHECK_EQUAL(attack.stop_reason, "header-too-far-ahead");

    if (attack.tip != nullptr) {
        const int lead{attack.tip->nHeight - validated_tip->nHeight};
        BOOST_TEST_MESSAGE("bounded lead over validated tip = " << lead);
        BOOST_CHECK_MESSAGE(lead <= attack.accepted,
                            "attacker height lead exceeded the headers it got accepted");
    }
}

//! The lead a proofless chain can take is what bounds the damage, so it is
//! checked directly rather than through accumulated work: any chain extending
//! the tip outweighs it, honest ones included.
BOOST_AUTO_TEST_CASE(proofless_pos_headers_lead_is_bounded)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);

    const CBlockIndex* validated_tip{nullptr};
    arith_uint256 validated_work;
    {
        LOCK(cs_main);
        validated_tip = chainman.ActiveChain().Tip();
        validated_work = validated_tip->nChainWork;
    }
    BOOST_REQUIRE(validated_tip != nullptr);

    // Far more than any allowance, so the bound has to be what stops it.
    static constexpr size_t attempted{512};
    const AttackResult attack{FeedProoflessPoSHeaders(chainman, validated_tip, attempted)};

    LOCK(cs_main);
    if (attack.tip == nullptr) {
        BOOST_TEST_MESSAGE("no proofless header entered the index: " << attack.stop_reason);
        return;
    }

    const int lead{attack.tip->nHeight - validated_tip->nHeight};
    BOOST_TEST_MESSAGE("attempted=" << attempted << " accepted=" << attack.accepted
                       << " lead=" << lead
                       << " stop_reason='" << attack.stop_reason << "'"
                       << " attacker_work=" << attack.tip->nChainWork.ToString()
                       << " validated_work=" << validated_work.ToString());

    BOOST_CHECK_MESSAGE(attack.accepted < static_cast<int>(attempted),
                        "a proofless chain of " << attempted << " headers was accepted whole");
    BOOST_CHECK_EQUAL(attack.stop_reason, "header-too-far-ahead");
}

//! The bound has to scale with how far behind the validated tip is, otherwise it
//! would block a node catching up after downtime and initial sync itself. Moving
//! the clock forward stands in for a node whose tip has gone stale.
BOOST_AUTO_TEST_CASE(bound_scales_with_time_behind_the_tip)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);

    const CBlockIndex* validated_tip{nullptr};
    {
        LOCK(cs_main);
        validated_tip = chainman.ActiveChain().Tip();
    }
    BOOST_REQUIRE(validated_tip != nullptr);

    const int64_t tip_time{validated_tip->GetBlockTime()};
    const int64_t spacing{Params().GetConsensus().nTargetSpacing};

    // A fresh tip: only the base allowance applies.
    SetMockTime(tip_time);
    const AttackResult fresh{FeedProoflessPoSHeaders(chainman, validated_tip, 512)};

    // An hour behind: an honest chain could have produced many blocks in that
    // time, so many more headers have to be allowed through.
    SetMockTime(tip_time + 3600);
    const AttackResult behind{FeedProoflessPoSHeaders(chainman, validated_tip, 512)};

    SetMockTime(0);

    BOOST_TEST_MESSAGE("fresh tip accepted=" << fresh.accepted
                       << " one hour behind accepted=" << behind.accepted
                       << " (spacing=" << spacing << "s)");

    BOOST_CHECK_MESSAGE(behind.accepted > fresh.accepted,
                        "the allowance did not grow as the validated tip aged, so a node "
                        "catching up would be throttled");
}

BOOST_AUTO_TEST_SUITE_END()
