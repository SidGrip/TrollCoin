// Copyright (c) 2015-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <chainparams.h>
#include <pow.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(pow_tests, BasicTestingSetup)

/* TrollCoin does not use Bitcoin's 2016-block retarget epochs. CalculateNextTargetRequired
 * retargets on EVERY block with a fixed nInterval of 10, moving the target exponentially
 * toward a per-block-type spacing S (PoW nTargetSpacingV1 = 60s, PoS nTargetSpacing = 64s):
 *
 *     A = pindexLast.nTime - nFirstBlockTime      (negative -> replaced with S;
 *                                                  PoS only: A > 10*S -> clamped to 10*S)
 *     target' = target * (9*S + 2*A) / (11*S)     then clamped to pow/posLimit
 *
 * So nFirstBlockTime is just the previous block's time, not an epoch boundary, and the
 * expected values below are derived by hand from that closed form -- they are NOT copied
 * from what the node prints, which would make these tests tautological.
 *
 * The PermittedDifficultyTransition assertions inherited from Bitcoin are gone rather than
 * commented out: pow.cpp implements that function as a deliberate always-true stub for this
 * chain, and a per-block EMA has no equivalent of Bitcoin's 4x-per-epoch invariant to assert.
 */

/* Test calculation of next difficulty target with no constraints applying */
BOOST_AUTO_TEST_CASE(get_next_work)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    CBlockIndex pindexLast;
    pindexLast.nHeight = 32255;
    pindexLast.nTime = 1395223285;
    pindexLast.nBits = 0x1d028699;  // target 0x028699 << 208 = 165529 * 2^208

    // A = 90s against the 60s PoW spacing: the block was slow, so the target eases.
    // 165529 * (9*60 + 2*90) / (11*60) = 165529 * 720 / 660 = 180577 = 0x02c161
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1395223195, chainParams->GetConsensus(), false), 0x1d02c161U);

    // A = S = 60s is the fixed point of the EMA (9*60 + 2*60 == 11*60), so the target
    // must come back unchanged.
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1395223225, chainParams->GetConsensus(), false), 0x1d028699U);

    // A = 30s: the block was fast, so the target tightens.
    // 165529 * (540 + 60) / 660 = 165529 * 600 / 660 = 150480 = 0x024bd0
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1395223255, chainParams->GetConsensus(), false), 0x1d024bd0U);
}

/* Test the constraint on the upper bound for next work (the powLimit clamp) */
BOOST_AUTO_TEST_CASE(get_next_work_pow_limit)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    CBlockIndex pindexLast;
    pindexLast.nHeight = 2015;
    pindexLast.nTime = 1393345424;
    // Start just under the mainnet powLimit (0x1e0fffff) so that a single slow block
    // pushes the target past it. Reaching the clamp from a realistic target would need a
    // multi-year gap between blocks, which cannot happen on a chain that retargets every
    // block -- that is what the inherited fixture used to do here.
    pindexLast.nBits = 0x1e0e0000;  // target 0x0e0000 << 216 = 917504 * 2^216

    // A = 120s = 2x the 60s spacing: 917504 * 780 / 660 -> mantissa 0x108ba2, which is
    // above the powLimit mantissa 0x0fffff, so the clamp fires.
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1393345304, chainParams->GetConsensus(), false), 0x1e0fffffU);

    // Negative control: A = 90s from the same target stays under the limit, which is what
    // proves the value above is a clamp and not just a constant.
    // 917504 * 720 / 660 = 1000913 = 0x0f45d1
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1393345334, chainParams->GetConsensus(), false), 0x1e0f45d1U);
}

/* Test the constraint on the lower bound for actual time taken: a negative nActualSpacing
 * (block timestamps are not required to increase monotonically) is replaced with S. */
BOOST_AUTO_TEST_CASE(get_next_work_lower_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    CBlockIndex pindexLast;
    pindexLast.nHeight = 68543;
    pindexLast.nTime = 1397374088;
    pindexLast.nBits = 0x1d055260;

    // nFirstBlockTime is 120s AFTER pindexLast.nTime, so A = -120 is replaced with
    // S = 60, which is the EMA fixed point: the target comes back unchanged. No magic
    // constant is needed -- the expectation follows from the formula alone.
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1397374208, chainParams->GetConsensus(), false), 0x1d055260U);
}

/* Test the constraint on the upper bound for actual time taken. That clamp is PoS-only,
 * so this case must pass fProofOfStake=true; with false it is unreachable and the test
 * merely re-exercises the powLimit clamp, which is what the inherited fixture did. */
