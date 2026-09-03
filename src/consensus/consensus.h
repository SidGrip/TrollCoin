// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_CONSENSUS_H
#define BITCOIN_CONSENSUS_CONSENSUS_H

#include <cstdlib>
#include <stdint.h>

/** The maximum allowed size for a serialized block, in bytes (TrollCoin: 7 MB) */
static const unsigned int MAX_BLOCK_SERIALIZED_SIZE = 7000000;
/** The maximum allowed weight for a block (4 x MAX_BLOCK_SERIALIZED_SIZE). After SegWit activates,
 *  the witness-inclusive serialized size is separately capped at MAX_BLOCK_SERIALIZED_SIZE in
 *  ContextualCheckBlock, so the effective maximum block size stays 7 MB. */
static const unsigned int MAX_BLOCK_WEIGHT = 28000000;
/** The maximum allowed signature-check cost in a block (TrollCoin 7 MB / 50 = 140k count x 4) */
static const int64_t MAX_BLOCK_SIGOPS_COST = 560000;

static const int WITNESS_SCALE_FACTOR = 4;

// TrollCoin's four-byte nTime raises Bitcoin's 60/10-byte lower bounds.
static const size_t MIN_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 64;
static const size_t MIN_SERIALIZABLE_TRANSACTION_WEIGHT = WITNESS_SCALE_FACTOR * 14;

/** Flags for nSequence and nLockTime locks */
/** Interpret sequence numbers as relative lock-time constraints. */
static constexpr unsigned int LOCKTIME_VERIFY_SEQUENCE = (1 << 0);

#endif // BITCOIN_CONSENSUS_CONSENSUS_H
