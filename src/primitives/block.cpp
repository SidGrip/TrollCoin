// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>
#include <crypto/common.h>
#include <crypto/scrypt.h>

uint256 CBlockHeader::GetHash() const
{
    // Legacy headers (nVersion<=6): identity hash is the cached scrypt hash. v>6 uses
    // sha256d (uncached; never on the live chain).
    if (nVersion > 6)
        return (CHashWriter{} << *this).GetHash();
    return GetPoWHash();
}

uint256 CBlockHeader::GetPoWHash() const
{
    // Return the memoized hash if set; acquire/release makes a parallel precompute's
    // store fully visible before we read m_pow_hash.
    if (m_pow_hash_valid.load(std::memory_order_acquire))
        return m_pow_hash;

    uint256 thash;
    scrypt_1024_1_1_256(BEGIN(nVersion), BEGIN(thash));
    m_pow_hash = thash;
    m_pow_hash_valid.store(true, std::memory_order_release);
    return thash;
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, nFlags=%08x, vtx=%u, vchBlockSig=%s)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        nFlags, vtx.size(),
        HexStr(vchBlockSig));
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}
