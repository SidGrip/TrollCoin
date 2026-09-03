// Copyright (c) 2026 The TrollCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Adversarial coverage for the persist-before-proof half of security finding
// H-02.
//
// The legacy 2.x client calls CheckProofOfStake inside AcceptBlock, ahead of
// WriteToDisk, so a block whose coinstake proves nothing is never stored.
// Headers-first sync moved that proof to ConnectBlock, which runs after the
// block is on disk. Everything checked before storage is structural: that a
// coinstake is present, correctly timestamped, and that the block is signed.
// Nothing asks whether the staked prevout exists.
//
// A peer can therefore mint sibling blocks at tip+1 that differ only in their
// bogus coinstake -- so each has a different hash and each carries more work
// than the tip -- and have every one of them written to disk. The header lead
// bound does not help: it limits how far ahead a chain runs, not how wide it
// is at a single height.

#include <chain.h>
#include <chainparams.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <key.h>
#include <pos.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(blk_pos_persist_dos_tests, TestChain100Setup)

namespace {
//! Build a structurally valid proof-of-stake block on top of `prev` whose
//! coinstake spends an outpoint that does not exist in any UTXO set. Every
//! rule that runs before persistence is satisfied deliberately, so what the
//! test measures is the absence of the kernel proof and nothing else.
CBlock MakeProoflessStakeBlock(const CBlockIndex* prev, const CKey& key, uint32_t seed)
{
    const Consensus::Params& consensus{Params().GetConsensus()};

    // The coinstake timestamp is consensus data and must equal the block time,
    // masked to the stake granularity.
    int64_t stake_time{prev->GetBlockTime() + 16};
    stake_time &= ~static_cast<int64_t>(consensus.nStakeTimestampMask);
    if (stake_time <= prev->GetBlockTime()) stake_time += consensus.nStakeTimestampMask + 1;

    // Coinbase: for a proof-of-stake block its single output must be empty.
    CMutableTransaction coinbase;
    coinbase.nTime = static_cast<uint32_t>(stake_time);
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << (prev->nHeight + 1) << CScriptNum(seed);
    coinbase.vout.resize(1);
    coinbase.vout[0].SetEmpty();

    // Coinstake: input 0 names an outpoint that was never created. The seed
    // makes each sibling a distinct transaction, hence a distinct block hash.
    CMutableTransaction coinstake;
    coinstake.nTime = static_cast<uint32_t>(stake_time);
    coinstake.vin.resize(1);
    coinstake.vin[0].prevout =
        COutPoint(Txid::FromUint256(uint256{static_cast<uint8_t>(seed + 1)}), 0);
    coinstake.vout.resize(2);
    coinstake.vout[0].SetEmpty();               // the coinstake marker
    coinstake.vout[1].nValue = 1 * COIN;
    // Pay-to-pubkey, which is also where CheckBlockSignature reads the signing
    // key from.
    coinstake.vout[1].scriptPubKey = CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIG;

    CBlock block;
    block.nVersion = 1;
    block.hashPrevBlock = prev->GetBlockHash();
    block.nTime = static_cast<uint32_t>(stake_time);
    block.nBits = GetNextTargetRequired(prev, consensus, /*fProofOfStake=*/true);
    block.nNonce = 0;
    block.nFlags = CBlockIndex::BLOCK_PROOF_OF_STAKE;
    block.vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block.vtx.push_back(MakeTransactionRef(std::move(coinstake)));
    block.hashMerkleRoot = BlockMerkleRoot(block);
    block.InvalidateCache();

    // Sign the block with the key named by the coinstake's pay-to-pubkey
    // output, which is what CheckBlockSignature verifies.
    key.Sign(block.GetHash(), block.vchBlockSig);
    return block;
}
} // namespace

//! A tip-extending block whose stake cannot be proven must not reach disk.
BOOST_AUTO_TEST_CASE(proofless_stake_block_is_not_persisted)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);

    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);

    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    BOOST_REQUIRE(tip != nullptr);

    const CBlock block{MakeProoflessStakeBlock(tip, key, /*seed=*/1)};
    const uint256 hash{block.GetHash()};

    // Confirm the block really is structurally sound, so a failure below is
    // the missing kernel proof rather than a malformed fixture.
    {
        LOCK(cs_main);
        BlockValidationState check_state;
        BOOST_REQUIRE_MESSAGE(
            CheckBlock(block, check_state, Params().GetConsensus(), chainman.ActiveChainstate()),
            "fixture block failed CheckBlock: " << check_state.ToString());
        BOOST_REQUIRE(block.IsProofOfStake());
    }

    auto shared{std::make_shared<const CBlock>(block)};
    bool new_block{false};
    const bool accepted{chainman.ProcessNewBlock(shared, /*force_processing=*/true,
                                                 /*min_pow_checked=*/true, &new_block)};

    LOCK(cs_main);
    const CBlockIndex* index{chainman.m_blockman.LookupBlockIndex(hash)};

    BOOST_TEST_MESSAGE("accepted=" << accepted
                       << " indexed=" << (index != nullptr)
                       << " have_data=" << (index && (index->nStatus & BLOCK_HAVE_DATA) ? 1 : 0));

    BOOST_CHECK_MESSAGE(!accepted, "a block with an unprovable coinstake was accepted");

    // The point of the fix: it must not be on disk. The header may legitimately
    // be indexed -- that is the headers-first design and is separately bounded
    // -- but the block data must never have been written.
    if (index != nullptr) {
        BOOST_CHECK_MESSAGE(!(index->nStatus & BLOCK_HAVE_DATA),
                            "an unprovable proof-of-stake block was written to disk");
    }

    // The validated chain must not have moved.
    BOOST_CHECK_EQUAL(chainman.ActiveChain().Tip(), tip);
}

//! The width case: many distinct siblings at tip+1, none of which may be
//! stored. This is what the height-based header bound does not cover.
BOOST_AUTO_TEST_CASE(proofless_stake_siblings_do_not_fill_disk)
{
    ChainstateManager& chainman = *Assert(m_node.chainman);

    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);

    const CBlockIndex* tip{WITH_LOCK(cs_main, return chainman.ActiveChain().Tip())};
    BOOST_REQUIRE(tip != nullptr);

    static constexpr uint32_t siblings{32};
    int persisted{0};
    int accepted_count{0};

    for (uint32_t seed = 1; seed <= siblings; ++seed) {
        const CBlock block{MakeProoflessStakeBlock(tip, key, seed)};
        auto shared{std::make_shared<const CBlock>(block)};
        bool new_block{false};
        if (chainman.ProcessNewBlock(shared, /*force_processing=*/true,
                                     /*min_pow_checked=*/true, &new_block)) {
            accepted_count++;
        }
        LOCK(cs_main);
        const CBlockIndex* index{chainman.m_blockman.LookupBlockIndex(block.GetHash())};
        if (index != nullptr && (index->nStatus & BLOCK_HAVE_DATA)) persisted++;
    }

    BOOST_TEST_MESSAGE("siblings offered=" << siblings
                       << " accepted=" << accepted_count
                       << " persisted=" << persisted);

    BOOST_CHECK_MESSAGE(persisted == 0,
                        persisted << " of " << siblings
                        << " unprovable sibling blocks were written to disk");

    LOCK(cs_main);
    BOOST_CHECK_EQUAL(chainman.ActiveChain().Tip(), tip);
}

BOOST_AUTO_TEST_SUITE_END()
