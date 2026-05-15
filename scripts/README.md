# Ringdown JSON Scripts

These scripts parse compact JSON emitted by `ringdown` and help inspect analysis,
batch, full-report, minimal-report, and Monte Carlo outputs.

The scripts intentionally reject sample-array JSON keys. Repository outputs are
compact summaries only.

## Summaries And Tables

```bash
./build/release/apps/ringdown batch path/to/data --report full > result.json
python3 scripts/analyze_ringdown_output.py result.json
python3 scripts/analyze_ringdown_output.py batch.json --rows-csv results/rows.csv
python3 scripts/analyze_ringdown_output.py batch_report.json \
  --rows-csv results/batch_rows.csv \
  --timings-csv results/batch_timings.csv \
  --failures-csv results/batch_failures.csv
```

## Plots

```bash
python3 scripts/plot_ringdown_output.py result.json -o results/plots
python3 scripts/plot_ringdown_output.py batch.json batch_report.json monte_carlo.json \
  -o results/plots
```

Prefer `>` when writing a new `ringdown` JSON file. If a file was built with
repeated `>>` appends, the scripts treat it as a stream of appended reports and
process each report separately.

Plotting requires `matplotlib`; the existing example requirements file includes
the needed Python plotting dependencies.