BOOST_AUTO_TEST_CASE(get_next_work_upper_limit_actual)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1396132439;
    pindexLast.nBits = 0x1d045f46;  // target 0x045f46 << 208 = 286534 * 2^208

    // A multi-year gap is clamped to 10 * S = 640s, so the target only eases by 1856/704:
    // 286534 * (9*64 + 2*640) / (11*64) = 286534 * 1856 / 704 = 755407 = 0x0b86cf
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1263163443, chainParams->GetConsensus(), true), 0x1d0b86cfU);

    // The identical result for an explicit A = 640s is what proves the clamp engaged
    // above, rather than the unclamped arithmetic coincidentally landing on the same value.
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1396131799, chainParams->GetConsensus(), true), 0x1d0b86cfU);
}

/* PoS retarget: the branch that governs every block this chain actually produces. Same
 * EMA, but S = nTargetSpacing (64s) rather than nTargetSpacingV1 (60s). */
BOOST_AUTO_TEST_CASE(get_next_work_pos)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    CBlockIndex pindexLast;
    pindexLast.nHeight = 46367;
    pindexLast.nTime = 1396132439;
    pindexLast.nBits = 0x1d045f46;  // target 0x045f46 << 208 = 286534 * 2^208

    // A = 100s against the 64s PoS spacing, below the 640s clamp:
    // 286534 * (9*64 + 2*100) / (11*64) = 286534 * 776 / 704 = 315838 = 0x04d1be
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1396132339, chainParams->GetConsensus(), true), 0x1d04d1beU);

    // posLimit clamp. posLimit equals powLimit on mainnet, so this is the same 0x1e0fffff
    // ceiling reached through the PoS branch: from just under the limit, a clamped 640s
    // gap multiplies by 1856/704 and goes past it.
    pindexLast.nBits = 0x1e0e0000;
    BOOST_CHECK_EQUAL(CalculateNextTargetRequired(&pindexLast, 1396131799, chainParams->GetConsensus(), true), 0x1e0fffffU);
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_negative_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    nBits = UintToArith256(consensus.powLimit).GetCompact(true);
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_overflow_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits{~0x00800000U};
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_too_easy_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 nBits_arith = UintToArith256(consensus.powLimit);
    nBits_arith *= 2;
    nBits = nBits_arith.GetCompact();
    hash.SetHex("0x1");
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_biger_hash_than_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith = UintToArith256(consensus.powLimit);
    nBits = hash_arith.GetCompact();
    hash_arith *= 2; // hash > nBits
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(CheckProofOfWork_test_zero_target)
{
    const auto consensus = CreateChainParams(*m_node.args, ChainType::MAIN)->GetConsensus();
    uint256 hash;
    unsigned int nBits;
    arith_uint256 hash_arith{0};
    nBits = hash_arith.GetCompact();
    hash = ArithToUint256(hash_arith);
    BOOST_CHECK(!CheckProofOfWork(hash, nBits, consensus));
}

BOOST_AUTO_TEST_CASE(GetBlockProofEquivalentTime_test)
{
    const auto chainParams = CreateChainParams(*m_node.args, ChainType::MAIN);
    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * chainParams->GetConsensus().nTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p2 = &blocks[InsecureRandRange(10000)];
        CBlockIndex *p3 = &blocks[InsecureRandRange(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, chainParams->GetConsensus());
        BOOST_CHECK_EQUAL(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}

void sanity_check_chainparams(const ArgsManager& args, ChainType chain_type)
{
    const auto chainParams = CreateChainParams(args, chain_type);
    const auto consensus = chainParams->GetConsensus();

    // hash genesis is correct
    BOOST_CHECK_EQUAL(consensus.hashGenesisBlock, chainParams->GenesisBlock().GetHash());

    // target timespan is an even multiple of spacing
    BOOST_CHECK_EQUAL(consensus.nTargetTimespan % consensus.nTargetSpacing, 0);

    // genesis nBits is positive, doesn't overflow and is lower than powLimit
    arith_uint256 pow_compact;
    bool neg, over;
    pow_compact.SetCompact(chainParams->GenesisBlock().nBits, &neg, &over);
    BOOST_CHECK(!neg && pow_compact != 0);
    BOOST_CHECK(!over);
    // Blackcoin
    // BOOST_CHECK(UintToArith256(consensus.powLimit) >= pow_compact);

    // check max target * 4*nPowTargetTimespan doesn't overflow -- see pow.cpp:CalculateNextWorkRequired()
    if (!consensus.fPowNoRetargeting) {
        arith_uint256 targ_max("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
        targ_max /= consensus.nTargetTimespan*4;
        BOOST_CHECK(UintToArith256(consensus.powLimit) < targ_max);
    }
}

BOOST_AUTO_TEST_CASE(ChainParams_MAIN_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(ChainParams_REGTEST_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::REGTEST);
}

BOOST_AUTO_TEST_CASE(ChainParams_TESTNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(ChainParams_SIGNET_sanity)
{
    sanity_check_chainparams(*m_node.args, ChainType::SIGNET);
}

BOOST_AUTO_TEST_SUITE_END()
