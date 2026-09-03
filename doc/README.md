TrollCoin
=============

Setup
---------------------
TrollCoin 3.0 is a full client for the TrollCoin network and builds the backbone of the network. It downloads and stores the entire history of TrollCoin transactions, which requires several gigabytes of disk space. Depending on the speed of your computer and network connection, the synchronization process can take from a few minutes to several hours.

To download TrollCoin, visit [trollcoin.com](https://trollcoin.com).

Running
---------------------
The following are some helpful notes on how to run TrollCoin on your native platform.

### Unix

Unpack the files into a directory and run:

- `./trollcoin-qt` (GUI) or
- `./trollcoind` (headless)

### Windows

Unpack the files into a directory, and then run trollcoin-qt.exe.

### macOS

Unpack the files, drag TrollCoin-Qt to your Applications folder, and then run TrollCoin-Qt.

### Upgrading from 2.x

See [Upgrading from TrollCoin 2.x](upgrade.md).

### Need Help?

* Visit the TrollCoin website: [trollcoin.com](https://trollcoin.com)
* Report problems on the [issue tracker](https://github.com/SidGrip/TrollCoin/issues).

Building
---------------------
The release binaries are produced by the multi-platform build script
[`build.sh`](/build.sh) in the repository root — use it as the reference for a
known-working build on every supported platform. The notes below cover the
manual per-platform paths: necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [Guix Building Guide](guix.md)

Development
---------------------
The repo's [root README](/README.md) covers the network's consensus parameters, the `build.sh` build lanes, and legacy wallet migration.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Shared Libraries](shared-libraries.md)
- [BIPS](bips.md)
- [Benchmarking](benchmarking.md)
- [Internal Design Docs](design/)

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [trollcoin.conf Configuration File](trollcoin-conf.md)
- [CJDNS Support](cjdns.md)
- [Descriptors](descriptors.md)
- [External Signer Support](external-signer.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [I2P Support](i2p.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [Managing Wallets](managing-wallets.md)
- [Multisig Tutorial](multisig-tutorial.md)
- [P2P bad ports definition and list](p2p-bad-ports.md)
- [PSBT support](psbt.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Transaction Relay Policy](policy/README.md)
- [USDT Tracing](tracing.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
