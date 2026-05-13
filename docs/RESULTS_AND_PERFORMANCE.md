# RingDownAnalysisCpp Results And Performance

## End-To-End Workflows

The C++ library now covers the core workflows from the Python reference:

- synthetic ring-down signal generation;
- CRLB-style frequency and Q diagnostics;
- DFT and analytic bounded least-squares frequency, tau, and Q estimation;
- Moku CSV, MATLAB v5 `moku.data`, and single-CSV ZIP loading;
- array and file analysis;
- sequential and parallel batch processing with structured summaries;
- deterministic Monte Carlo studies;
- JSON export for Python and Jupyter inspection.

## CLI Examples

Build first:

```bash
cmake --preset release
cmake --build --preset release
```

Analyze one file:

```bash
build/release/apps/ringdown_cli analyze path/to/file.csv > result.json
build/release/apps/ringdown_cli analyze path/to/file.mat > result.json
build/release/apps/ringdown_cli analyze path/to/file.zip > result.json
```

Analyze a batch:

```bash
build/release/apps/ringdown_cli batch path/to/a.csv path/to/b.mat path/to/c.zip > batch.json
```

Run a deterministic Monte Carlo study:

```bash
build/release/apps/ringdown_cli monte-carlo 100 100000 8 > monte_carlo.json
```

The arguments are trial count, samples per trial, and worker count. Omitted
arguments use the C++ defaults.

## Python/Jupyter Inspection

C++ is the analysis pipeline. Python can remain a downstream inspection tool by
loading exported JSON:

```python
import json
from pathlib import Path

import pandas as pd

result = json.loads(Path("result.json").read_text())
print(result["f_nls"], result["Q_nls"], result["plugin_crlb_std_f"])

batch = json.loads(Path("batch.json").read_text())
df = pd.DataFrame(batch["results"])
display(df[["filename", "type", "f_nls", "f_dft", "Q_nls", "Q_dft"]])

mc = json.loads(Path("monte_carlo.json").read_text())
errors = pd.Series(mc["errors_nls"], name="nls_frequency_error_hz")
display(errors.describe())
```

## Reference Data Validation

Representative real files from `.read-only/RingDownAnalysis/data/` were analyzed
with the release binary.

MAT validation command:

```bash
build/release/apps/ringdown_cli analyze \
  ".read-only/RingDownAnalysis/data/MTSRingdownFirstAttempt_20250826_163810 1.mat"
```

Observed result:

- File type: `MAT`
- Samples: `1,072,884`
- Sample rate: `596.04644775259931 Hz`
- NLS frequency: `7.2003996399559647 Hz`
- DFT frequency: `7.2004013370504403 Hz`
- NLS Q: `34796.01971377775`
- DFT Q: `34803.067006708967`
- Plug-in frequency bound: `3.0113216959305175e-08 Hz`
- Runtime: about `14.8 s` including release rebuild in the measured command.

CSV validation command:

```bash
build/release/apps/ringdown_cli analyze \
  ".read-only/RingDownAnalysis/data/MTS_ringdown_050_laser_out_Test5_20250901_224711_1.5e-6mbar.csv"
```

Observed result:

- File type: `CSV`
- Samples: `5,149,842`
- Sample rate: `119.20926112911778 Hz`
- NLS frequency: `7.1996680581597072 Hz`
- DFT frequency: `7.1996699971338467 Hz`
- NLS Q: `308739.61905864719`
- DFT Q: `317588.17130649026`
- Plug-in frequency bound: `1.4666293998540595e-08 Hz`
- Runtime: `43.8 s`.

## Benchmark Snapshot

Release C++ benchmark command:

```bash
build/release/benchmarks/ringdown_benchmark_smoke
```

Observed C++ timings:

- `build_type=Release`
- `signal_generation_10k_ms=0.125084`
- `dft_estimate_10k_ms=1.72138`
- `nls_estimate_10k_ms=1.04163`
- `stage_initial_dft_10k_ms=0.316666`
- `stage_envelope_tau_seed_10k_ms=0.010625`
- `stage_full_record_tau_estimation_10k_ms=1.08054`
- `stage_cropped_nls_10k_ms=0.961875`
- `stage_cropped_dft_tau_fit_10k_ms=1.68933`
- `stage_noise_fit_10k_ms=0.061584`
- `array_analysis_10k_ms=4.25446`
- `monte_carlo_8x2048_ms=4.71229`
- `batch_json_summary_one_result_ms=0.015084`
- `batch_notebook_json_one_result_ms=11.8216`
- `csv_load_only_ms=0.110041` on `tests/fixtures/reference/moku_small.csv`
- `mat_load_only_ms=0.174042` on `tests/fixtures/reference/moku_small.mat`
- `nls_evaluations=9`
- `dft_evaluations=34`

Comparable Python reference timings from the local `.venv`:

- `signal_generation_10k_ms=0.163`
- `dft_estimate_10k_ms=16.910`
- `nls_estimate_10k_ms=12.513`
- `array_analysis_10k_ms=26.296`
- `monte_carlo_8x2048_ms=486.724`

The C++ DFT, NLS, array analysis, and Monte Carlo paths are faster than the
Python reference on this synthetic workload. The benchmark also emits build
type, compiler, command line, estimator evaluation counts, and stage-level
timings so Debug results are not mistaken for production measurements.

## Validation Commands

The final local validation pass should include:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --preset release
cmake --build --preset release
ctest --preset release
build/release/benchmarks/ringdown_benchmark_smoke
```
