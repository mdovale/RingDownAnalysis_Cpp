"""Compare C++ `to_json_notebook` output with Python `analyze_array` JSON (same inputs recommended)."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

_REPO_PY = Path(__file__).resolve().parent
if str(_REPO_PY) not in sys.path:
    sys.path.insert(0, str(_REPO_PY))


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def is_close(a: float, b: float, rtol: float, atol: float) -> bool:
    if a is None or b is None:
        return a is None and b is None
    if isinstance(a, bool) or isinstance(b, bool):
        return bool(a) == bool(b)
    if not math.isfinite(float(a)) or not math.isfinite(float(b)):
        return math.isnan(float(a)) and math.isnan(float(b))
    return abs(float(a) - float(b)) <= atol + rtol * abs(float(b))


def compare_scalars(py: dict, cpp: dict, keys: list[str], rtol: float, atol: float) -> list[str]:
    errors: list[str] = []
    for k in keys:
        if k not in py and k not in cpp:
            continue
        pv, cv = py.get(k), cpp.get(k)
        if not is_close(pv, cv, rtol, atol):
            errors.append(f"  {k}: py={pv!r} cpp={cv!r}")
    return errors


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--py-json", type=Path, required=True)
    p.add_argument("--cpp-json", type=Path, required=True)
    p.add_argument("--rtol", type=float, default=1e-9)
    p.add_argument("--atol", type=float, default=1e-9)
    p.add_argument(
        "--waveform-rtol",
        type=float,
        default=None,
        help="If set, require max|data_py - data_cpp| <= atol + rtol*max|data_py| on overlapping prefix",
    )
    p.add_argument("--waveform-atol", type=float, default=0.0)
    p.add_argument("--plot", type=Path, default=None, help="Optional PNG path for overview figure")
    args = p.parse_args()

    py = load_json(args.py_json)
    cpp = load_json(args.cpp_json)

    keys = [
        "fs",
        "tau_est",
        "tau_model",
        "f_nls",
        "f_dft",
        "plugin_crlb_std_f",
        "N",
        "N_crop",
        "T",
        "T_crop",
        "nls_success",
        "dft_success",
        "dft_used_fallback",
        "nls_used_fallback",
    ]
    errs = compare_scalars(py, cpp, keys, args.rtol, args.atol)
    if errs:
        print("Scalar mismatches:")
        print("\n".join(errs))
        sys.exit(1)

    if args.waveform_rtol is not None:
        da = py.get("data")
        db = cpp.get("data")
        if not isinstance(da, list) or not isinstance(db, list):
            print("Missing data arrays for waveform compare")
            sys.exit(1)
        n = min(len(da), len(db))
        max_py = max(abs(float(x)) for x in da[:n]) or 1.0
        tol = args.waveform_atol + args.waveform_rtol * max_py
        worst = 0.0
        for i in range(n):
            worst = max(worst, abs(float(da[i]) - float(db[i])))
        if worst > tol:
            print(f"Waveform mismatch: max abs diff {worst} (tol {tol})")
            sys.exit(1)

    print("OK: scalars match within tolerances.")
    if args.plot is not None:
        import numpy as np
        import matplotlib.pyplot as plt

        from plots_style import apply_plotting_style

        apply_plotting_style()
        t = np.asarray(py["t"], dtype=float)
        d_py = np.asarray(py["data"], dtype=float)
        d_cpp = np.asarray(cpp["data"], dtype=float)
        fig, ax = plt.subplots(figsize=(8, 3))
        ax.plot(t, d_py, label="Python", alpha=0.8)
        ax.plot(t, d_cpp, "--", label="C++", alpha=0.8)
        ax.set_xlabel("t (s)")
        ax.set_ylabel("data")
        ax.legend()
        fig.tight_layout()
        args.plot.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(args.plot, dpi=150)
        plt.close(fig)
        print(f"Wrote plot {args.plot}")


if __name__ == "__main__":
    main()
