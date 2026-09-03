// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <addresstype.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <key.h>
#include <node/miner.h>
#include <pow.h>
#include <random.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <test/util/setup_common.h>
#include <timedata.h>
#include <txmempool.h>
#include <util/chaintype.h>
#include <validation.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <boost/test/unit_test.hpp>

bool CheckInputScripts(const CTransaction& tx, TxValidationState& state,
                       const CCoinsViewCache& inputs, unsigned int flags, bool cacheSigStore,
                       bool cacheFullScriptStore, PrecomputedTransactionData& txdata,
                       std::vector<CScriptCheck>* pvChecks) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

BOOST_AUTO_TEST_SUITE(txvalidationcache_tests)

BOOST_FIXTURE_TEST_CASE(tx_mempool_block_doublespend, TestChain100Setup)
{
    // Make sure skipping validation of transactions that were
    // validated going into the memory pool does not allow
    // double-spends in blocks to pass validation when they should not.

    CScript scriptPubKey = CScript() <<  ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    const auto ToMemPool = [this](const CMutableTransaction& tx) {
        LOCK(cs_main);

        const MempoolAcceptResult result = m_node.chainman->ProcessTransaction(MakeTransactionRef(tx));
        return result.m_result_type == MempoolAcceptResult::ResultType::VALID;
    };

    const CAmount modest_fee{11 * CENT};
    const CAmount input_value{m_coinbase_txns[0]->vout[0].nValue};
    BOOST_REQUIRE(input_value > modest_fee);

    // Create two distinct double-spends of a mature coinbase transaction.
    std::vector<CMutableTransaction> spends;
    spends.resize(2);
    for (int i = 0; i < 2; i++)
    {
        spends[i].nVersion = 1;
        spends[i].nTime = m_coinbase_txns[0]->nTime + static_cast<uint32_t>(i);
        spends[i].vin.resize(1);
        spends[i].vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
        spends[i].vin[0].prevout.n = 0;
        spends[i].vout.resize(1);
        spends[i].vout[0].nValue = input_value - modest_fee;
        spends[i].vout[0].scriptPubKey = scriptPubKey;

        // Sign:
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(scriptPubKey, spends[i], 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        spends[i].vin[0].scriptSig << vchSig;
    }
    BOOST_REQUIRE(spends[0].GetHash() != spends[1].GetHash());

    CBlock block;

    // Test 1: block with both of those transactions should be rejected.
    block = CreateAndProcessBlock(spends, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }

    // Test 2: ... and should be rejected if spend1 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[0]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 1U);
    WITH_LOCK(m_node.mempool->cs, m_node.mempool->removeRecursive(CTransaction{spends[0]}, MemPoolRemovalReason::CONFLICT));
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);

    // Test 3: ... and should be rejected if spend2 is in the memory pool
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(spends, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 1U);
    WITH_LOCK(m_node.mempool->cs, m_node.mempool->removeRecursive(CTransaction{spends[1]}, MemPoolRemovalReason::CONFLICT));
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);

    // Final sanity test: first spend in *m_node.mempool, second in block, that's OK:
    std::vector<CMutableTransaction> oneSpend;
    oneSpend.push_back(spends[0]);
    BOOST_CHECK(ToMemPool(spends[1]));
    block = CreateAndProcessBlock(oneSpend, scriptPubKey);
    {
        LOCK(cs_main);
        BOOST_CHECK(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == block.GetHash());
    }
    // spends[1] should have been removed from the mempool when the
    // block with spends[0] is accepted:
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_FIXTURE_TEST_CASE(tx_mempool_high_fee_v1, TestChain100Setup)
{
    const CTransactionRef& input_tx{m_coinbase_txns.at(0)};
    const CAmount input_value{input_tx->vout[0].nValue};
    const CAmount output_value{11 * CENT};
    const CAmount high_fee{input_value - output_value};
    BOOST_REQUIRE(MoneyRange(input_value));
    BOOST_REQUIRE(MoneyRange(output_value));
    BOOST_REQUIRE(MoneyRange(high_fee));
    // Valid fee; the old `high_fee * 1000` intermediate would overflow.
    BOOST_REQUIRE(high_fee > std::numeric_limits<CAmount>::max() / CAmount{1000});

    const CScript script_pub_key = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CMutableTransaction high_fee_spend;
    high_fee_spend.nVersion = 1;
    high_fee_spend.nTime = input_tx->nTime;
    high_fee_spend.vin.emplace_back(input_tx->GetHash(), 0);
    high_fee_spend.vout.emplace_back(output_value, script_pub_key);

    std::vector<unsigned char> signature;
    const uint256 signature_hash{SignatureHash(script_pub_key, high_fee_spend, 0, SIGHASH_ALL, 0, SigVersion::BASE)};
    BOOST_REQUIRE(coinbaseKey.Sign(signature_hash, signature));
    signature.push_back(static_cast<unsigned char>(SIGHASH_ALL));
    high_fee_spend.vin[0].scriptSig << signature;

    const CTransactionRef high_fee_tx{MakeTransactionRef(high_fee_spend)};
    BOOST_REQUIRE_EQUAL(high_fee_tx->nVersion, 1);
    BOOST_REQUIRE_EQUAL(high_fee_tx->nTime, input_tx->nTime);
    BOOST_REQUIRE(!high_fee_tx->HasWitness());

    const MempoolAcceptResult result = [this, &high_fee_tx] {
        LOCK(cs_main);
        return m_node.chainman->ProcessTransaction(high_fee_tx);
    }();
    BOOST_REQUIRE(result.m_result_type == MempoolAcceptResult::ResultType::VALID);
    BOOST_CHECK(result.m_state.IsValid());
    BOOST_REQUIRE(result.m_base_fees.has_value());
    BOOST_CHECK_EQUAL(result.m_base_fees.value(), high_fee);
    BOOST_REQUIRE(result.m_vsize.has_value());
    const int64_t vsize{result.m_vsize.value()};
    BOOST_REQUIRE(vsize > 0);
    BOOST_REQUIRE(vsize <= static_cast<int64_t>(std::numeric_limits<uint32_t>::max()));

    // floor(high_fee * 1000 / vsize), expressed without the unsafe product.
    const CAmount rate_quotient{high_fee / vsize};
    const CAmount rate_remainder{high_fee % vsize};
    BOOST_REQUIRE(rate_quotient <= (std::numeric_limits<CAmount>::max() - CAmount{999}) / CAmount{1000});
    const CAmount expected_rate{rate_quotient * CAmount{1000} + rate_remainder * CAmount{1000} / vsize};
    BOOST_REQUIRE(result.m_effective_feerate.has_value());
    BOOST_CHECK_EQUAL(result.m_effective_feerate.value().GetFeePerK(), expected_rate);
    BOOST_CHECK_EQUAL(result.m_effective_feerate.value().GetFee(static_cast<uint32_t>(vsize)), high_fee);
    BOOST_REQUIRE(result.m_replaced_transactions.has_value());
    BOOST_CHECK(result.m_replaced_transactions.value().empty());
    BOOST_REQUIRE(result.m_wtxids_fee_calculations.has_value());
    BOOST_CHECK_EQUAL(result.m_wtxids_fee_calculations.value().size(), 1U);
    BOOST_CHECK_EQUAL(result.m_wtxids_fee_calculations.value().front(), high_fee_tx->GetWitnessHash());
    BOOST_CHECK(!result.m_other_wtxid.has_value());
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 1U);
    BOOST_CHECK(m_node.mempool->exists(GenTxid::Txid(high_fee_tx->GetHash())));
    WITH_LOCK(m_node.mempool->cs, m_node.mempool->removeRecursive(*high_fee_tx, MemPoolRemovalReason::CONFLICT));
    BOOST_CHECK_EQUAL(m_node.mempool->size(), 0U);
}

