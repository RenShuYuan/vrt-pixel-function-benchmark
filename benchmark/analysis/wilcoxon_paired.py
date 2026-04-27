"""
Wilcoxon signed-rank test: VRT vs GeoTIFF runtime (paired by repetition).

Reads raw_runs.csv from both mechanism and production experiment directories,
runs a one-sided Wilcoxon signed-rank test per case (H1: VRT < GeoTIFF),
and prints a manuscript-ready summary table.
"""

import pathlib
import csv
import math
from scipy import stats

# Auto-detect the results directory across three plausible layouts:
#   1. zip distribution   : script and r1_*_cpp/ as siblings
#   2. original experiments dir: experiments/analysis/script.py + experiments/results/
#   3. GitHub companion repo  : benchmark/analysis/script.py + <root>/results/
HERE = pathlib.Path(__file__).resolve().parent
_CANDIDATES = [
    HERE,                                       # zip layout (CSVs as siblings)
    HERE.parent / "results",                    # experiments/<here>/.. + results/
    HERE.parent.parent / "results",             # repo: benchmark/analysis/.. + results/
    HERE.parent.parent.parent / "results",      # any 3-deep layout
]
BASE = next(
    (c for c in _CANDIDATES if (c / "r1_mechanism_cpp" / "raw_runs.csv").is_file()),
    None,
)
if BASE is None:
    raise FileNotFoundError(
        "Could not locate r1_mechanism_cpp/raw_runs.csv. Tried:\n  "
        + "\n  ".join(str(c) for c in _CANDIDATES)
    )

MECH = BASE / "r1_mechanism_cpp" / "raw_runs.csv"
PROD = BASE / "r1_production_cpp" / "raw_runs.csv"


def load_paired(csv_path):
    """Return dict: case_id -> {rep -> {mode -> elapsed_ms}}."""
    data = {}
    with open(csv_path, newline="") as f:
        for row in csv.DictReader(f):
            if row["success"] != "True":
                continue
            cid = row["case_id"]
            rep = int(row["repetition"])
            mode = row["mode"]
            ms = float(row["total_elapsed_ms"])
            data.setdefault(cid, {}).setdefault(rep, {})[mode] = ms
    return data


def wilcoxon_vrt_vs_geotiff(case_data):
    """Paired Wilcoxon test: H1 VRT < GeoTIFF (one-sided alternative='less')."""
    pairs = [(v["vrt"], v["geotiff"])
             for v in case_data.values()
             if "vrt" in v and "geotiff" in v]
    if len(pairs) < 5:
        return None
    vrt_times = [p[0] for p in pairs]
    gt_times  = [p[1] for p in pairs]
    diffs = [g - v for v, g in zip(vrt_times, gt_times)]  # positive = VRT wins
    stat, pval = stats.wilcoxon(diffs, alternative="greater")
    n = len(diffs)
    # Effect size r = Z / sqrt(N); approximate Z from normal distribution
    z = stats.norm.ppf(1 - pval)
    r = z / math.sqrt(n)
    mean_red = 100.0 * (sum(gt_times) - sum(vrt_times)) / sum(gt_times)
    return {
        "n": n,
        "mean_vrt_ms": sum(vrt_times) / n,
        "mean_gt_ms":  sum(gt_times) / n,
        "mean_red_pct": mean_red,
        "W": stat,
        "p": pval,
        "r": r,
    }


def main():
    all_data = {}
    for path in (MECH, PROD):
        for cid, cdata in load_paired(path).items():
            all_data[cid] = cdata

    print(f"{'Case':<32} {'N':>3} {'VRT(ms)':>9} {'GeoT(ms)':>9} "
          f"{'Red%':>6} {'W':>8} {'p':>8} {'r':>6} {'sig':>5}")
    print("-" * 95)

    order = [
        "mech_light_small", "mech_light_medium", "mech_light_optical_large",
        "mech_medium_small", "mech_medium_medium",
        "mech_heavy_small", "mech_heavy_medium",
        "prod_light_optical", "prod_medium_chain", "prod_heavy_chain",
    ]

    for cid in order:
        if cid not in all_data:
            continue
        res = wilcoxon_vrt_vs_geotiff(all_data[cid])
        if res is None:
            print(f"{cid:<32}  insufficient data")
            continue
        sig = "***" if res["p"] < 0.001 else ("**" if res["p"] < 0.01 else
              ("*" if res["p"] < 0.05 else "ns"))
        print(f"{cid:<32} {res['n']:>3} {res['mean_vrt_ms']:>9.1f} "
              f"{res['mean_gt_ms']:>9.1f} {res['mean_red_pct']:>6.1f} "
              f"{res['W']:>8.1f} {res['p']:>8.4f} {res['r']:>6.3f} {sig:>5}")

    print()
    print("One-sided Wilcoxon signed-rank test: H1 VRT < GeoTIFF (alternative='greater' on diffs=GT-VRT)")
    print("Significance: *** p<0.001  ** p<0.01  * p<0.05  ns p>=0.05")
    print("Effect size r = Z / sqrt(N);  r > 0.5 is large by Cohen's convention")


if __name__ == "__main__":
    main()
