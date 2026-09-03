Upgrading from TrollCoin 2.x
===========================

TrollCoin 3.0 joins the same network and the same chain as 2.x. Your coins and
addresses are unchanged. What does change is how the client stores the chain on
disk, so 3.0 cannot reuse the files 2.x left behind and syncs from the network
on first start.

**Your wallet is the only thing that matters. Back it up before you touch
anything else.**

Quick version
-------------

1. Close the 2.x wallet completely.
2. Rename your data folder to `TrollCoin-old` (see paths below).
3. Start TrollCoin 3.0. It creates a fresh folder and syncs from the network.
4. Close 3.0, copy `wallet.dat` from `TrollCoin-old` into the new folder, start
   3.0 again.

Renaming rather than deleting means the old wallet is untouched and you can go
back to 2.x at any point by renaming the folder back.

Where your data folder is
-------------------------

| System | Path |
| --- | --- |
| Windows | `%APPDATA%\TrollCoin` |
| macOS | `~/Library/Application Support/TrollCoin` |
| Linux | `~/.trollcoin` |

2.x and 3.0 use the same location, which is why the old files have to be moved
out of the way.

Step by step
------------

**1. Close the 2.x wallet** and wait until it has fully shut down. Check that no
`trollcoind` or `trollcoin-qt` process is still running.

**2. Back up your wallet.** Copy `wallet.dat` out of the data folder to a USB
stick or another drive. Copy `trollcoin.conf` too if you changed it. Nothing
else in that folder is worth keeping.

**3. Rename the data folder** from `TrollCoin` to `TrollCoin-old` (on Linux,
`.trollcoin` to `.trollcoin-old`). This is the step that lets you undo the
upgrade later.

**4. Start TrollCoin 3.0.** It creates a new, empty data folder and begins
downloading and verifying the chain. This takes a while — the chain is over
8.5 million blocks.

**5. Bring your wallet across.** Close 3.0, copy `wallet.dat` from
`TrollCoin-old` into the new data folder, and start 3.0 again. Your balance
appears once the sync has caught up.

Once 3.0 has been running happily for a while, you can delete `TrollCoin-old`
to reclaim the disk space. The old block files (`blk0001.dat` and the
`txleveldb` folder) are of no use to any 3.0 client.

Going back to 2.x
-----------------

Close 3.0, delete or rename its data folder, and rename `TrollCoin-old` back to
`TrollCoin`. 2.x picks up exactly where it left off.

Exporting your private keys as a second backup
----------------------------------------------

A copy of `wallet.dat` is enough for most people. If you would also like a
plain-text copy of your keys, the 2.x client can produce one before you
upgrade. Open the Debug Console (Help > Debug Window > Console) and use
`dumpwallet` for every key in the wallet, or `dumpprivkey` for a single
address.

Treat the result like cash: anyone who reads that file can spend your coins.
Keep it offline and delete it when you no longer need it.

Importing a private key into 3.0
--------------------------------

New 3.0 wallets are descriptor wallets, which do not accept `importprivkey`.
Import a key with `importdescriptors` instead.

First ask the client to check the descriptor and give you its checksum:

```
getdescriptorinfo "pkh(YOUR_PRIVATE_KEY)"
```

That returns a `checksum` field. Append it to the descriptor after a `#` and
import:

```
importdescriptors "[{\"desc\":\"pkh(YOUR_PRIVATE_KEY)#CHECKSUM\",\"timestamp\":\"now\",\"label\":\"imported\"}]"
```

`"timestamp": "now"` skips rescanning history. If the key has older
transactions you want the wallet to find, use the approximate Unix time the key
was first used instead, and the wallet will rescan from there.

Use `pkh(...)` for mainnet addresses, which are the `T...` form. Check the
result with `getaddressinfo <address>`; `"ismine": true` means the wallet holds
the key.

Note that `getdescriptorinfo` also returns a `descriptor` field. That one has
the private key stripped out and only imports as watch-only, so use your own
`pkh(KEY)#CHECKSUM` string, not that field.

Converting an old wallet instead
--------------------------------

If you would rather keep the wallet you have and modernise it, load it in 3.0
and run `migratewallet`. It converts a legacy wallet to a descriptor wallet in
place and writes a `<name>-<timestamp>.legacy.bak` backup first.

Configuration
-------------

Your old `trollcoin.conf` can be reused. Settings that no longer exist are
ignored with a warning in the log rather than stopping the client.
