# Benchmark Configurations

Two published configurations and one template. Each config fully describes a benchmark run: input datasets, repetitions, three-mode comparison protocol, output target.

| File | Purpose |
|---|---|
| `r1_mechanism_cpp.json` | Seven mechanism-oriented two-step cases (light × 3, medium × 2, heavy × 2) — paper §6.3, Tables 2–3, 5 |
| `r1_production_cpp.json` | Three production-like three-step chains — paper §6.4, Tables 4–5 |

## Editing for a local replication

Before running, replace the two placeholders in each JSON:

```text
${INPUT_VRT_DIR}            -> local directory holding your DEM/DSM .vrt mosaics
${TEST_DATA_DIR}/optical_large.tif  -> path to your large single-band optical raster
```

For example:

```json
"dem_small": {
  "path": "C:/data/raster/dem_small.vrt",
  "nodata": -9999
},
```

`output_root` defaults to `./results/<case>` relative to the working directory of `benchmark_cpp.exe`. Override per run if you want a custom location.

## Reproducing the published numbers

The exact rasters used in the paper (matched DEM/DSM map-sheet products and a JiLin-1 panchromatic scene) are restricted production data and cannot be redistributed. Provide your own equivalent inputs and expect:

- Storage reduction > 99.99% in every case (structural to VRT, independent of input choice)
- VRT total time < GeoTIFF total time for every repetition (qualitative ordering should reproduce)
- `/vsimem/` total time > GeoTIFF total time on NVMe storage (the §6.3 finding)

Absolute runtimes will differ depending on your hardware, storage, and dataset.
