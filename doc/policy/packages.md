# Package Mempool Accept

## Definitions

A **package** is an ordered list of transactions, representable by a connected Directed Acyclic
Graph (a directed edge exists between a transaction that spends the output of another transaction).

For every transaction `t` in a **topologically sorted** package, if any of its parents are present
in the package, they appear somewhere in the list before `t`.

A **child-with-unconfirmed-parents** package is a topologically sorted package that consists of
exactly one child and all of its unconfirmed parents (no other transactions may be present).
The last transaction in the package is the child, and its package can be canonically defined based
on the current state: each of its inputs must be available in the UTXO set as of the current chain
tip or some preceding transaction in the package.

Packages can be submitted to the mempool with the `submitpackage` RPC. Passing a list of
transactions to `testmempoolaccept` performs the test-accept variant of package validation.

## Package Mempool Acceptance Rules

The following rules are enforced for all packages:

* Packages cannot exceed `MAX_PACKAGE_COUNT=25` count and `MAX_PACKAGE_WEIGHT=404000` total weight
   (#20833)

   - *Rationale*: We want package size to be as small as possible to mitigate DoS via package
     validation. However, we want to make sure that the limit does not restrict ancestor
     packages that would be allowed if submitted individually.

   - Note that, if these mempool limits change, package limits should be reconsidered. Users may
     also configure their mempool limits differently.

   - Note that this is transaction weight, not "virtual" size as with other limits to allow
     simpler context-less checks.

* Packages must be topologically sorted. (#20833)

* Packages cannot have conflicting transactions, i.e. no two transactions in a package can spend
   the same inputs. Packages cannot have duplicate transactions. (#20833)

* No transaction in a package can conflict with a mempool transaction. TrollCoin does not
  support transaction replacement. (#20833)

* When packages are evaluated against ancestor/descendant limits, the union of all transactions'
  descendants and ancestors is considered. (#21800)

   - *Rationale*: This is essentially a "worst case" heuristic intended for packages that are
     heavily connected, i.e. some transaction in the package is the ancestor or descendant of all
     the other transactions.

The following rules are only enforced for packages to be submitted to the mempool (not enforced for
test accepts):

* Packages must be child-with-unconfirmed-parents packages. This also means packages must contain at
  least 2 transactions. (#22674)

   - *Rationale*: This allows for fee-bumping by CPFP. Allowing multiple parents makes it possible
     to fee-bump a batch of transactions. Restricting packages to a defined topology is easier to
     reason about and simplifies the validation logic greatly.

   - Warning: Batched fee-bumping may be unsafe for some use cases. Users and application developers
     should take caution if utilizing multi-parent packages.

* Transactions in the package that have the same txid as another transaction already in the mempool
  will be removed from the package prior to submission ("deduplication").

   - *Rationale*: Node operators are free to set their mempool policies however they please, nodes
     may receive transactions in different orders, and malicious counterparties may try to take
     advantage of policy differences to pin or delay propagation of transactions. As such, it's
     possible for some package transaction(s) to already be in the mempool, and there is no need to
     repeat validation for those transactions or double-count them in fees.

   - *Rationale*: We want to prevent potential censorship vectors. We should not reject entire
     packages because we already have one of the transactions. Also, if an attacker first broadcasts
     a competing package or transaction with a mutated witness, even though the two
     same-txid-different-witness transactions are conflicting and cannot replace each other, the
     honest package should still be considered for acceptance.

### Package Fees and Feerate

*Package Feerate* is the total transaction fees divided by the total virtual size of all
transactions in the package.
If any transactions in the package are already in the mempool, they are not submitted again
("deduplicated") and are thus excluded from this calculation.

TrollCoin does not use a dynamic mempool minimum feerate: the `mempoolminfee` reported by
`getmempoolinfo` always equals the minimum relay feerate (`-minrelaytxfee`, default
0.0001 TROLL/kvB). When a package is submitted, the minimum relay feerate is checked against the package feerate
instead of each transaction's individual feerate (the flat network minimum fee is separate and
per-transaction — see the note below). For example, on a node with `-minrelaytxfee` raised to 0.001 TROLL/kvB, a parent
transaction paying only the flat network minimum fee is rejected on its own ("min relay fee not
met") but may be accepted when submitted in a package with a high-feerate child.

*Rationale*: This can be thought of as "CPFP within a package," allowing a presigned transaction
(i.e. one that cannot be re-signed with a higher fee) to be accepted by nodes configured with a
higher minimum relay feerate.

Note: Every transaction must still pay the flat network minimum fee (0.0001 TROLL per started
kilobyte of virtual size) out of its own fees. This check is applied to each transaction
individually, and package feerate cannot be used to meet it: a parent paying less than the flat
minimum is rejected (`bad-txns-fee-not-enough`) even when submitted in a package with a
high-feerate child.

Implementation Note: Transactions within a package are always validated individually first, and
package validation is used for the transactions that failed. Since package feerate is only
calculated using transactions that are not in the mempool, this implementation detail affects the
outcome of package validation.

*Rationale*: It would be incorrect to use the fees of transactions that are already in the mempool, as
we do not want a transaction's fees to be double-counted.

*Rationale*: Packages are intended for incentive-compatible fee-bumping: transaction B is a
"legitimate" fee-bump for transaction A only if B is a descendant of A and has a *higher* feerate
than A. We want to prevent "parents pay for children" behavior; fees of parents should not help
their children, since the parents can be included in a block without the child.  More generally, if
transaction A is not needed in order for transaction B to be included in a block, A's fees cannot
help B. In a
child-with-parents package, simply excluding any parent transactions that meet feerate requirements
individually is sufficient to ensure this.

*Rationale*: We must not allow a low-feerate child to prevent its parent from being accepted; fees
of children should not negatively impact their parents, since they are not necessary for the parents
to be included in a block. More generally, if transaction B is not needed in order for transaction A
to be included in a block, B's fees cannot harm A. In a child-with-parents package, simply
validating parents individually first is sufficient to ensure this.

*Rationale*: As a principle, we want to avoid accidentally restricting policy in order to be
backward-compatible for users and applications that rely on p2p transaction relay. Concretely,
package validation should not prevent the acceptance of a transaction that would otherwise be
policy-valid on its own. By always accepting a transaction that passes individual validation before
trying package validation, we prevent any unintentional restriction of policy.
