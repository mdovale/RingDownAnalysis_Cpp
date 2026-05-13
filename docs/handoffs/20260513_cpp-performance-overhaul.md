# Handoff: C++ Performance Overhaul

## Scope

This handoff covers a repo-wide performance investigation for the C++20 port.
It supersedes neither the existing narrow batch handoff nor the Python parity
requirements; it gives the next session a concrete path to make C++ faster than
the Python reference across analysis, batch, estimator, loader, and reporting
paths.

## Problem / Goal

The C++ port is currently slower than Python in the most important user-facing
analysis paths. This is unacceptable for a C++ rewrite whose purpose is to run
large production analyses and Monte Carlo studies.

The current evidence is mixed:

- Release C++ is already faster for DFT and Monte Carlo smoke workloads.
- C++ is much slower for NLS and full array analysis, where Python delegates
  vector math and optimization to NumPy/SciPy native code.
- The batch notebook path has also been observed running `build/dev` binaries,
  and an old `build/dev/examples/batch_analysis_example --workers 2` process was
  still active during this investigation. Debug-build measurements should not be
  treated as production performance data, but the release NLS gap is real.

Reproduction / measurement commands from the repo root:

```bash
build/release/benchmarks/ringdown_benchmark_smoke
build/dev/benchmarks/ringdown_benchmark_smoke
.venv/bin/python - <<'PY'
from time import perf_counter
import numpy as np
from ringdownanalysis import RingDownSignal, DFTFrequencyEstimator, NLSFrequencyEstimator, RingDownAnalyzer, MonteCarloAnalyzer

params = dict(f0=5.0, fs=100.0, N=10000, snr_db=60.0)
rng = np.random.default_rng(42)

def measure(name, fn):
    start = perf_counter()
    result = fn()
    print(f"{name}_ms={(perf_counter() - start) * 1000.0:.3f}")
    return result

t, x, _ = measure("signal_generation_10k", lambda: RingDownSignal(**params).generate(phi0=0.2, rng=rng))
signal = RingDownSignal(**params)
measure("dft_estimate_10k", lambda: DFTFrequencyEstimator(window="rect").estimate_full(x, signal.fs))
measure("nls_estimate_10k", lambda: NLSFrequencyEstimator(tau_known=None).estimate_full(x, signal.fs))
measure("array_analysis_10k", lambda: RingDownAnalyzer().analyze_array(t=t, data=x))
measure("monte_carlo_8x2048", lambda: MonteCarloAnalyzer().run(N=2048, n_mc=8, n_workers=2))
PY
```

Observed in this session:

- Release C++ smoke: DFT `7 ms`, NLS `54 ms`, array analysis `115 ms`, Monte
  Carlo `50 ms`.
- Debug C++ smoke: DFT `45 ms`, NLS `111 ms`, array analysis `280 ms`, Monte
  Carlo `115 ms`.
- Python comparable snippet: signal generation `0.146 ms`, DFT `14.634 ms`,
  NLS `12.086 ms`, array analysis `25.289 ms`, Monte Carlo `471.636 ms`.

The key release problem is therefore approximately:

- NLS: C++ about 4.5x slower than Python.
- Full array analysis: C++ about 4.5x slower than Python.
- DFT and Monte Carlo are not the first bottlenecks, though they still need
  regression coverage as other paths are changed.

## Current Status

No performance fixes were implemented in this handoff pass. The investigation
read the existing performance docs, compared C++ hot paths with the Python
reference, ran release/debug C++ smoke benchmarks, and ran a matching Python
timing snippet.

There is already a narrower handoff at
`docs/handoffs/20260513_batch-analysis-cpp-slow.md` for the notebook batch
stall. Treat that as a symptom-focused companion. This document is the broader
architecture/performance plan.

## Most Likely Root Causes

### 1. C++ NLS Uses A Slow Coordinate Search, Not SciPy-Equivalent Least Squares

Python `ringdownanalysis.estimators.NLSFrequencyEstimator` uses
`scipy.optimize.least_squares(method="trf")` with bounded joint optimization over
amplitude, frequency, phase, tau, and offset. That calls optimized native
linear algebra and vectorized residual evaluation.

