# Published Results

CSV outputs from the benchmark runs reported in the paper. These are the reference data for the tables and statistical analysis in §6 and §7.

## Layout

```
results/
├── r1_mechanism_cpp/        Seven mechanism-oriented cases (paper §6.3, Tables 2-3, 5)
│   ├── case_summary.csv         Aggregated VRT/GeoTIFF/vsimem timings + storage figures
│   ├── raw_runs.csv             Per-run records (10 reps × 7 cases × 3 modes; 5 reps for the optical case)
│   └── timing_decomposition.csv  Per-step breakdown (materialisation pct, I/O overhead pct, VRT final-materialise time)
└── r1_production_cpp/       Three production-like three-step chains (paper §6.4, Tables 4-5)
    ├── case_summary.csv
    ├── raw_runs.csv
    └── timing_decomposition.csv
```

## Field reference

`case_summary.csv` (both subsets):

```
case_id, workload, repetitions,
vrt_avg_total_elapsed_ms, vrt_std_total_elapsed_ms,
geotiff_avg_total_elapsed_ms, geotiff_std_total_elapsed_ms,
vsimem_avg_total_elapsed_ms, vsimem_std_total_elapsed_ms,
vrt_avg_intermediate_bytes, geotiff_avg_intermediate_bytes,
checksum_vrt_geotiff, checksum_geotiff_vsimem,
intermediate_byte_reduction_pct, runtime_reduction_pct,
io_overhead_pct, vrt_io_avoidance_pct
```

`raw_runs.csv`: per-run records keyed by `(case_id, mode, repetition)`.

`timing_decomposition.csv`: per-case `(materialize_pct, io_overhead_pct, vrt_final_materialize_ms, ...)` used for the §6.5 conclusion that materialisation accounts for 75–89% of GeoTIFF-mode time.

## Reproducing tables and statistics

```bash
cd ../benchmark/analysis
python wilcoxon_paired.py
```

Reads both `case_summary.csv` files in this directory, runs the one-sided Wilcoxon signed-rank test (H₁: VRT runtime < GeoTIFF runtime), reproduces paper Table 5 (W, exact one-sided p, effect size r).