BOOST_FIXTURE_TEST_CASE(connectblock_early_return_waits_for_witness_checks, TestChain100Setup)
{
    // CVE-2024-52911: make one queued script-check batch depend on the
    // BIP143 cache, then take ConnectBlock's late bad-cb-amount return.  With
    // the historical declaration order, CCheckQueueControl would drain the
    // batch after the PrecomputedTransactionData vector had been destroyed.
    static constexpr size_t witness_inputs{256}; // two batches of the 128-check queue
    const CTransactionRef& mature_coinbase{m_coinbase_txns.at(0)};
    const CAmount coinbase_value{mature_coinbase->vout.at(0).nValue};
    const uint32_t tx_time{mature_coinbase->nTime + 1};
    const CScript p2pk_script_pub_key{CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG};
    const CScript p2pkh_script_pub_key{GetScriptForDestination(PKHash(coinbaseKey.GetPubKey()))};
    const CScript p2wpkh_script_pub_key{GetScriptForDestination(WitnessV0KeyHash(coinbaseKey.GetPubKey()))};

    // Create a confirmed fan-out that gives the candidate a large batch of
    // independently signed witness inputs. The first TestChain100 coinbase is
    // mature at the next block under regtest's 10-block maturity rule.
    const CAmount planned_funding_fee{CENT};
    BOOST_REQUIRE(coinbase_value > planned_funding_fee);
    const CAmount witness_input_value{
        (coinbase_value - planned_funding_fee) / static_cast<CAmount>(witness_inputs)};
    BOOST_REQUIRE(witness_input_value > CENT);

    CMutableTransaction funding;
    funding.nVersion = 1;
    funding.nTime = tx_time;
    funding.vin.emplace_back(mature_coinbase->GetHash(), 0);
    funding.vout.reserve(witness_inputs);
    for (size_t i{0}; i < witness_inputs; ++i) {
        funding.vout.emplace_back(witness_input_value, p2wpkh_script_pub_key);
    }
    {
        std::vector<unsigned char> signature;
        const uint256 signature_hash{
            SignatureHash(p2pk_script_pub_key, funding, 0, SIGHASH_ALL, 0, SigVersion::BASE)};
        BOOST_REQUIRE(coinbaseKey.Sign(signature_hash, signature));
        signature.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        funding.vin[0].scriptSig << signature;
    }

    const CBlock funding_block{CreateAndProcessBlock({funding}, p2pk_script_pub_key)};
    {
        LOCK(cs_main);
        BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChain().Tip()->GetBlockHash(), funding_block.GetHash());
        BOOST_REQUIRE_EQUAL(m_node.chainman->ActiveChainstate().CoinsTip().GetBestBlock(), funding_block.GetHash());
    }

    const CAmount witness_input_total{
        witness_input_value * static_cast<CAmount>(witness_inputs)};
    const CAmount witness_fee{CENT};
    BOOST_REQUIRE(witness_input_total > witness_fee);

    CMutableTransaction witness_spend;
    witness_spend.nVersion = 1;
    witness_spend.nTime = tx_time;
    witness_spend.vin.reserve(witness_inputs);
    for (size_t i{0}; i < witness_inputs; ++i) {
        witness_spend.vin.emplace_back(funding.GetHash(), static_cast<uint32_t>(i));
    }
    witness_spend.vout.emplace_back(witness_input_total - witness_fee, p2pk_script_pub_key);

    const std::vector<unsigned char> pubkey_bytes{ToByteVector(coinbaseKey.GetPubKey())};
    for (size_t i{0}; i < witness_inputs; ++i) {
        std::vector<unsigned char> signature;
        const uint256 signature_hash{SignatureHash(
            p2pkh_script_pub_key,
            witness_spend,
            static_cast<unsigned int>(i),
            SIGHASH_ALL,
            witness_input_value,
            SigVersion::WITNESS_V0)};
        BOOST_REQUIRE(coinbaseKey.Sign(signature_hash, signature));
        signature.push_back(static_cast<unsigned char>(SIGHASH_ALL));
        witness_spend.vin[i].scriptWitness.stack.push_back(std::move(signature));
        witness_spend.vin[i].scriptWitness.stack.push_back(pubkey_bytes);
    }

    CBlock candidate{CreateBlock(
        {witness_spend}, p2pk_script_pub_key, m_node.chainman->ActiveChainstate())};
    const CAmount candidate_fee{witness_input_total - witness_spend.vout.at(0).nValue};
    BOOST_REQUIRE_EQUAL(candidate_fee, witness_fee);
    const int candidate_height{
        WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height() + 1)};
    const CAmount subsidy{GetBlockSubsidy(candidate_height, Params().GetConsensus(), false)};
    BOOST_REQUIRE_EQUAL(candidate.vtx.at(0)->GetValueOut(), subsidy);

    // The template contained no mempool transactions, so it pays only the
    // subsidy. Add the actual manually appended witness fee plus one satoshi
    // to reach the late reward check after all script checks are queued.
    CMutableTransaction overpaid_coinbase{*candidate.vtx.at(0)};
    BOOST_REQUIRE(!overpaid_coinbase.vout.empty());
    overpaid_coinbase.vout.at(0).nValue += candidate_fee + 1;
    candidate.vtx.at(0) = MakeTransactionRef(overpaid_coinbase);
    node::RegenerateCommitments(candidate, *m_node.chainman);

    // CreateBlock() may have cached checks for the unmodified template.
    // RegenerateCommitments() updates the commitment and merkle root but does
    // not reset these memory-only validation flags.
    candidate.fChecked = false;
    candidate.m_checked_witness_commitment = false;
    candidate.m_checked_merkle_root = false;
    while (!CheckProofOfWork(candidate.GetHash(), candidate.nBits, m_node.chainman->GetConsensus())) {
        ++candidate.nNonce;
        candidate.InvalidateCache();
    }

    {
        LOCK(cs_main);
        Chainstate& active_chainstate{m_node.chainman->ActiveChainstate()};
        CBlockIndex* const tip{active_chainstate.m_chain.Tip()};
        BOOST_REQUIRE(tip != nullptr);

        BlockValidationState state;
        BOOST_CHECK(!TestBlockValidity(
            state, Params(), active_chainstate, candidate, tip, GetAdjustedTime,
            /*fCheckPOW=*/true, /*fCheckMerkleRoot=*/true));
        BOOST_CHECK(state.GetResult() == BlockValidationResult::BLOCK_CONSENSUS);
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-cb-amount");
        BOOST_CHECK_EQUAL(active_chainstate.m_chain.Tip()->GetBlockHash(), funding_block.GetHash());
        BOOST_CHECK_EQUAL(active_chainstate.CoinsTip().GetBestBlock(), funding_block.GetHash());
    }
}