C++ `src/estimators.cpp` instead performs separable fits by alternating golden
section searches:

- `estimate_with_separable_nls()` runs up to three frequency/tau alternating
  passes plus a final frequency pass.
- Each `minimize_golden()` uses 32 iterations.
- Each objective calls `fit_fixed_frequency_tau()`, which scans the whole sample
  buffer and solves a tiny 3x3 system.
- Unknown-tau NLS therefore does roughly `7 * 34` full-buffer linear fits before
  the final fit, plus repeated validation/initialization passes.

This is deterministic and inspectable, but it is not competitive with the
Python/SciPy implementation. The NLS benchmark gap is the dominant cause of the
array-analysis gap.

Relevant files/functions:

- `src/estimators.cpp`: `estimate_with_separable_nls()`,
  `fit_fixed_frequency_tau()`, `minimize_golden()`,
  `estimate_initial_tau_from_envelope()`.
- `.read-only/RingDownAnalysis/ringdownanalysis/estimators.py`:
  `NLSFrequencyEstimator._estimate_unknown_tau_full()`,
  `NLSFrequencyEstimator._estimate_known_tau_full()`.

### 2. Full Array Analysis Runs Multiple Expensive Fits Per Record

`RingDownAnalyzer::analyze_array()` is doing substantially more than one NLS
fit:

- Computes initial parameters from DFT on the full record.
- Computes `tau_seed` from envelope.
- Calls `estimate_tau()`, which itself calls a default `NLSFrequencyEstimator`
  over the full record.
- Crops by `tau_estimate`.
- Calls `nls_estimator_.estimate_full()` again on cropped samples.
- Calls `dft_estimator_.estimate_full()` on cropped samples, which also fits
  tau by golden search.
- Calls `estimate_noise_parameters()` with two full cropped passes.

Python has the same high-level pipeline, but expensive inner operations are
mostly vectorized native NumPy/SciPy. C++ needs either better algorithms or much
less repeated full-buffer work.

Relevant files/functions:

- `src/analyzer.cpp`: `RingDownAnalyzer::analyze_array()`,
  `RingDownAnalyzer::estimate_tau()`,
  `RingDownAnalyzer::estimate_noise_parameters()`.
- `.read-only/RingDownAnalysis/ringdownanalysis/analyzer.py`:
  `_run_analysis_pipeline()`, `estimate_tau()`, `estimate_noise_parameters()`.

### 3. C++ Keeps Large Waveform Copies In Results And JSON Paths

`AnalyzerResult` owns full `time`, `samples`, `cropped_time`, `cropped_samples`,
and optional secondary samples. Batch processing stores a vector of these
results, then `to_json_batch_report()` serializes notebook-shaped per-file
results through `to_json_notebook()` and re-parses each JSON chunk line-by-line
with `std::istringstream`.

For real CSV files with millions of samples, this can dominate memory pressure
and reporting time even after estimator improvements. The Python notebook path
also carries arrays, but C++ should expose a fast summary path that does not
retain or serialize raw waveforms unless explicitly requested.

Relevant files/functions:

- `include/ringdown/analyzer.hpp`: `AnalyzerResult`.
- `src/batch.cpp`: `BatchRingDownAnalyzer::process_files()`,
  `to_json_batch_report()`, `to_json()`.
- `src/analyzer.cpp`: `to_json_notebook()`.

### 4. CSV/MAT Loading Is Correct But Not Optimized For Large Files

Python uses pandas' C CSV engine with `usecols=[0, 3]`. C++ reads line-by-line
with `std::getline`, `find_first_not_of`, `strchr`, and `strtod`. That is
reasonable as a first port but likely slower than pandas for 5M-row files.

MAT loading in C++ is custom and copies decompressed/compressed data into
vectors while walking MATLAB structures. It needs benchmark coverage before
changing, but it should be considered part of the performance overhaul because
large real files are a primary workflow.

Relevant files/functions:

- `src/analyzer.cpp`: `load_csv_stream()`, `parse_csv_data_row()`,
  `RingDownDataLoader::load_csv()`, `RingDownDataLoader::load_mat()`,
  `RingDownDataLoader::load_zip()`.
