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
import math
import sys
from pathlib import Path
from typing import Any


def _load_reference_package(reference_root: Path) -> None:
    sys.path.insert(0, str(reference_root))


def _case_to_json(case: dict[str, Any]) -> str:
    return json.dumps(case, indent=2, sort_keys=True) + "\n"


def _finite_scalar(value: Any) -> float | None:
    if value is None:
        return None
    value = float(value)
    if math.isfinite(value):
        return value
    return None


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
    from ringdownanalysis import (  # noqa: PLC0415
        CRLBCalculator,
        DFTFrequencyEstimator,
        NLSFrequencyEstimator,
        RingDownAnalyzer,
        RingDownSignal,
    )

    args.output.mkdir(parents=True, exist_ok=True)

    signal = RingDownSignal(f0=5.0, fs=100.0, N=2048, A0=1.0, snr_db=60.0, Q=10000.0)
    rng = np.random.default_rng(42)
    t, x, phi0 = signal.generate(rng=rng)

    nls = NLSFrequencyEstimator(tau_known=None).estimate_full(x, signal.fs)
    dft = DFTFrequencyEstimator(window="rect").estimate_full(x, signal.fs)
    analyzer = RingDownAnalyzer()
    analysis = analyzer.analyze_array(t=t, data=x, max_tau_multiplier=1.0, max_nfev=250)

    deterministic = RingDownSignal(f0=7.25, fs=128.0, N=64, A0=0.8, snr_db=300.0, Q=2500.0)
    deterministic_phi0 = 0.35
    t_deterministic = deterministic.t
    x_deterministic = deterministic.A0 * np.exp(-t_deterministic / deterministic.tau) * np.cos(
        2.0 * np.pi * deterministic.f0 * t_deterministic + deterministic_phi0
    )

    crlb_inputs = {
        "A0": 1.0,
        "sigma": 0.1,
        "fs": 100.0,
        "N": 10000,
        "tau": 1000.0,
        "f0": 5.0,
    }
    crlb_var_f = CRLBCalculator.variance(
        crlb_inputs["A0"],
        crlb_inputs["sigma"],
        crlb_inputs["fs"],
        crlb_inputs["N"],
        crlb_inputs["tau"],
    )
    crlb_var_q = CRLBCalculator.q_variance(
        crlb_inputs["A0"],
        crlb_inputs["sigma"],
        crlb_inputs["fs"],
        crlb_inputs["N"],
        crlb_inputs["tau"],
        crlb_inputs["f0"],
    )

    case = {
        "name": "synthetic_5hz_seed42",
        "crlb": {
            "inputs": crlb_inputs,
            "variance_f": crlb_var_f,
            "std_f": float(np.sqrt(crlb_var_f)),
            "variance_q": crlb_var_q,
            "std_q": float(np.sqrt(crlb_var_q)),
        },
        "deterministic_signal": {
            "parameters": {
                "f0": deterministic.f0,
                "fs": deterministic.fs,
                "N": deterministic.N,
                "A0": deterministic.A0,
                "snr_db": deterministic.snr_db,
                "Q": deterministic.Q,
                "tau": deterministic.tau,
                "sigma": deterministic.sigma,
                "phi0": deterministic_phi0,
            },
            "samples": {
                "t": t_deterministic.tolist(),
                "x": x_deterministic.tolist(),
            },
        },
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
            "nls": {
                key: _finite_scalar(value) if isinstance(value, (float, int)) else value
                for key, value in nls._asdict().items()
            },
            "dft": {
                key: _finite_scalar(value) if isinstance(value, (float, int)) else value
                for key, value in dft._asdict().items()
            },
        },
        "array_analysis": {
            key: _finite_scalar(value)
            for key, value in analysis.items()
            if key
            in {
                "fs",
                "tau_seed",
                "tau_full_init",
                "tau_est",
                "tau_nls",
                "tau_dft",
                "tau_model",
                "f_nls",
                "f_dft",
                "Q_nls",
                "Q_dft",
                "A0_est",
                "sigma_est",
                "plugin_crlb_var_f",
                "plugin_crlb_std_f",
                "N",
                "N_crop",
                "T",
                "T_crop",
            }
        },
    }

    (args.output / "synthetic_5hz_seed42.json").write_text(_case_to_json(case), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
