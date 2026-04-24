#!/usr/bin/env python3
"""
Plot acceleration signals from a CSV log file.

What it does:
- Loads a selectable CSV log file.
- Calculates a moving average only for: lin_ax_raw_mps2
- Calculates a low-pass filtered signal only for: lin_ax_raw_mps2
- Plots these together with: lin_ax_filt_mps2
- Uses t_s as x-axis when available, otherwise uses sample index.

Edit the CONFIG section below for quick use, or run from the command line.
"""

from pathlib import Path
import argparse
import sys

import matplotlib.pyplot as plt
import pandas as pd


# =========================
# CONFIG - easy to change
# =========================
INPUT_FILE = "231833_engineoff.CSV"      # Change this to another log file when needed
WINDOW_SIZE = 5               # Change this to another sample count when needed
LOW_PASS_ALPHA = 0.17           # Change this alpha when needed
RAW_COLUMN = "raw_ax_filt_mps2"
FILTERED_COLUMN = "raw_ax_filt_mps2"
TIME_COLUMN = "t_s"


def calculate_low_pass(series: pd.Series, alpha: float) -> pd.Series:
    """First-order low-pass filter: y[n] = alpha*x[n] + (1-alpha)*y[n-1]."""
    if not 0 < alpha <= 1:
        raise ValueError("Alpha must be in the range 0 < alpha <= 1.")

    filtered = []
    previous = None

    for value in series:
        if pd.isna(value):
            filtered.append(previous if previous is not None else value)
            continue

        if previous is None:
            previous = value
        else:
            previous = alpha * value + (1 - alpha) * previous

        filtered.append(previous)

    return pd.Series(filtered, index=series.index)


def load_and_prepare_data(
    input_file: str | Path,
    window_size: int,
    alpha: float,
    raw_column: str = RAW_COLUMN,
    filtered_column: str = FILTERED_COLUMN,
) -> tuple[pd.DataFrame, str, str]:
    """Load CSV and add calculated filter columns."""

    input_path = Path(input_file)

    if not input_path.exists():
        raise FileNotFoundError(f"Input file not found: {input_path}")

    if window_size <= 0:
        raise ValueError("Window size must be greater than 0.")

    df = pd.read_csv(input_path)

    for required_column in (raw_column, filtered_column):
        if required_column not in df.columns:
            available = ", ".join(df.columns)
            raise KeyError(
                f"Column '{required_column}' not found in file. Available columns: {available}"
            )

    moving_average_column = f"{raw_column}_ma_{window_size}"
    low_pass_column = f"{raw_column}_lp_a{str(alpha).replace('.', '_')}"

    df[moving_average_column] = df[raw_column].rolling(window=window_size, min_periods=1).mean()
    df[low_pass_column] = calculate_low_pass(df[raw_column], alpha)

    return df, moving_average_column, low_pass_column


def plot_signals(
    df: pd.DataFrame,
    moving_average_column: str,
    low_pass_column: str,
    raw_column: str = RAW_COLUMN,
    filtered_column: str = FILTERED_COLUMN,
    time_column: str = TIME_COLUMN,
    input_file: str | Path = INPUT_FILE,
    window_size: int = WINDOW_SIZE,
    alpha: float = LOW_PASS_ALPHA,
) -> None:
    """Plot lin_ax_filt_mps2, moving average, and low-pass filtered raw signal."""

    if time_column in df.columns:
        x = df[time_column]
        x_label = time_column
    else:
        x = df.index
        x_label = "sample"

    plt.figure(figsize=(12, 6))
    plt.plot(x, df[filtered_column], label=filtered_column)
    plt.plot(x, df[moving_average_column], label=f"moving average ({window_size} samples)")
    #plt.plot(x, df[low_pass_column], label=f"low-pass alpha={alpha}")

    plt.xlabel(x_label)
    plt.ylabel("acceleration [m/s²]")
    plt.title(f"{Path(input_file).name}: {filtered_column} vs calculated filters")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Plot lin_ax_filt_mps2 together with a moving average and a low-pass "
            "filter calculated from lin_ax_raw_mps2."
        )
    )
    parser.add_argument(
        "input_file",
        nargs="?",
        default=INPUT_FILE,
        help=f"Path to the CSV log file (default: {INPUT_FILE})",
    )
    parser.add_argument(
        "--window",
        type=int,
        default=WINDOW_SIZE,
        help=f"Moving average window in samples (default: {WINDOW_SIZE})",
    )
    parser.add_argument(
        "--alpha",
        type=float,
        default=LOW_PASS_ALPHA,
        help=f"Low-pass filter alpha (default: {LOW_PASS_ALPHA})",
    )
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    try:
        df, moving_average_column, low_pass_column = load_and_prepare_data(
            args.input_file,
            args.window,
            args.alpha,
        )
        plot_signals(
            df,
            moving_average_column,
            low_pass_column,
            input_file=args.input_file,
            window_size=args.window,
            alpha=args.alpha,
        )
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
