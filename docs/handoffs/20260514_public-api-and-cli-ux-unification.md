# Handoff: Public API and CLI / product UX unification

## Scope

This handoff covers **library-facing API clarity** and **command-line (or primary binary) UX** for RingDownAnalysis C++, aligned with discussion on:

- **Public API**: batch processing options, unified export story (minimal vs full batch report), Monte Carlo configuration surface, stable umbrella API.
- **CLI / product UX**: reducing fragmentation between `ringdown_cli` and `examples/batch_analysis_example`, consistent flags and exit behavior, directory discovery including `.zip`, mapping CLI to existing options structs.

It does **not** prescribe a specific third-party argument parser; choose one consistent with repo dependencies and maintainer preference.

## Feature / Goal

**Goal:** Embeddable C++ consumers and shell users should follow **one coherent story**: the `ringdown` namespace and existing types remain the contract of record; binaries are thin adapters that expose the same knobs already present on `MonteCarloOptions`, `BatchRingDownAnalyzer::process_files`, and JSON serializers—without forking analysis behavior.

**Library**

1. **Batch “one workflow, two exports”**  
   Document and optionally formalize the split between:
   - **minimal**: `ProcessResult` serialized via `to_json(const ProcessResult&)` (per-file successes + failures, small / fast), and  
   - **full report**: `to_json_batch_report(BatchRingDownAnalyzer&, const ProcessResult&, BatchReportOptions)` (summaries, Q stats, consistency, CRLB comparison, optional notebook-shaped payloads).  
   Consider a single discoverable type or enum (e.g. extend `BatchReportOptions` or add `BatchExportOptions`) so callers do not have to infer two parallel paths from reading two different programs.

2. **`BatchProcessOptions` (or equivalent)**  
   Colocate **worker count**, **optional progress callback**, and **file size limits** with `process_files` at the API level (these already exist on `process_files` / loader; the gap is consistency and documentation, and ensuring any new CLI uses the same entry point). Defaults must stay backward compatible with current `process_files(..., worker_count = 1, ...)`.

3. **Monte Carlo**  
   Treat `MonteCarloOptions` as the **single** configuration object for studies (`signal`, `trial_count`, `seed`, `worker_count`). Any user-facing runner should map argv or a small config file onto that struct; avoid hard-coding only `trials` / `samples` / `workers` while leaving `frequency_hz`, `snr_db`, `seed`, etc. inaccessible.

**CLI / product**

1. **Unify or label**  
   Either:
   - **Option A:** One primary binary (e.g. `ringdown` or expanded `ringdown_cli`) with subcommands `analyze`, `batch`, `monte-carlo` and shared flags (`--json`, `--workers`, `--progress`, `--fail-on-error`, `--report minimal|full`, directory inputs, `.zip` in directory scans), **or**
   - **Option B:** Keep `ringdown_cli` explicitly minimal for CI/smoke and promote **one** documented workflow binary; demote duplicate behavior in examples.

2. **Batch UX gaps to close**  
   - Directory mode: include **`.zip`** when scanning a data directory (today `batch_analysis_example` only globs `.csv` and `.mat` under `--data-dir`).  
   - Optional **file list** or `@paths.txt` for large batches (shell argv limits).  
   - **Exit codes**: align policy (e.g. fail on any file error vs warn) with a single `--fail-on-error` flag across commands.

3. **Monte Carlo CLI**  
   Expose at least **seed** and key **signal** fields (`frequency_hz`, `sample_rate_hz`, `snr_db`, `quality_factor`, etc.) via flags or `--config` JSON that deserializes into `MonteCarloOptions` / nested `SignalParameters`.

## Current Status

- **Not implemented**: this document records **design intent** from a planning discussion only.
- **Existing behavior** unchanged:
  - `apps/ringdown_cli.cpp`: `analyze` (single file), `batch` (paths only, sequential `process_files`, `to_json(ProcessResult)` only, exit `1` on any failure), `monte-carlo` (partial override of `MonteCarloOptions` only), `monte-carlo-smoke`.
  - `examples/batch_analysis_example.cpp`: discovers `.csv` / `.mat` under `--data-dir`, optional `--workers`, `--progress`, writes `batch_report.json`, `file_list.json`, `meta.json`; optional `--fail-on-file-error`; does not scan `.zip`.

## Context

**Headers / API**

