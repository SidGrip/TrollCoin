# Support for signing transactions outside of TrollCoin

TrollCoin can be launched with `-signer=<cmd>` where `<cmd>` is an external tool which can sign transactions and perform other functions. For example, it can be used to communicate with a hardware wallet.

External signer support is compiled in when Boost::Process is available (the `--enable-external-signer` configure option, default `auto`). Binaries built without it lack the `-signer` option and the RPC methods described below.

## Example usage

The following example is based on the [HWI](https://github.com/bitcoin-core/HWI) tool. Version 2.0 or newer is required. Although this tool is hosted under the Bitcoin Core GitHub organization and maintained by Bitcoin Core developers, it should be used with caution. It is considered experimental and has far less review than Bitcoin Core itself. Be particularly careful when running tools such as these on a computer with private keys on it.

Note that HWI and hardware wallet firmware understand Bitcoin's networks and address formats, not TrollCoin's. On mainnet the signer is called with `--chain main`, which HWI treats as Bitcoin mainnet: the device derives Bitcoin coin-type keys and displays addresses in Bitcoin's encoding, which will not visually match TrollCoin addresses. SegWit address types are not active on TrollCoin mainnet. Treat hardware wallet signing on TrollCoin as experimental.

When using a hardware wallet, consult the manufacturer website for (alternative) software they recommend. As long as their software conforms to the standard below, it should be able to work with TrollCoin.

Start TrollCoin:

```sh
$ trollcoind -signer=../HWI/hwi.py
```

### Device setup

Follow the hardware manufacturers instructions for the initial device setup, as well as their instructions for creating a backup. Alternatively, for some devices, you can use the `setup`, `restore` and `backup` commands provided by [HWI](https://github.com/bitcoin-core/HWI).

### Create wallet and import keys

Get a list of signing devices / services:

```
$ trollcoin-cli enumeratesigners
{
  "signers": [
    {
      "fingerprint": "c8df832a",
      "name": "trezor_t"
    }
  ]
}
```

The master key fingerprint is used to identify a device.

Create a wallet, this automatically imports the public keys:

```sh
$ trollcoin-cli createwallet "hww" true true "" true true true true
```

The final `true` is the `external_signer` argument; without it the wallet is created as a plain blank watch-only wallet and no keys are imported.

### Verify an address

Display an address on the device:

```sh
$ trollcoin-cli -rpcwallet=<wallet> getnewaddress
$ trollcoin-cli -rpcwallet=<wallet> walletdisplayaddress <address>
```

Replace `<address>` with the result of `getnewaddress`.

### Spending

Under the hood this uses a [Partially Signed Bitcoin Transaction](psbt.md).

```sh
$ trollcoin-cli -rpcwallet=<wallet> sendtoaddress <address> <amount>
```

This prompts your hardware wallet to sign, and fails if it's not connected. If successful it automatically broadcasts the transaction and returns the transaction id.

## Signer API

In order to be compatible with TrollCoin any signer command should conform to the specification below. This specification is subject to change. Ideally a BIP should propose a standard so that other wallets can also make use of it.

Prerequisite knowledge:
* [Output Descriptors](descriptors.md)
* Partially Signed Bitcoin Transaction ([PSBT](psbt.md))

### `enumerate` (required)

Usage:
```
$ <cmd> enumerate
[
    {
        "fingerprint": "00000000"
    }
]
```

The command MUST return an (empty) array with at least a `fingerprint` field. A `model` field, if present, is used as the device name. An entry MAY instead contain a string `error` field; enumeration then fails with that message.

A future extension could add an optional return field with device capabilities. Perhaps a descriptor with wildcards. For example: `["pkh("44'/0'/$'/{0,1}/*"), sh(wpkh("49'/0'/$'/{0,1}/*")), wpkh("84'/0'/$'/{0,1}/*")]`. This would indicate the device supports legacy, wrapped SegWit and native SegWit. In addition it restricts the derivation paths that can used for those, to maintain compatibility with other wallet software. It also indicates the device, or the driver, doesn't support multisig.

A future extension could add an optional return field `reachable`, in case `<cmd>` knows a signer exists but can't currently reach it.

### `signtx` (required)

The command is invoked as:

```
$ <cmd> --stdin --fingerprint <fingerprint> --chain <chain>
```

with the following written to its standard input:

```
signtx "<base64 psbt>"
```

The command MUST return a JSON object with a `psbt` field containing the base64-encoded psbt with any signatures, or an `error` field with a message.

The `psbt` SHOULD include bip32 derivations. The command SHOULD fail if none of the bip32 derivations match a key owned by the device. Note that TrollCoin only calls the signer when at least one input's master key fingerprint matches the device.

The command SHOULD fail if the user cancels.

The command MAY complain if `--chain` is not `main`, but any of the BIP32 derivation paths contain a coin type other than `1h` (and vice versa).

### `getdescriptors` (optional)

Usage:

```
$ <cmd> --fingerprint <fingerprint> --chain <chain> getdescriptors --account <account>
```

Returns descriptors supported by the device. Example:

```
$ <cmd> --fingerprint 00000000 --chain test getdescriptors --account 0
{
  "receive": [
    "pkh([00000000/44h/0h/0h]xpub6C.../0/*)#fn95jwmg",
    "sh(wpkh([00000000/49h/0h/0h]xpub6B..../0/*))#j4r9hntt",
    "wpkh([00000000/84h/0h/0h]xpub6C.../0/*)#qw72dxa9"
  ],
  "internal": [
    "pkh([00000000/44h/0h/0h]xpub6C.../1/*)#c8q40mts",
    "sh(wpkh([00000000/49h/0h/0h]xpub6B..../1/*))#85dn0v75",
    "wpkh([00000000/84h/0h/0h]xpub6C..../1/*)#36mtsnda"
  ]
}
```

### `displayaddress` (optional)

Usage:
```
<cmd> --fingerprint <fingerprint> --chain <chain> displayaddress --desc <descriptor>
```

Example, display the first native SegWit receive address on Testnet:

```
<cmd> --fingerprint 00000000 --chain test displayaddress --desc "wpkh([00000000/84h/1h/0h]tpubDDUZ..../0/0)"
```

The command MUST be able to figure out the address type from the descriptor.

If <descriptor> contains a master key fingerprint, the command MUST fail if it does not match the fingerprint known by the device.

If <descriptor> contains an xpub, the command MUST fail if it does not match the xpub known by the device.

The command MAY complain if `--chain` is not `main`, but the BIP32 coin type is not `1h` (and vice versa).

## How TrollCoin uses the Signer API

The `enumeratesigners` RPC simply calls `<cmd> enumerate`.

The `createwallet` RPC calls:

* `<cmd> --fingerprint <fingerprint> --chain <chain> getdescriptors --account 0`

It then imports descriptors for all supported address types, in a BIP44/49/84 compatible manner.

The `walletdisplayaddress` RPC reuses some code from `getaddressinfo` on the provided address and obtains the inferred descriptor. It then calls `<cmd> --fingerprint <fingerprint> --chain <chain> displayaddress --desc <descriptor>`.

`sendtoaddress` and `sendmany` check `inputs->bip32_derivs` to see if any inputs have the same `master_fingerprint` as the signer. If so, it calls `<cmd> --stdin --fingerprint <fingerprint> --chain <chain>` and writes `signtx "<psbt>"` to its standard input. It waits for the device to return a (partially) signed psbt, tries to finalize it and broadcasts the transaction.
