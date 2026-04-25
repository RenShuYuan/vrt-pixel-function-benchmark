# Pixel Functions

This directory contains the four GDAL-VRT pixel functions evaluated in the paper, in the form deployed in the production wfeditor system. Both files are kept in their as-deployed state — they are not extracted or refactored — so that the runtime characteristics reported in the paper can be reproduced against the exact same code path.

| Function | Workload tier | Purpose |
|---|---|---|
| `pixelDecimalFunction` | light | per-pixel decimal-precision adjustment / value rounding |
| `pixelDSMDEMDiffProcessFunction` | medium | two-source DSM−DEM difference processing with NoData handling, threshold logic, and conditional replacement |
| `pixelSlopFunction_S2` | heavy | local slope derivation |
| `pixelTerrainRoughness5x5Function` | heavy | 5×5 neighbourhood-based local terrain roughness (standard deviation of valid elevations within a 5×5 window) |

All four are registered through `GDALAddDerivedBandPixelFunc()` and invoked by GDAL when a `VRTDerivedRasterBand` whose `<PixelFunctionType>` matches the registered name is read.

## Integration into a separate codebase

`ipfgdalprogresstools.{h,cpp}` were factored as part of the wfeditor application and reference internal headers (`commonutils.h` and other ipf-prefixed utility headers). To use these pixel functions in a different host application you have three options:

1. **Vendor the pair as-is.** Drop both files and the small set of internal headers they use into your project, expose only the four `pixel*Function` entry points, and call `GDALAddDerivedBandPixelFunc` for each at startup.
2. **Re-implement against the GDAL signature.** The four functions follow GDAL's `GDALDerivedPixelFunc` (or `GDALDerivedPixelFuncWithArgs` in GDAL 3.x) signature; the algorithms are described in the paper §4 and §5 and are straightforward to port without taking the entire ipf utility layer.
3. **Build the benchmark only.** The `benchmark/benchmark_cpp.cpp` runner contains its own minimal pixel-function registrations sufficient to reproduce the experimental results, and does not link against this directory.

The "as deployed" form is included here primarily so that reviewers and follow-up researchers can audit the exact production code path that produced the paper's measurements.
