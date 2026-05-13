# Agent Instructions

This repository is the C++20 port of the Python `RingDownAnalysis` library in
`.read-only/RingDownAnalysis`.

## Operating Model

- Work on `main`.
- Keep history linear.
- Use `git pull --rebase` before continuing if the remote may have moved.
- Commit after each coherent, working increment.
- Stage narrowly and do not commit generated build products.
- Use Conventional Commits, hard-wrapped per `.cursor/rules/commit-message.mdc`.
- Do not force-push unless the user explicitly requests it.

## Implementation Priorities

1. Establish Python-equivalent golden tests before porting behavior.
2. Port the smallest useful vertical slice at a time.
3. Preserve scientific behavior before optimizing.
4. Benchmark only after tests pin the expected behavior.
5. Keep Python/Jupyter as downstream analysis tools for result inspection only.

## Reference Paths

- Python source: `.read-only/RingDownAnalysis/ringdownanalysis/`
- Python tests: `.read-only/RingDownAnalysis/tests/`
- Example notebooks: `.read-only/RingDownAnalysis/notebooks/`
- Data format notes: `.read-only/RingDownAnalysis/docs/data_format.md`
- Pipeline audit: `.read-only/RingDownAnalysis/DATA_ANALYSIS_PIPELINE_AUDIT.md`
