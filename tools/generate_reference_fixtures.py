#!/usr/bin/env python3
"""Generate Python reference fixtures for C++ regression tests.

Run this from the repository root after installing the reference Python project:

    python tools/generate_reference_fixtures.py --output tests/fixtures/reference

The script imports the local reference checkout from `.read-only/RingDownAnalysis`
by default. It writes JSON fixtures with deterministic synthetic signals and
selected estimator outputs. Agent #1 should expand this early, before porting
each algorithm.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def _load_reference_package(reference_root: Path) -> None:
    sys.path.insert(0, str(reference_root))


def _case_to_json(case: dict[str, Any]) -> str:
    return json.dumps(case, indent=2, sort_keys=True) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--reference-root",
        type=Path,
        default=Path(".read-only/RingDownAnalysis"),
        help="Path to the Python RingDownAnalysis checkout",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("tests/fixtures/reference"),
        help="Directory where JSON reference fixtures will be written",
    )
    args = parser.parse_args()

    _load_reference_package(args.reference_root.resolve())

    import numpy as np  # noqa: PLC0415
    from ringdownanalysis import DFTFrequencyEstimator, NLSFrequencyEstimator, RingDownSignal  # noqa: PLC0415

    args.output.mkdir(parents=True, exist_ok=True)

    signal = RingDownSignal(f0=5.0, fs=100.0, N=2048, A0=1.0, snr_db=60.0, Q=10000.0)
    rng = np.random.default_rng(42)
    t, x, phi0 = signal.generate(rng=rng)

    nls = NLSFrequencyEstimator(tau_known=None).estimate_full(x, signal.fs)
    dft = DFTFrequencyEstimator(window="rect").estimate_full(x, signal.fs)

    case = {
        "name": "synthetic_5hz_seed42",
        "parameters": {
            "f0": signal.f0,
            "fs": signal.fs,
            "N": signal.N,
            "A0": signal.A0,
            "snr_db": signal.snr_db,
            "Q": signal.Q,
            "tau": signal.tau,
            "sigma": signal.sigma,
            "phi0": phi0,
        },
        "samples": {
            "t": t.tolist(),
            "x": x.tolist(),
        },
        "estimates": {
            "nls": nls._asdict(),
            "dft": dft._asdict(),
        },
    }

    (args.output / "synthetic_5hz_seed42.json").write_text(_case_to_json(case), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
