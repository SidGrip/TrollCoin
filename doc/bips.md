# BIP support in TrollCoin 3.0

TrollCoin 3.0 is built on the Bitcoin Core 26.2 codebase (via Blackcoin More), so
it inherits most of Bitcoin Core's BIP implementations. This page lists what
applies to TrollCoin. TrollCoin is pure Proof-of-Stake — there is no mining, so
mining-only BIPs are omitted.

## Active

* [`BIP 11`](https://github.com/bitcoin/bips/blob/master/bip-0011.mediawiki): Multisig outputs are standard.
* [`BIP 13`](https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki) / [`BIP 16`](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki): Pay-to-script-hash (P2SH) addresses and evaluation rules.
* [`BIP 14`](https://github.com/bitcoin/bips/blob/master/bip-0014.mediawiki): The subversion string is used as the User Agent (`/Trolloshi:3.0/`).
* [`BIP 21`](https://github.com/bitcoin/bips/blob/master/bip-0021.mediawiki): Payment URIs, using the `trollcoin:` scheme.
* [`BIP 30`](https://github.com/bitcoin/bips/blob/master/bip-0030.mediawiki): The rule forbidding new transactions with the same txid as an existing not-fully-spent transaction is always enforced.
* [`BIP 31`](https://github.com/bitcoin/bips/blob/master/bip-0031.mediawiki): The 'pong' protocol message.
* [`BIP 32`](https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki): Hierarchical Deterministic wallets. Descriptor wallets derive keys along
  [`BIP 43`](https://github.com/bitcoin/bips/blob/master/bip-0043.mediawiki)/[`44`](https://github.com/bitcoin/bips/blob/master/bip-0044.mediawiki)/[`49`](https://github.com/bitcoin/bips/blob/master/bip-0049.mediawiki)/[`84`](https://github.com/bitcoin/bips/blob/master/bip-0084.mediawiki)/[`86`](https://github.com/bitcoin/bips/blob/master/bip-0086.mediawiki) paths; the default address type on mainnet is legacy base58 ("T...").
* [`BIP 35`](https://github.com/bitcoin/bips/blob/master/bip-0035.mediawiki): The 'mempool' protocol message (available to `NODE_BLOOM` peers only).
* [`BIP 37`](https://github.com/bitcoin/bips/blob/master/bip-0037.mediawiki): Bloom filtering for SPV clients. Disabled by default; enable with `-peerbloomfilters`.
* [`BIP 65`](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki): CHECKLOCKTIMEVERIFY is enforced.
* [`BIP 147`](https://github.com/bitcoin/bips/blob/master/bip-0147.mediawiki): the NULLDUMMY rule is enforced alongside BIP 65 (both under the inherited v3 protocol gate), independent of the parked SegWit deployment.
* [`BIP 111`](https://github.com/bitcoin/bips/blob/master/bip-0111.mediawiki): The `NODE_BLOOM` service bit.
* [`BIP 130`](https://github.com/bitcoin/bips/blob/master/bip-0130.mediawiki): Direct headers announcement.
* [`BIP 133`](https://github.com/bitcoin/bips/blob/master/bip-0133.mediawiki): feefilter messages are respected and sent.
* [`BIP 152`](https://github.com/bitcoin/bips/blob/master/bip-0152.mediawiki): Compact block transfer and related optimizations.
* [`BIP 155`](https://github.com/bitcoin/bips/blob/master/bip-0155.mediawiki): The 'addrv2' and 'sendaddrv2' messages, enabling relay of Tor v3 (and other network) addresses.
* [`BIP 157`](https://github.com/bitcoin/bips/blob/master/bip-0157.mediawiki) / [`158`](https://github.com/bitcoin/bips/blob/master/bip-0158.mediawiki): Compact block filters can be indexed (`-blockfilterindex`) and served to peers (`-peerblockfilters`).
* [`BIP 159`](https://github.com/bitcoin/bips/blob/master/bip-0159.mediawiki): The `NODE_NETWORK_LIMITED` service bit is signalled and honoured.
* [`BIP 174`](https://github.com/bitcoin/bips/blob/master/bip-0174.mediawiki): RPCs to operate on Partially Signed Transactions (PSBT).
* [`BIP 324`](https://github.com/bitcoin/bips/blob/master/bip-0324.mediawiki): The v2 encrypted transport protocol and `NODE_P2P_V2` service bit. On by default; disable with `-v2transport=0`.
* [`BIP 339`](https://github.com/bitcoin/bips/blob/master/bip-0339.mediawiki): Transaction relay by wtxid.
* [`BIP 380`](https://github.com/bitcoin/bips/blob/master/bip-0380.mediawiki)–[`386`](https://github.com/bitcoin/bips/blob/master/bip-0386.mediawiki): Output script descriptors.

## Implemented but parked on mainnet

The following soft forks are present in the code but **not active on TrollCoin
mainnet** (no activation height is set). They are active on testnet and regtest
for development:

* [`BIP 66`](https://github.com/bitcoin/bips/blob/master/bip-0066.mediawiki): Strict DER signatures.
* [`BIP 68`](https://github.com/bitcoin/bips/blob/master/bip-0068.mediawiki) / [`112`](https://github.com/bitcoin/bips/blob/master/bip-0112.mediawiki) / [`113`](https://github.com/bitcoin/bips/blob/master/bip-0113.mediawiki): Sequence locks, CHECKSEQUENCEVERIFY, and median-time-past lock-time calculations (CSV).
* [`BIP 141`](https://github.com/bitcoin/bips/blob/master/bip-0141.mediawiki) / [`143`](https://github.com/bitcoin/bips/blob/master/bip-0143.mediawiki) / [`144`](https://github.com/bitcoin/bips/blob/master/bip-0144.mediawiki) / [`147`](https://github.com/bitcoin/bips/blob/master/bip-0147.mediawiki): Segregated Witness.
* [`BIP 173`](https://github.com/bitcoin/bips/blob/master/bip-0173.mediawiki) / [`350`](https://github.com/bitcoin/bips/blob/master/bip-0350.mediawiki): Bech32/Bech32m addresses. HRPs: `troll` (mainnet, inactive), `ttroll` (testnet), `trollrt` (regtest).
* [`BIP 340`](https://github.com/bitcoin/bips/blob/master/bip-0340.mediawiki) / [`341`](https://github.com/bitcoin/bips/blob/master/bip-0341.mediawiki) / [`342`](https://github.com/bitcoin/bips/blob/master/bip-0342.mediawiki): Taproot (Schnorr signatures and Tapscript).
* [`BIP 371`](https://github.com/bitcoin/bips/blob/master/bip-0371.mediawiki): Taproot fields for PSBT (usable where Taproot is active).

## Not used

* [`BIP 9`](https://github.com/bitcoin/bips/blob/master/bip-0009.mediawiki): Versionbits signalling is not used. TrollCoin activations are height-based ([`BIP 90`](https://github.com/bitcoin/bips/blob/master/bip-0090.mediawiki)-style buried deployments).
* [`BIP 34`](https://github.com/bitcoin/bips/blob/master/bip-0034.mediawiki): The coinbase-height rule is not enforced on any network — TrollCoin's historic chain predates it — although newly created blocks still place the height in the coinbase.
* [`BIP 125`](https://github.com/bitcoin/bips/blob/master/bip-0125.mediawiki): Opt-in replace-by-fee has been removed: a transaction conflicting with one already in the mempool is rejected. The `bip125-replaceable` RPC fields remain for compatibility but only report signalling.
* [`BIP 325`](https://github.com/bitcoin/bips/blob/master/bip-0325.mediawiki): The signet code is retained from upstream, but TrollCoin does not operate a signet.
