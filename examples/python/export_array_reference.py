"""Export Python reference artifacts for array_analysis_example parity (synthetic signal + analysis)."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _add_ringdown_to_path() -> Path:
    root = _repo_root()
    env = os.environ.get("RINGDOWN_PYTHON_REF", "").strip()
    ref = Path(env) if env else (root.parent / "RingDownAnalysis")
    if not ref.is_dir():
        raise SystemExit(
            f"Missing Python reference checkout: {ref}\n"
            "Set RINGDOWN_PYTHON_REF to the RingDownAnalysis repo root, or use ../RingDownAnalysis."
        )
    sys.path.insert(0, str(ref))
    return ref


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("results/examples/array_analysis_py"),
        help="Directory for meta.json, shared_input.csv, from_t_and_data.json, manifest.json",
    )
    parser.add_argument("--max-tau-multiplier", type=float, default=1.0)
    args = parser.parse_args()

    _add_ringdown_to_path()
    import numpy as np

    from ringdownanalysis.signal import RingDownSignal
    from ringdownanalysis.analyzer import RingDownAnalyzer

    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)

    sig = RingDownSignal(f0=5.0, fs=1000.0, N=100_000, A0=0.1, snr_db=50.0, Q=500.0)
    rng = np.random.default_rng(42)
    t, data, phi0 = sig.generate(rng=rng)

    np.savetxt(
        out / "shared_input.csv",
        np.column_stack([t, data]),
        delimiter=",",
        header="time_s,phase_cycles",
    )

    analyzer = RingDownAnalyzer()
    r_td = analyzer.analyze_array(t, data, max_tau_multiplier=args.max_tau_multiplier)
    r_fs = analyzer.analyze_array(data=data, fs=1000.0, max_tau_multiplier=args.max_tau_multiplier)

    def pack(result: dict) -> dict:
        """JSON-serializable copy (lists instead of ndarray)."""
        out_d = {}
        for k, v in result.items():
            if hasattr(v, "tolist"):
                out_d[k] = v.tolist()
            elif isinstance(v, (np.floating, np.integer)):
                out_d[k] = float(v) if isinstance(v, np.floating) else int(v)
            elif isinstance(v, (bool, np.bool_)):
                out_d[k] = bool(v)
            elif v is None or isinstance(v, (str, int, float)):
                out_d[k] = v
            else:
                out_d[k] = v
        return out_d

    (out / "from_t_and_data.json").write_text(
        json.dumps(pack(r_td), indent=2), encoding="utf-8"
    )
    (out / "from_data_and_fs.json").write_text(
        json.dumps(pack(r_fs), indent=2), encoding="utf-8"
    )

    meta = {
        "source": "synthetic_python",
        "f0_hz": 5.0,
        "fs_hz": 1000.0,
        "N": 100_000,
        "A0": 0.1,
        "snr_db": 50.0,
        "Q": 500.0,
        "rng_seed": 42,
        "phi0": float(phi0),
        "max_tau_multiplier": args.max_tau_multiplier,
    }
    (out / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")

    manifest = {
        "meta": "meta.json",
        "runs": [
            {"label": "t_and_data", "file": "from_t_and_data.json"},
            {"label": "data_and_fs", "file": "from_data_and_fs.json"},
        ],
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Wrote Python reference under {out}")


if __name__ == "__main__":
    main()
