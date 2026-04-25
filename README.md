# VRT Pixel Function Benchmark

Reproducibility package for the paper:

> **A Lightweight Chained Raster Computation Framework Based on GDAL VRT Pixel Functions for Geospatial Workflows**
> Yuan Long, *ISPRS International Journal of Geo-Information*, 2026 (under review).

## What this repository contains

This is the **research and reproducibility package** for the IJGI submission. It does **not** include the full visual workflow application (wfeditor) the paper describes; only the components required to reproduce the experimental results and to inspect the pixel-function implementations.

| Path | Content |
|---|---|
| `manuscript/` | Markdown source of the manuscript (figures inlined as references) |
| `pixel_functions/` | C++ pixel-function library (`pixelDecimalFunction`, `pixelDSMDEMDiffProcessFunction`, `pixelTerrainRoughness5x5Function`, `pixelSlopFunction_S2`) as deployed in the production system, registered through `GDALAddDerivedBandPixelFunc()` |
| `benchmark/` | Three-mode benchmark runner (`benchmark_cpp.cpp`), build script, JSON configs, and the Wilcoxon paired-test script |
| `results/` | Aggregated case summaries, per-run benchmark records, and timing-decomposition tables for the seven mechanism cases and three production-like chains reported in the paper |
| `figures/` | Figure-generation scripts (matplotlib) |

## Quick start

### Reproduce statistical results (no GDAL required)

```bash
cd benchmark/analysis
python wilcoxon_paired.py
```

Reads `../../results/r1_mechanism_cpp/case_summary.csv` and `../../results/r1_production_cpp/case_summary.csv`, prints the ten Wilcoxon paired-test outcomes (W, p-value, effect size r) reported in Table 5 of the paper.

Requires Python 3.8+, scipy, numpy, pandas.

### Reproduce the full benchmark (GDAL 2.2.4 required)

The benchmark targets the legacy GDAL 2.2.4 runtime to match the source production system. Newer GDAL versions (3.x) introduced VRT multithreading and other VRT-side optimisations that may shift the reported runtime margins; replicating under GDAL 3.x is suggested follow-up work.

```bash
# Set GDAL location
set GDAL_ROOT=C:\OSGeo4W64

# Build
cd benchmark
build.bat

# Configure your local DEM/DSM and optical input
#   - Edit configs/r1_mechanism_cpp.json
#   - Replace ${INPUT_VRT_DIR} with your local path to .vrt mosaics
#   - Replace ${TEST_DATA_DIR}/optical_large.tif with your large optical raster

# Run
set PATH=%GDAL_ROOT%\bin;%PATH%
benchmark_cpp.exe configs\r1_mechanism_cpp.json
benchmark_cpp.exe configs\r1_production_cpp.json
```

The source raster datasets used in the published experiments (matched DEM/DSM map-sheet products and a 1.9 GB JiLin-1 panchromatic scene) cannot be redistributed because they were derived from restricted production data; you will need to supply equivalent inputs locally.

### Generate Figure 5

```bash
cd figures/runner_cpp
python gen_fig5.py
```

## Citation

If you use this code or the reported results, please cite:

```bibtex
@article{Long2026VRT,
  title   = {A Lightweight Chained Raster Computation Framework Based on GDAL VRT Pixel Functions for Geospatial Workflows},
  author  = {Long, Yuan},
  journal = {ISPRS International Journal of Geo-Information},
  year    = {2026},
  note    = {Under review}
}
```

## License

MIT — see [LICENSE](LICENSE). Note that GDAL itself is X/MIT-licensed; nlohmann/json (`benchmark/json.hpp`) is MIT.

## Contact

Yuan Long — y15496439@gmail.com — Sichuan Surveying and Mapping Technology Service Center Co., Ltd.
