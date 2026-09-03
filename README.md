<p align="center">
  <img src="https://avatars2.githubusercontent.com/u/16044831?v=3&u=c30f9a963a650436d286920035513bc94828d560&s=140" alt="TrollCoin Logo">
</p>

What is TrollCoin?
----------------

TrollCoin is a decentralised digital currency with near-instant transactions and negligible fees, built on Proof of Stake 3.1 (PoSv3, BPoS). The software connects to the TrollCoin peer-to-peer network to download and fully validate blocks and transactions.

For more information, see https://trollcoin.com.

TrollCoin 3.0 — modernized client (consensus-identical drop-in)
---------------------------------------------------------------

TrollCoin 3.0 re-implements the original TrollCoin chain on the Bitcoin Core
26.2 / Blackcoin More codebase. It joins the **same live network** and extends
the **same chain** — every consensus parameter below is byte-identical to the
original wallet, proven by a full from-genesis sync (8.5M+ blocks, 0 rejects).
Network user agent: `/Trolloshi:3.0/`.

| Parameter | Value (unchanged from the original TrollCoin) |
|---|---|
| Block PoW hash | scrypt(1024,1,1); PoW ended at block 7,777,777 |
| Consensus | pure Proof-of-Stake (PoSv3, value-weighted) after PoW; flat 7 TROLL reward |
| Minimum fee | 0.0001 TROLL per started kB — `(1 + ⌊vbytes/1000⌋) × 0.0001`; no fee estimation, no fee market |
| Target spacing | 64 s (early PoW: 60 s) |
| Stake maturity | 777 confirmations |
| Coinbase / coinstake maturity | 77 confirmations |
| Coinstake timestamp mask | 0xf (16 s) |
| Max block size | 7,000,000 bytes |
| Network magic | 70 35 22 05 |
| Default P2P port | 15000 |
| Base58 prefixes | pubkey 66 (`T…`), script 5, secret 153 |
| Genesis hash | 000001faef25dec4fbcf906e6242621df2c183bf232f263d0ba5b101911e4563 |
| Genesis time / nonce / bits | 1393221600 / 164482 / 0x1e0fffff |
| DNS seeds | dnsfeed.trollcoin.com |

The original wallet's 1-hour minimum stake age was wallet-side coin-selection
policy only; it is subsumed by the 777-confirmation maturity rule (~13.8 h at
64 s spacing), so on-chain staking eligibility is identical.

Test networks
-------------

Testnet and regtest enable the modern script stack from height 1 for
development and testing — CSV (BIP68/112/113), SegWit (BIP141/143/147),
Taproot (BIP340/341/342) and strict DER (BIP66) — so bech32/bech32m
(`ttroll1…` on testnet, `trollrt1…` on regtest) addresses can be exercised there. Mainnet keeps these rules off
and uses legacy base58 (`T…`) addresses.

Descriptor (HD) wallets are the **default on every network**, independent of
these soft forks. On mainnet today a descriptor wallet simply holds legacy
`pkh()` (base58) descriptors; bech32/bech32m descriptors become available once
the script stack is enabled.

Multi-wallet staking
--------------------

Each loaded wallet stakes **independently** — there is no pooling across wallets.
On load, every wallet runs its own staking thread over its own coins with its own
stake weight (its eligible UTXOs: mature, 777-confirmation, amount-weighted).
`getstakinginfo` reports only the selected/queried wallet's weight, never a
node-wide total (the only global figure shown is *network* weight, for the
expected-time estimate).

- **Daemon and Qt:** every loaded wallet that has staking enabled (`-staking`,
  default on; per-wallet `staking true`) and is unlocked stakes concurrently —
  load 3 wallets and you get 3 independent stakers.
- **Qt display gotcha:** the status-bar staking icon reflects only the
  *selected* wallet. Other loaded, enabled, unlocked wallets are still staking
  even when the icon shows a different wallet — selection is just a view.
- Holding the same coins in two loaded wallets is harmless (one block simply
  orphans the other; no double-spend). Multiple wallets give key separation;
  consolidating into one is marginally more efficient but not required.

Building from source (`build.sh`)
---------------------------------

A single `build.sh` at the repo root builds the daemon, CLI, `tx`, `wallet` and
Qt5 GUI for every supported platform. Run `./build.sh --help` for the full
option list.

