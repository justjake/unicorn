Unicorn Engine
==============

[![pypi downloads](https://pepy.tech/badge/unicorn)](https://pepy.tech/project/unicorn)
[![Fuzzing Status](https://oss-fuzz-build-logs.storage.googleapis.com/badges/unicorn.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:unicorn)


<p align="center">
<img width="250" src="docs/unicorn-logo.png">
</p>

Unicorn is a lightweight, multi-platform, multi-architecture CPU emulator framework, based on [QEMU](http://qemu.org).

Unicorn offers some unparalleled features:

- Multi-architecture: ARM, ARM64 (ARMv8), M68K, MIPS, PowerPC, RISCV, SPARC, S390X, TriCore and X86 (16, 32, 64-bit)
- Clean/simple/lightweight/intuitive architecture-neutral API
- Implemented in pure C language, with bindings for Crystal, Clojure, Visual Basic, Perl, Rust, Ruby, Python, Java, .NET, Go, Delphi/Free Pascal, Haskell, Pharo, Lua and Zig.
- Native support for Windows & *nix (with Mac OSX, Linux, Android, *BSD & Solaris confirmed)
- High performance via Just-In-Time compilation
- Support for fine-grained instrumentation at various levels
- Thread-safety by design
- Distributed under free software license GPLv2

Further information is available at http://www.unicorn-engine.org

Embedder extensions in this fork
-------------------------------

This fork builds on Unicorn 2.1.4 with extensions for reducing instrumentation
overhead and running the translation engine without a host JIT:

- Page-scoped memory hooks and explicit physical-page watches.
- ARM translation-time instruction filtering, bounded block retries and
  instruction restarts at memory-hook boundaries. A filter may replace an
  instruction with its code hook, which then performs it inside translated code
  (`uc_embedder_features()` bit 32).
- Budgeted block accounting with an embedder-owned trace ring.
- Translated coarse block accounting: fast ARM blocks debit the interactive
  budget and test asynchronous pauses in generated code
  (`uc_embedder_features()` bit 256). Exact accounting keeps its ring write
  in the block helper; a fast block enters that helper only once its budget
  runs out or a pause arrives.
- `uc_embedder_store_watched` (`uc_embedder_features()` bit 4096) tells
  whether a store is certain to reach watched write hooks: every target page
  it touches holds a watched 1 KiB page. Other stores reach those hooks only
  when they take the slow path for the engine's own reasons.
- Block chains (`uc_embedder_set_block_chains`, `uc_embedder_block_chain`,
  `uc_embedder_admit_block`; `uc_embedder_features()` bit 1024): one admitted
  run can cover several translated blocks without a block hook call for each.
  A block hook admits the block it was called for with a weight, a key and a
  tag. That translation is then entered without its hooks and without the
  exit request helper while no exit is requested, the guard word of its page
  slot equals its key and the budget covers its weight; translated code
  subtracts the weight and records the tag. Every other entry calls the
  hooks as before. The embedder owns the budget and the 64 guard words, and
  an admission ends with its translation. Disabled by default; enabled with
  nothing admitted, every block still calls its hooks.
- `FUJIMOD_PERF_MAP` writes a perf-map line for every installed translation,
  for host profilers and Samply. Set it to a path or `1` for
  `/tmp/perf-PID.map`; entries flush periodically and at process exit.
- Indirect branches remember their last target per processor. The entry is
  keyed by the same block-state word the block lookup compares and dropped
  whenever a translation is invalidated, so a repeated return skips the jump
  cache probe.
- Observation of mapped host-memory pages during TLB fills.
- Cortex-A7 ACTLR with its reset value and Secure/Non-secure write
  permissions (SMP writable from Non-secure only when NSACR.NS_SMP is set).
- A bounded owner queue for accepted Cortex-A7 shared TLB maintenance
  (`uc_embedder_tlbi_attach`, `_status`, `_ack`; `uc_embedder_features()`
  bit 512). Each engine records the inner-shareable invalidations its guest
  executed; the embedder delivers them to peer processors and acknowledges
  each record before the engine runs again.
- Memory read and write hooks that return into their translated block leave
  ITSTATE as translated code expects it (`uc_embedder_features()` bit 2048).
  The hook still sees the ITSTATE of the accessing instruction.
- Stoppable Thumb instruction-entry hooks with synchronized ITSTATE, including
  instructions whose condition fails (`uc_embedder_features()` bit 4).
- An optional Tiny Code Interpreter (TCI) backend, including Emscripten support.

Extension exports use the `uc_embedder_` namespace. Their implementation and
contracts are in `uc.c` and `include/uc_priv.h`; these are experimental extensions
to the upstream API. Existing upstream project information follows below.

The host JIT remains the default. To build the ARM interpreter backend:

```sh
cmake -S . -B build -G Ninja -DUNICORN_ARCH=arm -DUNICORN_TCG_INTERPRETER=ON
cmake --build build
ctest --test-dir build -R '^test_arm$' --output-on-failure
```

The ARM context regression also runs against the optimized host JIT:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DUNICORN_ARCH=arm -DUNICORN_BUILD_TESTS=ON
cmake --build build-release --target test_arm
./build-release/test_arm test_arm_context_repeated_restore
```


License
-------

This project is released under the [GPL license](COPYING).


Compilation & Docs
------------------

See [docs/COMPILE.md](docs/COMPILE.md) file for how to compile and install Unicorn.

More documentation is available in [docs/README.md](docs/README.md).

For common questions, read [docs/FAQ.md](docs/FAQ.md) before raising an issue.

Contact
-------

[Contact us](http://www.unicorn-engine.org/contact/) via mailing list, email or twitter for any questions.


Join [our group](https://t.me/+lnNl0fPpyCYzZmVh) for instant feedback.

Contribute
----------

If you want to contribute, please pick up something from our [Github issues](https://github.com/unicorn-engine/unicorn/issues).

We also maintain a list of more challenged problems in [milestones](https://github.com/unicorn-engine/unicorn/milestones) for our regular release.

Please send pull request to our [dev branch](https://github.com/unicorn-engine/unicorn/tree/dev).

[CREDITS.TXT](CREDITS.TXT) records important contributors of our project.
