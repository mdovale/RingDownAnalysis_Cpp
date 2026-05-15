# Handoff: C++ Batch Notebook Performance

## Scope

Fix the remaining performance gap in
`examples/batch_analysis_cpp_comparison.ipynb`, where the C++ batch example is
still much slower than the Python reference on real CSV/MAT data despite the
synthetic release benchmark now being fast.

This handoff is narrower than
`docs/handoffs/20260513_cpp-performance-overhaul.md`: it focuses on the
end-to-end notebook workflow and real-file batch path, not the already-improved
10k synthetic smoke benchmark.

## Problem / Goal

The notebook workflow is still unacceptable for the C++ port. The latest saved
notebook output shows:

- Python reference export completed and printed
  `Python compute time: 73.412 s`.
- The C++ step is running
  `build/dev/examples/batch_analysis_example --workers 2`.
- The C++ output has only reached
  `Warning: Debug build timings are not production performance data.`

That means the current example is still measuring a Debug binary and the
real-file C++ path is not giving usable feedback or runtime. The goal
is to make the C++ example decisively faster than Python on the same full data
selection, using Release binaries and preserving parity.

Current reproduction from the repository root:

```bash
.venv/bin/python examples/python/export_batch_reference.py --n-jobs -1
build/dev/examples/batch_analysis_example --workers 2
.venv/bin/python examples/python/compare_batch_analysis.py \
  --py-report results/examples/batch_analysis_py/batch_report.json \
  --cpp-report results/examples/batch_analysis_cpp/batch_report.json \
  --plot results/examples/batch_analysis_cpp/f_nls_overlay.png \
  --plot-dir results/examples/batch_analysis_plots
```

The first thing to change for any performance comparison is the C++ command:

```bash
cmake --build build/release
build/release/examples/batch_analysis_example --workers 2 --progress
```

## Current Status

Recent performance work has been committed on `main`:

- `dfd7058 perf(core): replace NLS search optimizer`
- `43b330a feat(batch): report progress and build metadata`
- `58d7a68 perf(benchmarks): add stage-level smoke timings`
- `93d2a85 feat(examples): expand batch comparison plots`
- `b7566f8 docs(perf): refresh benchmark snapshot`

The release smoke benchmark is now fast on synthetic data:

- `nls_estimate_10k_ms` is about `1 ms`.
- `array_analysis_10k_ms` is about `4 ms`.
- The benchmark reports stage-level timings and evaluation counts.

The real-file batch path still needs validation:

- The workflow now processes all top-level CSV/MAT files rather than a small
  `--max-files` subset.
- No real-file Release profiling evidence has been captured after the NLS
  optimizer change.

## What Was Tried

### Synthetic NLS / Analyzer Optimization

The core estimator was rewritten from alternating scalar golden searches to an
analytic bounded nonlinear least-squares fit. This fixed the synthetic NLS and
array-analysis gap, but it does not prove the real batch path is fast.

### Stage-Level Smoke Benchmark

`benchmarks/benchmark_smoke.cpp` now reports build metadata, stage timings,
JSON timing, fixture CSV/MAT load-only timing, and estimator evaluation counts.
This is useful but currently exercises tiny fixture files for loader probes, not
the large real files used by the notebook.

### Batch Progress Hooks

`BatchRingDownAnalyzer::process_files` accepts an optional progress callback,
and `examples/batch_analysis_example.cpp` exposes it with `--progress` or
`RINGDOWN_BATCH_PROGRESS`.

### Comparison Plot Expansion

`examples/python/compare_batch_analysis.py` now writes richer plots. That helps
diagnostics but may add post-compute time and does not address C++ analysis
throughput.

## Context

Relevant files:

- `examples/batch_analysis_example.cpp`: C++ batch example; writes
  `batch_report.json`, `file_list.json`, and `meta.json`.
- `include/ringdown/batch.hpp` and `src/batch.cpp`: batch processing and
  progress callback API.
- `include/ringdown/analyzer.hpp` and `src/analyzer.cpp`: real-file load,
  analyze, crop, noise, and result retention path.
- `src/estimators.cpp`: optimized NLS and DFT tau fitting.
- `examples/python/export_batch_reference.py`: Python reference batch exporter.
- `examples/python/compare_batch_analysis.py`: parity comparison and plot
  generation.
- `docs/RESULTS_AND_PERFORMANCE.md`: current synthetic performance snapshot.

Likely performance suspects for the real batch path:

