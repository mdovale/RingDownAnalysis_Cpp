"""Build `batch_report.json` using the Python RingDownAnalysis library (reference for C++ parity)."""

from __future__ import annotations

import argparse
import glob
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


def to_jsonable(obj):  # noqa: ANN001
    import numpy as np

    if isinstance(obj, dict):
        return {str(k): to_jsonable(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [to_jsonable(v) for v in obj]
    if isinstance(obj, np.ndarray):
        return obj.tolist()
    if isinstance(obj, (np.floating, float)):
        return float(obj)
    if isinstance(obj, (np.integer, int)):
        return int(obj)
    if isinstance(obj, (np.bool_, bool)):
        return bool(obj)
    if obj is None or isinstance(obj, str):
        return obj
    return str(obj)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="Directory with *.csv and *.mat (default: ../RingDownAnalysis/data)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("results/examples/batch_analysis_py"),
        help="Output directory for batch_report.json, meta.json, file_list.json",
    )
    parser.add_argument("--n-jobs", type=int, default=1, help="Parallel workers (default 1)")
    parser.add_argument(
        "--max-files",
        type=int,
        default=None,
        help="Optional cap on number of files after sorting (CSV then MAT)",
    )
    args = parser.parse_args()

    _add_ringdown_to_path()

    from ringdownanalysis.batch_analyzer import BatchRingDownAnalyzer

    root = _repo_root()
    data_dir = args.data_dir or (root.parent / "RingDownAnalysis" / "data")
    if not data_dir.is_dir():
        raise SystemExit(f"Data directory not found: {data_dir}")

    csv_files = sorted(glob.glob(str(data_dir / "*.csv")))
    mat_files = sorted(glob.glob(str(data_dir / "*.mat")))
    all_files = csv_files + mat_files
    if args.max_files is not None:
        all_files = all_files[: max(0, args.max_files)]

    batch = BatchRingDownAnalyzer()
    pr = batch.process_files(all_files, verbose=False, n_jobs=args.n_jobs)

    summary = batch.get_summary_table()
    q_factors = batch.calculate_q_factors()
    q_stats = batch.get_q_factor_statistics()
    consistency = batch.consistency_analysis()
    crlb_cmp = batch.crlb_comparison_analysis()

    report = {
        "success_count": len(pr.results),
        "failure_count": len(pr.failed_files),
        "failures": [
            {"filepath": fp, "message": str(exc)} for fp, exc in pr.failed_files
        ],
        "q_factors": [float(x) for x in q_factors],
        "q_factor_statistics": to_jsonable(q_stats),
        "summary_table": to_jsonable(summary.get("data", [])),
        "consistency_analysis": to_jsonable(consistency),
        "crlb_comparison_analysis": to_jsonable(crlb_cmp),
    }

    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    (out / "batch_report.json").write_text(
        json.dumps(report, indent=2), encoding="utf-8"
    )

    file_list = {
        "success_count": len(pr.results),
        "failure_count": len(pr.failed_files),
        "results": [
            {
                "filename": r.get("filename", ""),
                "type": r.get("type", ""),
                "f_nls": float(r["f_nls"]),
                "f_dft": float(r["f_dft"]),
            }
            for r in pr.results
        ],
        "failures": report["failures"],
    }
    (out / "file_list.json").write_text(
        json.dumps(file_list, indent=2), encoding="utf-8"
    )

    meta = {
        "data_dir": str(data_dir),
        "worker_count": args.n_jobs,
        "file_count": len(all_files),
        "success_count": len(pr.results),
        "failure_count": len(pr.failed_files),
    }
    if args.max_files is not None:
        meta["max_files"] = args.max_files
    (out / "meta.json").write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print(f"Wrote Python batch reference under {out}")


if __name__ == "__main__":
    main()
