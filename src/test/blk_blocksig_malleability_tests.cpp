// Copyright (c) 2026 The TrollCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// vchBlockSig is serialized after vtx and is not part of the block hash, so any
// relaying peer can rewrite S as n-S and the block keeps its identity. The signature
// still verifies against the same key and message afterwards -- that is what makes it
// a malleability rather than a forgery.
//
// This is why the encoding cannot be a bannable offence. Legacy 2.x tests
// IsDERSignature alone and repairs the encoding instead of punishing the sender, so
// requiring low-S here scored the relaying peer 100 for something it may not have
// done, while the same block arrived intact from everyone else.
//
// Three properties are pinned, because the fix only makes sense if all three hold:
//   1. mutating the signature really does leave the block hash alone
//   2. a high-S but well-formed signature is accepted, matching the majority
//   3. relaxing to DER-only did not stop rejecting genuinely malformed encodings
//
// Case 2 is the regression guard; case 1 is its justification; case 3 is the bound.

#include <chainparams.h>
#include <key.h>
#include <consensus/amount.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(blk_blocksig_malleability_tests, BasicTestingSetup)

namespace {

const unsigned char SECP256K1_ORDER[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41,
};

//! Rewrite S as n-S on a bare DER signature. Block signatures carry no trailing
//! hashtype byte, unlike script signatures, so this works on the whole buffer.
void NegateS(std::vector<unsigned char>& sig)
{
    const unsigned rlen{sig[3]};
    const unsigned slen{sig[5 + rlen]};
    const std::vector<unsigned char> r(sig.begin() + 4, sig.begin() + 4 + rlen);
    std::vector<unsigned char> s(sig.begin() + 6 + rlen, sig.begin() + 6 + rlen + slen);

    while (s.size() > 1 && s.front() == 0x00) s.erase(s.begin());
    std::vector<unsigned char> s32(32, 0x00);
    std::copy(s.begin(), s.end(), s32.end() - s.size());

    std::vector<unsigned char> neg(32, 0x00);
    int borrow{0};
    for (int i = 31; i >= 0; --i) {
        int d{int{SECP256K1_ORDER[i]} - int{s32[i]} - borrow};
        if (d < 0) { d += 256; borrow = 1; } else { borrow = 0; }
        neg[i] = static_cast<unsigned char>(d);
    }
    size_t lead{0};
    while (lead < 31 && neg[lead] == 0x00) ++lead;
    std::vector<unsigned char> snew(neg.begin() + lead, neg.end());
    if (snew.front() & 0x80) snew.insert(snew.begin(), 0x00);

    std::vector<unsigned char> out;
    out.push_back(0x30);
    out.push_back(static_cast<unsigned char>(2 + r.size() + 2 + snew.size()));
    out.push_back(0x02);
    out.push_back(static_cast<unsigned char>(r.size()));
    out.insert(out.end(), r.begin(), r.end());
    out.push_back(0x02);
    out.push_back(static_cast<unsigned char>(snew.size()));
    out.insert(out.end(), snew.begin(), snew.end());
    sig = out;
}

//! A proof-of-stake block, signed over its own hash.
//!
//! The stake marker that matters here is NOT nFlags. CBlock::IsProofOfStake() is
//! derived from the transactions -- vtx.size() > 1 && vtx[1]->IsCoinStake() -- and
//! IsCoinStake() in turn requires a non-null first input, at least two outputs, and an
//! empty vout[0]. nFlags is only the peer-supplied hint used during header sync. A
//! fixture that sets nFlags and leaves vtx empty reads as proof-of-work, and then
//! IsCanonicalBlockSignature answers vchBlockSig.empty() and every assertion here
//! measures the wrong branch.
std::shared_ptr<CBlock> MakeSignedStakeBlock(const CKey& key)
{
    auto block{std::make_shared<CBlock>()};
    block->nVersion = 1;
    block->hashPrevBlock = uint256::ONE;
    block->hashMerkleRoot = uint256::ONE;
    block->nTime = 1500000000;
    block->nBits = 0x1f00ffff;
    block->nNonce = 0;
    block->nFlags = CBlockIndex::BLOCK_PROOF_OF_STAKE;

    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].SetEmpty();

    CMutableTransaction coinstake;
    coinstake.vin.resize(1);
    coinstake.vin[0].prevout = COutPoint(Txid::FromUint256(uint256::ONE), 0);
    coinstake.vout.resize(2);
    coinstake.vout[0].SetEmpty();          // the stake marker
    coinstake.vout[1].nValue = 1 * COIN;

    block->vtx.push_back(MakeTransactionRef(std::move(coinbase)));
    block->vtx.push_back(MakeTransactionRef(std::move(coinstake)));

