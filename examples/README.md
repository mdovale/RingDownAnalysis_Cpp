# Examples: notebook parity

These examples mirror the workflows in the Python reference notebooks:

- `array_analysis_example.ipynb` → C++ `array_analysis_example`, Python `export_array_reference.py`
- `batch_analysis_example.ipynb` → C++ `batch_analysis_example`, Python `export_batch_reference.py`

Artifacts are written under `results/examples/` (gitignored). From the repository root:

## Build

```bash
cmake --preset dev
cmake --build --preset dev
```

## Array workflow (1:1 on shared samples)

1. Export the Python synthetic signal and analyses (requires `.read-only/RingDownAnalysis` or `RINGDOWN_PYTHON_REF`):

   ```bash
   python examples/python/export_array_reference.py
   ```

2. Run the C++ example on the shared CSV so both sides analyze identical samples:

   ```bash
   build/dev/examples/array_analysis_example \
     --input-csv results/examples/array_analysis_py/shared_input.csv \
     --output-dir results/examples/array_analysis_cpp
   ```

3. Compare JSON outputs (exact waveform match with zero tolerances when using the shared CSV):

   ```bash
   python examples/python/compare_array_analysis.py \
     --py-json results/examples/array_analysis_py/from_t_and_data.json \
     --cpp-json results/examples/array_analysis_cpp/from_t_and_data.json \
     --waveform-rtol 0 \
     --waveform-atol 0
   ```

Optional overlay plot:

```bash
python examples/python/compare_array_analysis.py \
  --py-json results/examples/array_analysis_py/from_t_and_data.json \
  --cpp-json results/examples/array_analysis_cpp/from_t_and_data.json \
  --waveform-rtol 0 --waveform-atol 0 \
  --plot results/examples/array_analysis_cpp/array_overlay.png
```

Jupyter: open [`array_analysis_cpp_comparison.ipynb`](array_analysis_cpp_comparison.ipynb) and run the cells (expects `build/dev/examples/` and repo-root paths).

## Batch workflow

For a **quick smoke** on the small tracked CSV fixture (milliseconds):

```bash
build/dev/examples/batch_analysis_example --data-dir tests/fixtures/reference
```

Full notebook-style workflow on reference measurement data (defaults to `.read-only/RingDownAnalysis/data`; can be slow for large captures):

1. Python reference batch report (defaults to `.read-only/RingDownAnalysis/data`):

   ```bash
   python examples/python/export_batch_reference.py
   ```

2. C++ batch report (same default data directory resolution). Large directories can be slow; cap files for a quick check:

   ```bash
   build/dev/examples/batch_analysis_example --max-files 3
   ```

   Full directory:

   ```bash
   build/dev/examples/batch_analysis_example
   ```

3. Compare (use the same `--max-files` cap when exporting the Python reference):

   ```bash
   python examples/python/export_batch_reference.py --max-files 3
   ```

   ```bash
   python examples/python/compare_batch_analysis.py \
     --py-report results/examples/batch_analysis_py/batch_report.json \
     --cpp-report results/examples/batch_analysis_cpp/batch_report.json
   ```

Environment variables:

| Variable | Purpose |
|----------|---------|
| `RINGDOWN_PYTHON_REF` | Path to the `RingDownAnalysis` Python repo if not under `.read-only/RingDownAnalysis` |
| `RINGDOWN_EXAMPLES_DATA` | Override batch data directory |
| `RINGDOWN_EXAMPLES_OUTPUT` | Override default `results/examples/...` output root for the C++ examples |

## Python dependencies

```bash
pip install -r examples/python/requirements.txt
```
