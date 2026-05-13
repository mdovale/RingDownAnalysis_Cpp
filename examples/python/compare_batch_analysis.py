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


def _float_array(values):  # noqa: ANN001
    import numpy as np

    return np.asarray(values or [], dtype=float)


def _hist_bins(values) -> int:  # noqa: ANN001
    return min(30, max(1, len(values)))


def _finite_stats(values) -> dict:
    import numpy as np

    finite = np.asarray(values, dtype=float)
    finite = finite[np.isfinite(finite)]
    if finite.size == 0:
        nan = float("nan")
        return {"mean": nan, "std": nan, "min": nan, "max": nan, "median": nan}
    return {
        "mean": float(np.mean(finite)),
        "std": float(np.std(finite)),
        "min": float(np.min(finite)),
        "max": float(np.max(finite)),
        "median": float(np.median(finite)),
    }


def _positive_plot_limits(*arrays) -> tuple[float, float]:
    import numpy as np

    positives = []
    for values in arrays:
        arr = np.asarray(values, dtype=float)
        positives.extend(arr[np.isfinite(arr) & (arr > 0.0)])
    if not positives:
        return (1.0e-12, 1.0)
    min_val = min(positives)
    max_val = max(positives)
    if min_val == max_val:
        return (min_val * 0.5, max_val * 2.0)
    return (min_val * 0.5, max_val * 2.0)


def _result_series(report: dict, key: str, summary_key: str):
    values = [r.get(key) for r in report.get("results_notebook") or [] if r.get(key) is not None]
    if not values:
        values = [
            r.get(summary_key)
            for r in report.get("summary_table") or []
            if r.get(summary_key) is not None
        ]
    return _float_array(values)


def _save_frequency_overlay(py: dict, cpp: dict, path: Path) -> None:
    import matplotlib.pyplot as plt
    import numpy as np

    from plots_style import apply_plotting_style

    apply_plotting_style()
    py_rows = py.get("summary_table") or []
    cpp_rows = cpp.get("summary_table") or []
    if not py_rows or not cpp_rows:
        return
    count = min(len(py_rows), len(cpp_rows))
    idx = np.arange(count)
    fpy = np.array([float(r["f_NLS (Hz)"]) for r in py_rows[:count]])
    fcpp = np.array([float(r["f_NLS (Hz)"]) for r in cpp_rows[:count]])
    fig, ax = plt.subplots(figsize=(7, 3))
    ax.plot(idx, fpy, "o", label="Python f_NLS")
    ax.plot(idx, fcpp, "x", label="C++ f_NLS")
    ax.set_xlabel("file index")
    ax.set_ylabel("f_NLS (Hz)")
    ax.legend()
    fig.tight_layout()
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=150)
    plt.close(fig)


