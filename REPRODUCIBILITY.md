# Reproducibility Notes

This document specifies the software, hardware, and data conditions under which the results in the paper were produced, and what to expect when replicating.

## 1. Software stack

| Component | Version used |
|---|---|
| GDAL (raster runtime) | **2.2.4** (legacy OSGeo4W64 stack) |
| Compiler (pixel functions, benchmark binary) | MSVC `cl.exe` from Visual Studio 2022 C++ Build Tools, `/O2` optimisation |
| Python (analysis scripts) | 3.8+ |
| Python packages | `scipy`, `numpy`, `pandas`, `matplotlib` |
| OS | Windows 11 Pro, build 26200 |

The choice of GDAL 2.2.4 reflects the long-lived OSGeo4W64 stack still deployed in production environments. Newer GDAL releases (3.x) introduced VRT multithreading and other driver-side optimisations that may shift the reported runtime margins. Replicating the protocol under GDAL 3.x is identified as follow-up work in §7.4 of the paper.

## 2. Hardware

| Component | Spec |
|---|---|
| CPU | 12th Gen Intel Core i9-12900H |
| RAM | 34.0 GB |
| Storage | NVMe Samsung SSD 990 PRO 4TB |

The reported `/vsimem/`-vs-on-disk-GeoTIFF result (in-memory 31–50% slower) is specific to NVMe storage. The relative ordering of the three modes may shift on slower storage media (HDD, NAS). Cold-cache replication is also identified as follow-up.

## 3. Datasets

The published experiments used:

- Matched DEM/DSM map-sheet products organised through VRT mosaics at three elevation scales (single-sheet `2697 × 1957`, `2 × 2` mosaic `5299 × 3816`, `2 × 3` mosaic `7891 × 3835`); single-band `Float32`, `10 m` pixel size, `NoData = -9999`.
- A JiLin-1 panchromatic image (satellite `JL1KF02B02`, sensor `PMS09`, product level `L1`) acquired 2025-03-27 at `0.5 m` GSD, `30839 × 30948` pixels, single-band `UInt16`, 14-bit radiometric depth, scene-centred at approximately 30.146° N / 103.984° E.

These datasets cannot be redistributed because they are derived from restricted production data. To replicate the benchmark you will need to supply equivalent local rasters and update the `path` fields in `benchmark/configs/*.json`.

## 4. Configuration entry points

`benchmark/configs/*.json` use two environment-variable placeholders that you must replace with paths on your machine:

- `${INPUT_VRT_DIR}` — directory containing your DEM/DSM `.vrt` mosaics (`dem_small.vrt`, `dem_medium.vrt`, `dsm_small.vrt`, `dsm_medium.vrt`).
- `${TEST_DATA_DIR}/optical_large.tif` — path to your large single-band optical raster.

`output_root` is by default `./results/<case>` relative to the directory you run the benchmark from.

## 5. Repetition protocol

Each case is repeated:

- **10 times** for DEM-scale mechanism cases and the two production DEM/DSM chains
- **5 times** for the optical mechanism case (`mech_light_optical_large`) and the optical production chain (`prod_light_optical`) — driven by data-volume and runtime cost

Within each repetition, the execution order of the three modes (VRT, GeoTIFF, `/vsimem/`) is randomised through a seeded Mersenne Twister (`seed = 42`).

## 6. Expected outputs

After successfully running both configs you should obtain CSV files matching the schema and approximate magnitudes shown in `results/r1_mechanism_cpp/` and `results/r1_production_cpp/`. The exact runtimes will differ — what should reproduce qualitatively is:

- VRT intermediate sizes in the **KB** range vs GeoTIFF intermediates in the **MB to GB** range (storage reduction > 99.99% in every case).
- VRT total runtime **shorter than** GeoTIFF total runtime in every repetition (Wilcoxon W reaches the maximum for every case, paper Table 5).
- `/vsimem/` total runtime **slower than** on-disk GeoTIFF on NVMe storage (the counter-intuitive finding discussed in §6.3 and §7.1).

`benchmark/analysis/wilcoxon_paired.py` reproduces the statistical analysis of the published runtimes; running it on your own re-collected `case_summary.csv` should yield comparable W and p values if the qualitative ordering above is preserved.

## 7. Out of scope for this package

- The full visual workflow application (wfeditor) the paper describes is not part of this repository. Only the pixel-function library it calls (`pixel_functions/`) and the standalone benchmark are released here.
- Cold-cache experiments and ports to GDAL 3.x, HDD/NAS storage, or compressed-GeoTIFF intermediates are explicitly identified as follow-up work in §7.4 and §7.7 of the paper.
