# Handoff: RingDownAnalysisCpp Agent #1

## Scope

Implement the C++20 `RingDownAnalysisCpp` library end-to-end from the Python
reference project in `.read-only/RingDownAnalysis`.

The desired result is a fully functional, fully tested, high-performance C++
library that can run workflows equivalent to:

- `.read-only/RingDownAnalysis/notebooks/array_analysis_example.ipynb`
- `.read-only/RingDownAnalysis/notebooks/analysis_example.ipynb`
- `.read-only/RingDownAnalysis/notebooks/batch_analysis_example.ipynb`
- large Monte Carlo studies currently represented by
  `.read-only/RingDownAnalysis/ringdownanalysis/monte_carlo.py`

Python and Jupyter remain downstream tools for inspecting exported C++ results.
The analysis pipeline itself should run 100% in C++.

## Current Status

Infrastructure has been created, but the scientific implementation is not yet
ported.

Implemented in this repository:

- CMake C++20 project scaffold with library, CLI, tests, and benchmark targets.
- `.clang-format`, `.clang-tidy`, `.gitignore`, CMake presets, and CI workflow.
- Minimal `ringdown::version()` API so the project builds and CTest runs.
- `tools/generate_reference_fixtures.py`, a starter script for golden fixtures
  from the Python reference implementation.
- `AGENTS.md` with repo-local operating instructions.

The working tree should be clean after the setup commit(s). Continue on `main`
and follow `.cursor/rules/git-workflow.mdc`.

## Feature / Goal

Build a modern C++20 implementation of the core `RingDownAnalysis` data-analysis
pipeline:

- Generate synthetic exponentially decaying sinusoid signals.
- Estimate frequency, decay time constant, and Q using NLS and DFT methods.
- Load supported Moku CSV and MAT files.
- Analyze arrays and files using the real-data pipeline.
- Batch process files with sequential and parallel execution.
- Run large deterministic Monte Carlo studies.
- Compute CRLB-style frequency and Q uncertainty diagnostics.
- Export structured results for Python/Jupyter post-analysis.

Non-goals unless the user explicitly asks:

- Recreating Python plotting APIs in C++.
- Preserving every Python compatibility helper before the core C++ API works.
- Optimizing algorithms before reference tests pin behavior.

## Context

Reference Python package:

- `.read-only/RingDownAnalysis/ringdownanalysis/signal.py`
- `.read-only/RingDownAnalysis/ringdownanalysis/estimators.py`
- `.read-only/RingDownAnalysis/ringdownanalysis/crlb.py`
- `.read-only/RingDownAnalysis/ringdownanalysis/data_loader.py`
- `.read-only/RingDownAnalysis/ringdownanalysis/analyzer.py`
- `.read-only/RingDownAnalysis/ringdownanalysis/batch_analyzer.py`
- `.read-only/RingDownAnalysis/ringdownanalysis/monte_carlo.py`

Reference tests and docs:

- `.read-only/RingDownAnalysis/tests/`
- `.read-only/RingDownAnalysis/docs/data_format.md`
- `.read-only/RingDownAnalysis/DATA_ANALYSIS_PIPELINE_AUDIT.md`
- `.read-only/RingDownAnalysis/benchmarks/`

Important Python public API concepts:

- `RingDownSignal`
- `NLSFrequencyEstimator`
- `DFTFrequencyEstimator`
- `EstimationResult`
- `CRLBCalculator`
- `RingDownDataLoader`
- `RingDownAnalyzer`
- `BatchRingDownAnalyzer`
- `MonteCarloAnalyzer`

The Python real-data pipeline, summarized by the audit, is:

1. Load CSV/MAT data and detrend phase.
2. Validate uniform finite timebase.
3. Estimate initial parameters and tau.
4. Crop data to `max_tau_multiplier * tau`.
5. Run NLS and DFT estimators on the crop.
6. Estimate residual noise and plug-in CRLB-style uncertainty.
7. Build per-file and batch result summaries.

## Design Decisions

- Use modern C++20 with small, typed data structures rather than mirroring
  Python dictionaries internally.
- Keep serialization explicit at the boundary so result files are convenient for
  Python/Jupyter.
- Golden fixtures from Python are mandatory before algorithm ports are treated
  as complete.
- Numerical parity should be tolerance-based, not bitwise, unless a local test
  explicitly requires bitwise reproducibility.
- Deterministic random-number behavior is required for C++ Monte Carlo tests,
  but exact NumPy RNG stream parity is not required unless the user later asks.
- Prefer simple, inspectable dependencies. Add dependencies only when they buy
  real numerical correctness, file-format support, FFT performance, or testing
  leverage.

Recommended dependency direction:

