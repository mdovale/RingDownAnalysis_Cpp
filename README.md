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
- [`examples/`](examples/) — notebook-shaped exports and Python/Jupyter parity checks

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

The reference Python project lives next to this repository at
`../RingDownAnalysis`. Use it to generate golden fixtures and compare behavior,
but do not vendor it into this repository.

## CLI

`ringdown` is the primary user-facing command. The `ringdown_cli` target is
still built as a compatibility name during the migration.

```bash
build/dev/apps/ringdown analyze path/to/data.csv
build/dev/apps/ringdown analyze path/to/data.mat
build/dev/apps/ringdown analyze path/to/data.zip
build/dev/apps/ringdown batch --workers 4 --report minimal path/to/data-dir
build/dev/apps/ringdown batch --file-list paths.txt --report full --progress
build/dev/apps/ringdown monte-carlo --trials 100 --samples 100000 --workers 8 --seed 42
build/dev/apps/ringdown monte-carlo-smoke
```

Batch directory inputs discover `.csv`, `.mat`, and single-CSV `.zip` archives.
By default per-file failures are reported in JSON and produce a warning on
stderr; add `--fail-on-error` when any failed file should make the command
return nonzero.
