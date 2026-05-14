# Handoff: RingDownAnalysisCpp Follow-Up

## Scope

This follows the first implementation pass from
`docs/handoffs/20260513_ringdownanalysis-cpp-agent-1.md`.

## Current Status

Superseded status note: the remaining items listed in this handoff were
implemented in the follow-up commits after this document was created. See
`docs/RESULTS_AND_PERFORMANCE.md` for the current validation record.

Implemented and committed on `main`:

- Python-generated JSON goldens for deterministic signal, CRLB, estimator, and
  array-analysis behavior.
- C++ public APIs for signal generation, CRLB, estimators, analyzer, batch, and
  Monte Carlo.
- A Moku-style CSV loader and CLI `analyze <file.csv>` JSON export path.
- Fixture-backed CTest coverage for CRLB, deterministic signal generation,
  estimators, analyzer workflow, and Monte Carlo determinism.

The current suite passes with:

- `cmake --build --preset dev`
- `ctest --preset dev`

`clang-format` was not available in the local shell, so formatting is only
manually aligned with the existing style.

## Problem, Feature, Or Goal

The repository now has a functional C++ vertical slice, but not every acceptance
criterion from the original full-port handoff is complete. The next goal is to
turn this into a production-ready scientific port by closing dependency,
validation, and performance gaps.

## Context

Relevant files:

- Public API: `include/ringdown/*.hpp`
- Implementations: `src/*.cpp`
- CLI: `apps/ringdown_cli.cpp`
- Golden fixture generator: `tools/generate_reference_fixtures.py`
- Fixture tests: `tests/test_algorithms.cpp`
- First fixture: `tests/fixtures/reference/synthetic_5hz_seed42.json`

Important design choices already made:

- The C++ NLS path uses separable least squares: for fixed frequency and tau, it
  solves amplitude, phase, and offset linearly, then searches frequency/tau.
- The DFT path uses an in-tree radix-2 FFT and interpolated peak estimate. It is
  intentionally dependency-free for the first vertical slice.
- Exact NumPy RNG stream parity is not required. Monte Carlo determinism is
  based on C++ seeds.

## Remaining Work

1. Select and wire a MAT/HDF5 dependency, then implement `RingDownDataLoader`
   MAT support equivalent to the Python `moku.data` path.
2. Expand fixtures with real CSV files from `../RingDownAnalysis/data/`
   and compare C++ analyzer outputs against Python tolerances.
3. Add a release/performance benchmark that covers estimator and Monte Carlo
   workloads beyond the current smoke target.
4. Tighten numerical parity after profiling: decide whether the current
   separable least-squares implementation is good enough or whether to add a
   dedicated optimization dependency.
5. Add exported-result examples for Python/Jupyter inspection.

## Success Criteria

- C++ can analyze representative CSV and MAT files from the Python reference
  data set.
- Batch summaries and Monte Carlo statistics are covered by regression tests.
- Benchmarks document C++ runtime against the Python baseline.
- Release and dev presets pass on macOS and Linux.

## References

- Original handoff:
  `docs/handoffs/20260513_ringdownanalysis-cpp-agent-1.md`
- Python reference package: `../RingDownAnalysis`
- Data format notes: `../RingDownAnalysis/docs/data_format.md`
- Pipeline audit: `../RingDownAnalysis/DATA_ANALYSIS_PIPELINE_AUDIT.md`
