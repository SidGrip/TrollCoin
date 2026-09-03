// Copyright (c) 2018-2023 The Blackcoin More developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <chainparams.h>
#include <coins.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <key.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/script.h>
#include <script/signingprovider.h>
#include <span.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <test/util/transaction_utils.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <cstdint>
#include <vector>

static CFeeRate g_dust{DUST_RELAY_TX_FEE};
static bool g_bare_multi{DEFAULT_PERMIT_BAREMULTISIG};

namespace {

CMutableTransaction MakeV2Spend(const COutPoint& prevout, uint32_t ntime, CAmount value_out)
{
    CMutableTransaction tx;
    tx.nVersion = 2;
    tx.nTime = ntime;
    tx.vin.emplace_back(prevout);
    tx.vout.emplace_back(value_out, CScript() << OP_TRUE);
    return tx;
}

uint256 V2TaprootSighash(const CMutableTransaction& tx, const CTxOut& spent_output)
{
    PrecomputedTransactionData txdata;
    txdata.Init(tx, std::vector<CTxOut>{spent_output}, /*force=*/true);

    ScriptExecutionData execdata;
    execdata.m_annex_init = true;
    execdata.m_annex_present = false;

    uint256 hash;
    BOOST_CHECK(SignatureHashSchnorr(hash, execdata, tx, 0, SIGHASH_DEFAULT,
                                     SigVersion::TAPROOT, txdata,
                                     MissingDataBehavior::FAIL));
    return hash;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(v2_transaction_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(IsStandard_test)
{
    SelectParams(ChainType::MAIN);

    FillableSigningProvider keystore;
    CCoinsView coinsDummy;
    CCoinsViewCache coins(&coinsDummy);
    std::vector<CMutableTransaction> dummyTransactions =
        SetupDummyInputs(keystore, coins, {11*CENT, 50*CENT, 21*CENT, 22*CENT});

    CMutableTransaction t;
    t.vin.resize(1);
    t.vin[0].prevout.hash = dummyTransactions[0].GetHash();
    t.vin[0].prevout.n = 1;
    t.vin[0].scriptSig << std::vector<unsigned char>(65, 0);
    t.vout.resize(1);
    t.vout[0].nValue = 90*CENT;
    CKey key;
    key.MakeNewKey(true);
    t.vout[0].scriptPubKey = GetScriptForDestination(PKHash(key.GetPubKey()));

    constexpr auto CheckIsStandard = [](const auto& t) {
        std::string reason;
        BOOST_CHECK(IsStandardTx(CTransaction{t}, MAX_OP_RETURN_RELAY, g_bare_multi, g_dust, reason));
        BOOST_CHECK(reason.empty());
    };
    constexpr auto CheckIsNotStandard = [](const auto& t, const std::string& reason_in) {
        std::string reason;
        BOOST_CHECK(!IsStandardTx(CTransaction{t}, MAX_OP_RETURN_RELAY, g_bare_multi, g_dust, reason));
        BOOST_CHECK_EQUAL(reason_in, reason);
    };

    CheckIsStandard(t);

    // Allowed nVersion
    t.nVersion = 1;
    CheckIsStandard(t);

    t.nVersion = 2;
    CheckIsStandard(t);

    // The policy caller selects the v1 limit while CSV is parked. Keep the
    // generic standardness helper's post-CSV default separate from that gate.
    {
        std::string reason;
        BOOST_CHECK(!IsStandardTx(CTransaction{t}, MAX_OP_RETURN_RELAY, g_bare_multi, g_dust, reason,
                                  /*witnessEnabled=*/false, TX_MAX_STANDARD_VERSION_PRE_CSV));
        BOOST_CHECK_EQUAL(reason, "version");
    }

    // Disallowed nVersion
    t.nVersion = 3;
    CheckIsNotStandard(t, "version");

    // Allowed nVersion, empty nTime
    t.nVersion = 1;
    t.nTime = 0;
    CheckIsStandard(t);

    t.nVersion = 2;
    t.nTime = 0;
    CheckIsStandard(t);

    // Disallowed nVersion, empty nTime
    t.nVersion = 3;
    t.nTime = 0;
    CheckIsNotStandard(t, "version");

    // Check transaction version after V3_1 fork
    // Allowed nVersion, after-fork nTime
    t.nVersion = 1;
    t.nTime = Params().GetConsensus().nProtocolV3_1Time + 1;
    CheckIsStandard(t);

    t.nVersion = 2;
    t.nTime = Params().GetConsensus().nProtocolV3_1Time + 1;
    CheckIsStandard(t);

    // Disallowed nVersion, after-fork nTime
    t.nVersion = 3;
    t.nTime = Params().GetConsensus().nProtocolV3_1Time + 1;
    CheckIsNotStandard(t, "version");
}

BOOST_AUTO_TEST_CASE(v2_ntime_commits_all_signature_hashes)
{
    const COutPoint prevout{uint256S("01"), 0};
    const CAmount amount{11 * CENT};
    const CMutableTransaction tx{MakeV2Spend(prevout, /*ntime=*/1'700'000'000, amount - CENT)};

    CKey key;
    key.MakeNewKey(true);
    const CScript p2pk_script{CScript() << ToByteVector(key.GetPubKey()) << OP_CHECKSIG};
    const CScript p2wpkh_script_code{GetScriptForDestination(PKHash(key.GetPubKey()))};
    const CScript taproot_spent_script{
        CScript() << OP_1 << std::vector<unsigned char>(WITNESS_V1_TAPROOT_SIZE, 0)};
    const CTxOut taproot_spent_output{amount, taproot_spent_script};

    const uint256 legacy_before{
        SignatureHash(p2pk_script, tx, 0, SIGHASH_ALL, /*amount=*/0, SigVersion::BASE)};
    const uint256 segwit_before{
        SignatureHash(p2wpkh_script_code, tx, 0, SIGHASH_ALL, amount, SigVersion::WITNESS_V0)};
    const uint256 taproot_before{V2TaprootSighash(tx, taproot_spent_output)};

    CMutableTransaction mutated{tx};
    ++mutated.nTime;
    BOOST_CHECK(legacy_before != SignatureHash(p2pk_script, mutated, 0, SIGHASH_ALL,
                                                /*amount=*/0, SigVersion::BASE));
    BOOST_CHECK(segwit_before != SignatureHash(p2wpkh_script_code, mutated, 0, SIGHASH_ALL,
                                                amount, SigVersion::WITNESS_V0));
    BOOST_CHECK(taproot_before != V2TaprootSighash(mutated, taproot_spent_output));

    // Demonstrate the real malleability boundary for the legacy path: a v2
    // signature validates at the exact serialized time and fails after only
    // that four-byte field is changed.
    CMutableTransaction signed_tx{tx};
    std::vector<unsigned char> signature;
    BOOST_REQUIRE(key.Sign(legacy_before, signature));
    signature.push_back(static_cast<unsigned char>(SIGHASH_ALL));
    signed_tx.vin[0].scriptSig << signature;

    const CTransaction signed_immutable{signed_tx};
    ScriptError error{SCRIPT_ERR_UNKNOWN_ERROR};
    BOOST_CHECK(VerifyScript(signed_immutable.vin[0].scriptSig, p2pk_script,
                             &signed_immutable.vin[0].scriptWitness,
                             /*flags=*/0,
                             TransactionSignatureChecker(&signed_immutable, 0, /*amount=*/0,
                                                         MissingDataBehavior::ASSERT_FAIL),
                             &error));
    BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

    ++signed_tx.nTime;
    const CTransaction time_mutated{signed_tx};
    error = SCRIPT_ERR_UNKNOWN_ERROR;
    BOOST_CHECK(!VerifyScript(time_mutated.vin[0].scriptSig, p2pk_script,
                              &time_mutated.vin[0].scriptWitness,
                              /*flags=*/0,
                              TransactionSignatureChecker(&time_mutated, 0, /*amount=*/0,
                                                          MissingDataBehavior::ASSERT_FAIL),
                              &error));
}

BOOST_AUTO_TEST_CASE(v2_zero_time_and_empty_marker_are_deterministic_consensus_inputs)
{
    SelectParams(ChainType::MAIN);

    CCoinsView coins_dummy;
    CCoinsViewCache coins{&coins_dummy};
    const COutPoint timed_prevout{uint256S("02"), 0};
    const CAmount input_value{11 * CENT};
    const uint32_t input_time{1'700'000'000};
    coins.AddCoin(timed_prevout,
                  Coin{CTxOut{input_value, CScript() << OP_TRUE}, /*height=*/1,
                       /*coinbase=*/false, /*coinstake=*/false, input_time},
                  /*possible_overwrite=*/false);

    // Literal nTime == 0 is compared with the serialized input time. It is
    // not substituted with the validating node's wall clock.
    const CMutableTransaction zero_time{MakeV2Spend(timed_prevout, /*ntime=*/0, input_value - CENT)};
    TxValidationState zero_time_state;
    CAmount fee{0};
    BOOST_CHECK(!Consensus::CheckTxInputs(CTransaction{zero_time}, zero_time_state, coins,
                                          /*nSpendHeight=*/2, fee));
    BOOST_CHECK_EQUAL(zero_time_state.GetRejectReason(), "bad-txns-time-earlier-than-input");

    const CMutableTransaction exact_time{MakeV2Spend(timed_prevout, input_time, input_value - CENT)};
    TxValidationState exact_time_state;
    BOOST_CHECK(Consensus::CheckTxInputs(CTransaction{exact_time}, exact_time_state, coins,
                                         /*nSpendHeight=*/2, fee));
    BOOST_CHECK_EQUAL(fee, CENT);

    CTxOut marker;
    marker.SetEmpty();
    const COutPoint marker_prevout{uint256S("03"), 0};
    coins.AddCoin(marker_prevout,
                  Coin{marker, /*height=*/1, /*coinbase=*/false, /*coinstake=*/false, input_time},
                  /*possible_overwrite=*/false);
    const CMutableTransaction marker_spend{MakeV2Spend(marker_prevout, input_time, /*value_out=*/0)};
    TxValidationState marker_state;
    BOOST_CHECK(!Consensus::CheckTxInputs(CTransaction{marker_spend}, marker_state, coins,
                                          /*nSpendHeight=*/2, fee));
    BOOST_CHECK_EQUAL(marker_state.GetRejectReason(), "bad-txns-spend-empty-marker");
}

BOOST_AUTO_TEST_SUITE_END()