    key.Sign(block->GetHash(), block->vchBlockSig);
    return block;
}

//! Strip the stake marker so the same block reads as proof-of-work.
void MakeItWork(const std::shared_ptr<CBlock>& block)
{
    block->vtx.resize(1);
    block->nFlags = 0;
    block->InvalidateCache();
}

} // namespace

//! The premise. If the signature were covered by the hash, a mutated block would be a
//! different block and refusing it would cost the relaying peer nothing unfair.
BOOST_AUTO_TEST_CASE(mutating_the_signature_does_not_change_the_block)
{
    CKey key;
    key.MakeNewKey(true);
    auto block{MakeSignedStakeBlock(key)};

    const uint256 before{block->GetHash()};
    const std::vector<unsigned char> original{block->vchBlockSig};

    NegateS(block->vchBlockSig);
    block->InvalidateCache();

    BOOST_CHECK(block->vchBlockSig != original);      // it really was mutated
    BOOST_CHECK_EQUAL(block->GetHash(), before);      // and it is still the same block

    // The mutation is a malleability, not a forgery: it still verifies.
    BOOST_CHECK(key.GetPubKey().Verify(before, block->vchBlockSig));
}

//! The regression guard. A relayed block whose signature was negated in flight must
//! still be acceptable, because legacy 2.x accepts it and the sender did nothing.
BOOST_AUTO_TEST_CASE(a_high_s_block_signature_is_accepted)
{
    CKey key;
    key.MakeNewKey(true);
    auto block{MakeSignedStakeBlock(key)};

    // CKey::Sign already produces low-S, so negating gives the high-S counterpart.
    BOOST_REQUIRE(IsLowDERSignature(block->vchBlockSig, nullptr, /*haveHashType=*/false));
    NegateS(block->vchBlockSig);
    BOOST_REQUIRE(!IsLowDERSignature(block->vchBlockSig, nullptr, /*haveHashType=*/false));

    std::shared_ptr<const CBlock> as_const{block};
    BOOST_CHECK(CheckCanonicalBlockSignature(as_const));
}

//! The bound. DER-only is a relaxation of low-S, not of everything.
BOOST_AUTO_TEST_CASE(a_malformed_block_signature_is_still_refused)
{
    CKey key;
    key.MakeNewKey(true);

    {   // truncated
        auto block{MakeSignedStakeBlock(key)};
        block->vchBlockSig.resize(block->vchBlockSig.size() / 2);
        std::shared_ptr<const CBlock> as_const{block};
        BOOST_CHECK(!CheckCanonicalBlockSignature(as_const));
    }
    {   // wrong leading tag
        auto block{MakeSignedStakeBlock(key)};
        block->vchBlockSig[0] = 0x31;
        std::shared_ptr<const CBlock> as_const{block};
        BOOST_CHECK(!CheckCanonicalBlockSignature(as_const));
    }
    {   // empty, on a block that claims stake
        auto block{MakeSignedStakeBlock(key)};
        block->vchBlockSig.clear();
        std::shared_ptr<const CBlock> as_const{block};
        BOOST_CHECK(!CheckCanonicalBlockSignature(as_const));
    }
}

//! The other branch: a work block carries no signature at all, and a signature
//! attached to one is refused rather than ignored.
BOOST_AUTO_TEST_CASE(a_work_block_must_carry_no_signature)
{
    CKey key;
    key.MakeNewKey(true);

    auto block{MakeSignedStakeBlock(key)};
    MakeItWork(block);                       // keeps the signature, drops the coinstake
    std::shared_ptr<const CBlock> signed_work{block};
    BOOST_CHECK(!CheckCanonicalBlockSignature(signed_work));

    auto bare{MakeSignedStakeBlock(key)};
    MakeItWork(bare);
    bare->vchBlockSig.clear();
    std::shared_ptr<const CBlock> unsigned_work{bare};
    BOOST_CHECK(CheckCanonicalBlockSignature(unsigned_work));
}

//! Guard the fixture itself. Every assertion above depends on the block reading as
//! stake, and nFlags alone does not achieve that.
BOOST_AUTO_TEST_CASE(the_fixture_really_builds_a_stake_block)
{
    CKey key;
    key.MakeNewKey(true);
    auto block{MakeSignedStakeBlock(key)};
    BOOST_REQUIRE(block->IsProofOfStake());
    BOOST_REQUIRE(!block->IsProofOfWork());
    MakeItWork(block);
    BOOST_CHECK(block->IsProofOfWork());
}

BOOST_AUTO_TEST_SUITE_END()
