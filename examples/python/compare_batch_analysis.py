"""Compare C++ `batch_report.json` with Python reference `batch_report.json`."""

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


def is_close(a, b, rtol: float, atol: float) -> bool:
    if a is None and b is None:
        return True
    try:
        fa = float(a)
        fb = float(b)
    except (TypeError, ValueError):
        return a == b
    if not math.isfinite(fa) or not math.isfinite(fb):
        return math.isnan(fa) and math.isnan(fb)
    return abs(fa - fb) <= atol + rtol * abs(fb)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--py-report", type=Path, required=True)
    p.add_argument("--cpp-report", type=Path, required=True)
    p.add_argument("--rtol", type=float, default=1e-6)
    p.add_argument("--atol", type=float, default=1e-6)
    p.add_argument("--plot", type=Path, default=None)
    args = p.parse_args()

    py = load_json(args.py_report)
    cpp = load_json(args.cpp_report)

    errs: list[str] = []
    if py.get("success_count") != cpp.get("success_count"):
        errs.append(
            f"success_count py={py.get('success_count')} cpp={cpp.get('success_count')}"
        )

    py_c = py.get("consistency_analysis") or {}
    cpp_c = cpp.get("consistency_analysis") or {}
    if py_c and cpp_c:
        for k in (
            "n_realizations",
            "n_pairwise_comparisons",
            "nls_mean",
            "dft_mean",
            "nls_span",
            "dft_span",
        ):
            if not is_close(py_c.get(k), cpp_c.get(k), args.rtol, args.atol):
                errs.append(f"consistency.{k} py={py_c.get(k)!r} cpp={cpp_c.get(k)!r}")

    py_b = py.get("crlb_comparison_analysis") or {}
    cpp_b = cpp.get("crlb_comparison_analysis") or {}
    if py_b and cpp_b:
        for k in ("frequency_diffs", "plugin_crlb_stds"):
            pa, ca = py_b.get(k), cpp_b.get(k)
            if not isinstance(pa, list) or not isinstance(ca, list):
                continue
            if len(pa) != len(ca):
                errs.append(f"{k} length py={len(pa)} cpp={len(ca)}")
                continue
            for i, (a, b) in enumerate(zip(pa, ca)):
                if not is_close(a, b, args.rtol, args.atol):
                    errs.append(f"{k}[{i}] py={a!r} cpp={b!r}")
                    break

    py_rows = py.get("summary_table") or []
    cpp_rows = cpp.get("summary_table") or []
    if len(py_rows) != len(cpp_rows):
        errs.append(f"summary_table rows py={len(py_rows)} cpp={len(cpp_rows)}")
    else:
        for i, (rpy, rcpp) in enumerate(zip(py_rows, cpp_rows)):
            for k in ("f_NLS (Hz)", "f_DFT (Hz)", "|f_NLS - f_DFT| (Hz)"):
                if not is_close(rpy.get(k), rcpp.get(k), args.rtol, args.atol):
                    errs.append(f"row{i}.{k} py={rpy.get(k)!r} cpp={rcpp.get(k)!r}")

    if errs:
        print("Mismatches:")
        print("\n".join(errs))
        sys.exit(1)

    print("OK: batch report fields match within tolerances.")

    if args.plot is not None:
        import matplotlib.pyplot as plt
        import numpy as np

        from plots_style import apply_plotting_style

        apply_plotting_style()
        py_rows = py.get("summary_table") or []
        cpp_rows = cpp.get("summary_table") or []
        if not py_rows:
            return
        idx = np.arange(len(py_rows))
        fpy = np.array([float(r["f_NLS (Hz)"]) for r in py_rows])
        fcpp = np.array([float(r["f_NLS (Hz)"]) for r in cpp_rows])
        fig, ax = plt.subplots(figsize=(7, 3))
        ax.plot(idx, fpy, "o", label="Python f_NLS")
        ax.plot(idx, fcpp, "x", label="C++ f_NLS")
        ax.set_xlabel("file index")
        ax.set_ylabel("f_NLS (Hz)")
        ax.legend()
        fig.tight_layout()
        args.plot.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(args.plot, dpi=150)
        plt.close(fig)
        print(f"Wrote plot {args.plot}")


if __name__ == "__main__":
    main()
