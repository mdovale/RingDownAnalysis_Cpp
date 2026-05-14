#!/usr/bin/env python3
"""Generate Python reference fixtures for C++ regression tests.

Run this from the repository root after installing the reference Python project:

    python tools/generate_reference_fixtures.py --output tests/fixtures/reference

The script imports the sibling reference checkout from `../RingDownAnalysis`
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
        default=Path(__file__).resolve().parents[1].parent / "RingDownAnalysis",
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
        BatchRingDownAnalyzer,
        DFTFrequencyEstimator,
        MonteCarloAnalyzer,
        NLSFrequencyEstimator,
        RingDownAnalyzer,
        RingDownSignal,
    )
    from scipy.io import savemat  # noqa: PLC0415

    args.output.mkdir(parents=True, exist_ok=True)

    signal = RingDownSignal(f0=5.0, fs=100.0, N=2048, A0=1.0, snr_db=60.0, Q=10000.0)
    rng = np.random.default_rng(42)
    t, x, phi0 = signal.generate(rng=rng)

    nls = NLSFrequencyEstimator(tau_known=None).estimate_full(x, signal.fs)
    dft = DFTFrequencyEstimator(window="rect").estimate_full(x, signal.fs)
    analyzer = RingDownAnalyzer()
    analysis = analyzer.analyze_array(t=t, data=x, max_nfev=250)

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

    file_signal = RingDownSignal(f0=6.25, fs=125.0, N=1024, A0=0.75, snr_db=70.0, Q=1500.0)
    t_file, x_file, file_phi0 = file_signal.generate(phi0=0.2, rng=np.random.default_rng(7))
    csv_path = args.output / "moku_small.csv"
    csv_lines = ["% Moku fixture generated from Python reference\n", "time,a,b,phase\n"]
    csv_lines.extend(f"{ti:.17g},0,0,{xi:.17g}\n" for ti, xi in zip(t_file, x_file))
    csv_path.write_text("".join(csv_lines), encoding="utf-8")

    mat_path = args.output / "moku_small.mat"
    moku_data = np.column_stack(
        [
            t_file,
            np.zeros_like(t_file),
            np.zeros_like(t_file),
            x_file,
            np.zeros_like(t_file),
            np.zeros_like(t_file),
            np.zeros_like(t_file),
            np.zeros_like(t_file),
            x_file * 0.5,
        ]
    )
    moku = np.empty((1, 1), dtype=[("data", object)])
    moku[0, 0]["data"] = moku_data
    savemat(mat_path, {"moku": moku})

    csv_analysis = analyzer.analyze_file(str(csv_path), max_nfev=250)
    mat_analysis = analyzer.analyze_file(str(mat_path), max_nfev=250)

    batch = BatchRingDownAnalyzer()
    batch_process = batch.process_files([str(csv_path), str(mat_path)], verbose=False, n_jobs=1)
    batch.calculate_q_factors()
    q_stats = batch.get_q_factor_statistics(include_invalid=False)
    batch_summary = batch.get_summary_table()
    batch_consistency = batch.consistency_analysis()
    batch_uncertainty = batch.crlb_comparison_analysis()

    mc = MonteCarloAnalyzer().run(
        f0=5.0,
        fs=100.0,
        N=512,
        A0=1.0,
        snr_db=50.0,
        Q=1000.0,
        n_mc=4,
        seed=11,
    )

    def analysis_fields(result: dict[str, Any]) -> dict[str, Any]:
        """Scalar / bool / short-string fields for C++ fixture parity (no large grids)."""

        float_keys = (
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
            "Q_nls_raw",
            "Q_dft_raw",
            "Q_pre_crop",
            "Q_profile",
            "Q_profile_rss_min",
            "Q_profile_sigma",
            "Q_profile_dof",
            "Q_profile_n_grid",
            "A0_est",
            "sigma_est",
            "plugin_crlb_var_f",
            "plugin_crlb_std_f",
            "N",
            "N_crop",
            "T",
            "T_crop",
        )
        bool_keys = (
            "Q_nls_valid",
            "Q_dft_valid",
            "Q_profile_valid",
            "tau_est_low_confidence",
            "tau_est_at_lower_bound",
            "tau_est_at_upper_bound",
            "tau_nls_at_lower_bound",
            "tau_nls_at_upper_bound",
            "tau_dft_at_lower_bound",
            "tau_dft_at_upper_bound",
        )
        str_keys = (
            "Q_nls_status",
            "Q_dft_status",
            "Q_profile_status",
            "Q_profile_method",
        )
        out: dict[str, Any] = {}
        for key in float_keys:
            if key in result:
                out[key] = _finite_scalar(result[key])
        for key in bool_keys:
            if key in result:
                out[key] = bool(result[key])
        for key in str_keys:
            if key in result and result[key] is not None:
                out[key] = str(result[key])
        return out

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
            **analysis_fields(analysis),
        },
        "file_fixtures": {
            "csv": {
                "path": "moku_small.csv",
                "parameters": {
                    "f0": file_signal.f0,
                    "fs": file_signal.fs,
                    "N": file_signal.N,
                    "A0": file_signal.A0,
                    "snr_db": file_signal.snr_db,
                    "Q": file_signal.Q,
                    "tau": file_signal.tau,
                    "sigma": file_signal.sigma,
                    "phi0": file_phi0,
                },
                "analysis": analysis_fields(csv_analysis),
            },
            "mat": {
                "path": "moku_small.mat",
                "analysis": analysis_fields(mat_analysis),
                "v2_first": float((x_file * 0.5 - np.mean(x_file * 0.5))[0]),
            },
        },
        "batch": {
            "success_count": len(batch_process.results),
            "failure_count": len(batch_process.failed_files),
            "summary_rows": len(batch_summary["data"]),
            "q_values": [float(value) for value in q_stats["values"]],
            "q_factor_statistics": {
                "n_total": int(q_stats["n_total"]),
                "n_valid": int(q_stats["n_valid"]),
                "n_skipped": int(q_stats["n_skipped"]),
                "n_invalid": int(q_stats["n_invalid"]),
                "n_profile_limits": int(q_stats["n_profile_limits"]),
                "include_invalid": bool(q_stats["include_invalid"]),
                "mean": _finite_scalar(q_stats.get("mean")),
                "std": _finite_scalar(q_stats.get("std")),
                "min": _finite_scalar(q_stats.get("min")),
                "max": _finite_scalar(q_stats.get("max")),
                "range": _finite_scalar(q_stats.get("range")),
            },
            "nls_mean": float(batch_consistency["nls_mean"]),
            "dft_mean": float(batch_consistency["dft_mean"]),
            "frequency_diff_count": len(batch_uncertainty["frequency_diffs"]),
        },
        "monte_carlo": {
            "parameters": {
                "f0": mc["f0"],
                "fs": mc["fs"],
                "N": mc["N"],
                "Q": mc["Q"],
                "tau": mc["tau"],
                "snr_db": mc["snr_db"],
            },
            "crlb_std": mc["crlb_std"],
            "crlb_std_q": mc["crlb_std_q"],
            "errors_nls": mc["errors_nls"].tolist(),
            "errors_dft": mc["errors_dft"].tolist(),
            "errors_q_nls": mc["errors_q_nls"].tolist(),
            "errors_q_dft": mc["errors_q_dft"].tolist(),
            "stats": mc["stats"],
        },
    }

    (args.output / "synthetic_5hz_seed42.json").write_text(_case_to_json(case), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
