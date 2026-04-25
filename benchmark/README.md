# Benchmark Runner

Three-mode raster-workflow benchmark used to produce the runtime, storage, and timing-decomposition tables in §6 of the paper.

## Files

| File | Purpose |
|---|---|
| `benchmark_cpp.cpp` | Single-file C++ benchmark runner. Loads a JSON config, registers the four pixel functions, executes each case in three modes (VRT, on-disk GeoTIFF, in-memory `/vsimem/`) for the configured number of repetitions, and writes per-run + aggregated CSVs |
| `json.hpp` | nlohmann/json single-header JSON parser (MIT) |
| `build.bat` | Windows build script — requires `GDAL_ROOT` environment variable pointing to a GDAL 2.2.4 install (e.g. `C:\OSGeo4W64`) |
| `configs/` | Two published configurations (`r1_mechanism_cpp.json`, `r1_production_cpp.json`) plus a small template |
| `analysis/wilcoxon_paired.py` | Wilcoxon paired-test reproduction of paper Table 5 |

## Build

```bash
set GDAL_ROOT=C:\OSGeo4W64        # or your GDAL 2.2.4 install root
build.bat
```

Produces `benchmark_cpp.exe` in the current directory.

## Configure

Each config under `configs/` uses two placeholders:

- `${INPUT_VRT_DIR}` — directory containing your local DEM/DSM `.vrt` mosaics
- `${TEST_DATA_DIR}/optical_large.tif` — your large single-band optical raster

Replace these with paths on your machine before running. (No automatic substitution; edit the JSON files in place.)

`output_root` defaults to `./results/<case>` relative to the working directory.

## Run

```bash
set PATH=%GDAL_ROOT%\bin;%PATH%
benchmark_cpp.exe configs\r1_mechanism_cpp.json
benchmark_cpp.exe configs\r1_production_cpp.json
```

Outputs per case_id three CSVs (`case_summary.csv`, `raw_runs.csv`, `timing_decomposition.csv`) plus a JSON summary. Compare against `../results/` for the published reference values.

## Statistical reproduction (no GDAL required)

```bash
cd analysis
python wilcoxon_paired.py
```

Reads `../../results/r1_mechanism_cpp/case_summary.csv` and `../../results/r1_production_cpp/case_summary.csv`, prints W, exact one-sided p, and effect size r for the ten cases in Table 5. Requires Python 3.8+, scipy, numpy, pandas.
