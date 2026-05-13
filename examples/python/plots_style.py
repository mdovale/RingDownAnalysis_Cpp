"""Matplotlib rcParams aligned with `ringdownanalysis.plots.get_default_rc` for example figures."""

from __future__ import annotations

import matplotlib.pyplot as plt


def get_default_rc() -> dict:
    return {
        "figure.dpi": 150,
        "font.size": 8,
        "axes.labelsize": 8,
        "axes.titlesize": 8,
        "xtick.labelsize": 8,
        "ytick.labelsize": 8,
        "axes.prop_cycle": plt.cycler(
            "color",
            [
                "#000000",
                "#DC143C",
                "#00BFFF",
                "#FFD700",
                "#32CD32",
                "#FF69B4",
                "#FF4500",
                "#1E90FF",
                "#8A2BE2",
                "#FFA07A",
                "#8B0000",
            ],
        ),
    }


def apply_plotting_style() -> None:
    plt.rcParams.update(get_default_rc())