- `.read-only/RingDownAnalysis/ringdownanalysis/data_loader.py`:
  `RingDownDataLoader.load_csv()`, `RingDownDataLoader.load_mat()`.

### 5. Benchmark Coverage Is Too Coarse To Guide A Rewrite

The current C++ benchmark smoke gives top-level timings only. It does not time
per-stage analyzer costs, large real-file loading, batch summary serialization,
or NLS evaluation counts. The current `EstimationResult::evaluations` exists but
is mostly not populated for successful NLS/DFT tau paths.

Relevant files/functions:

- `benchmarks/benchmark_smoke.cpp`.
- `include/ringdown/estimators.hpp`: `EstimationResult::evaluations`.
- `docs/RESULTS_AND_PERFORMANCE.md`.

## Recommended Implementation Plan

### Phase 0: Stop Measuring Debug As Production

1. Update notebook/example docs and commands so performance comparisons use
   `build/release/...` binaries.
2. Add a clear warning or metadata field to example output when built without
   `NDEBUG`.
3. Stop any existing long-running `build/dev/examples/batch_analysis_example`
   process before running fresh profiling.

Acceptance for this phase:

- Batch notebook comparison uses release C++ by default or makes Debug mode
  impossible to mistake for a performance result.
- The next profiling pass reports build type, compiler, and command line.

### Phase 1: Add Stage-Level Performance Instrumentation

Add focused benchmark/profiling coverage before changing algorithms:

1. Add release benchmark cases for:
   - initial DFT,
   - envelope tau seed,
   - full-record tau estimation,
   - cropped NLS,
   - DFT tau fit,
   - noise fit,
   - CSV load only,
   - MAT load only,
   - batch JSON summary without waveform arrays,
   - batch notebook JSON with waveform arrays.
2. Populate `EstimationResult::evaluations` for NLS and tau fits.
3. Add temporary or gated per-file progress/timing in batch examples so stalls
   show the active file and stage.

Acceptance for this phase:

- A single release command identifies the slowest stage for 10k synthetic data
  and at least one real CSV/MAT file.
- Existing fixture tests still pass.

### Phase 2: Replace The C++ NLS Core

Do not optimize around the current 200+ full-buffer golden-search objective
evaluations as the primary fix. Replace it with a real bounded nonlinear
least-squares implementation or a faster separable optimizer:

Preferred paths to evaluate:

1. Add a small dependency on a mature optimizer / linear algebra stack and
   implement SciPy-equivalent bounded least squares for the 4-parameter
   known-tau and 5-parameter unknown-tau models.
2. If avoiding dependencies, implement a compact Levenberg-Marquardt or
   trust-region reflective solver specialized to this model, using analytic
   Jacobians and bounded parameter transforms.
3. If keeping separable least squares, replace alternating scalar golden search
   with a 2D optimizer over `(frequency, tau)` using cached recurrence state,
   early convergence, and evaluation counts.

Algorithm requirements:

- Preserve Python parity first. Use existing fixture tests and add golden tests
  for `nfev`, fallback/sanity behavior where exact values are meaningful.
- Match Python bounds/tolerances or explicitly document any scientifically
  justified difference.
- Avoid per-evaluation heap allocation.
- Reuse recurrence-based sin/cos/envelope updates or precomputed time vectors
  where they actually reduce runtime.

Acceptance for this phase:

- Release `nls_estimate_10k` is faster than the Python timing on the same
  machine while preserving current numerical tolerances.
- Release `array_analysis_10k` is faster than the Python timing on the same
  machine.
- NLS success/fallback behavior remains compatible with the Python reference
  on fixtures and real sample files.

### Phase 3: Remove Avoidable Full-Buffer Passes And Copies

After NLS is fixed, reduce repeated scans/copies:

1. Introduce lightweight array views/spans for internal analysis stages so
   cropping can pass a range instead of allocating `cropped_time` and
   `cropped_samples` for every fit.
2. Add a summary-only analyzer/batch result path that omits raw waveform arrays.
3. Make notebook-shaped JSON opt-in for examples, not the default batch
   production path.
