# RingDownAnalysisCpp Results And Performance

## End-To-End Workflows

The C++ library now covers the core workflows from the Python reference:

- synthetic ring-down signal generation;
- CRLB-style frequency and Q diagnostics;
- DFT and separable least-squares frequency, tau, and Q estimation;
- Moku CSV and MATLAB v5 `moku.data` loading;
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
```

Analyze a batch:

```bash
build/release/apps/ringdown_cli batch path/to/a.csv path/to/b.mat > batch.json
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

- `signal_generation_10k_ms=0`
- `dft_estimate_10k_ms=6`
- `nls_estimate_10k_ms=52`
- `array_analysis_10k_ms=111`
- `monte_carlo_8x2048_ms=13`

Comparable Python reference timings from the local `.venv`:

- `signal_generation_10k_ms=0.128`
- `dft_estimate_10k_ms=14.529`
- `nls_estimate_10k_ms=11.217`
- `array_analysis_10k_ms=24.586`
- `monte_carlo_8x2048_ms=438.883`

The C++ DFT and Monte Carlo paths are already faster on these workloads. The
current separable least-squares path prioritizes inspectability and fixture
coverage over peak speed; it is documented by the benchmark and can be replaced
with a dedicated optimizer if future profiling requires it.

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