// Run CheckInputScripts (using CoinsTip()) on the given transaction, for all script
// flags.  Test that CheckInputScripts passes for all flags that don't overlap with
// the failing_flags argument, but otherwise fails.
// CHECKLOCKTIMEVERIFY and CHECKSEQUENCEVERIFY (and future NOP codes that may
// get reassigned) have an interaction with DISCOURAGE_UPGRADABLE_NOPS: if
// the script flags used contain DISCOURAGE_UPGRADABLE_NOPS but don't contain
// CHECKLOCKTIMEVERIFY (or CHECKSEQUENCEVERIFY), but the script does contain
// OP_CHECKLOCKTIMEVERIFY (or OP_CHECKSEQUENCEVERIFY), then script execution
// should fail.
// Capture this interaction with the upgraded_nop argument: set it when evaluating
// any script flag that is implemented as an upgraded NOP code.
static void ValidateCheckInputsForAllFlags(const CTransaction &tx, uint32_t failing_flags, bool add_to_cache, CCoinsViewCache& active_coins_tip) EXCLUSIVE_LOCKS_REQUIRED(::cs_main)
{
    PrecomputedTransactionData txdata;

    FastRandomContext insecure_rand(true);

    for (int count = 0; count < 10000; ++count) {
        TxValidationState state;

        // Randomly selects flag combinations
        uint32_t test_flags = (uint32_t) insecure_rand.randrange((SCRIPT_VERIFY_END_MARKER - 1) << 1);

        // Filter out incompatible flag choices
        if ((test_flags & SCRIPT_VERIFY_CLEANSTACK)) {
            // CLEANSTACK requires P2SH and WITNESS, see VerifyScript() in
            // script/interpreter.cpp
            test_flags |= SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS;
        }
        if ((test_flags & SCRIPT_VERIFY_WITNESS)) {
            // WITNESS requires P2SH
            test_flags |= SCRIPT_VERIFY_P2SH;
        }
        bool ret = CheckInputScripts(tx, state, &active_coins_tip, test_flags, true, add_to_cache, txdata, nullptr);
        // CheckInputScripts should succeed iff test_flags doesn't intersect with
        // failing_flags
        bool expected_return_value = !(test_flags & failing_flags);
        BOOST_CHECK_EQUAL(ret, expected_return_value);

        // Test the caching
        if (ret && add_to_cache) {
            // Check that we get a cache hit if the tx was valid
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputScripts(tx, state, &active_coins_tip, test_flags, true, add_to_cache, txdata, &scriptchecks));
            BOOST_CHECK(scriptchecks.empty());
        } else {
            // Check that we get script executions to check, if the transaction
            // was invalid, or we didn't add to cache.
            std::vector<CScriptCheck> scriptchecks;
            BOOST_CHECK(CheckInputScripts(tx, state, &active_coins_tip, test_flags, true, add_to_cache, txdata, &scriptchecks));
            BOOST_CHECK_EQUAL(scriptchecks.size(), tx.vin.size());
        }
    }
}