- Full `AnalyzerResult` retains raw `time`, `samples`, `cropped_time`,
  `cropped_samples`, and optional `secondary_samples` for every file.
- CSV/MAT loading has only fixture-scale benchmark coverage after the latest
  changes; large-file loader time is unknown.
- The C++ example may process both large CSV and MAT files while the Python
  reference may have different file ordering, filtering, or parallel behavior.
- Real-file analysis may still perform avoidable copies during normalization,
  cropping, result storage, and JSON export even if estimator calls are now
  fast.

Constraints:

- Preserve Python parity and existing fixture tests.
- Do not hide an algorithmic or serialization bottleneck behind parallelism
  alone.
- Use Release binaries for performance claims.
- Keep exported JSON compact and useful for downstream inspection.

## Repro / Measurement Plan

Run these from the repo root.

1. Build Release and confirm the smoke benchmark is still fast:

```bash
cmake --build build/release
build/release/benchmarks/ringdown_benchmark_smoke
```

2. Compare Python and Release C++ on one file, with C++ progress enabled:

```bash
.venv/bin/python examples/python/export_batch_reference.py --max-files 1 --n-jobs 1
build/release/examples/batch_analysis_example --max-files 1 --workers 1 --progress
```

3. Compare full notebook selection with Release:

```bash
.venv/bin/python examples/python/export_batch_reference.py --n-jobs -1
build/release/examples/batch_analysis_example --workers 2 --progress
```

4. Measure output serialization separately. If analysis is fast but report
   writing is slow, add or benchmark a summary-only report mode:

```bash
build/release/benchmarks/ringdown_benchmark_smoke
```

Then add a real-file batch JSON benchmark if the smoke output is insufficient.

## Recommended Implementation Plan

### Phase 1: Stop Measuring Debug In The Notebook

Update `examples/batch_analysis_cpp_comparison.ipynb` so the C++ command uses
`build/release/examples/batch_analysis_example` by default, or checks for the
Release binary and prints a hard warning/fails if only Debug is available.

Also pass `--progress` in the notebook so long runs show the active file and
elapsed time.

### Phase 2: Add Real-File Stage Telemetry

Extend the batch example or analyzer with gated timing for:

- file load;
- timebase validation / normalization;
- initial DFT;
- tau seed;
- full-record tau estimation;
- crop and copies;
- cropped NLS;
- cropped DFT tau fit;
- noise fit;
- result retention;
- summary JSON;
- JSON report serialization.

The output should make it obvious whether the bottleneck is analysis, loader,
or reporting.

### Phase 3: Keep Batch JSON Compact

Make the production/default C++ batch output compact. Options:

- Keep `file_list.json` / summary rows as the default fast path.
- Keep detailed diagnostics in scalar summary fields.

Success here should make memory and serialization time scale with file count,
not total sample count, for the default batch example.

### Phase 4: Remove Real-File Copies And Loader Bottlenecks

Use the telemetry to decide whether to optimize:

- CSV parsing for multi-million-row files;
- MAT decompression/parsing copies;
- analyzer result storage;
- crop-by-view/span instead of vector copies;
- JSON array formatting.

Do this only after the timings show which path dominates.

### Phase 5: Re-run Full Batch Parity

Run the full equivalent workflow with Release C++ and compare:

- wall time Python vs C++;
- per-file C++ timings;
- output file sizes;
- parity comparison result;
- plot generation time.

## Success Criteria

- Release C++ completes the full batch selection faster than Python on
  the same machine and same file set.
- `--max-files 1` and the full selection both print enough C++ progress/stage
  timing to identify regressions.
- Default C++ batch output is fast in wall time and memory use.
- Existing CTest fixture parity passes.
- `examples/python/compare_batch_analysis.py` still reports parity within
  tolerances for the generated reports.

## Test Coverage Gaps

- No benchmark currently times load/analyze/report stages on the large real
  CSV/MAT files.
- No memory-use regression guard exists for retaining raw samples across a
  batch.

## Branch / Workspace Notes

- The working tree was clean after the five recent commits listed above.
- Continue on `main` per `AGENTS.md` and `.cursor/rules/git-workflow.mdc`.
- Commit each coherent increment separately.
- Do not delete older handoff documents; they are historical context.

## References

- `docs/handoffs/20260513_cpp-performance-overhaul.md`
- `docs/handoffs/20260513_batch-analysis-cpp-slow.md`
- `docs/RESULTS_AND_PERFORMANCE.md`
- `AGENTS.md`
- Python reference:
  `../RingDownAnalysis/ringdownanalysis/`
- No external references yet.
