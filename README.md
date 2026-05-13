# RingDownAnalysisCpp

Modern C++20 implementation of the core data-analysis pipeline from the Python
`RingDownAnalysis` project.

The goal is to run production analyses and large Monte Carlo studies entirely in
C++, while still exporting results that are easy to inspect from Python and
Jupyter notebooks.

## Current Status

This repository contains the first C++ vertical slice of the Python
`RingDownAnalysis` pipeline:

- deterministic ring-down signal generation
- CRLB frequency and Q diagnostics
- DFT and separable least-squares frequency/tau/Q estimators
- array analysis and Moku-style CSV, MAT, and single-CSV ZIP loading
- sequential/parallel batch processing helpers
- deterministic Monte Carlo runner
- JSON export for analysis results
- fixture-backed CTest coverage against Python-generated numerical goldens
- representative benchmark target
- CI workflow for macOS and Linux

Real CSV and MAT files from the Python reference checkout have been analyzed
from the C++ CLI. See `docs/RESULTS_AND_PERFORMANCE.md` for commands, measured
outputs, and Python/Jupyter JSON inspection examples.

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

## CLI

```bash
build/dev/apps/ringdown_cli analyze path/to/data.csv
build/dev/apps/ringdown_cli analyze path/to/data.mat
build/dev/apps/ringdown_cli analyze path/to/data.zip
build/dev/apps/ringdown_cli batch path/to/a.csv path/to/b.mat path/to/c.zip
build/dev/apps/ringdown_cli monte-carlo 100 100000 8
build/dev/apps/ringdown_cli monte-carlo-smoke
```
