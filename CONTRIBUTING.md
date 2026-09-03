Contributing to TrollCoin
============================

The TrollCoin project operates an open contributor model where anyone is
welcome to contribute towards development in the form of peer review, testing
and patches. This document explains the practical process and guidelines for
contributing.

Repository maintainers are responsible for merging pull requests, releases,
and moderation.

Getting Started
---------------

New contributors are very welcome and needed.

Reviewing and testing open pull requests is highly valued and the most
effective way to contribute as a new contributor. It also teaches you more
about the code than opening pull requests.

Before you start contributing, familiarize yourself with the build system
(see [doc/](doc)) and the unit tests in [src/test](src/test).

Issues labelled `good first issue` are suitable for contributors without a
deep understanding of the codebase. You do not need to request permission to
start working on an issue, but leaving a comment helps other contributors see
which issues are actively being addressed.

Discussion about codebase improvements happens in GitHub issues and pull
requests on this repository.

Contributor Workflow
--------------------

Everyone contributes patch proposals using pull requests (PRs):

  1. Fork the repository
  1. Create a topic branch
  1. Commit patches
  1. Push changes to your fork
  1. Create a pull request

The project coding conventions in the [developer notes](doc/developer-notes.md)
must be followed.

### Committing Patches

In general, [commits should be atomic](https://en.wikipedia.org/wiki/Atomic_commit#Atomic_commit_convention)
and diffs should be easy to read. Do not mix formatting fixes or code moves
with actual code changes.

Make sure each individual commit is hygienic: that it builds successfully on
its own without warnings, errors, regressions, or test failures.

Commit messages should consist of a short subject line (50 chars max), a
blank line and detailed explanatory text as separate paragraph(s), unless the
title alone is self-explanatory (like "Correct typo in init.cpp") in which
case a single title line is sufficient. If a commit references another issue,
add the reference, for example `refs #1234` or `fixes #4321`. Commit messages
should never contain any `@` mentions.

### Creating the Pull Request

The title of the pull request should be prefixed by the component or area
that the pull request affects. Valid areas:

  - `consensus` for changes to consensus critical code
  - `doc` for changes to the documentation
  - `qt` or `gui` for changes to trollcoin-qt
  - `log` for changes to log messages
  - `net` or `p2p` for changes to the peer-to-peer network code
  - `refactor` for structural changes that do not change behavior
  - `rpc`, `rest` or `zmq` for changes to the RPC, REST or ZMQ APIs
  - `script` for changes to the scripts and tools
  - `test` or `ci` for changes to the tests or CI code
  - `util` or `lib` for changes to the utils or libraries
  - `wallet` for changes to the wallet code
  - `build` for changes to the build system

The body of the pull request should describe *what* the patch does, and even
more importantly, *why*, with justification and reasoning.

If a pull request is not yet to be considered for merging, prefix the title
with [WIP] or use task lists in the body to indicate tasks are pending.

### Squashing and Rebasing

If your pull request contains fixup commits, you may be asked to
[squash](https://git-scm.com/docs/git-rebase#_interactive_mode) them before
it is reviewed. When a pull request conflicts with the target branch, you may
be asked to rebase it on top of the current target branch. Please use the
pull request that is already open to amend changes rather than opening a new
one for the same change.

### Peer Review

Anyone may participate in peer review, which is expressed by comments in the
pull request. Typical review comments:

 * `Concept (N)ACK` — "I do (not) agree with the general goal of this pull
   request". A `NACK` should include a rationale.
 * `ACK <commit>` — the reviewer has reviewed (and ideally tested) the change
   at that commit.
 * A "nit" refers to a trivial, often non-blocking issue.

You are expected to reply to review comments before your pull request is
merged. You may update the code or reject the feedback if you do not agree
with it, but you should say so in a reply.

Decision Making
---------------

Whether a pull request is merged rests with the repository maintainers.
In general, all pull requests must:

  - Have a clear use case, fix a demonstrable bug or serve the greater good
    of the project;
  - Be well peer-reviewed;
  - Have tests, where appropriate;
  - Follow code style guidelines ([developer notes](doc/developer-notes.md));
  - Not break the existing test suite;
  - Change relevant comments and documentation when behaviour of code changes.

Where a patch set affects consensus-critical code, the bar is much higher in
terms of discussion and peer review requirements, keeping in mind that
mistakes could be very costly to the wider community. This includes
refactoring of consensus-critical code.

Copyright
---------

By contributing to this repository, you agree to license your work under the
MIT license unless specified otherwise in `contrib/debian/copyright` or at
the top of the file itself. Any work contributed where you are not the original
author must contain its license header with the original author(s) and source.
