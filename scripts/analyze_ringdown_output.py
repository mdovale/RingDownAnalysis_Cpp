#!/usr/bin/env python3
"""Summarize and export tables from compact `ringdown` JSON outputs."""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path
from typing import Any

from ringdown_json import (
    RingdownJsonError,
    analysis_row,
    batch_rows,
    describe,
    failures,
    load_reports,
    monte_carlo_error_series,
    report_label,
    summary_lines,
    timing_rows,
)


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    """Write rows with a stable union of keys."""

    path.parent.mkdir(parents=True, exist_ok=True)
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)


def table_rows(report: Any) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    """Return analysis/batch rows, timing rows, and failure rows for one report."""

    if report.kind == "analysis":
        rows = [analysis_row(report.data)]
    elif report.kind.startswith("batch"):
        rows = batch_rows(report)
    elif report.kind == "monte_carlo":
        rows = []
        for label, values in monte_carlo_error_series(report.data).items():
            stats = describe(values)
            rows.append({"series": label, **stats})
    else:
        rows = []
    return rows, timing_rows(report.data), failures(report.data)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Parse compact ringdown JSON and print summaries or export CSV tables."
    )
    parser.add_argument("json_files", nargs="+", type=Path, help="JSON files produced by ringdown")
    parser.add_argument("--rows-csv", type=Path, default=None, help="Write analysis/batch/MC rows to CSV")
    parser.add_argument("--timings-csv", type=Path, default=None, help="Write per-file timing rows to CSV")
    parser.add_argument("--failures-csv", type=Path, default=None, help="Write batch failures to CSV")
    parser.add_argument("--summary-json", type=Path, default=None, help="Write machine-readable summary JSON")
    args = parser.parse_args(argv)

    all_rows: list[dict[str, Any]] = []
    all_timings: list[dict[str, Any]] = []
    all_failures: list[dict[str, Any]] = []
    summaries: list[dict[str, Any]] = []

    try:
        for path in args.json_files:
            reports = load_reports(path)
            if len(reports) > 1:
                print(f"{path}: found {len(reports)} appended JSON reports")
            for report in reports:
                for line in summary_lines(report):
                    print(line)
                rows, timings, failed = table_rows(report)
                source = report_label(report)
                for row in rows:
                    all_rows.append({"source": source, "kind": report.kind, **row})
                for row in timings:
                    flat = {"source": source, "filename": row.get("filename", "")}
                    flat.update(row.get("timings") or {})
                    all_timings.append(flat)
                for item in failed:
                    all_failures.append({"source": source, **item})
                summaries.append({"source": source, "kind": report.kind, "summary": summary_lines(report)})
    except (OSError, RingdownJsonError) as error:
        print(f"analyze_ringdown_output: {error}", file=sys.stderr)
        return 1

    if args.rows_csv is not None:
        write_csv(args.rows_csv, all_rows)
    if args.timings_csv is not None:
        write_csv(args.timings_csv, all_timings)
    if args.failures_csv is not None:
        write_csv(args.failures_csv, all_failures)
    if args.summary_json is not None:
        args.summary_json.parent.mkdir(parents=True, exist_ok=True)
        args.summary_json.write_text(json.dumps(summaries, indent=2), encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
