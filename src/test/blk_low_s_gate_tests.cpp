// Copyright (c) 2026 The TrollCoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// The height from which low-S signatures are required must track the 2.x
// majority, because that is the client the network is made of.
//
// Legacy skips ECDSA verification entirely while its tip is below its highest
// hardcoded checkpoint (ConnectInputs: "fBlock && nBestHeight <
// GetTotalBlocksEstimate()"), and enforces low-S above it, because STRICTENC
// routes through IsLowDERSignature there. Two consequences, and the tests below
// pin both:
//
//  * Above that height, not requiring low-S left this client accepting a block
//    every legacy node rejects. That was demonstrated on the wire: two isolated
//    nodes, the same block shape, one signature's S flipped to N-S, 3.0
//    accepted and 2.1.1 refused.
//  * Below it nothing ever checked, so that history may legitimately contain
//    high-S signatures. Enabling the rule from genesis would reject the live
//    chain. This is the failure that costs a fleet, so the mainnet height is
//    asserted exactly rather than as a bound.
//
// The mainnet checkpoint is 1,000,000, so the first height legacy verified is
// 1,000,001. Legacy has no testnet checkpoints at all -- mapCheckpointsTestnet
// is empty and GetTotalBlocksEstimate() returns 0 -- so it verified those chains
// from the first block, and the height is 0 there. A single chain-wide constant
// would therefore reproduce the very divergence this closes on every network
// except mainnet.
//
// The heights alone are not enough: a build with the flag line deleted from
// GetBlockScriptFlags accepts a strict superset of blocks, so it passes any
// number of height assertions and passes the genesis-to-tip replay too. The
// last case therefore drives a real block through validation, which is how
// txvalidationcache_tests already pins STRICTENC inside the same function.

#include <chainparams.h>
#include <common/args.h>
#include <consensus/amount.h>
#include <consensus/params.h>
#include <key.h>
#include <primitives/transaction.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <validation.h>

#include <algorithm>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(blk_low_s_gate_tests)

BOOST_AUTO_TEST_CASE(low_s_height_matches_the_legacy_checkpoint_skip)
{
    ArgsManager args;

    const auto main{CreateChainParams(args, ChainType::MAIN)};
    const auto test{CreateChainParams(args, ChainType::TESTNET)};
    const auto signet{CreateChainParams(args, ChainType::SIGNET)};
    const auto reg{CreateChainParams(args, ChainType::REGTEST)};

    // One past legacy's highest mainnet checkpoint. Asserted exactly: 0 or 1
    // would apply the rule to history nothing ever verified, and any larger
    // value would leave a window where we still accept what the majority
    // rejects.
    BOOST_CHECK_EQUAL(main->GetConsensus().LowSHeight, 1000001);

    // Legacy carries no checkpoints on these chains, so it verified them from
    // the start and so must we. If these ever pick up the mainnet constant, the
    // cross-client harness stops being able to demonstrate the fix, because it
    // runs at a height far below it.
    BOOST_CHECK_EQUAL(test->GetConsensus().LowSHeight, 0);
    BOOST_CHECK_EQUAL(signet->GetConsensus().LowSHeight, 0);
    BOOST_CHECK_EQUAL(reg->GetConsensus().LowSHeight, 0);
}

//! The rule is not a deployment of ours and must not be wired to one. If it ever
//! gets tied to the parked BIP66/SegWit heights it would switch off on mainnet,
//! since those sit at INT_MAX.
BOOST_AUTO_TEST_CASE(low_s_height_is_independent_of_the_parked_deployments)
{
    ArgsManager args;
    const auto main{CreateChainParams(args, ChainType::MAIN)};
    const Consensus::Params& c{main->GetConsensus()};

    BOOST_CHECK_LT(c.LowSHeight, c.BIP66Height);
    BOOST_CHECK_LT(c.LowSHeight, c.SegwitHeight);
    BOOST_CHECK_LT(c.LowSHeight, c.CSVHeight);
    BOOST_CHECK_LT(c.LowSHeight, c.TaprootHeight);

    // And it is reached on the live chain, unlike those, which are parked.
    BOOST_CHECK_LT(c.LowSHeight, c.nLastPOWBlock);
}

