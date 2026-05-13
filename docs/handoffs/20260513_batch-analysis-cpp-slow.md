# Handoff: Batch Analysis C++ Slowness

## Scope

Investigate and fix the C++ batch example being much slower than the Python
reference path when running the notebook parity workflow.

## Problem

`examples/batch_analysis_cpp_comparison.ipynb` now uses the project `.venv` for
Python subprocesses, but the full notebook flow stalls in the C++ batch step.
The Python reference export finishes quickly, then
`build/dev/examples/batch_analysis_example --max-files 3` runs for several
minutes with no completion output.

Reproduction from the repository root:

```bash
.venv/bin/python examples/python/export_batch_reference.py --max-files 3
build/dev/examples/batch_analysis_example --max-files 3
.venv/bin/python examples/python/compare_batch_analysis.py \
  --py-report results/examples/batch_analysis_py/batch_report.json \
  --cpp-report results/examples/batch_analysis_cpp/batch_report.json \
  --plot results/examples/batch_analysis_cpp/f_nls_overlay.png
```

Expected: the C++ example should complete in a reasonable notebook-friendly
time for `--max-files 3`, then the comparison should run.

Actual: Python prints `Wrote Python batch reference under
results/examples/batch_analysis_py`; the C++ command is still running after
roughly five minutes and has not printed `Wrote batch analysis artifacts under
results/examples/batch_analysis_cpp`.

## Current Status

- The notebook wrapper was updated to use `ROOT / ".venv/bin/python"` instead
  of `sys.executable`.
- The notebook also now tolerates being launched with `Path.cwd()` equal to
  `examples/` by falling back to `ROOT.parent`.
- Linter diagnostics for the notebook were clean.
- Verification is not complete because the C++ batch step is too slow. A prior
  verification command may still be running; stop it before starting another
  long profiling run if needed.

## What Was Tried

### Notebook Python Path Fix

Changed `examples/batch_analysis_cpp_comparison.ipynb` so the Python export and
compare steps call `.venv/bin/python` explicitly. This fixed the interpreter
and `examples/examples/...` path issue, but exposed that the C++ batch step is
the runtime bottleneck.

### Direct Workflow Run

Ran the same three-step workflow from the repository root. The Python export
completed and wrote the reference output. The chained command then entered the
C++ batch example and did not complete within the observed window.

## Context

Relevant files:

- `examples/batch_analysis_cpp_comparison.ipynb`: notebook workflow wrapper.
- `examples/batch_analysis_example.cpp`: CLI entry point. It defaults to
  `worker_count = 1`, collects sorted CSV files followed by MAT files, applies
  `--max-files`, and calls `BatchRingDownAnalyzer::process_files`.
- `examples/python/export_batch_reference.py`: Python reference exporter. With
  `--max-files 3`, it processes the three CSV fixtures currently present under
  `.read-only/RingDownAnalysis/data`.
- `examples/python/compare_batch_analysis.py`: parity comparison and optional
  plot generation.
- `src/batch.cpp`: `BatchRingDownAnalyzer::process_files`. With
  `worker_count <= 1`, it loops through files and calls
  `RingDownAnalyzer::analyze_file(filepath)` serially.
- `src/analyzer.cpp`: `RingDownAnalyzer::analyze_file` and MAT/CSV parsing plus
  the analysis pipeline.

Observed input selection:

- `.read-only/RingDownAnalysis/data` currently has three CSV files matching
  `*.csv`.
- `--max-files 3` should therefore run only those three CSV files before any
  MAT fixtures.

Constraints and repo rules:

- Prefer scientific parity with the Python reference over superficial speedups.
- Do not add brittle workarounds. Fix the real bottleneck or document a clear
  reason if the Python and C++ algorithms intentionally differ.
- Keep changes small and commit coherent increments when implementing the fix.

## Most Likely Remaining Failure Modes

- The C++ NLS fit path may be doing far more work than the Python reference for
  these full-size CSV captures.
- Cropping or `max_tau_multiplier` behavior may differ, causing C++ to fit a
  much larger sample window than Python.
- CSV parsing may be fine, but the analysis pipeline may lack an iteration cap,
  tolerance setting, or fallback that the Python reference uses.
- The default notebook command uses one worker. Parallelism may help throughput,
  but it should not hide a per-file pathological slowdown.
- The example currently prints no per-file progress, so it is unclear whether
  the command is stuck on the first file or merely slow across all three.

## Recommended Next Steps

1. Stop any previous long-running `batch_analysis_example --max-files 3`
   process before starting new profiling runs.
2. Add temporary or gated timing around `examples/batch_analysis_example.cpp`
   and/or `BatchRingDownAnalyzer::process_files` to print the active filepath
   and elapsed time per file.
3. Run `build/dev/examples/batch_analysis_example --max-files 1` and compare
   wall time against `.venv/bin/python examples/python/export_batch_reference.py
   --max-files 1`.
4. If one file is pathological, reproduce directly through a smaller harness
   around `RingDownAnalyzer::analyze_file` and inspect where time is spent:
   CSV load, timebase validation, DFT estimate, tau estimation, NLS fit, or JSON
   serialization.
5. Compare C++ analyzer defaults with the Python reference for crop length,
   `max_tau_multiplier`, least-squares tolerances, max evaluations, and fallback
   behavior.
6. Once the bottleneck is fixed, rerun the notebook flow with `--max-files 3`
   and the full comparison command. Consider updating the notebook to pass
   `--workers` only after per-file runtime is reasonable.

## Success Criteria

- `build/dev/examples/batch_analysis_example --max-files 3` completes in a
  notebook-friendly time on the three CSV fixtures.
- The full batch notebook sequence completes using `.venv/bin/python`.
- `examples/python/compare_batch_analysis.py` reports batch fields match within
  tolerances and writes the optional overlay plot.
- Any performance fix preserves the existing Python parity expectations.
- If progress logging or profiling hooks are added, they are either useful
  permanent CLI output or removed before finalizing.

## Files Modified During Attempts

- `examples/batch_analysis_cpp_comparison.ipynb`

Related earlier notebook parity fixes in the same session:

- `examples/array_analysis_cpp_comparison.ipynb`
- `examples/python/export_array_reference.py`

## References

- Internal docs: `AGENTS.md`
- Internal handoff rule: `.cursor/rules/handoff-documents.mdc`
- No external references yet.