```text
./build.sh [PLATFORM ...] [TARGET] [OPTIONS]

Platforms (default: --native):
  --native      build for THIS host      (Mac -> Homebrew qt@5 .app; Windows -> MSYS2/MINGW64; Linux -> autotools)
  --ubuntu24    Ubuntu 24.04 in Docker   (sidgrip/native-base:24.04, GCC 13)
  --ubuntu26    Ubuntu 26.04 in Docker   (sidgrip/native-base:26.04, GCC 15)
  --windows     Windows x86_64 .exe      (MXE cross-build; sidgrip/mxe-base, static Qt5)
  --macos       macOS x86_64 .app        (on a Mac same as --native; on Linux: osxcross cross-build)
  --appimage    portable Linux AppImage  (sidgrip/appimage-base:22.04)
  --all         ubuntu24 + ubuntu26 + windows + macos + appimage (skips a failed
                platform, prints a summary; macOS uses the osxcross container on Linux)

Targets (default: both):
  --daemon      trollcoind + trollcoin-cli + trollcoin-tx + trollcoin-wallet
  --qt          trollcoin-qt only
  --both        daemon + Qt

Options:
  --jobs N        parallel make jobs             (default: nproc-1)
  --pull-docker   docker pull any missing image from Docker Hub
  --build-docker  docker build any missing image from ./docker/Dockerfile.*
  --strip         strip the --native binaries    (cross builds are always stripped)
  --copy DIR      also copy the --native binaries into DIR
  --clean         re-run configure + 'make clean' first (--native only)
  --release       compress ./outputs/<platform>/ into ready-to-ship archives
  -h | --help     show this help
```

Every configure path enables **SQLite** (descriptor wallets, alongside the
Berkeley DB legacy wallets) and full **hardening** (PIE, full RELRO, BIND_NOW,
NX, stack canary, CET control-flow, separate-code). There is no separate flag —
all release artifacts are SQLite-enabled and hardened by default.

### Quick start

```bash
./build.sh                       # native daemon + Qt on this host
./build.sh --ubuntu24 --both     # reproducible Ubuntu 24.04 release build (Docker)
./build.sh --all --pull-docker   # every platform, pulling base images as needed
./build.sh --all --release       # build all, then package the outputs/release/ archives
./build.sh --release             # package whatever is already in outputs/ (standalone)
```

The `--ubuntu24`/`--ubuntu26`, `--windows`, `--appimage` and (on Linux) `--macos`
lanes need Docker and the `sidgrip/*-base` images (pass `--pull-docker` to fetch
them from Docker Hub). The `--macos` lane builds **natively with Homebrew**
(`qt@5`, `berkeley-db@4`, `boost`) when run on a Mac, and **cross-compiles in the
`osxcross` container** when run on Linux — no Mac required.

### Output layout

```text
outputs/
├── Ubuntu-24/   trollcoind  trollcoin-cli  trollcoin-qt  trollcoin-tx  trollcoin-wallet  trollcoin-util
├── Ubuntu-26/   (same set)
├── Windows/     same set as .exe + bundled runtime DLLs / Qt assets
├── macOS/       same set + TrollCoin-Qt.app + TrollCoin.zip
├── AppImage/    TrollCoin-3.0-x86_64.AppImage
└── release/     compressed per-platform archives + SHA256SUMS   (written by --release)
```

### Docker images

`--pull-docker` / `--build-docker` use these base images:

| Image | Purpose |
|---|---|
| `sidgrip/native-base:24.04`   | Ubuntu 24.04 build (GCC 13) |
| `sidgrip/native-base:26.04`   | Ubuntu 26.04 build (GCC 15) |
| `sidgrip/mxe-base:latest`     | Windows MXE cross-compile (static Qt5) |
| `sidgrip/appimage-base:22.04` | Ubuntu 22+ AppImage |
| `sidgrip/osxcross-base:sdk-26.2` | macOS cross-compile (osxcross) |

(The macOS lane can instead build on a real Mac over SSH via `MAC_HOST=user@mac`.)

Importing legacy private keys
-----------------------------

TrollCoin 3.0 uses descriptor wallets. To move an **entire** old wallet across, convert it in place — keys, addresses and labels are preserved.

In the Qt wallet: **File → Open Wallet**, let it load, then **File → Migrate Wallet**.

Or with `trollcoin-cli`:

    trollcoin-cli migratewallet "<wallet_name>"

To import **individual** old keys instead, the old `importprivkey` is unavailable — use `importdescriptors`:

    # 1. get the checksum for your key
    trollcoin-cli getdescriptorinfo "pkh(<WIF_PRIVATE_KEY>)"

    # 2. import it, keeping the WIF; timestamp 0 rescans the chain for existing coins
    trollcoin-cli -rpcwallet=<wallet> importdescriptors \
      '[{"desc":"pkh(<WIF_PRIVATE_KEY>)#<checksum>","timestamp":0,"label":"imported"}]'

Use the `checksum` field from step 1 — **not** its `descriptor` field, which is public-key only and imports watch-only (unspendable). A full rescan can take a while on a synced node.

License
-------

TrollCoin is released under the terms of the MIT license. See [COPYING](COPYING) for more
information or see https://opensource.org/licenses/MIT.