def _save_report_plots(report: dict, output_dir: Path, label: str) -> list[Path]:
    import matplotlib.pyplot as plt
    import numpy as np

    from plots_style import apply_plotting_style

    apply_plotting_style()
    output_dir.mkdir(parents=True, exist_ok=True)
    saved: list[Path] = []

    def save(fig, filename: str) -> None:  # noqa: ANN001
        path = output_dir / filename
        fig.tight_layout()
        fig.savefig(path, dpi=150)
        plt.close(fig)
        saved.append(path)

    results = report.get("results_notebook") or []
    f_nls_all = _result_series(report, "f_nls", "f_NLS (Hz)")
    f_dft_all = _result_series(report, "f_dft", "f_DFT (Hz)")
    tau_all = _result_series(report, "tau_est", "tau_est (s)")
    plugin_all = _result_series(report, "plugin_crlb_std_f", "Plugin bound std (Hz)")
    q_factors = _float_array(report.get("q_factors") or [])
    if q_factors.size == 0:
        q_factors = _result_series(report, "Q_nls", "Q_NLS")
    q_stats = report.get("q_factor_statistics") or _finite_stats(q_factors)
    consistency = report.get("consistency_analysis") or {}
    crlb_analysis = report.get("crlb_comparison_analysis") or {}

    if results:
        n_plots = len(results)
        fig, axes = plt.subplots(n_plots, 1, figsize=(14, 4 * n_plots), squeeze=False)
        for ax, result in zip(axes[:, 0], results):
            time = _float_array(result.get("t"))
            samples = _float_array(result.get("data"))
            cropped_time = _float_array(result.get("t_crop"))
            cropped_samples = _float_array(result.get("data_cropped"))
            step = max(1, len(time) // 50000)
            step_crop = max(1, len(cropped_time) // 50000)

            if time.size and samples.size:
                ax.plot(time[::step], samples[::step], "b-", alpha=0.5, label="Original", linewidth=0.5)
            if cropped_time.size and cropped_samples.size:
                ax.plot(
                    cropped_time[::step_crop],
                    cropped_samples[::step_crop],
                    "r-",
                    alpha=0.7,
                    label="Analyzed crop",
                    linewidth=1,
                )

            t_crop = result.get("T_crop")
            tau_est = result.get("tau_est")
            if t_crop is not None:
                ax.axvline(
                    float(t_crop),
                    color="r",
                    linestyle="--",
                    linewidth=2,
                    label=f"T_crop = {float(t_crop):.2f} s",
                )
            if tau_est is not None:
                tau_3x = 3.0 * float(tau_est)
                ax.axvline(
                    tau_3x,
                    color="g",
                    linestyle=":",
                    linewidth=2,
                    label=f"3x tau reference = {tau_3x:.2f} s",
                )

            ax.set_xlabel("Time (s)")
            ax.set_ylabel("Phase (cycles)")
            ax.set_title(
                f"{Path(result.get('filename', '')).name}\n"
                f"tau = {float(result.get('tau_est', float('nan'))):.2f} s, "
                f"f_NLS = {float(result.get('f_nls', float('nan'))):.6f} Hz, "
                f"f_DFT = {float(result.get('f_dft', float('nan'))):.6f} Hz"
            )
            ax.legend(loc="best")
            ax.grid(True, alpha=0.3)
        fig.suptitle(f"{label}: Time Series", y=1.0)
        save(fig, "time_series.png")

    if f_nls_all.size and f_dft_all.size:
        count = min(f_nls_all.size, f_dft_all.size)
        f_nls = f_nls_all[:count]
        f_dft = f_dft_all[:count]
        diffs = np.abs(f_nls - f_dft)
        fig, axes = plt.subplots(1, 2, figsize=(14, 5))

        ax = axes[0]
        ax.scatter(f_nls, f_dft, s=100, alpha=0.6, edgecolors="black", linewidths=1)
        f_min = min(float(np.min(f_nls)), float(np.min(f_dft)))
        f_max = max(float(np.max(f_nls)), float(np.max(f_dft)))
        margin = max((f_max - f_min) * 0.1, abs(f_min) * 1.0e-6, 1.0e-12)
        ax.plot(
            [f_min - margin, f_max + margin],
            [f_min - margin, f_max + margin],
            "r--",
            linewidth=2,
            label="Perfect agreement",
        )
        ax.set_xlabel("NLS Frequency (Hz)")
        ax.set_ylabel("DFT Frequency (Hz)")
        ax.set_title("Frequency Estimation Comparison: NLS vs DFT")
        ax.legend()
        ax.grid(True, alpha=0.3)

        ax = axes[1]
        ax.hist(diffs, bins=_hist_bins(diffs), edgecolor="black", alpha=0.7, color="steelblue")
        ax.axvline(np.mean(diffs), color="r", linestyle="--", linewidth=2, label=f"Mean = {np.mean(diffs):.6e} Hz")
        ax.axvline(
            np.median(diffs),
            color="g",
            linestyle="--",
            linewidth=2,
            label=f"Median = {np.median(diffs):.6e} Hz",
        )
        ax.set_xlabel("|f_NLS - f_DFT| (Hz)")
        ax.set_ylabel("Count")
        ax.set_title("Frequency Difference Distribution")
        ax.legend()
        ax.grid(True, alpha=0.3, axis="y")
        fig.suptitle(f"{label}: Frequency Comparison", y=1.02)
        save(fig, "frequency_estimation_comparison.png")

    if q_factors.size:
        q_stats = {**_finite_stats(q_factors), **q_stats}
        fig, axes = plt.subplots(1, 2, figsize=(14, 5))

        ax = axes[0]
        ax.hist(q_factors, bins=_hist_bins(q_factors), edgecolor="black", alpha=0.7, color="steelblue")
        ax.axvline(q_stats["mean"], color="r", linestyle="--", linewidth=2, label=f"Mean = {q_stats['mean']:.2e}")
        ax.axvline(q_stats["mean"] + q_stats["std"], color="orange", linestyle="--", linewidth=1, label="+/-1 sigma")
        ax.axvline(q_stats["mean"] - q_stats["std"], color="orange", linestyle="--", linewidth=1)
        ax.set_xlabel("Q Factor")
        ax.set_ylabel("Count")
        ax.set_title("Q Factor Distribution")
        ax.legend()
        ax.grid(True, alpha=0.3, axis="y")

        ax = axes[1]
        bp = ax.boxplot([q_factors], patch_artist=True, widths=0.6)
        ax.set_xticklabels(["Q Factor"])
        bp["boxes"][0].set_facecolor("lightblue")
        bp["boxes"][0].set_alpha(0.7)
        ax.set_ylabel("Q Factor")
        ax.set_title("Q Factor Box Plot")
        ax.grid(True, alpha=0.3, axis="y")
        fig.suptitle(f"{label}: Q Factor Analysis", y=1.02)
        save(fig, "q_factor_analysis.png")

    if consistency and f_nls_all.size and f_dft_all.size:
        fig, axes = plt.subplots(2, 2, figsize=(14, 10))

        nls_diffs = _float_array(consistency.get("nls_pairwise_diffs"))
        dft_diffs = _float_array(consistency.get("dft_pairwise_diffs"))
        nls_stats = consistency.get("nls_statistics") or _finite_stats(nls_diffs)
        dft_stats = consistency.get("dft_statistics") or _finite_stats(dft_diffs)

        ax = axes[0, 0]
        ax.hist(nls_diffs, bins=_hist_bins(nls_diffs), edgecolor="black", alpha=0.7, color="steelblue")
        ax.axvline(nls_stats["mean"], color="r", linestyle="--", linewidth=2, label=f"Mean = {nls_stats['mean']:.6e} Hz")
        ax.set_xlabel("Pairwise Frequency Difference (Hz)")
        ax.set_ylabel("Count")
        ax.set_title("NLS: Pairwise Frequency Differences")
        ax.legend()
        ax.grid(True, alpha=0.3, axis="y")

        ax = axes[0, 1]
        ax.hist(dft_diffs, bins=_hist_bins(dft_diffs), edgecolor="black", alpha=0.7, color="coral")
        ax.axvline(dft_stats["mean"], color="r", linestyle="--", linewidth=2, label=f"Mean = {dft_stats['mean']:.6e} Hz")
        ax.set_xlabel("Pairwise Frequency Difference (Hz)")
        ax.set_ylabel("Count")
        ax.set_title("DFT: Pairwise Frequency Differences")
        ax.legend()
        ax.grid(True, alpha=0.3, axis="y")

        count = min(f_nls_all.size, f_dft_all.size)
        f_nls = f_nls_all[:count]
        f_dft = f_dft_all[:count]

        ax = axes[1, 0]
        ax.hist(
            f_nls,
            bins=_hist_bins(f_nls),
            alpha=0.6,
            label=f"NLS (std={float(consistency.get('nls_std_across_realizations', np.std(f_nls))):.6e})",
            edgecolor="black",
            color="steelblue",
        )
        ax.hist(
            f_dft,
            bins=_hist_bins(f_dft),
            alpha=0.6,
            label=f"DFT (std={float(consistency.get('dft_std_across_realizations', np.std(f_dft))):.6e})",
            edgecolor="black",
            color="coral",
        )
        ax.axvline(float(consistency.get("nls_mean", np.mean(f_nls))), color="blue", linestyle="--", linewidth=2)
        ax.axvline(float(consistency.get("dft_mean", np.mean(f_dft))), color="red", linestyle="--", linewidth=2)
        ax.set_xlabel("Frequency (Hz)")
        ax.set_ylabel("Count")
        ax.set_title("Frequency Distribution Across Realizations")
        ax.legend()
        ax.grid(True, alpha=0.3, axis="y")

        ax = axes[1, 1]
        bp = ax.boxplot([f_nls, f_dft], patch_artist=True, widths=0.6)
        ax.set_xticklabels(["NLS", "DFT"])
        bp["boxes"][0].set_facecolor("steelblue")
        bp["boxes"][0].set_alpha(0.7)
        bp["boxes"][1].set_facecolor("coral")
        bp["boxes"][1].set_alpha(0.7)
        ax.set_ylabel("Frequency (Hz)")
        ax.set_title("Frequency Estimation Comparison (Box Plot)")
        ax.grid(True, alpha=0.3, axis="y")
        fig.suptitle(f"{label}: Consistency Analysis", y=1.02)
        save(fig, "consistency_analysis.png")

    if crlb_analysis:
        freq_diffs = _float_array(crlb_analysis.get("frequency_diffs"))
        plugin_stds = _float_array(crlb_analysis.get("plugin_crlb_stds"))
        valid_ratios = _float_array(crlb_analysis.get("valid_ratios"))
        ratio_stats = crlb_analysis.get("ratio_statistics") or _finite_stats(valid_ratios)
        crlb_stats = crlb_analysis.get("crlb_statistics") or _finite_stats(plugin_stds)

        if freq_diffs.size and plugin_stds.size:
            fig, axes = plt.subplots(2, 2, figsize=(14, 10))

            ax = axes[0, 0]
            positive_mask = (freq_diffs > 0.0) & (plugin_stds > 0.0)
            if np.any(positive_mask):
                ax.scatter(plugin_stds[positive_mask], freq_diffs[positive_mask], s=100, alpha=0.6, edgecolors="black")
                min_plot, max_plot = _positive_plot_limits(plugin_stds[positive_mask], freq_diffs[positive_mask])
                ax.loglog([min_plot, max_plot], [min_plot, max_plot], "r--", linewidth=2, label="Difference = plugin bound")
                ax.set_xlim([min_plot, max_plot])
                ax.set_ylim([min_plot, max_plot])
            else:
                ax.scatter(plugin_stds, freq_diffs, s=100, alpha=0.6, edgecolors="black")
            ax.set_xlabel("Plug-in uncertainty std (Hz)")
            ax.set_ylabel("|f_NLS - f_DFT| (Hz)")
            ax.set_title("Frequency Difference vs Plug-in Bound")
            ax.legend()
            ax.grid(True, alpha=0.3)

            ax = axes[0, 1]
            if valid_ratios.size:
                ax.hist(valid_ratios, bins=_hist_bins(valid_ratios), edgecolor="black", alpha=0.7, color="steelblue")
                ax.axvline(ratio_stats["mean"], color="r", linestyle="--", linewidth=2, label=f"Mean = {ratio_stats['mean']:.4f}")
                ax.axvline(1.0, color="g", linestyle="--", linewidth=2, label="Ratio = 1.0")
                ax.set_xlabel("|Delta f| / plugin bound")
                ax.set_ylabel("Count")
                ax.set_title("Heuristic Ratio Distribution")
                ax.legend()
                ax.grid(True, alpha=0.3, axis="y")
            else:
                ax.text(0.5, 0.5, "No valid ratios", ha="center", va="center", transform=ax.transAxes)

            ax = axes[1, 0]
            ax.hist(plugin_stds, bins=_hist_bins(plugin_stds), edgecolor="black", alpha=0.7, color="steelblue")
            ax.axvline(crlb_stats["mean"], color="r", linestyle="--", linewidth=2, label=f"Mean = {crlb_stats['mean']:.6e} Hz")
            ax.set_xlabel("Plug-in uncertainty std (Hz)")
            ax.set_ylabel("Count")
            ax.set_title("Plug-in Bound Distribution")
            ax.legend()
            ax.grid(True, alpha=0.3, axis="y")

            ax = axes[1, 1]
            ax.hist(freq_diffs, bins=_hist_bins(freq_diffs), edgecolor="black", alpha=0.7, color="coral")
            ax.axvline(np.mean(freq_diffs), color="r", linestyle="--", linewidth=2, label=f"Mean = {np.mean(freq_diffs):.6e} Hz")
            ax.set_xlabel("|f_NLS - f_DFT| (Hz)")
            ax.set_ylabel("Count")
            ax.set_title("Frequency Difference Distribution")
            ax.legend()
            ax.grid(True, alpha=0.3, axis="y")
            fig.suptitle(f"{label}: Plug-in Uncertainty Comparison", y=1.02)
            save(fig, "plugin_uncertainty_comparison.png")

    if f_nls_all.size and f_dft_all.size and tau_all.size and plugin_all.size:
        count = min(f_nls_all.size, f_dft_all.size, tau_all.size, plugin_all.size)
        indices = np.arange(count)
        f_nls = f_nls_all[:count]
        f_dft = f_dft_all[:count]
        tau = tau_all[:count]
        plugin = plugin_all[:count]
        diffs = np.abs(f_nls - f_dft)
        fig, axes = plt.subplots(2, 3, figsize=(18, 10))

        ax = axes[0, 0]
        ax.plot(indices, f_nls, "o-", label="NLS", alpha=0.7, linewidth=2, markersize=6)
        ax.plot(indices, f_dft, "s-", label="DFT", alpha=0.7, linewidth=2, markersize=6)
        if consistency:
            ax.axhline(float(consistency.get("nls_mean", np.mean(f_nls))), color="blue", linestyle="--", alpha=0.5)
            ax.axhline(float(consistency.get("dft_mean", np.mean(f_dft))), color="red", linestyle="--", alpha=0.5)
        ax.set_xlabel("File Index")
        ax.set_ylabel("Frequency (Hz)")
        ax.set_title("Frequency Estimates Across Files")
        ax.legend()
        ax.grid(True, alpha=0.3)

        ax = axes[0, 1]
        ax.plot(indices, tau, "o-", color="green", alpha=0.7, linewidth=2, markersize=6)
        ax.axhline(np.mean(tau), color="green", linestyle="--", alpha=0.5, label=f"Mean = {np.mean(tau):.2f} s")
        ax.set_xlabel("File Index")
        ax.set_ylabel("Tau (s)")
        ax.set_title("Tau Estimates Across Files")
        ax.legend()
        ax.grid(True, alpha=0.3)

        ax = axes[0, 2]
        ax.plot(indices, plugin, "o-", color="purple", alpha=0.7, linewidth=2, markersize=6)
        ax.axhline(np.mean(plugin), color="purple", linestyle="--", alpha=0.5, label=f"Mean = {np.mean(plugin):.6e} Hz")
        ax.set_xlabel("File Index")
        ax.set_ylabel("Plug-in std (Hz)")
        ax.set_title("Plug-in Bounds Across Files")
        if np.all(plugin > 0.0):
            ax.set_yscale("log")
        ax.legend()
        ax.grid(True, alpha=0.3)

        ax = axes[1, 0]
        if q_factors.size:
            q_count = min(q_factors.size, count)
            ax.plot(indices[:q_count], q_factors[:q_count], "o-", color="orange", alpha=0.7, linewidth=2, markersize=6)
            ax.axhline(q_stats["mean"], color="orange", linestyle="--", alpha=0.5, label=f"Mean = {q_stats['mean']:.2e}")
            ax.set_xlabel("File Index")
            ax.set_ylabel("Q Factor")
            ax.set_title("Q Factors Across Files")
            if np.all(q_factors[:q_count] > 0.0):
                ax.set_yscale("log")
            ax.legend()
            ax.grid(True, alpha=0.3)
        else:
            ax.axis("off")

        ax = axes[1, 1]
        ax.plot(indices, diffs, "o-", color="red", alpha=0.7, linewidth=2, markersize=6)
        ax.axhline(np.mean(diffs), color="red", linestyle="--", alpha=0.5, label=f"Mean = {np.mean(diffs):.6e} Hz")
        ax.set_xlabel("File Index")
        ax.set_ylabel("|f_NLS - f_DFT| (Hz)")
        ax.set_title("Frequency Differences Across Files")
        if np.all(diffs > 0.0):
            ax.set_yscale("log")
        ax.legend()
        ax.grid(True, alpha=0.3)

        ax = axes[1, 2]
        ratios = _float_array(crlb_analysis.get("ratios")) if crlb_analysis else _float_array([])
        if ratios.size:
            ratio_count = min(ratios.size, count)
            ratio_indices = indices[:ratio_count]
            ratio_values = ratios[:ratio_count]
            valid_mask = np.isfinite(ratio_values)
            ax.plot(ratio_indices[valid_mask], ratio_values[valid_mask], "o-", color="teal", alpha=0.7, linewidth=2, markersize=6)
            ax.axhline(1.0, color="g", linestyle="--", linewidth=2, label="Ratio = 1.0")
            if ratio_stats:
                ax.axhline(ratio_stats["mean"], color="teal", linestyle="--", alpha=0.5, label=f"Mean = {ratio_stats['mean']:.4f}")
            ax.set_xlabel("File Index")
            ax.set_ylabel("|Delta f| / plugin bound")
            ax.set_title("Heuristic Difference Ratio")
            ax.legend()
            ax.grid(True, alpha=0.3)
        else:
            ax.axis("off")

        fig.suptitle(f"{label}: Summary Statistics Overview", y=1.02)
        save(fig, "summary_statistics_overview.png")

    return saved


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--py-report", type=Path, required=True)
    p.add_argument("--cpp-report", type=Path, required=True)
    p.add_argument(
        "--rtol",
        type=float,
        default=5e-6,
        help="Relative tolerance for independent Python/C++ estimator outputs.",
    )
    p.add_argument(
        "--atol",
        type=float,
        default=3e-5,
        help="Absolute tolerance in Hz for independent Python/C++ estimator outputs.",
    )
    p.add_argument("--plot", type=Path, default=None)
    p.add_argument(
        "--plot-dir",
        type=Path,
        default=None,
        help="Directory for reference-notebook-style plots for Python and C++ reports.",
    )
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
        ):
            if not is_close(py_c.get(k), cpp_c.get(k), args.rtol, args.atol):
                errs.append(f"consistency.{k} py={py_c.get(k)!r} cpp={cpp_c.get(k)!r}")
        for k in ("nls_span", "dft_span"):
            if not is_close(py_c.get(k), cpp_c.get(k), args.rtol, 2.0 * args.atol):
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

    if args.plot is not None:
        _save_frequency_overlay(py, cpp, args.plot)
        print(f"Wrote plot {args.plot}")

    if args.plot_dir is not None:
        py_plots = _save_report_plots(py, args.plot_dir / "python", "Python")
        cpp_plots = _save_report_plots(cpp, args.plot_dir / "cpp", "C++")
        overlay = args.plot_dir / "comparison_f_nls_overlay.png"
        _save_frequency_overlay(py, cpp, overlay)
        print(f"Wrote {len(py_plots)} Python plot(s) under {args.plot_dir / 'python'}")
        print(f"Wrote {len(cpp_plots)} C++ plot(s) under {args.plot_dir / 'cpp'}")
        print(f"Wrote comparison plot {overlay}")

    if errs:
        print("Mismatches:")
        print("\n".join(errs))
        sys.exit(1)

    print("OK: batch report fields match within tolerances.")


if __name__ == "__main__":
    main()
