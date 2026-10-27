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
- Observation of mapped host-memory pages during TLB fills.
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
