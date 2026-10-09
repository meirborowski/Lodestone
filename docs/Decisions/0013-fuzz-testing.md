# 0013 - Fuzz Testing with a Built-In Mutation Fuzzer

## Status
Accepted

## Context
Everything that parses untrusted input needs fuzz tests, run for a fixed time budget as the `fuzz` test tier (see [Testing & CI](../Testing.md)). Milestone 3 adds the first: the scene and asset metadata loaders. The fuzzers have to run on every CI platform and compiler - MSVC, Apple Clang, GCC and Clang - and under the sanitizers.

## Decision
- **A small mutation fuzzer in the test support library** - `RunFuzzer()` (`Tests/Common/Fuzzer.h`) feeds a target its corpus (`Tests/Fuzz/Corpus/<name>`), then random mutations of it until the time budget runs out: bit flips, interesting bytes, insertions, deletions, duplications, splices from other inputs, and strings from a per-format dictionary (keys, component names, extreme numbers). Mutated inputs sometimes join the pool, so mutations stack
- **Reproducible** - mutations depend only on the corpus and a seed. The seed is random unless `LS_FUZZ_SEED` sets it, and every run prints it, so a failure on CI is reproduced locally with the same seed. `LS_FUZZ_SECONDS` sets the budget per target (5 seconds by default)
- **Round-trip oracle** - besides never crashing, an input that loads must save and load again to the same document. That catches loaders accepting input their writers can't represent, not just crashes
- **Fuzz tests are doctest test cases** in `LodestoneFuzzTests`, under the `fuzz` label, so they run wherever the tests run - including the ASan+UBSan job, which turns memory errors into failures

## Alternatives
- **libFuzzer** - coverage-guided, so it finds deep bugs faster, but it's only available with Clang (and recent MSVC), and needs its own build setup per compiler. Worth adding later as an extra Clang-only CI job, with the same targets and corpus
- **AFL++** - also coverage-guided, but Linux-centric and run as a separate process

## Consequences
- Without coverage feedback, the fuzzer relies on its corpus and dictionary to reach deep code; new loaders bring a corpus that covers their format's features, and a dictionary of its keys
- A fuzz failure is a real bug, even when it appears on one CI run and not the next - reproduce it with the printed seed