BOOST_FIXTURE_TEST_CASE(checkinputs_test, TestChain100Setup)
{
    // Test that passing CheckInputScripts with one set of script flags doesn't imply
    // that we would pass again with a different set of flags.
    CScript p2pk_scriptPubKey = CScript() << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    CScript p2sh_scriptPubKey = GetScriptForDestination(ScriptHash(p2pk_scriptPubKey));
    CScript p2pkh_scriptPubKey = GetScriptForDestination(PKHash(coinbaseKey.GetPubKey()));
    CScript p2wpkh_scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(coinbaseKey.GetPubKey()));

    FillableSigningProvider keystore;
    BOOST_CHECK(keystore.AddKey(coinbaseKey));
    BOOST_CHECK(keystore.AddCScript(p2pk_scriptPubKey));

    // flags to test: SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, SCRIPT_VERIFY_CHECKSEQUENCE_VERIFY, SCRIPT_VERIFY_NULLDUMMY, uncompressed pubkey thing

    // Create 2 outputs that match the three scripts above, spending the first
    // coinbase tx.
    CMutableTransaction spend_tx;

    spend_tx.nVersion = 1;
    spend_tx.vin.resize(1);
    spend_tx.vin[0].prevout.hash = m_coinbase_txns[0]->GetHash();
    spend_tx.vin[0].prevout.n = 0;
    spend_tx.vout.resize(4);
    spend_tx.vout[0].nValue = 11*CENT;
    spend_tx.vout[0].scriptPubKey = p2sh_scriptPubKey;
    spend_tx.vout[1].nValue = 11*CENT;
    spend_tx.vout[1].scriptPubKey = p2wpkh_scriptPubKey;
    spend_tx.vout[2].nValue = 11*CENT;
    spend_tx.vout[2].scriptPubKey = CScript() << OP_CHECKLOCKTIMEVERIFY << OP_DROP << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;
    spend_tx.vout[3].nValue = 11*CENT;
    spend_tx.vout[3].scriptPubKey = CScript() << OP_CHECKSEQUENCEVERIFY << OP_DROP << ToByteVector(coinbaseKey.GetPubKey()) << OP_CHECKSIG;

    CMutableTransaction non_der_spend_tx{spend_tx};

    // Sign a copy with a non-DER signature.
    {
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(p2pk_scriptPubKey, non_der_spend_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char) 0); // padding byte makes this non-DER
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        non_der_spend_tx.vin[0].scriptSig << vchSig;
    }

    // Test that invalidity under a set of flags doesn't preclude validity
    // under other flag sets. The legacy consensus flags always include
    // STRICTENC, so this padded signature is invalid even before BIP66 adds
    // DERSIG.
    {
        LOCK(cs_main);

        TxValidationState state;
        PrecomputedTransactionData ptd_spend_tx;

        BOOST_CHECK(!CheckInputScripts(CTransaction(non_der_spend_tx), state, &m_node.chainman->ActiveChainstate().CoinsTip(), SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG, true, true, ptd_spend_tx, nullptr));

        TxValidationState strictenc_state;
        PrecomputedTransactionData strictenc_ptd;
        BOOST_CHECK(!CheckInputScripts(CTransaction(non_der_spend_tx), strictenc_state, &m_node.chainman->ActiveChainstate().CoinsTip(), SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, true, true, strictenc_ptd, nullptr));

        // If we call again asking for scriptchecks (as happens in
        // ConnectBlock), we should add a script check object for this -- we're
        // not caching invalidity (if that changes, delete this test case).
        std::vector<CScriptCheck> scriptchecks;
        BOOST_CHECK(CheckInputScripts(CTransaction(non_der_spend_tx), state, &m_node.chainman->ActiveChainstate().CoinsTip(), SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG, true, true, ptd_spend_tx, &scriptchecks));
        BOOST_CHECK_EQUAL(scriptchecks.size(), 1U);

        // Test that CheckInputScripts returns true iff DERSIG-enforcing flags are
        // not present.  Don't add these checks to the cache, so that we can
        // test later that block validation works fine in the absence of cached
        // successes.
        ValidateCheckInputsForAllFlags(CTransaction(non_der_spend_tx), SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC, false, m_node.chainman->ActiveChainstate().CoinsTip());
    }

    // The inherited live STRICTENC rule rejects this malformed transaction,
    // even when the test delays BIP66. Do not mine its nonexistent outputs.
    const uint256 tip_before{WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip()->GetBlockHash())};
    CBlock block{CreateAndProcessBlock({non_der_spend_tx}, p2pk_scriptPubKey)};
    {
        LOCK(cs_main);
        BOOST_REQUIRE(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == tip_before);
        BOOST_REQUIRE(m_node.chainman->ActiveChainstate().CoinsTip().GetBestBlock() == tip_before);
        BOOST_REQUIRE(m_node.chainman->ActiveChain().Tip()->GetBlockHash() != block.GetHash());
    }

    // Mine a canonical-DER-signed transaction for the remaining P2SH, CLTV,
    // CSV, and witness cache cases.
    {
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(p2pk_scriptPubKey, spend_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        spend_tx.vin[0].scriptSig << vchSig;
    }

    block = CreateAndProcessBlock({spend_tx}, p2pk_scriptPubKey);
    LOCK(cs_main);
    BOOST_REQUIRE(m_node.chainman->ActiveChain().Tip()->GetBlockHash() == block.GetHash());
    BOOST_REQUIRE(m_node.chainman->ActiveChainstate().CoinsTip().GetBestBlock() == block.GetHash());

    // Test P2SH: construct a transaction that is valid without P2SH, and
    // then test validity with P2SH.
    {
        CMutableTransaction invalid_under_p2sh_tx;
        invalid_under_p2sh_tx.nVersion = 1;
        invalid_under_p2sh_tx.vin.resize(1);
        invalid_under_p2sh_tx.vin[0].prevout.hash = spend_tx.GetHash();
        invalid_under_p2sh_tx.vin[0].prevout.n = 0;
        invalid_under_p2sh_tx.vout.resize(1);
        invalid_under_p2sh_tx.vout[0].nValue = 11*CENT;
        invalid_under_p2sh_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;
        std::vector<unsigned char> vchSig2(p2pk_scriptPubKey.begin(), p2pk_scriptPubKey.end());
        invalid_under_p2sh_tx.vin[0].scriptSig << vchSig2;

        ValidateCheckInputsForAllFlags(CTransaction(invalid_under_p2sh_tx), SCRIPT_VERIFY_P2SH, true, m_node.chainman->ActiveChainstate().CoinsTip());
    }

    // Test CHECKLOCKTIMEVERIFY
    {
        CMutableTransaction invalid_with_cltv_tx;
        invalid_with_cltv_tx.nVersion = 1;
        invalid_with_cltv_tx.nLockTime = 100;
        invalid_with_cltv_tx.vin.resize(1);
        invalid_with_cltv_tx.vin[0].prevout.hash = spend_tx.GetHash();
        invalid_with_cltv_tx.vin[0].prevout.n = 2;
        invalid_with_cltv_tx.vin[0].nSequence = 0;
        invalid_with_cltv_tx.vout.resize(1);
        invalid_with_cltv_tx.vout[0].nValue = 11*CENT;
        invalid_with_cltv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

        // Sign
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(spend_tx.vout[2].scriptPubKey, invalid_with_cltv_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

        ValidateCheckInputsForAllFlags(CTransaction(invalid_with_cltv_tx), SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, true, m_node.chainman->ActiveChainstate().CoinsTip());

        // Make it valid, and check again
        invalid_with_cltv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
        TxValidationState state;
        PrecomputedTransactionData txdata;
        BOOST_CHECK(CheckInputScripts(CTransaction(invalid_with_cltv_tx), state, m_node.chainman->ActiveChainstate().CoinsTip(), SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY, true, true, txdata, nullptr));
    }

    // TEST CHECKSEQUENCEVERIFY
    {
        CMutableTransaction invalid_with_csv_tx;
        invalid_with_csv_tx.nVersion = 2;
        invalid_with_csv_tx.vin.resize(1);
        invalid_with_csv_tx.vin[0].prevout.hash = spend_tx.GetHash();
        invalid_with_csv_tx.vin[0].prevout.n = 3;
        invalid_with_csv_tx.vin[0].nSequence = 100;
        invalid_with_csv_tx.vout.resize(1);
        invalid_with_csv_tx.vout[0].nValue = 11*CENT;
        invalid_with_csv_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

        // Sign
        std::vector<unsigned char> vchSig;
        uint256 hash = SignatureHash(spend_tx.vout[3].scriptPubKey, invalid_with_csv_tx, 0, SIGHASH_ALL, 0, SigVersion::BASE);
        BOOST_CHECK(coinbaseKey.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 101;

        ValidateCheckInputsForAllFlags(CTransaction(invalid_with_csv_tx), SCRIPT_VERIFY_CHECKSEQUENCEVERIFY, true, m_node.chainman->ActiveChainstate().CoinsTip());

        // Make it valid, and check again
        invalid_with_csv_tx.vin[0].scriptSig = CScript() << vchSig << 100;
        TxValidationState state;
        PrecomputedTransactionData txdata;
        BOOST_CHECK(CheckInputScripts(CTransaction(invalid_with_csv_tx), state, &m_node.chainman->ActiveChainstate().CoinsTip(), SCRIPT_VERIFY_CHECKSEQUENCEVERIFY, true, true, txdata, nullptr));
    }

    // TODO: add tests for remaining script flags

    // Test that passing CheckInputScripts with a valid witness doesn't imply success
    // for the same tx with a different witness.
    {
        CMutableTransaction valid_with_witness_tx;
        valid_with_witness_tx.nVersion = 1;
        valid_with_witness_tx.vin.resize(1);
        valid_with_witness_tx.vin[0].prevout.hash = spend_tx.GetHash();
        valid_with_witness_tx.vin[0].prevout.n = 1;
        valid_with_witness_tx.vout.resize(1);
        valid_with_witness_tx.vout[0].nValue = 11*CENT;
        valid_with_witness_tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

        // Sign
        SignatureData sigdata;
        BOOST_CHECK(ProduceSignature(keystore, MutableTransactionSignatureCreator(valid_with_witness_tx, 0, 11 * CENT, SIGHASH_ALL), spend_tx.vout[1].scriptPubKey, sigdata));
        UpdateInput(valid_with_witness_tx.vin[0], sigdata);

        // This should be valid under all script flags.
        ValidateCheckInputsForAllFlags(CTransaction(valid_with_witness_tx), 0, true, m_node.chainman->ActiveChainstate().CoinsTip());

        // Remove the witness, and check that it is now invalid.
        valid_with_witness_tx.vin[0].scriptWitness.SetNull();
        ValidateCheckInputsForAllFlags(CTransaction(valid_with_witness_tx), SCRIPT_VERIFY_WITNESS, true, m_node.chainman->ActiveChainstate().CoinsTip());
    }

    {
        // Test a transaction with multiple inputs.
        CMutableTransaction tx;

        tx.nVersion = 1;
        tx.vin.resize(2);
        tx.vin[0].prevout.hash = spend_tx.GetHash();
        tx.vin[0].prevout.n = 0;
        tx.vin[1].prevout.hash = spend_tx.GetHash();
        tx.vin[1].prevout.n = 1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 22*CENT;
        tx.vout[0].scriptPubKey = p2pk_scriptPubKey;

        // Sign
        for (int i = 0; i < 2; ++i) {
            SignatureData sigdata;
            BOOST_CHECK(ProduceSignature(keystore, MutableTransactionSignatureCreator(tx, i, 11 * CENT, SIGHASH_ALL), spend_tx.vout[i].scriptPubKey, sigdata));
            UpdateInput(tx.vin[i], sigdata);
        }

        // This should be valid under all script flags
        ValidateCheckInputsForAllFlags(CTransaction(tx), 0, true, m_node.chainman->ActiveChainstate().CoinsTip());

        // Check that if the second input is invalid, but the first input is
        // valid, the transaction is not cached.
        // Invalidate vin[1]
        tx.vin[1].scriptWitness.SetNull();

        TxValidationState state;
        PrecomputedTransactionData txdata;
        // This transaction is now invalid under segwit, because of the second input.
        BOOST_CHECK(!CheckInputScripts(CTransaction(tx), state, &m_node.chainman->ActiveChainstate().CoinsTip(), SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, true, true, txdata, nullptr));

        std::vector<CScriptCheck> scriptchecks;
        // Make sure this transaction was not cached (ie because the first
        // input was valid)
        BOOST_CHECK(CheckInputScripts(CTransaction(tx), state, &m_node.chainman->ActiveChainstate().CoinsTip(), SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_WITNESS, true, true, txdata, &scriptchecks));
        // Should get 2 script checks back -- caching is on a whole-transaction basis.
        BOOST_CHECK_EQUAL(scriptchecks.size(), 2U);
    }
}

BOOST_AUTO_TEST_SUITE_END()