namespace {
const unsigned char SECP256K1_ORDER[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41,
};

//! Rewrite S as n-S and re-encode minimally, keeping the trailing hashtype byte.
//! The result verifies against the same message and key -- that is what makes it
//! a malleability, not a forgery -- so the only thing that changes is whether the
//! signature is canonical.
void NegateSignatureS(std::vector<unsigned char>& sig)
{
    const unsigned char hashtype{sig.back()};
    const std::vector<unsigned char> der(sig.begin(), sig.end() - 1);
    const unsigned rlen{der[3]};
    const unsigned slen{der[5 + rlen]};
    const std::vector<unsigned char> r(der.begin() + 4, der.begin() + 4 + rlen);
    std::vector<unsigned char> s(der.begin() + 6 + rlen, der.begin() + 6 + rlen + slen);

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
    out.push_back(hashtype);
    sig = out;
}
} // namespace

//! The rule actually reaching block validation, not just the constant.
//!
//! Regtest sets LowSHeight to 0, so every block is at or above the gate and the
//! two sides of "flag applied" and "flag deleted" separate on the very first
//! block. The low-S control has to connect first, otherwise a refusal below
//! proves only that the fixture is broken.
BOOST_FIXTURE_TEST_CASE(high_s_spend_is_refused_by_block_validation, TestChain100Setup)
{
    BOOST_REQUIRE_EQUAL(Params().GetConsensus().LowSHeight, 0);

    const CScript p2pk{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};

    const auto build_spend = [&](size_t coinbase_index, bool negate) {
        CMutableTransaction tx;
        tx.nVersion = 1;
        tx.vin.resize(1);
        tx.vin[0].prevout.hash = m_coinbase_txns[coinbase_index]->GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vout.resize(1);
        tx.vout[0].nValue = 11 * CENT;
        tx.vout[0].scriptPubKey = p2pk;

        std::vector<unsigned char> sig;
        const uint256 hash{SignatureHash(p2pk, tx, 0, SIGHASH_ALL, 0, SigVersion::BASE)};
        BOOST_REQUIRE(coinbaseKey.Sign(hash, sig));
        sig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        if (negate) NegateSignatureS(sig);
        tx.vin[0].scriptSig = CScript() << sig;
        return std::make_pair(tx, sig);
    };

    // Control: an ordinary low-S spend must connect. libsecp256k1 signs low-S,
    // so this is what every honest wallet produces.
    {
        const auto [tx, sig] = build_spend(0, /*negate=*/false);
        BOOST_CHECK(IsLowDERSignature(sig));
        const uint256 before{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockHash())};
        const CBlock block{CreateAndProcessBlock({tx}, p2pk)};
        LOCK(cs_main);
        BOOST_CHECK_MESSAGE(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == block.GetHash(),
                            "the low-S control block did not connect, so the fixture proves nothing");
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != before);
    }

    // The rule: same shape, S negated. Still valid DER and still a valid
    // signature for the same message -- only canonicality differs -- so a block
    // carrying it must be refused, exactly as the 2.x majority refuses it.
    {
        const auto [tx, sig] = build_spend(1, /*negate=*/true);
        BOOST_REQUIRE_MESSAGE(IsDERSignature(sig), "the malleated signature must stay DER-valid");
        BOOST_REQUIRE_MESSAGE(!IsLowDERSignature(sig), "the malleated signature must be high-S");

        const uint256 before{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockHash())};
        const CBlock block{CreateAndProcessBlock({tx}, p2pk)};
        LOCK(cs_main);
        BOOST_CHECK_MESSAGE(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == before,
                            "a high-S spend was accepted into a block: SCRIPT_VERIFY_LOW_S is not "
                            "reaching GetBlockScriptFlags, and this node would follow a chain the "
                            "2.x majority rejects");
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }
}

BOOST_AUTO_TEST_SUITE_END()
