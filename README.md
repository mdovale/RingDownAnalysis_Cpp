# RingDownAnalysisCpp

Modern C++20 implementation of the core data-analysis pipeline from the Python
`RingDownAnalysis` project.

The goal is to run production analyses and large Monte Carlo studies entirely in
C++, while still exporting results that are easy to inspect from Python and
Jupyter notebooks.

## Current Status

This repository currently contains project infrastructure only:

- CMake-based C++20 library scaffold
- smoke-test executable wired into CTest
- command-line app placeholder
- benchmark smoke target
- CI workflow for macOS and Linux
- fixture-generation helper for Python reference outputs
- handoff document for the full implementation pass

The implementation work starts from `docs/handoffs/20260513_ringdownanalysis-cpp-agent-1.md`.

## Build

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

For optimized builds:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

## Reference Python Project

The local `.read-only/RingDownAnalysis` checkout is for porting reference only
and is intentionally ignored by git. Use it to generate golden fixtures and to
compare behavior, but do not vendor it into this repository.
