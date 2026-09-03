# Transaction Relay Policy

**Policy** (Mempool or Transaction Relay Policy) is the node's set of validation rules, in addition
to consensus, enforced for unconfirmed transactions before submitting them to the mempool. These
rules are local to the node and configurable (e.g. `-minrelaytxfee`, `-limitancestorsize`,
`-maxmempool`). Policy may include restrictions on the transaction itself, the transaction
in relation to the current chain tip, and the transaction in relation to the node's mempool
contents. Policy is *not* applied to transactions in blocks.

TrollCoin does not support transaction replacement (RBF): a transaction cannot be
replaced while unconfirmed.

This documentation is not an exhaustive list of all policy rules.

- [Mempool Limits](mempool-limits.md)
- [Packages](packages.md)

