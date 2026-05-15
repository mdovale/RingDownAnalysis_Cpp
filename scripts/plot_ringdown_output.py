#!/usr/bin/env python3
"""Create plots from compact `ringdown` JSON outputs."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

from ringdown_json import (
    RingdownJsonError,
    analysis_row,
    as_float,
    batch_rows,
    failures,
    finite_values,
    load_reports,
    monte_carlo_error_series,
    report_label,
    timing_rows,
)


def require_matplotlib() -> Any:
    """Import matplotlib only for plotting commands."""

    try:
        import matplotlib.pyplot as plt
    except ImportError as error:  # pragma: no cover - depends on local env
        raise SystemExit("plot_ringdown_output requires matplotlib; install examples/python/requirements.txt") from error
    plt.style.use("seaborn-v0_8-whitegrid")
    return plt


def safe_stem(path: Path) -> str:
    """Build a stable filename stem for plots."""

    return path.stem.replace(" ", "_").replace("/", "_")


def report_stem(path: Path, document_index: int, document_count: int) -> str:
    """Build a unique stem for one report in a possibly appended JSON file."""

    stem = safe_stem(path)
    if document_count == 1:
        return stem
    return f"{stem}_{document_index:03d}"


def short_label(value: str, max_chars: int = 36) -> str:
    """Keep long filenames from consuming the whole figure canvas."""

    if len(value) <= max_chars:
        return value
    keep = max_chars - 3
    front = max(10, keep // 2)
    back = max(10, keep - front)
    return f"{value[:front]}...{value[-back:]}"


def set_file_xticklabels(ax: Any, positions: list[int], labels: list[str]) -> None:
    """Apply bounded-length file labels with a readable rotation."""

    ax.set_xticks(positions)
    ax.set_xticklabels([short_label(label) for label in labels], rotation=35, ha="right", fontsize=8)


def enforce_min_axes_area(fig: Any, min_height: float = 0.50, min_width: float = 0.58) -> None:
    """Reserve a minimum fraction of the figure for actual plot axes."""

    axes = [ax for ax in fig.axes if ax.get_visible()]
    if not axes:
        return

    min_x0 = min(ax.get_position().x0 for ax in axes)
    max_x1 = max(ax.get_position().x1 for ax in axes)
    min_y0 = min(ax.get_position().y0 for ax in axes)
    max_y1 = max(ax.get_position().y1 for ax in axes)
    width = max_x1 - min_x0
    height = max_y1 - min_y0

    kwargs: dict[str, float] = {}
    if height < min_height:
        top = min(0.92, max_y1)
        kwargs["bottom"] = max(0.08, top - min_height)
        kwargs["top"] = top
    if width < min_width:
        right = min(0.96, max_x1)
        kwargs["left"] = max(0.08, right - min_width)
        kwargs["right"] = right
    if kwargs:
        fig.subplots_adjust(**kwargs)


def save_figure(fig: Any, output_dir: Path, filename: str) -> Path:
    """Save one plot and close it."""

    output_dir.mkdir(parents=True, exist_ok=True)
    path = output_dir / filename
    fig.tight_layout()
    enforce_min_axes_area(fig)
    fig.savefig(path, dpi=160, bbox_inches="tight")
    return path


def plot_analysis(report_path: Path, data: dict[str, Any], output_dir: Path) -> list[Path]:
    plt = require_matplotlib()
    row = analysis_row(data)
    saved: list[Path] = []

    labels = ["f_nls", "f_dft"]
    values = [as_float(row.get(label)) for label in labels]
    fig, ax = plt.subplots(figsize=(6.5, 4))
    ax.bar(labels, [value if value is not None else 0.0 for value in values], color=["#4C78A8", "#F58518"])
    ax.set_ylabel("Frequency (Hz)")
    ax.set_title(f"Frequency Estimates: {row.get('filename') or report_path.name}")
    saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_frequency.png"))
    plt.close(fig)

    q_labels = ["Q_nls", "Q_dft"]
    q_values = [as_float(row.get(label)) for label in q_labels]
    if any(value is not None for value in q_values):
        fig, ax = plt.subplots(figsize=(6.5, 4))
        ax.bar(q_labels, [value if value is not None else 0.0 for value in q_values], color=["#54A24B", "#B279A2"])
        ax.set_ylabel("Quality Factor")
        ax.set_title(f"Q Estimates: {row.get('filename') or report_path.name}")
        saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_q.png"))
        plt.close(fig)

    saved.extend(plot_timings(report_path, data, output_dir))
    return saved


def plot_batch(report_path: Path, kind: str, data: dict[str, Any], output_dir: Path) -> list[Path]:
    plt = require_matplotlib()
    rows = batch_rows(type("Report", (), {"kind": kind, "data": data})())
    saved: list[Path] = []
    if not rows:
        return saved

    indices = list(range(len(rows)))
    labels = [Path(str(row.get("filename") or index)).name for index, row in enumerate(rows)]
    f_nls = finite_values([row.get("f_nls") for row in rows])
    f_dft = finite_values([row.get("f_dft") for row in rows])

    if f_nls and f_dft:
        count = min(len(f_nls), len(f_dft), len(indices))
        fig, ax = plt.subplots(figsize=(max(7, count * 0.45), 4.5))
        ax.plot(indices[:count], f_nls[:count], "o-", label="NLS", color="#4C78A8")
        ax.plot(indices[:count], f_dft[:count], "s-", label="DFT", color="#F58518")
        ax.set_ylabel("Frequency (Hz)")
        ax.set_xlabel("File")
        ax.set_title(f"Batch Frequency Estimates: {report_path.name}")
        set_file_xticklabels(ax, indices[:count], labels[:count])
        ax.legend()
        saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_batch_frequency.png"))
        plt.close(fig)

        fig, ax = plt.subplots(figsize=(5.5, 5.5))
        ax.scatter(f_nls[:count], f_dft[:count], s=70, color="#4C78A8", edgecolor="black", alpha=0.75)
        low = min(min(f_nls[:count]), min(f_dft[:count]))
        high = max(max(f_nls[:count]), max(f_dft[:count]))
        margin = max((high - low) * 0.08, abs(high) * 1.0e-9, 1.0e-12)
        ax.plot([low - margin, high + margin], [low - margin, high + margin], "--", color="#E45756")
        ax.set_xlabel("NLS Frequency (Hz)")
        ax.set_ylabel("DFT Frequency (Hz)")
        ax.set_title("NLS vs DFT Frequency")
        saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_nls_vs_dft.png"))
        plt.close(fig)

    q_values = finite_values([row.get("Q") or row.get("Q_nls") for row in rows])
    if q_values:
        fig, ax = plt.subplots(figsize=(7, 4))
        ax.hist(q_values, bins=min(30, max(5, len(q_values))), color="#54A24B", edgecolor="white")
        ax.set_xlabel("Quality Factor")
        ax.set_ylabel("Count")
        ax.set_title(f"Q Distribution: {report_path.name}")
        saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_q_distribution.png"))
        plt.close(fig)

    failed = failures(data)
    if failed:
        fig, ax = plt.subplots(figsize=(6, 3.5))
        ax.bar(["success", "failure"], [data.get("success_count", len(rows)), len(failed)], color=["#54A24B", "#E45756"])
        ax.set_ylabel("Files")
        ax.set_title(f"Batch Outcomes: {report_path.name}")
        saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_outcomes.png"))
        plt.close(fig)

    saved.extend(plot_timings(report_path, data, output_dir))
    return saved


def plot_timings(report_path: Path, data: dict[str, Any], output_dir: Path) -> list[Path]:
    plt = require_matplotlib()
    rows = timing_rows(data)
    if not rows:
        return []

    totals = [(row.get("filename") or str(index), as_float((row.get("timings") or {}).get("total"))) for index, row in enumerate(rows)]
    totals = [(Path(name).name, value) for name, value in totals if value is not None]
    if not totals:
        return []

    labels, values = zip(*totals)
    fig, ax = plt.subplots(figsize=(max(7, len(values) * 0.45), 4.5))
    ax.bar(range(len(values)), values, color="#72B7B2")
    ax.set_ylabel("Total Time (ms)")
    ax.set_xlabel("File")
    ax.set_title(f"Analysis Timings: {report_path.name}")
    set_file_xticklabels(ax, list(range(len(values))), list(labels))
    path = save_figure(fig, output_dir, f"{safe_stem(report_path)}_timings.png")
    plt.close(fig)
    return [path]


def plot_monte_carlo(report_path: Path, data: dict[str, Any], output_dir: Path) -> list[Path]:
    plt = require_matplotlib()
    series = monte_carlo_error_series(data)
    saved: list[Path] = []

    fig, axes = plt.subplots(2, 2, figsize=(10, 7))
    for ax, (label, values) in zip(axes.ravel(), series.items()):
        if values:
            ax.hist(values, bins=min(40, max(5, len(values))), color="#4C78A8", edgecolor="white")
            ax.axvline(0.0, color="#E45756", linestyle="--", linewidth=1.5)
        ax.set_title(label)
        ax.set_xlabel("Error")
        ax.set_ylabel("Count")
    fig.suptitle(f"Monte Carlo Error Distributions: {report_path.name}")
    saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_monte_carlo_errors.png"))
    plt.close(fig)

    labels = ["f0", "Q", "fs", "N", "snr_db", "crlb_std", "crlb_std_q"]
    text = "\n".join(f"{label}: {data.get(label, '-')}" for label in labels)
    fig, ax = plt.subplots(figsize=(6, 3.5))
    ax.axis("off")
    ax.text(0.02, 0.95, text, va="top", family="monospace")
    ax.set_title("Monte Carlo Configuration")
    saved.append(save_figure(fig, output_dir, f"{safe_stem(report_path)}_monte_carlo_config.png"))
    plt.close(fig)
    return saved


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Create plots from compact ringdown JSON outputs.")
    parser.add_argument("json_files", nargs="+", type=Path, help="JSON files produced by ringdown")
    parser.add_argument("-o", "--output-dir", type=Path, default=Path("results/ringdown_plots"))
    args = parser.parse_args(argv)

    all_saved: list[Path] = []
    try:
        for path in args.json_files:
            reports = load_reports(path)
            if len(reports) > 1:
                print(f"{path}: found {len(reports)} appended JSON reports")
            for report in reports:
                stem = report_stem(path, report.document_index, report.document_count)
                output_dir = args.output_dir / stem
                display_path = Path(report_label(report))
                if report.kind == "analysis":
                    all_saved.extend(plot_analysis(display_path, report.data, output_dir))
                elif report.kind.startswith("batch"):
                    all_saved.extend(plot_batch(display_path, report.kind, report.data, output_dir))
                elif report.kind == "monte_carlo":
                    all_saved.extend(plot_monte_carlo(display_path, report.data, output_dir))
    except (OSError, RingdownJsonError) as error:
        print(f"plot_ringdown_output: {error}", file=sys.stderr)
        return 1

    for path in all_saved:
        print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