- Umbrella: `include/ringdown/ringdown.hpp`
- Analysis / load: `include/ringdown/analyzer.hpp` (`RingDownDataLoader`, `RingDownAnalyzer`, `analyze_file`)
- Batch: `include/ringdown/batch.hpp` (`BatchRingDownAnalyzer::process_files`, `ProcessResult`, `to_json`, `to_json_batch_report`, `BatchReportOptions`)
- Monte Carlo: `include/ringdown/monte_carlo.hpp` (`MonteCarloOptions`, `MonteCarloAnalyzer`, `MonteCarloResult`, `to_json`)
- Signal defaults: `include/ringdown/signal.hpp` (`SignalParameters`)

**Binaries**

- `apps/ringdown_cli.cpp`
- `examples/batch_analysis_example.cpp`

**Docs**

- `AGENTS.md` — trunk workflow, port priorities (golden tests before behavior changes, small vertical slices, parity with Python).
- `docs/RESULTS_AND_PERFORMANCE.md` — end-to-end CLI examples and Python JSON inspection patterns.

**Constraints**

- Preserve **scientific parity** with Python reference; wire new UX through existing analyzers/serializers rather than duplicating logic.
- Follow `.cursor/rules/git-workflow.mdc` and commit message rules when implementing.
- Add or extend **tests** when changing public behavior (CLI contract, JSON field guarantees, batch discovery).

## Design decisions (preserve unless explicitly revised)

1. **Library is the contract**; CLI is an adapter.
2. **No duplicate estimator or batch logic** in a second code path—only wiring and I/O.
3. **Backward compatibility**: existing C++ call sites using `process_files` with defaults must remain valid; JSON consumers may get new optional fields but avoid breaking renames without a version bump story.
4. **ZIP semantics** stay as today: loader supports archives with **exactly one** CSV entry (`RingDownDataLoader::load_zip`); directory scanning should not weaken that rule.

## Success criteria

**Library**

- [ ] Batch workflow documented (or encoded in types) as **minimal vs full** export; new readers can choose without reading two binaries.
- [ ] Optional `BatchProcessOptions` (or documented pattern) groups workers, progress, and size limits for `process_files` if a struct is added.
- [ ] `MonteCarloOptions` is the documented configuration surface for Monte Carlo; tests cover any new parsing path.

**CLI / product**

- [ ] Users can run batch over a directory of **`.zip`** files without hand-globbing every path (either unified tool or `batch_analysis_example` extended—pick one product story and document it in `README.md` / `docs/RESULTS_AND_PERFORMANCE.md`).
- [ ] Monte Carlo subcommand exposes **seed** and core **signal** parameters (or `--config` JSON).
- [ ] Consistent **exit code** policy documented and implemented for batch failure modes.
- [ ] Large batch inputs avoid argv explosion (file list or `@file`), if scoped in this effort.

**Quality**

- [ ] Existing golden / parity tests pass; new tests for new flags and directory zip discovery.
- [ ] Docs updated so `ringdown_cli` vs primary batch tool is unambiguous for new contributors.

## Recommended next steps

1. **Decision record** (short paragraph in README or `docs/RESULTS_AND_PERFORMANCE.md`): one primary user-facing binary vs minimal + full; avoid permanent dual-product confusion.
2. **API sketch** in code or doc: `BatchExportMode` / extended `BatchReportOptions` / `BatchProcessOptions`—implement smallest additive change first.
3. **Extend `batch_analysis_example`** OR **extend `ringdown_cli batch`** for `.zip` directory listing and shared flags; prefer one code path calling `process_files` + chosen JSON export.
4. **Monte Carlo CLI**: map flags to `MonteCarloOptions` and `SignalParameters`; add tests for argv parsing.
5. **Follow-up**: deprecate redundant entry points only after migration period and doc update.

## Test coverage gaps

- CLI argv parsing and exit codes (if not already covered by integration tests).
- Directory iterator including `.zip` mixed with `.csv` / `.mat`.
- Monte Carlo with non-default `seed` and `signal` fields via CLI (regression: deterministic reproducibility).

## References

- `AGENTS.md` — operating model and Python reference paths.
- `docs/RESULTS_AND_PERFORMANCE.md` — CLI examples and downstream Python inspection.
- Prior related handoffs (batch performance / notebook JSON): `docs/handoffs/20260513_cpp-batch-notebook-performance.md`, `docs/handoffs/20260513_batch-analysis-cpp-slow.md` (context only; this handoff is API/UX scope).

No external forum links were used for this handoff.
