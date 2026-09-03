# Mempool Limits

## Definitions

Given any two transactions Tx0 and Tx1 where Tx1 spends an output of Tx0,
Tx0 is a *parent* of Tx1 and Tx1 is a *child* of Tx0.

A transaction's *ancestors* include, recursively, its parents, the parents of its parents, etc.
A transaction's *descendants* include, recursively, its children, the children of its children, etc.

A mempool entry's *ancestor count* is the total number of in-mempool (unconfirmed) transactions in
its ancestor set, including itself.
A mempool entry's *descendant count* is the total number of in-mempool (unconfirmed) transactions in
its descendant set, including itself.

A mempool entry's *ancestor size* is the aggregated virtual size of in-mempool (unconfirmed)
transactions in its ancestor set, including itself.
A mempool entry's *descendant size* is the aggregated virtual size of in-mempool (unconfirmed)
transactions in its descendant set, including itself.

Transactions submitted to the mempool must not exceed the ancestor and descendant limits (aka
mempool *package limits*) set by the node (see `-limitancestorcount`, `-limitancestorsize`,
`-limitdescendantcount`, `-limitdescendantsize`).

## Exemptions

### Small Transaction Exemption

If a transaction candidate for submission to the mempool would exceed an ancestor or descendant
limit, an exemption is made if the candidate transaction is no more than 10,000 virtual bytes.
Such a transaction is accepted regardless of the ancestor and descendant limits.

A candidate transaction larger than 10,000 virtual bytes that exceeds any of the limits is
rejected (`too-long-mempool-chain`).

The exemption applies to individually submitted transactions only; multi-transaction (package)
evaluations enforce the limits without it.

*Rationale*: this is a widened form of the Bitcoin Core CPFP carve-out, introduced upstream in
[PR #15681](https://github.com/bitcoin/bitcoin/pull/15681) to prevent pinning by domination of a
transaction's descendant limits in two-party contract protocols such as LN (see the [mailing list
post](https://lists.linuxfoundation.org/pipermail/bitcoin-dev/2018-November/016518.html)).
The upstream carve-out's additional conditions — an ancestor count of exactly 2, and exceeding
the descendant limit by no more than 1 — are not applied here.
