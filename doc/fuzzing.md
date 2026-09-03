# Fuzzing TrollCoin using libFuzzer

## Quickstart guide

To quickly get started fuzzing TrollCoin using [libFuzzer](https://llvm.org/docs/LibFuzzer.html):

```sh
$ git clone https://github.com/SidGrip/TrollCoin.git
$ cd TrollCoin/
$ ./autogen.sh
$ CC=clang CXX=clang++ ./configure --enable-fuzz --with-sanitizers=address,fuzzer,undefined
# macOS users: If you have problem with this step then make sure to read
# "macOS hints for libFuzzer" below.
$ make
$ FUZZ=process_message src/test/fuzz/fuzz
# abort fuzzing using ctrl-c
```

## Overview

[Google](https://github.com/google/fuzzing/) has a good overview of fuzzing in general, with contributions from key architects of some of the most-used fuzzers. [John Regehr](https://blog.regehr.org/archives/1687) provides good advice on writing code that assists fuzzers in finding bugs, which is useful for developers to keep in mind.

## Fuzzing harnesses and output

`process_message` (`src/test/fuzz/process_message.cpp`) is a fuzzing harness for the `ProcessMessage(...)` function in `src/net_processing.cpp`. The available fuzzing harnesses are found in `src/test/fuzz/`.

The fuzzer will output `NEW` every time it has created a test input that covers new areas of the code under test. For more information on how to interpret the fuzzer output, see the [libFuzzer documentation](https://llvm.org/docs/LibFuzzer.html).

If you specify a corpus directory then any new coverage increasing inputs will be saved there:

```sh
$ mkdir -p process_message-seeded-from-thin-air/
$ FUZZ=process_message src/test/fuzz/fuzz process_message-seeded-from-thin-air/
INFO: Seed: 840522292
INFO: Loaded 1 modules   (424174 inline 8-bit counters): 424174 [0x55e121ef9ab8, 0x55e121f613a6),
INFO: Loaded 1 PC tables (424174 PCs): 424174 [0x55e121f613a8,0x55e1225da288),
INFO:        0 files found in process_message-seeded-from-thin-air/
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 4096 bytes
INFO: A corpus is not provided, starting from an empty corpus
#2      INITED cov: 94 ft: 95 corp: 1/1b exec/s: 0 rss: 150Mb
#3      NEW    cov: 95 ft: 96 corp: 2/3b lim: 4 exec/s: 0 rss: 150Mb L: 2/2 MS: 1 InsertByte-
#4      NEW    cov: 96 ft: 98 corp: 3/7b lim: 4 exec/s: 0 rss: 150Mb L: 4/4 MS: 1 CrossOver-
...
# abort fuzzing using ctrl-c
$ ls process_message-seeded-from-thin-air/
349ac589fc66a09abc0b72bb4ae445a7a19e2cd8 4df479f1f421f2ea64b383cd4919a272604087a7
a640312c98dcc55d6744730c33e41c5168c55f09 b135de16e4709558c0797c15f86046d31c5d86d7
c000f7b41b05139de8b63f4cbf7d1ad4c6e2aa7f fc52cc00ec1eb1c08470e69f809ae4993fa70082
$ cat --show-nonprinting process_message-seeded-from-thin-air/349ac589fc66a09abc0b72bb4ae445a7a19e2cd8
block^@M-^?M-^?M-^?M-^?M-^?nM-^?M-^?
```

In this case the fuzzer managed to create a `block` message which when passed to `ProcessMessage(...)` increased coverage.

It is possible to specify `trollcoind` arguments to the `fuzz` executable.
Depending on the test, they may be ignored or consumed and alter the behavior
of the test. Just make sure to use double-dash to distinguish them from the
fuzzer's own arguments:

```sh
$ FUZZ=address_deserialize src/test/fuzz/fuzz -runs=1 fuzz_corpora/address_deserialize --checkaddrman=5 --printtoconsole=1
```

## Fuzzing corpora

Since most of the code under test is shared with the upstream Bitcoin Core
codebase, the upstream seed corpora in the
[`bitcoin-core/qa-assets`](https://github.com/bitcoin-core/qa-assets) repo can
be used as a starting point:

```sh
$ git clone https://github.com/bitcoin-core/qa-assets
$ FUZZ=process_message src/test/fuzz/fuzz qa-assets/fuzz_corpora/process_message/
```

## Run without sanitizers for increased throughput

Fuzzing on a harness compiled with `--with-sanitizers=address,fuzzer,undefined` is good for finding bugs. However, the very slow execution even under libFuzzer will limit the ability to find new coverage. A good approach is to perform occasional long runs without the additional bug-detectors (configure `--with-sanitizers=fuzzer`) and use libFuzzer's `-merge=1` option to fold the new inputs into your corpus.  Patience is useful; even with improved throughput, libFuzzer may need days and 10s of millions of executions to reach deep/hard targets.

## macOS hints for libFuzzer

The default Clang/LLVM version supplied by Apple on macOS does not include
fuzzing libraries, so macOS users will need to install a full version, for
example using `brew install llvm`.

Should you run into problems with the address sanitizer, it is possible you
may need to run `./configure` with `--disable-asm` to avoid errors
with certain assembly code in the codebase. See [developer notes on sanitizers](developer-notes.md#sanitizers)
for more information.

You may also need to take care of giving the correct path for `clang` and
`clang++`, like `CC=/path/to/clang CXX=/path/to/clang++` if the non-systems
`clang` does not come first in your path.

Full configure that was tested on macOS with `brew` installed `llvm`:

```sh
./configure --enable-fuzz --with-sanitizers=fuzzer,address,undefined --disable-asm CC=$(brew --prefix llvm)/bin/clang CXX=$(brew --prefix llvm)/bin/clang++
```

Read the [libFuzzer documentation](https://llvm.org/docs/LibFuzzer.html) for more information. This [libFuzzer tutorial](https://github.com/google/fuzzing/blob/master/tutorial/libFuzzerTutorial.md) might also be of interest.

# Fuzzing TrollCoin using afl++

## Quickstart guide

To quickly get started fuzzing TrollCoin using [afl++](https://github.com/AFLplusplus/AFLplusplus):

```sh
$ git clone https://github.com/SidGrip/TrollCoin.git
$ cd TrollCoin/
$ git clone https://github.com/AFLplusplus/AFLplusplus
$ make -C AFLplusplus/ source-only
$ ./autogen.sh
# If afl-clang-lto is not available, see
# https://github.com/AFLplusplus/AFLplusplus#a-selecting-the-best-afl-compiler-for-instrumenting-the-target
$ CC=$(pwd)/AFLplusplus/afl-clang-lto CXX=$(pwd)/AFLplusplus/afl-clang-lto++ ./configure --enable-fuzz
$ make
# For macOS you may need to ignore x86 compilation checks when running "make". If so,
# try compiling using: AFL_NO_X86=1 make
$ mkdir -p inputs/ outputs/
$ echo A > inputs/thin-air-input
$ FUZZ=bech32 AFLplusplus/afl-fuzz -i inputs/ -o outputs/ -- src/test/fuzz/fuzz
# You may have to change a few kernel parameters to test optimally - afl-fuzz
# will print an error and suggestion if so.
```

Read the [afl++ documentation](https://github.com/AFLplusplus/AFLplusplus) for more information.

# Fuzzing TrollCoin using Honggfuzz

## Quickstart guide

To quickly get started fuzzing TrollCoin using [Honggfuzz](https://github.com/google/honggfuzz):

```sh
$ git clone https://github.com/SidGrip/TrollCoin.git
$ cd TrollCoin/
$ ./autogen.sh
$ git clone https://github.com/google/honggfuzz
$ cd honggfuzz/
$ make
$ cd ..
$ CC=$(pwd)/honggfuzz/hfuzz_cc/hfuzz-clang CXX=$(pwd)/honggfuzz/hfuzz_cc/hfuzz-clang++ ./configure --enable-fuzz --with-sanitizers=address,undefined
$ make
$ mkdir -p inputs/
$ FUZZ=process_message honggfuzz/honggfuzz -i inputs/ -- src/test/fuzz/fuzz
```

Read the [Honggfuzz documentation](https://github.com/google/honggfuzz/blob/master/docs/USAGE.md) for more information.

## Fuzzing the P2P layer using Honggfuzz NetDriver

Honggfuzz NetDriver allows for very easy fuzzing of TCP servers such as
`trollcoind` without having to write any custom fuzzing harness. The
`trollcoind` server process is largely fuzzed without modification.

This makes the fuzzing highly realistic: a bug reachable by the fuzzer is likely
also remotely triggerable by an untrusted peer.

To quickly get started fuzzing the P2P layer using Honggfuzz NetDriver:

```sh
$ git clone https://github.com/SidGrip/TrollCoin.git
$ cd TrollCoin/
$ ./autogen.sh
$ git clone https://github.com/google/honggfuzz
$ cd honggfuzz/
$ make
$ cd ..
$ CC=$(pwd)/honggfuzz/hfuzz_cc/hfuzz-clang \
      CXX=$(pwd)/honggfuzz/hfuzz_cc/hfuzz-clang++ \
      ./configure --disable-wallet --with-gui=no \
                  --with-sanitizers=address,undefined
$ git apply << "EOF"
diff --git a/src/compat/compat.h b/src/compat/compat.h
index 435a403552..a9a6e8123b 100644
--- a/src/compat/compat.h
+++ b/src/compat/compat.h
@@ -89,8 +89,12 @@ typedef char* sockopt_arg_type;
 // building with a binutils < 2.36 is subject to this ld bug.
 #define MAIN_FUNCTION __declspec(dllexport) int main(int argc, char* argv[])
 #else
+#ifdef HFND_FUZZING_ENTRY_FUNCTION_CXX
+#define MAIN_FUNCTION HFND_FUZZING_ENTRY_FUNCTION_CXX(int argc, char* argv[])
+#else
 #define MAIN_FUNCTION int main(int argc, char* argv[])
 #endif
+#endif

 // Note these both should work with the current usage of poll, but best to be safe
 // WIN32 poll is broken https://daniel.haxx.se/blog/2012/10/10/wsapoll-is-broken/
diff --git a/src/net.cpp b/src/net.cpp
index 989b7e867d..5fe40aa99b 100644
--- a/src/net.cpp
+++ b/src/net.cpp
@@ -717,7 +717,7 @@ int V1Transport::readHeader(Span<const uint8_t> msg_bytes)
     }

     // Check start string, network magic
-    if (hdr.pchMessageStart != m_magic_bytes) {
+    if (false && hdr.pchMessageStart != m_magic_bytes) { // skip network magic checking
         LogPrint(BCLog::NET, "Header error: Wrong MessageStart %s received, peer=%d\n", HexStr(hdr.pchMessageStart), m_node_id);
         return -1;
     }
@@ -782,7 +782,7 @@ CNetMessage V1Transport::GetReceivedMessage(const std::chrono::microseconds time
     RandAddEvent(ReadLE32(hash.begin()));

     // Check checksum and header message type string
-    if (memcmp(hash.begin(), hdr.pchChecksum, CMessageHeader::CHECKSUM_SIZE) != 0) {
+    if (false && memcmp(hash.begin(), hdr.pchChecksum, CMessageHeader::CHECKSUM_SIZE) != 0) { // skip checksum checking
         LogPrint(BCLog::NET, "Header error: Wrong checksum (%s, %u bytes), expected %s was %s, peer=%d\n",
                  SanitizeString(msg.m_type), msg.m_message_size,
                  HexStr(Span{hash}.first(CMessageHeader::CHECKSUM_SIZE)),
EOF
$ make -C src/ trollcoind
$ mkdir -p inputs/
$ honggfuzz/honggfuzz --exit_upon_crash --quiet --timeout 4 -n 1 -Q \
      -E HFND_TCP_PORT=35714 -f inputs/ -- \
          src/trollcoind -regtest -discover=0 -dns=0 -dnsseed=0 -listenonion=0 \
                         -nodebuglogfile -bind=127.0.0.1:35714 -logthreadnames \
                         -debug
```

# Fuzzing TrollCoin using Eclipser (v1.x)

## Quickstart guide

To quickly get started fuzzing TrollCoin using [Eclipser v1.x](https://github.com/SoftSec-KAIST/Eclipser/tree/v1.x):

```sh
$ git clone https://github.com/SidGrip/TrollCoin.git
$ cd TrollCoin/
$ sudo vim /etc/apt/sources.list # Uncomment the lines starting with 'deb-src'.
$ sudo apt-get update
$ sudo apt-get build-dep qemu
$ sudo apt-get install libtool libtool-bin wget automake autoconf bison gdb
```

At this point, you must install the .NET core.  The process differs, depending on your Linux distribution.
See [this link](https://learn.microsoft.com/en-us/dotnet/core/install/linux) for details.
On Ubuntu 20.04, the following should work:

```sh
$ wget -q https://packages.microsoft.com/config/ubuntu/20.04/packages-microsoft-prod.deb
$ sudo dpkg -i packages-microsoft-prod.deb
$ rm packages-microsoft-prod.deb
$ sudo apt-get update
$ sudo apt-get install -y dotnet-sdk-2.1
```

You will also want to make sure Python is installed as `python` for the Eclipser install to succeed.

```sh
$ git clone https://github.com/SoftSec-KAIST/Eclipser.git
$ cd Eclipser
$ git checkout v1.x
$ make
$ cd ..
$ ./autogen.sh
$ ./configure --enable-fuzz
$ make
$ mkdir -p outputs/
$ FUZZ=bech32 dotnet Eclipser/build/Eclipser.dll fuzz -p src/test/fuzz/fuzz -t 36000 -o outputs --src stdin
```

This will perform 10 hours of fuzzing.

To make further use of the inputs generated by Eclipser, you
must first decode them:

```sh
$ dotnet Eclipser/build/Eclipser.dll decode -i outputs/testcase -o decoded_outputs
```
This will place raw inputs in the directory `decoded_outputs/decoded_stdins`.  Crashes are in the `outputs/crashes` directory, and must
be decoded in the same way.

Note that fuzzing with Eclipser on certain targets (those that create 'full nodes', e.g. `process_message*`) will,
for now, slowly fill `/tmp/` with improperly cleaned-up files, which will cause spurious crashes.

Read the [Eclipser documentation for v1.x](https://github.com/SoftSec-KAIST/Eclipser/tree/v1.x) for more details on using Eclipser.