- Linear algebra / vector expressions: Eigen or xtensor.
- FFT: pocketfft, FFTW, or a small well-maintained C++ FFT dependency.
- Optimization: start with a tested Levenberg-Marquardt / trust-region choice;
  do not hand-roll nonlinear least squares without a strong reason.
- MAT/HDF5/results: choose based on required file formats and notebook workflow;
  CSV and JSON/NDJSON are acceptable early interchange formats.
- Tests: the current no-dependency smoke harness is temporary. Replace or
  augment it with Catch2 or GoogleTest once the first real test suite lands.

## Implementation Plan / Remaining Work

1. Verify the scaffold:
   - `cmake --preset dev`
   - `cmake --build --preset dev`
   - `ctest --preset dev`

2. Generate and commit a first small set of Python golden fixtures:
   - synthetic signal parameters and samples
   - CRLB scalar outputs
   - NLS and DFT estimator outputs for stable cases
   - array-analysis outputs for a small deterministic case

3. Port foundational types:
   - signal parameters and generated signal container
   - estimator result type
   - analyzer result type
   - error/status metadata for fit fallback behavior

4. Port and test pure numerical utilities:
   - derived tau and sigma calculations
   - CRLB weighted sums and variance/std calculations
   - validation helpers

5. Port signal generation:
   - deterministic synthetic ringdown generation
   - configurable phase and seed
   - tests against fixture tolerances

6. Port estimators:
   - DFT initializer and DFT estimator
   - NLS known-tau estimator
   - NLS unknown-tau estimator
   - tau/Q full-estimate path
   - fallback metadata behavior

7. Port data loading:
   - Moku CSV loader first
   - MAT loader after deciding the dependency strategy
   - finite-value and column-count validation matching the reference docs

8. Port `RingDownAnalyzer`:
   - array input path first
   - file input path second
   - crop multiplier behavior
   - residual-noise and uncertainty diagnostics

9. Port batch and Monte Carlo:
   - sequential batch processing
   - parallel batch processing preserving analyzer configuration
   - deterministic Monte Carlo scheduler
   - large-study output format suitable for Python/Jupyter

10. Performance pass:
    - profile before optimizing
    - benchmark representative workloads
    - preserve regression tests around every optimized path

11. Documentation:
    - README examples for C++ API, CLI, and exported result analysis
    - architecture notes for data model, estimator contracts, and file formats
    - performance notes with reproducible benchmark commands

## Success Criteria / Acceptance

- The project builds on macOS and Linux with CMake and C++20.
- CI passes format-relevant checks, build, CTest, and benchmark smoke tests.
- C++ tests cover every ported algorithm against Python golden fixtures.
- `array_analysis_example.ipynb`-style workflows can be performed using C++
  outputs loaded in Python/Jupyter.
- Real CSV data in `.read-only/RingDownAnalysis/data/` can be analyzed from C++.
- Batch analyses produce structured summaries equivalent to the Python API.
- Monte Carlo studies run fully in C++ with deterministic seeds and scalable
  parallel execution.
- Performance is measured against Python benchmark baselines and documented.
- `main` contains a linear sequence of small, coherent commits.
- The final working tree is clean, or any remaining work is captured in a WIP
  commit plus a fresh handoff document.

## Git Workflow Requirements

Follow `.cursor/rules/git-workflow.mdc` throughout:

- Work on `main`; do not create a feature branch unless the user asks.
- Use `git pull --rebase` if the remote may have moved.
- Commit after each coherent, working increment.
- Stage narrowly.
- Use Conventional Commit messages per `.cursor/rules/commit-message.mdc`.
- Do not force-push.
- Do not commit `.read-only/`, build products, generated result dumps, or local
  editor/system files.

Recommended commit cadence examples:

- `chore(build): add dependency manager`
- `test(fixtures): add synthetic signal goldens`
- `feat(signal): port ringdown generation`
- `test(crlb): pin reference variance outputs`
- `feat(estimator): add dft frequency estimator`
- `perf(monte-carlo): parallelize trial runner`

## Test Coverage Gaps To Close First

- No C++ scientific tests exist yet.
- No generated reference fixtures are committed yet.
- The current benchmark is only a build smoke test.
- The CLI is only a placeholder.
- MAT loading dependency and output serialization format are not yet chosen.

## References

- Python reference package: `.read-only/RingDownAnalysis`
- Array workflow: `.read-only/RingDownAnalysis/notebooks/array_analysis_example.ipynb`
- Data format specification: `.read-only/RingDownAnalysis/docs/data_format.md`
- Pipeline audit: `.read-only/RingDownAnalysis/DATA_ANALYSIS_PIPELINE_AUDIT.md`
- Python benchmark suite: `.read-only/RingDownAnalysis/benchmarks/`
- Repo workflow: `.cursor/rules/git-workflow.mdc`
- Commit messages: `.cursor/rules/commit-message.mdc`
