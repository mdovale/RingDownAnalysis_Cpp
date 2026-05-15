#!/usr/bin/env python3
"""Utilities for parsing compact JSON emitted by the `ringdown` CLI."""

from __future__ import annotations

import json
import math
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Any


FORBIDDEN_OUTPUT_KEYS = {
    "t",
    "data",
    "V2",
    "t_" + "crop",
    "data_" + "cropped",
    "results_" + "notebook",
}


class RingdownJsonError(ValueError):
    """Raised when an input is not a supported compact ringdown JSON file."""


@dataclass(frozen=True)
class LoadedReport:
    """Loaded ringdown JSON with detected schema kind."""

    path: Path
    kind: str
    data: dict[str, Any]
    document_index: int = 1
    document_count: int = 1


def load_report(path: str | Path) -> LoadedReport:
    """Load one JSON report, returning the last document when a file was appended."""

    return load_reports(path)[-1]


def load_reports(path: str | Path) -> list[LoadedReport]:
    """Load one or more concatenated ringdown JSON reports from a file."""

    report_path = Path(path)
    text = report_path.read_text(encoding="utf-8")
    documents = parse_json_stream(text, report_path)
    reports: list[LoadedReport] = []
    for index, data in enumerate(documents, start=1):
        if not isinstance(data, dict):
            raise RingdownJsonError(f"{report_path}#{index}: expected a JSON object at top level")
        forbidden = find_forbidden_keys(data)
        if forbidden:
            keys = ", ".join(sorted(forbidden))
            raise RingdownJsonError(f"{report_path}#{index}: sample-array output keys are not supported: {keys}")
        reports.append(LoadedReport(report_path, detect_kind(data), data, index, len(documents)))
    return reports


def parse_json_stream(text: str, path: Path) -> list[Any]:
    """Parse one JSON object or a whitespace-separated stream of JSON objects."""

    decoder = json.JSONDecoder()
    cursor = 0
    documents: list[Any] = []
    while True:
        while cursor < len(text) and text[cursor].isspace():
            cursor += 1
        if cursor >= len(text):
            break
        try:
            value, cursor = decoder.raw_decode(text, cursor)
        except json.JSONDecodeError as error:
            raise RingdownJsonError(f"{path}: invalid JSON near line {error.lineno}: {error.msg}") from error
        documents.append(value)
    if not documents:
        raise RingdownJsonError(f"{path}: empty JSON file")
    return documents


def report_label(report: LoadedReport) -> str:
    """Return a display label that distinguishes appended JSON documents."""

    if report.document_count == 1:
        return str(report.path)
    return f"{report.path}#{report.document_index}"


def detect_kind(data: dict[str, Any]) -> str:
    """Classify a compact ringdown JSON object."""

    if {"errors_nls", "errors_dft", "stats"}.issubset(data):
        return "monte_carlo"
    if "summary_table" in data or "file_timings_ms" in data:
        return "batch_full"
    if {"success_count", "failure_count", "results", "failures"}.issubset(data):
        return "batch_minimal"
    if {"f_nls", "f_dft", "tau_est"}.issubset(data):
        return "analysis"
    raise RingdownJsonError("unrecognized ringdown JSON schema")


def find_forbidden_keys(value: Any) -> set[str]:
    """Recursively find keys that would imply sample-array output."""

    found: set[str] = set()
    if isinstance(value, dict):
        for key, item in value.items():
            if key in FORBIDDEN_OUTPUT_KEYS:
                found.add(key)
            found.update(find_forbidden_keys(item))
    elif isinstance(value, list):
        for item in value:
            found.update(find_forbidden_keys(item))
    return found


def as_float(value: Any) -> float | None:
    """Return a finite float or None for missing/non-finite values."""

    if value is None or isinstance(value, bool):
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    return parsed if math.isfinite(parsed) else None


def format_number(value: Any, digits: int = 6) -> str:
    """Format scalars for terminal summaries."""

    parsed = as_float(value)
    if parsed is None:
        return "-"
    return f"{parsed:.{digits}g}"


def finite_values(values: list[Any]) -> list[float]:
    """Filter JSON values to finite floats."""

    out: list[float] = []
    for value in values:
        parsed = as_float(value)
        if parsed is not None:
            out.append(parsed)
    return out


def describe(values: list[Any]) -> dict[str, float | int | None]:
    """Compute compact descriptive statistics for finite values."""

    finite = finite_values(values)
    if not finite:
        return {"count": 0, "mean": None, "median": None, "std": None, "min": None, "max": None}
    return {
        "count": len(finite),
        "mean": statistics.fmean(finite),
        "median": statistics.median(finite),
        "std": statistics.pstdev(finite) if len(finite) > 1 else 0.0,
        "min": min(finite),
        "max": max(finite),
    }