4. Avoid recomputing `estimate_initial_tau_from_envelope()` in
   `RingDownAnalyzer::estimate_tau()` fallback paths.
5. Consider combining normal-equation accumulation and RSS computation where it
   remains numerically acceptable, or cache model terms in the noise fit.

Acceptance for this phase:

- Real-file batch summary memory use scales with result count, not total raw
  sample count, unless waveform export is explicitly requested.
- Batch JSON summary time is negligible relative to analysis for small result
  counts.

### Phase 4: Optimize Loaders For Real Data

Use profiler data from Phase 1 to decide how much loader work is justified.
Likely tasks:

1. Benchmark `load_csv_stream()` separately against pandas on the real 5M-row
   CSV fixtures.
2. Replace stream extraction with a buffered scanner or memory-mapped parser if
   CSV load time is material after NLS is fixed.
3. Reserve row counts more accurately for CSV files to avoid growth churn.
4. Benchmark MAT decompression/parsing and reduce avoidable copies if it shows
   up in real-file profiles.

Acceptance for this phase:

- C++ CSV and MAT load-only timings are no slower than Python by more than a
  small, documented margin, and ideally faster.
- Loader changes preserve validation behavior for malformed/unsupported files.

## What Was Tried

- Read existing `docs/RESULTS_AND_PERFORMANCE.md`.
- Read existing `docs/handoffs/20260513_batch-analysis-cpp-slow.md`.
- Compared C++ estimator/analyzer/batch/loader code to the Python reference.
- Ran release and debug C++ smoke benchmarks.
- Ran a matching Python timing snippet in `.venv`.

No code changes were attempted beyond this handoff.

## Context / Constraints

- Python reference path: `.read-only/RingDownAnalysis/ringdownanalysis/`.
- C++ estimator path: `src/estimators.cpp`, `include/ringdown/estimators.hpp`.
- C++ analyzer path: `src/analyzer.cpp`, `include/ringdown/analyzer.hpp`.
- C++ batch path: `src/batch.cpp`, `include/ringdown/batch.hpp`.
- C++ benchmark path: `benchmarks/benchmark_smoke.cpp`.
- Existing performance doc: `docs/RESULTS_AND_PERFORMANCE.md`.
- Existing narrow batch handoff:
  `docs/handoffs/20260513_batch-analysis-cpp-slow.md`.

Repo constraints:

- Preserve scientific behavior and Python-equivalent golden tests before
  optimizing.
- Do not hide algorithmic problems behind parallelism alone.
- Keep changes incremental and commit after each coherent working improvement
  when implementing this handoff.
- Do not delete existing docs or handoffs.

## Success Criteria

Performance:

- Release C++ `nls_estimate_10k` beats Python on the same machine.
- Release C++ `array_analysis_10k` beats Python on the same machine.
- Release C++ real-file batch with `--max-files 3` completes in notebook-friendly
  time and no longer appears stalled.
- Full batch throughput scales with workers after the per-file bottleneck is
  fixed.
- DFT and Monte Carlo remain at least as fast as the current C++ baseline.

Correctness:

- Existing CTest fixture parity remains green.
- New stage-level benchmarks or tests pin any changed estimator behavior.
- Python comparison scripts still pass within documented tolerances.

Operational:

- Performance docs distinguish Debug and Release results.
- Benchmarks include enough stage detail to prevent regressions from hiding
  inside `array_analysis_10k`.

## References

- Internal docs: `AGENTS.md`.
- Existing performance results: `docs/RESULTS_AND_PERFORMANCE.md`.
- Existing batch-stall handoff:
  `docs/handoffs/20260513_batch-analysis-cpp-slow.md`.
- Python reference checkout:
  `.read-only/RingDownAnalysis/ringdownanalysis/estimators.py`,
  `.read-only/RingDownAnalysis/ringdownanalysis/analyzer.py`,
  `.read-only/RingDownAnalysis/ringdownanalysis/data_loader.py`,
  `.read-only/RingDownAnalysis/ringdownanalysis/batch_analyzer.py`.
- No external optimizer/library research has been done yet.
