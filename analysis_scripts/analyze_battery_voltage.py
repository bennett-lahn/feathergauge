"""
Battery voltage analysis and comparison for FeatherGauge datasets (burst and continuous).

This script:
- Loads one or more CSV files like: "W.G. Num: 31,Timestamp,Pressure [mbar],Temp [deg C],Battery [VDC]"
- Uses every sample for both burst and continuous datasets
- Converts timestamps to elapsed time per dataset (so different start dates align)
- Fits a sigmoidal (logistic) discharge curve typical of Li-ion cells per dataset
- Overlays measured voltage and fitted curves for comparison

Special filename handling (auto labels and modes):
- *BAT_TEST_CONS_NO_SLEEP.csv*  -> label: "Old code, 8 Hz"      (continuous)
- *BAT_TEST_CONS_SLEEP.csv*     -> label: "New code, 16 Hz"     (continuous)
- *BAT_TEST.csv* or *BAT_TEST*  -> label: "2 minute burst sampling" (burst)

Usage (PowerShell examples):
  # Compare three datasets together and save a figure
  python .\analyze_battery_voltage.py --files BAT_TEST.csv BAT_TEST_CONS_NO_SLEEP.csv BAT_TEST_CONS_SLEEP.csv --gap-seconds 120 --time-unit hours --savefig battery_comparison.png

  # Single file (backwards-compatible)
  python .\analyze_battery_voltage.py --csv BAT_TEST.CSV --gap-seconds 120 --time-unit hours --savefig BAT_TEST_battery_fit.png

Dependencies: pandas, numpy, scipy, matplotlib
  pip install pandas numpy scipy matplotlib
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from typing import List, Tuple

import numpy as np
import pandas as pd
from matplotlib import pyplot as plt
from scipy.optimize import curve_fit


# --------------------------- Parsing & Utilities --------------------------- #

def _parse_timestamp_strings(date_str: str, time_str: str) -> pd.Timestamp:
    """Parse date and time strings in various formats.
    
    Format 1: '2025/9/16' and '17:04:35:039' (separate date/time)
    Format 2: '2000/1/1 0:0:1' (combined date/time)
    """
    # Check if this is format 2 (combined date/time)
    if " " in date_str and ":" in date_str:
        # Format 2: "2000/1/1 0:0:1" - timestamp is in date_str, time_str is actually pressure
        ts = pd.to_datetime(date_str, errors="coerce")
        return ts
    
    # Format 1: separate date and time
    if isinstance(time_str, str) and time_str.count(":") >= 3:
        # Convert last ':' to '.' -> HH:MM:SS.mmm
        # For format like "17:04:35:039", we want "17:04:35.039"
        parts = time_str.split(":")
        if len(parts) == 4:
            time_str = f"{parts[0]}:{parts[1]}:{parts[2]}.{parts[3]}"
    
    # Combine and let pandas infer; coerce errors to NaT
    ts = pd.to_datetime(f"{date_str} {time_str}", errors="coerce")
    return ts


def _robust_read(path: str) -> pd.DataFrame:
    """Read CSV robustly regardless of header quirks.

    Expected columns (by position):
      0=date, 1=time, 2=pressure[mbar], 3=temp[C], 4=battery[V]
    """
    print(f"Reading CSV file: {path}")
    
    # Try different encodings to handle various file formats
    encodings = ['utf-8', 'latin-1', 'cp1252', 'iso-8859-1']
    df = None
    
    for i, encoding in enumerate(encodings):
        print(f"  Trying encoding {i+1}/{len(encodings)}: {encoding}")
        try:
            df = pd.read_csv(
                path,
                header=None,
                names=["c0", "c1", "c2", "c3", "c4"],
                engine="python",
                dtype=str,
                encoding=encoding,
            )
            print(f"  Successfully read with {encoding} encoding")
            break
        except (UnicodeDecodeError, UnicodeError) as e:
            print(f"  Failed with {encoding}: {str(e)[:100]}...")
            continue
    
    if df is None:
        raise ValueError(f"Could not read {path} with any supported encoding")

    print(f"  Raw data shape: {df.shape}")
    
    # Drop obvious header row if present
    if len(df) > 0:
        row0 = df.iloc[0]
        if isinstance(row0["c0"], str) and ("W.G." in row0["c0"] or "Timestamp" in str(row0["c0"]) or "Timestamp" in str(row0["c1"])):
            print("  Dropping header row")
            df = df.iloc[1:].reset_index(drop=True)
            print(f"  After header removal: {df.shape}")
            if len(df) > 0:
                print(f"  First data row: c0='{df.iloc[0]['c0']}', c1='{df.iloc[0]['c1']}', c2='{df.iloc[0]['c2']}', c3='{df.iloc[0]['c3']}', c4='{df.iloc[0]['c4']}'")

    # Basic cleaning - keep rows that have valid data in first two columns
    print("  Cleaning data...")
    df = df.dropna(subset=["c0", "c1"]).copy()
    print(f"  After basic cleaning: {df.shape}")
    
    # Sample every 100th row (1%) for large datasets to reduce processing time
    # Do this before timestamp parsing which is the slowest step
    if len(df) > 100000:
        print(f"  Large dataset detected, sampling every 100th row before timestamp parsing...")
        df = df.iloc[::100].reset_index(drop=True)
        # Record sampling factor so downstream computations (e.g., Hz) can compensate
        try:
            df.attrs["sampling_factor"] = float(df.attrs.get("sampling_factor", 1.0)) * 100.0
        except Exception:
            df.attrs["sampling_factor"] = 100.0
        print(f"  Sampled {len(df)} rows from original dataset (1% sampling)")
    
    # Detect data format by checking if first row has combined timestamp
    first_row = df.iloc[0] if len(df) > 0 else None
    is_combined_format = (first_row is not None and 
                         isinstance(first_row["c0"], str) and 
                         " " in str(first_row["c0"]) and ":" in str(first_row["c0"]))
    
    print(f"  First row c0: '{first_row['c0'] if first_row is not None else 'None'}'")
    print(f"  Detected format: {'Combined timestamp' if is_combined_format else 'Separate date/time'}")
    
    # Parse timestamps
    print("  Parsing timestamps...")
    if is_combined_format:
        # Format 2: timestamp in c0, pressure in c1, temp in c2, battery in c3
        df["timestamp"] = [_parse_timestamp_strings(d, "") for d in df["c0"].astype(str)]
        df["pressure_mbar"] = pd.to_numeric(df["c1"], errors="coerce")
        df["temp_c"] = pd.to_numeric(df["c2"], errors="coerce")
        df["battery_v"] = pd.to_numeric(df["c3"], errors="coerce")
    else:
        # Format 1: date in c0, time in c1, pressure in c2, temp in c3, battery in c4
        df["timestamp"] = [
            _parse_timestamp_strings(d, t) for d, t in zip(df["c0"].astype(str), df["c1"].astype(str))
        ]
        df["pressure_mbar"] = pd.to_numeric(df["c2"], errors="coerce")
        df["temp_c"] = pd.to_numeric(df["c3"], errors="coerce")
        df["battery_v"] = pd.to_numeric(df["c4"], errors="coerce")
    
    print("  Parsing numeric columns...")

    # Keep only rows with valid timestamps and battery voltage
    print("  Filtering valid data...")
    df = df.dropna(subset=["timestamp", "battery_v"]).copy()
    print(f"  After timestamp/voltage filtering: {df.shape}")

    # Filter out obviously bad voltages
    df = df[(df["battery_v"] > 2.0) & (df["battery_v"] < 6.0)].copy()
    # Preserve any sampling factor metadata
    try:
        df.attrs["sampling_factor"] = float(df.attrs.get("sampling_factor", 1.0))
    except Exception:
        pass
    # Preserve original row order to detect backward time jumps later
    df = df.reset_index(drop=True)
    print(f"  Final data shape: {df.shape}")
    
    # Debug output
    if len(df) == 0:
        print(f"Debug: After parsing, no valid rows found. Original shape: {len(pd.read_csv(path, header=None, names=['c0', 'c1', 'c2', 'c3', 'c4'], engine='python', dtype=str, encoding='utf-8'))}")
        print(f"Debug: First few rows after header removal:")
        temp_df = pd.read_csv(path, header=None, names=['c0', 'c1', 'c2', 'c3', 'c4'], engine='python', dtype=str, encoding='utf-8')
        if len(temp_df) > 1:
            temp_df = temp_df.iloc[1:]  # Skip header
        print(temp_df.head())
        print(f"Debug: Sample timestamp parsing attempts:")
        for i in range(min(3, len(temp_df))):
            date_str = str(temp_df.iloc[i]['c0'])
            time_str = str(temp_df.iloc[i]['c1'])
            print(f"  Row {i}: date='{date_str}', time='{time_str}'")
            try:
                ts = _parse_timestamp_strings(date_str, time_str)
                print(f"    -> Parsed as: {ts}")
            except Exception as e:
                print(f"    -> Failed: {e}")
    
    return df


def _select_burst_end_rows(df: pd.DataFrame, gap_seconds: float) -> pd.DataFrame:
    """Return rows that are the final sample within each burst.

    A row is considered a burst end if the time delta to the next row
    is >= gap_seconds, or if it is the last row.
    """
    if df.empty:
        return df
    next_ts = df["timestamp"].shift(-1)
    delta = (next_ts - df["timestamp"]).dt.total_seconds()
    is_end = delta.isna() | (delta >= gap_seconds)
    return df[is_end].reset_index(drop=True)


def _elapsed_with_resets(ts: pd.Series, threshold_seconds: float = 60.0) -> np.ndarray:
    """Compute elapsed seconds from a timestamp series while handling backward jumps.

    When time moves backward by more than threshold_seconds, start a new segment
    and continue accumulating total elapsed time across segments.
    """
    if ts.empty:
        return np.array([], dtype=float)

    base_elapsed = 0.0
    segment_start = ts.iloc[0]
    last_ts = ts.iloc[0]
    out: list[float] = []

    for cur in ts:
        delta = (cur - last_ts).total_seconds()
        if delta < -threshold_seconds:
            base_elapsed += max(0.0, (last_ts - segment_start).total_seconds())
            segment_start = cur
        current_segment_elapsed = max(0.0, (cur - segment_start).total_seconds())
        out.append(base_elapsed + current_segment_elapsed)
        last_ts = cur

    return np.asarray(out, dtype=float)


def _classify_dataset(path: str) -> Tuple[str, str]:
    """Classify dataset mode and assign label based on filename.

    Returns (mode, label) where mode is one of {"burst", "continuous"}.
    """
    name = path.lower()
    if "cons_no_sleep" in name:
        return "continuous", "Old code, 8 Hz"
    if "cons_sleep" in name:
        return "continuous", "New code, 16 Hz"
    if "bat_test" in name and "cons" not in name:
        return "burst", "2 minute burst sampling"
    # Fallback
    return "burst", "Burst dataset"


def _prepare_time_series(
    df: pd.DataFrame,
    mode: str,
    gap_seconds: float,
    to_hours: bool,
) -> Tuple[np.ndarray, np.ndarray]:
    """Produce elapsed-time array and voltage array for a dataset.

    Uses all rows for both burst and continuous datasets.
    Sampling is done earlier in the data processing pipeline.
    """
    print(f"  Preparing time series for {mode} dataset...")
    df_use = df.reset_index(drop=True)
    print(f"  Data shape: {df_use.shape}")

    print("  Computing elapsed time with backward-jump handling...")

    dt_seconds = _elapsed_with_resets(df_use["timestamp"])  # seconds
    t_values = dt_seconds / 3600.0 if to_hours else dt_seconds / 60.0
    v_values = df_use["battery_v"].to_numpy(dtype=float)
    print(f"  Time series prepared: {len(t_values)} points")
    return t_values, v_values


def _compute_avg_hz_series(df: pd.DataFrame, to_hours: bool) -> Tuple[np.ndarray, np.ndarray]:
    """Compute rolling 1-hour average sampling rate (Hz) vs elapsed time.

    If dataset is very large (>100k rows), downsample to 1% before computing.
    Returns arrays aligned to the same time unit as plots (minutes or hours).
    """
    if df.empty:
        return np.array([], dtype=float), np.array([], dtype=float)

    df_use = df.reset_index(drop=True)
    # Start with any pre-applied global sampling factor from the loader
    sampling_factor = float(df.attrs.get("sampling_factor", 1.0))
    if len(df_use) > 100000:
        df_use = df_use.iloc[::100].reset_index(drop=True)
        sampling_factor *= 100.0  # compensate for additional 1% downsampling here

    # Elapsed time (seconds) with backward-jump handling
    elapsed_s = _elapsed_with_resets(df_use["timestamp"])  # seconds
    if len(elapsed_s) == 0:
        return np.array([], dtype=float), np.array([], dtype=float)

    # Instantaneous Hz from positive forward deltas only
    delta_s = np.diff(df_use["timestamp"]).astype("timedelta64[ns]").astype(np.int64) / 1e9
    delta_s = np.where(delta_s > 0, delta_s, np.nan)
    inst_hz = np.empty_like(elapsed_s, dtype=float)
    inst_hz[:] = np.nan
    inst_hz[1:] = (1.0 / delta_s) * sampling_factor

    # Time-based rolling mean over 10 minutes
    t_index = pd.to_timedelta(elapsed_s, unit="s")
    s = pd.Series(inst_hz, index=t_index)
    hz_avg = s.rolling("10min").mean().to_numpy()

    t_values = elapsed_s / 3600.0 if to_hours else elapsed_s / 60.0
    return t_values, hz_avg


# ------------------------------- Curve Model ------------------------------ #

def logistic_discharge(t: np.ndarray, v_min: float, delta_v: float, t0: float, tau: float) -> np.ndarray:
    """Sigmoidal (logistic) model for Li-ion discharge.

    V(t) = v_min + delta_v / (1 + exp((t - t0) / tau))
    - v_min ~ lower asymptote (near empty)
    - delta_v ~ (v_max - v_min), must be positive
    - t0 ~ inflection point (half capacity), in same time units as t
    - tau ~ time scaling (>0)
    """
    return v_min + delta_v / (1.0 + np.exp((t - t0) / np.maximum(tau, 1e-9)))


def estimate_battery_death_time(fit_params: Tuple[float, float, float, float], threshold_voltage: float = 3.0) -> float:
    """Estimate when battery voltage will reach threshold (battery death time).
    
    Solves: threshold_voltage = v_min + delta_v / (1 + exp((t - t0) / tau))
    for t.
    """
    v_min, delta_v, t0, tau = fit_params
    
    if delta_v <= 0 or threshold_voltage <= v_min:
        return float('inf')  # Battery will never reach threshold
    
    # Solve for t when voltage = threshold_voltage
    # threshold_voltage = v_min + delta_v / (1 + exp((t - t0) / tau))
    # (threshold_voltage - v_min) = delta_v / (1 + exp((t - t0) / tau))
    # (threshold_voltage - v_min) * (1 + exp((t - t0) / tau)) = delta_v
    # 1 + exp((t - t0) / tau) = delta_v / (threshold_voltage - v_min)
    # exp((t - t0) / tau) = delta_v / (threshold_voltage - v_min) - 1
    # (t - t0) / tau = ln(delta_v / (threshold_voltage - v_min) - 1)
    # t = t0 + tau * ln(delta_v / (threshold_voltage - v_min) - 1)
    
    ratio = delta_v / (threshold_voltage - v_min)
    if ratio <= 1:
        return float('inf')  # Battery will never reach threshold
    
    t_death = t0 + tau * np.log(ratio - 1)
    return float(t_death)


@dataclass
class FitResult:
    params: Tuple[float, float, float, float]
    r2: float


def fit_logistic(time_values: np.ndarray, voltage_values: np.ndarray) -> FitResult:
    print("    Fitting logistic curve...")
    if len(time_values) < 4:
        print("    Not enough points for reliable fit, using defaults")
        # Not enough points to fit reliably; return simple defaults
        vmin_obs = float(np.nanmin(voltage_values))
        vmax_obs = float(np.nanmax(voltage_values))
        params = (max(3.2, vmin_obs - 0.05), max(0.1, vmax_obs - vmin_obs), time_values.mean() if len(time_values) else 0.0, max(0.05, (time_values.max() - time_values.min()) / 5.0 if len(time_values) else 0.2))
        return FitResult(params=params, r2=float("nan"))

    t = np.asarray(time_values, dtype=float)
    v = np.asarray(voltage_values, dtype=float)

    vmin_obs = float(np.nanmin(v))
    vmax_obs = float(np.nanmax(v))
    dv_obs = max(0.05, vmax_obs - vmin_obs)
    t_span = max(1e-3, float(t.max() - t.min()))

    p0 = (
        np.clip(vmin_obs, 3.2, 4.1),   # v_min initial
        np.clip(dv_obs, 0.1, 1.6),     # delta_v initial
        float(t.min() + 0.5 * t_span), # t0 initial
        max(0.02, 0.2 * t_span),       # tau initial
    )

    lower_bounds = (3.2, 0.05, 0.0, 0.001)
    upper_bounds = (4.2, 2.0, float(t.max() + t_span), float(max(t_span * 5.0, 0.1)))

    try:
        print("    Running curve fitting optimization...")
        popt, _ = curve_fit(
            logistic_discharge,
            t,
            v,
            p0=p0,
            bounds=(lower_bounds, upper_bounds),
            maxfev=20000,
        )
        print("    Curve fitting completed successfully")
    except Exception as e:
        print(f"    Curve fitting failed: {str(e)[:100]}..., using initial parameters")
        # Fall back to initial parameters if fit fails
        popt = p0

    v_pred = logistic_discharge(t, *popt)
    ss_res = float(np.sum((v - v_pred) ** 2))
    ss_tot = float(np.sum((v - np.mean(v)) ** 2))
    r2 = float(1.0 - ss_res / ss_tot) if ss_tot > 0 else float("nan")
    print(f"    Fit R² = {r2:.4f}")
    return FitResult(params=tuple(map(float, popt)), r2=r2)


# --------------------------------- Plotting -------------------------------- #

def plot_multi_series(
    series: List[Tuple[np.ndarray, np.ndarray, Tuple[float, float, float, float], str, str, np.ndarray, np.ndarray]],
    time_unit_label: str,
    savefig: str | None = None,
) -> None:
    """Plot multiple measured series and their logistic fits on one figure.

    Each item in `series` is (t_values, v_values, fit_params, label, mode)
    """
    print("Creating plot...")
    plt.figure(figsize=(11, 6))

    # Color cycle
    colors = plt.rcParams["axes.prop_cycle"].by_key().get("color", ["#1f77b4", "#ff7f0e", "#2ca02c", "#d62728"])  # type: ignore[index]

    # Determine time range for projection
    print("  Determining time range...")
    all_t_max = max([float(np.max(s[0])) for s in series if len(s[0]) > 0], default=0.0)
    
    # Project up to at least 1500 hours (or 90,000 minutes) to show estimates
    if time_unit_label == "hours":
        t_max_plot = max(all_t_max, 800.0)
    else:  # minutes
        t_max_plot = max(all_t_max, 48000.0)
    
    print(f"  Plotting time range: 0 to {t_max_plot:.1f} {time_unit_label}")

    ax = plt.gca()
    ax2 = ax.twinx()

    for idx, (t_values, v_values, fit_params, label, mode, hz_t, hz_avg) in enumerate(series):
        print(f"  Plotting series {idx+1}/{len(series)}: {label}")
        color = colors[idx % len(colors)]
        v_min, delta_v, t0, tau = fit_params
        
        # Calculate battery death time estimates
        t_death_3v = estimate_battery_death_time(fit_params, 3.0)
        t_death_32v = estimate_battery_death_time(fit_params, 3.2)
        
        if np.isfinite(t_death_3v):
            print(f"    Estimated death at 3.0V: {t_death_3v:.1f} {time_unit_label}")
        if np.isfinite(t_death_32v):
            print(f"    Estimated death at 3.2V: {t_death_32v:.1f} {time_unit_label}")
        
        if len(t_values) >= 2:
            # Create extended time grid for projection
            t_data_max = float(np.max(t_values))
            t_grid = np.linspace(float(np.min(t_values)), t_max_plot, 600)
        else:
            # Degenerate case
            t_grid = np.linspace(0.0, t_max_plot, 100)
        v_fit = logistic_discharge(t_grid, v_min, delta_v, t0, tau)

        # Measured voltage
        ax.scatter(t_values, v_values, s=20, color=color, alpha=0.85, label=f"{label} (measured)")

        # Fit curve (show full projection)
        ax.plot(t_grid, v_fit, linestyle="--", linewidth=2.0, color=color, alpha=0.95, label=f"{label} (fit)")
        
        # Add death threshold lines if within plot range
        if np.isfinite(t_death_3v) and t_death_3v <= t_max_plot:
            ax.axvline(x=t_death_3v, color=color, linestyle=':', alpha=0.7, linewidth=1)
            ax.text(t_death_3v, 3.1, f'3.0V\n{t_death_3v:.0f}h', ha='center', va='bottom', fontsize=8, color=color)

        # Average Hz on secondary y-axis
        if len(hz_t) > 1 and np.isfinite(np.nanmean(hz_avg)):
            ax2.plot(hz_t, hz_avg, linewidth=1.2, color=color, alpha=0.6, label=f"{label} (avg Hz, 10min)")

    print("  Adding plot elements...")
    ax.set_xlabel(f"Time since start [{time_unit_label}]")
    ax.set_ylabel("Battery Voltage [V]")
    # Adaptive title based on plotted max range
    title_range = f"0–{int(round(t_max_plot))} {time_unit_label}"
    plt.title(f"Battery Voltage with Sigmoidal Fits and Avg Hz ({title_range})")
    ax.grid(True, alpha=0.3)

    # Requested y-axis range: 3–5 V
    ax.set_ylim(3.0, 5.0)
    ax2.set_ylabel("Avg sampling rate [Hz] (10min rolling)")
    # Build combined legend
    handles1, labels1 = ax.get_legend_handles_labels()
    handles2, labels2 = ax2.get_legend_handles_labels()
    ax.legend(handles1 + handles2, labels1 + labels2, ncol=2)

    if savefig:
        print(f"  Saving plot to {savefig}...")
        plt.tight_layout()
        plt.savefig(savefig, dpi=150, bbox_inches='tight')
        print(f"  Plot saved successfully to {savefig}")
    else:
        print("  No save path specified, plot not saved")
    # Note: plt.show() removed for WSL compatibility


# ----------------------------------- Main ---------------------------------- #

def main() -> None:
    parser = argparse.ArgumentParser(description="Analyze and compare battery voltage over time; fit sigmoidal curves per dataset.")
    parser.add_argument("--csv", help="Single CSV file (backwards-compatible)")
    parser.add_argument("--files", nargs="+", help="One or more CSV files to compare")
    parser.add_argument("--gap-seconds", type=float, default=90.0, help="Gap threshold (s) to detect burst boundaries [default: 90]")
    parser.add_argument("--time-unit", choices=["minutes", "hours"], default="hours", help="X-axis time unit [default: hours]")
    parser.add_argument("--savefig", default=None, help="Optional path to save the figure (PNG)")

    args = parser.parse_args()

    file_list: List[str] = []
    if args.files:
        file_list = list(args.files)
    elif args.csv:
        file_list = [args.csv]
    else:
        raise SystemExit("Provide --files <paths...> or --csv <path>.")

    print(f"Processing {len(file_list)} file(s): {file_list}")
    to_hours = args.time_unit == "hours"
    time_label = "hours" if to_hours else "minutes"

    series: List[Tuple[np.ndarray, np.ndarray, Tuple[float, float, float, float], str, str, np.ndarray, np.ndarray]] = []

    for i, path in enumerate(file_list):
        print(f"\n=== Processing file {i+1}/{len(file_list)}: {path} ===")
        df = _robust_read(path)
        if df.empty:
            print(f"Skipping {path}: no valid data parsed.")
            continue

        mode, label = _classify_dataset(path)
        print(f"Dataset classified as: {mode} - {label}")
        t_values, v_values = _prepare_time_series(df, mode=mode, gap_seconds=args.gap_seconds, to_hours=to_hours)
        hz_t, hz_avg = _compute_avg_hz_series(df, to_hours=to_hours)

        if len(t_values) == 0 or len(v_values) == 0:
            print(f"Skipping {path}: no usable time/voltage values.")
            continue

        fit = fit_logistic(t_values, v_values)
        print(f"Final result: {label} -> Fit (v_min, delta_v, t0, tau): {fit.params}; R^2 = {fit.r2 if np.isfinite(fit.r2) else float('nan')}")
        series.append((t_values, v_values, fit.params, label, mode, hz_t, hz_avg))

    if not series:
        raise SystemExit("No datasets to plot.")

    print(f"\n=== Creating comparison plot with {len(series)} dataset(s) ===")
    plot_multi_series(series, time_label, savefig=args.savefig)
    print("\nAnalysis complete!")


if __name__ == "__main__":
    main()