def analysis_row(data: dict[str, Any]) -> dict[str, Any]:
    """Return a normalized one-file analysis row."""

    return {
        "filename": data.get("filename", ""),
        "type": data.get("type", ""),
        "N": data.get("N"),
        "N_crop": data.get("N_crop"),
        "fs": data.get("fs"),
        "f_nls": data.get("f_nls"),
        "f_dft": data.get("f_dft"),
        "tau_est": data.get("tau_est"),
        "Q_nls": data.get("Q_nls"),
        "Q_dft": data.get("Q_dft"),
        "plugin_crlb_std_f": data.get("plugin_crlb_std_f"),
    }


def batch_rows(report: LoadedReport) -> list[dict[str, Any]]:
    """Return normalized per-file rows from minimal or full batch output."""

    data = report.data
    if report.kind == "batch_minimal":
        return [analysis_row(item) for item in data.get("results", [])]
    if report.kind != "batch_full":
        return []

    rows: list[dict[str, Any]] = []
    for item in data.get("summary_table", []) or []:
        rows.append(
            {
                "filename": item.get("Filename", ""),
                "type": item.get("Type", ""),
                "N": item.get("N (samples)"),
                "N_crop": item.get("N_crop (samples)"),
                "fs": item.get("fs (Hz)"),
                "f_nls": item.get("f_NLS (Hz)"),
                "f_dft": item.get("f_DFT (Hz)"),
                "tau_est": item.get("tau_est (s)"),
                "Q_nls": item.get("Q_NLS"),
                "Q_dft": item.get("Q_DFT"),
                "Q": item.get("Q"),
                "plugin_crlb_std_f": item.get("Plugin bound std (Hz)"),
            }
        )
    return rows


def failures(data: dict[str, Any]) -> list[dict[str, Any]]:
    """Return batch failure records, if present."""

    items = data.get("failures") or []
    return [item for item in items if isinstance(item, dict)]


def timing_rows(data: dict[str, Any]) -> list[dict[str, Any]]:
    """Return timing rows from analysis, minimal batch, or full batch output."""

    if "timings_ms" in data and isinstance(data["timings_ms"], dict):
        return [{"filename": data.get("filename", ""), "timings": data["timings_ms"]}]
    if isinstance(data.get("results"), list):
        return [
            {"filename": item.get("filename", ""), "timings": item.get("timings_ms") or {}}
            for item in data["results"]
            if isinstance(item, dict)
        ]
    if isinstance(data.get("file_timings_ms"), list):
        return [
            {"filename": item.get("filename", ""), "timings": item.get("timings") or {}}
            for item in data["file_timings_ms"]
            if isinstance(item, dict)
        ]
    return []


def monte_carlo_error_series(data: dict[str, Any]) -> dict[str, list[float]]:
    """Return Monte Carlo error arrays by label."""

    return {
        "NLS frequency": finite_values(data.get("errors_nls") or []),
        "DFT frequency": finite_values(data.get("errors_dft") or []),
        "NLS Q": finite_values(data.get("errors_q_nls") or []),
        "DFT Q": finite_values(data.get("errors_q_dft") or []),
    }


def summary_lines(report: LoadedReport) -> list[str]:
    """Build human-readable summary lines for any supported output."""

    data = report.data
    lines = [f"{report_label(report)}: {report.kind}"]
    if report.kind == "analysis":
        row = analysis_row(data)
        lines.append(
            "  file={filename} type={type} N={N} f_nls={f_nls} f_dft={f_dft} "
            "Q_nls={Q_nls} Q_dft={Q_dft}".format(
                filename=row["filename"] or "-",
                type=row["type"] or "-",
                N=row["N"] if row["N"] is not None else "-",
                f_nls=format_number(row["f_nls"]),
                f_dft=format_number(row["f_dft"]),
                Q_nls=format_number(row["Q_nls"]),
                Q_dft=format_number(row["Q_dft"]),
            )
        )
    elif report.kind.startswith("batch"):
        rows = batch_rows(report)
        lines.append(
            f"  success_count={data.get('success_count', 0)} "
            f"failure_count={data.get('failure_count', 0)} rows={len(rows)}"
        )
        if rows:
            lines.append(f"  f_nls: {format_stats(describe([row.get('f_nls') for row in rows]))}")
            lines.append(f"  f_dft: {format_stats(describe([row.get('f_dft') for row in rows]))}")
            lines.append(f"  Q:     {format_stats(describe([row.get('Q') or row.get('Q_nls') for row in rows]))}")
    elif report.kind == "monte_carlo":
        lines.append(
            "  f0={f0} Q={q} fs={fs} N={n} snr_db={snr}".format(
                f0=format_number(data.get("f0")),
                q=format_number(data.get("Q")),
                fs=format_number(data.get("fs")),
                n=data.get("N", "-"),
                snr=format_number(data.get("snr_db")),
            )
        )
        for label, values in monte_carlo_error_series(data).items():
            lines.append(f"  {label}: {format_stats(describe(values))}")
    return lines


def format_stats(stats: dict[str, float | int | None]) -> str:
    """Format statistics for one terminal line."""

    if not stats.get("count"):
        return "count=0"
    return (
        f"count={stats['count']} mean={format_number(stats['mean'])} "
        f"std={format_number(stats['std'])} min={format_number(stats['min'])} "
        f"max={format_number(stats['max'])}"
    )
