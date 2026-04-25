# A Lightweight Chained Raster Computation Framework Based on GDAL VRT Pixel Functions for Geospatial Workflows

**Yuan Long** <sup>1,</sup>\* and **[Co-author Full Name]** <sup>2</sup>

<sup>1</sup> Sichuan Surveying and Mapping Technology Service Center Co., Ltd., Sichuan, China; y15496439@gmail.com  
<sup>2</sup> [Co-author's Institution, City, Country]; [co-author-email@domain.com]

\* Correspondence: y15496439@gmail.com

## Abstract

Multi-step raster workflows in surveying and remote sensing production often materialize a full raster product after every processing step; in production-scale chains observed here, individual intermediates reached 42 MB to 3.8 GB and required operator-side cleanup between batch runs. This paper presents a chained raster computation framework in which GDAL VRT pixel functions act as executable intermediate representations: selected pixel-level operators are chained inside workflow nodes through VRT-derived bands, deferring materialization until a terminal output stage. The framework is integrated into a visual geospatial workflow system and evaluated under the legacy GDAL 2.2.4 runtime on real DEM/DSM map-sheet mosaics and a 1.9 GB JiLin-1 panchromatic scene. A three-mode comparison (VRT, on-disk GeoTIFF, in-memory `/vsimem/`) with repeated measurements and per-step timing decomposition isolates materialization cost from computation. Across ten cases, VRT replaced GeoTIFF intermediates with KB-sized XML descriptors (storage reduction 99.995–100.000%) and reduced runtime by 7.4–19.2%. The `/vsimem/` baseline was 31–50% slower than on-disk GeoTIFF on NVMe SSD, and timing decomposition shows materialization accounts for 75–89% of GeoTIFF mode time, indicating the gain comes from avoiding repeated encode-decode cycles rather than disk I/O. Results are scope-bound to GDAL 2.2.4 + NVMe + uncompressed GeoTIFF; cold-cache, GDAL 3.x, branching topology, and storage-medium variations are identified as follow-up.

**Keywords:** VRT pixel function; GDAL; raster processing; geospatial workflow; workflow orchestration; chained computation; lazy evaluation; executable intermediate representation; geoinformation

## 1. Introduction

Geoinformation workflows in surveying and remote sensing production rarely consist of a single isolated operation. In practical workflows, clipping, resampling, value transformation, quality inspection, thematic extraction, and output formatting are often chained into multi-step processing procedures [1, 2]. A common engineering strategy is to materialize a raster file after each step. Although this approach is straightforward and robust, it also results in repeated disk writes, increased storage occupation, and weaker portability of workflow configurations [3, 4].

The concept of deferring computation until results are actually needed — often called lazy evaluation in computer science [5] — has been applied in several geospatial contexts. Google Earth Engine, for instance, employs server-side lazy evaluation to avoid unnecessary data transfer and intermediate storage in planetary-scale analysis [6]. The Dask library applies a similar principle by building task graphs that execute only upon explicit materialization [7]. In the GDAL ecosystem, the Virtual Raster (VRT) mechanism provides a lightweight raster representation that can reference source datasets without materializing them. Beyond its common use for mosaicking and reprojection, VRT supports derived raster bands and custom pixel functions [8], making it capable of serving as an executable intermediate computation layer. However, this capability has seen limited adoption in workflow-oriented raster systems, where many chains continue to rely on physical intermediate files even when selected operators could be represented virtually.

This work examines a visual geospatial processing system developed for surveying and remote sensing production. Based on the existing codebase, a lightweight chained raster computation framework is described and organized, in which GDAL VRT pixel functions serve as computation entries for selected raster-processing nodes. The framework is embedded in a node-based workflow system so that intermediate computation can be represented by VRT-derived raster bands and scheduled through a unified workflow mechanism.

The paper focuses on mechanism-and-system design rather than novel algorithm development. Selected raster operators are transformed into reusable executable intermediates inside a workflow engine, and the engineering effects of this design are evaluated on real data. Specifically, the paper contributes: (1) a lightweight chained raster computation mechanism that applies VRT pixel functions as executable intermediate representations within a visual workflow system, extending VRT's role beyond static raster description; (2) integration of this mechanism into a visual geospatial workflow system with reusable nodes and model persistence; and (3) an evaluation protocol combining mechanism experiments and production-like workflow experiments, with per-step timing decomposition to separate I/O overhead from computation cost.

## 2. Related Work and Research Positioning

This work builds directly on prior geoinformation workflow and geospatial processing research. Among the most closely related contributions are Graser and Olaya [10], who presented QGIS Processing as a framework for integrating geoprocessing tools across heterogeneous backends and explicitly identified the management of temporary intermediate files as an open issue, and Pakdil and Celik [13], who proposed a serverless geospatial workflow execution system that addresses statelessness through external object storage — both published in *ISPRS International Journal of Geo-Information*. The present study takes the opposite direction from Pakdil and Celik's solution: rather than externalizing intermediate storage, it eliminates intermediate materialization where the operator can be expressed as a VRT pixel function, providing a complementary approach to the open issue identified by Graser and Olaya.

### 2.1 Workflow-oriented geospatial processing systems

Workflow-oriented systems improve modularity, reusability, and automation in data-intensive analysis. Kepler [1], Taverna [9], QGIS Processing [10], ArcGIS ModelBuilder [11], and the OTB pipeline [12] all show that visual chaining and execution management are integral parts of practical analytical systems rather than peripheral interface features.

Beyond desktop tools, geospatial workflows have also been studied in constrained, distributed, and service-oriented settings. Zhang and Xu [2], Pakdil and Celik [13], and workflow-as-a-service efforts [14] further support the view that workflow representation and execution strategy are valid research objects in geospatial computing. Recent KNIME-based scientific workflow systems extend this direction with metanode-style abstractions that improve transparency in multi-step pipelines [24]; their architectural concern complements but does not address the intermediate-storage question explored here.

### 2.2 Lazy evaluation and deferred computation in geospatial processing

Deferred evaluation has been adopted at several system levels. Google Earth Engine [6] materializes computation only when results are requested, while Dask/xarray and related Python tools [7, 15] represent raster operations as lazy task graphs. Rasterio [16] and ODC [17] support efficient access and lazy loading, but they do not provide the same GDAL-driver-level chaining mechanism considered here.

Within GDAL, VRT derived raster bands [8] provide demand-driven pixel computation triggered by downstream reads. The relevance of this mechanism is that it can be inserted into legacy GDAL-based software without migrating to a cloud platform or a Python-centric processing stack.

Among raster-specific benchmarking work, Haynes et al. [25] developed a comparative benchmark for big-data raster platforms (GeoTrellis, SciDB, RasterFrames) that explicitly contrasts lazy with eager execution and uses a forced-materialization terminal step to make the two modes directly comparable. The same methodological principle is applied here: the in-process `GDALTranslate()` step that terminates the VRT chain serves as the equivalent forced-materialization point, allowing VRT (lazy), GeoTIFF (eager), and `/vsimem/` (in-memory eager) to be compared on the same total-runtime metric.

### 2.3 Intermediate representation strategies for raster processing

In multi-step raster processing, intermediate results are commonly stored in three ways: on-disk materialization, in-memory materialization, and streaming or pipeline evaluation. On-disk GeoTIFF outputs are simple and robust but increase I/O overhead when used at every intermediate step [3]; workflow systems with provenance requirements [26] further compound this overhead by retaining intermediate files for reproducibility and recovery, multiplying the cumulative storage burden across batch runs. In-memory approaches such as `/vsimem/` avoid physical writes but still allocate raster arrays in RAM [8]. Streaming approaches such as OTB pipelines and GDAL VRT defer evaluation until a final consumer reads the data [12, 18].

The VRT pixel-function approach used here belongs to the third category. Its main advantage is that intermediate raster values are computed on demand instead of being allocated and written as full datasets — particularly useful when intermediate products are large and persistent writes are undesirable.

### 2.4 Raster benchmarking methodology

Benchmarking raster processing performance requires attention to storage overhead, run-to-run variability, and numerical consistency [19]. General benchmarking methodology also recommends repeated measurements and statistical reporting when runtime behavior is affected by non-deterministic system factors [20]. Accordingly, this study uses repeated measurements, reports standard deviations, and complements runtime comparison with per-step timing decomposition so that materialization cost can be separated from computation cost.

### 2.5 Production-oriented raster platforms

Production-oriented raster platforms have long emphasized scalability, engineering integration, and management of large raster collections [21, 22]. Cloud-based geospatial data processing has been surveyed in [23], including web service and cloud-native methods that situate the present work within the broader landscape of geospatial processing infrastructure.

## 3. System Architecture

### 3.1 Overall architecture

![Figure 1. Overall architecture of the proposed workflow-oriented geospatial processing system.](figures/01_system_architecture.png)

The overall architecture is illustrated in Figure 1. The system can be abstracted into five layers: the user interface layer, the visual workflow modeling layer, the workflow scheduling layer, the algorithm node layer, and the foundational capability layer. The user interface is implemented with Qt and provides the main window, toolbars, parameter dialogs, and workflow canvas. The visual workflow layer is built on the Graphics View framework and supports node dragging, branch creation, connection management, and model loading and saving.

At the scheduling layer, a workflow manager maintains the main line and branch structures of the processing flow. Each node is encapsulated as a processing object with unified input, output, parameter, and execution interfaces. The foundational capability layer relies on GDAL, OGR, QGIS, PROJ, Clipper, OpenMP, and ActiveQt to support raster computation, vector processing, projection transformation, geometry operations, parallel execution, and document generation.

### 3.2 Workflow node abstraction and scheduling

![Figure 2. filesOut-filesIn based workflow execution and branch scheduling mechanism.](figures/02_workflow_execution.png)

The workflow execution and branch scheduling mechanism is shown in Figure 2. The workflow engine organizes modules through a unified node abstraction. Each node exposes parameter configuration interfaces and exchanges data with neighboring nodes through input and output file lists. During execution, the output file list of the previous node is passed to the next node as its input. This design forms a simple but effective chained processing model and reduces coupling among heterogeneous raster processing modules.

In the conventional GeoTIFF chain, the output file path produced by step *n* becomes the input file path of step *n+1*; intermediate raster data is therefore exchanged through the on-disk filesystem and is fully decoded and re-encoded between every pair of adjacent nodes. In the VRT chain, the same `filesOut`–`filesIn` interface is preserved, but the path passed downstream points to a VRT descriptor whose dependency graph references the previous source dataset directly; data is exchanged in-process at the GDAL driver level and only crosses the encode-decode boundary at the terminal `GDALTranslate()` materialization step.

### 3.3 Workflow persistence and reuse

To support repeatable processing, the workflow model can be serialized into XML. Node types, positions, identifiers, and parameter settings are stored in the model file and can be reconstructed later. This capability is valuable for surveying production, where similar workflows are repeatedly used across different datasets and projects.

## 4. Lightweight Chained Raster Computation Framework

### 4.1 Pixel functions as executable intermediate representations

![Figure 3. VRT derived-band computation mechanism driven by registered pixel functions.](figures/03_vrt_pixel_function_mechanism.png)

The VRT derived-band computation mechanism is shown in Figure 3. In the proposed framework, selected raster processing logic is implemented as pixel functions and dynamically associated with VRT-derived raster bands. This design allows intermediate processing steps to be represented by lightweight virtual datasets rather than always being materialized as full raster products. As a result, the framework can reduce the burden of repeated intermediate file generation in chained workflows.

The key distinction from a conventional VRT used for mosaicking or format conversion is that the derived raster band contains executable logic. When a downstream consumer (the next workflow node or the final output stage) reads the VRT, GDAL invokes the registered pixel function to compute values on demand. The following XML fragment illustrates the structure of a VRT-derived band with an embedded Python pixel function for decimal adjustment:

```xml
<VRTDataset rasterXSize="2697" rasterYSize="1957">
  <VRTRasterBand dataType="Float32" band="1"
                 subClass="VRTDerivedRasterBand">
    <PixelFunctionLanguage>Python</PixelFunctionLanguage>
    <PixelFunctionType>ipf_decimal</PixelFunctionType>
    <PixelFunctionCode><![CDATA[
def ipf_decimal(in_ar, out_ar, xoff, yoff,
                xsize, ysize, rasterXSize, rasterYSize,
                buf_radius, gt, **kwargs):
    src = in_ar[0]
    out_ar[:] = src
    mask = src != 0
    out_ar[mask] = numpy.trunc(src[mask] * 100.0) / 100.0
    ]]></PixelFunctionCode>
    <SimpleSource>
      <SourceFilename relativeToVRT="0">
        /path/to/input_dem.vrt
      </SourceFilename>
      <SourceBand>1</SourceBand>
      ...
    </SimpleSource>
  </VRTRasterBand>
</VRTDataset>
```

In this structure, `PixelFunctionCode` contains the Python implementation, `PixelFunctionType` provides the function identifier, and `SimpleSource` links the input raster. GDAL evaluates the function block-by-block at read time, so no full intermediate array is allocated. This example uses the Python pixel-function interface for readability; the benchmark and production system implement equivalent logic as compiled C++ pixel functions registered via `GDALAddDerivedBandPixelFunc()`, as described in Section 6.1.

### 4.2 Dynamic registration and VRT band construction

The implementation relies on explicit registration of custom pixel functions in the GDAL-based processing utility layer. Functions such as pixel decimal adjustment, DSM/DEM difference processing, slope-related computation, and local terrain roughness computation are registered through the derived band function registration interface. After registration, workflow methods create VRT-derived raster bands, set the target `PixelFunctionType`, and attach source bands through `VRTAddSimpleSource`.

The construction process for a two-step VRT chain can be summarized in the following pseudocode:

```
function buildVRTChain(inputPath, operations):
    currentSource = inputPath
    for each op in operations:
        vrt = createVRTDataset(dimensions from currentSource)
        band = addDerivedRasterBand(vrt, dataType, subClass="VRTDerived")
        setPixelFunction(band, op.functionName, op.code)
        addSimpleSource(band, currentSource)
        if op.requiresBufferRadius:
            setBufferRadius(band, op.radius)
        currentSource = vrt   // VRT becomes input for next step
    return currentSource      // final VRT: entire chain, no materialization
```

This chain is entirely virtual until a terminal node materializes the result via `gdal_translate` to GeoTIFF or another persistent format.

### 4.3 Encapsulation into workflow nodes

The VRT-based processing capability is encapsulated into workflow nodes such as pixel decimal adjustment and DSM/DEM difference processing. Workflow nodes do not directly expose low-level GDAL details to the user. Instead, they receive input file paths and parameters from dialogs, invoke corresponding GDAL-based processing logic, and pass generated VRT outputs to downstream nodes under the workflow manager.

### 4.4 Workload stratification of pixel functions

![Figure 4. Three-tier workload design for light, medium, and relatively heavy pixel functions.](figures/04_workload_tiers.png)

The three-tier workload design is illustrated in Figure 4. The framework organizes representative operators into three workload levels. The light level is represented by `pixelDecimalFunction`, which performs simple per-pixel numerical transformation. The medium level is represented by `pixelDSMDEMDiffProcessFunction`, which involves two-source input, nodata handling, threshold logic, and conditional replacement. The relatively heavy level is represented by `pixelSlopFunction_S2` and the newly introduced `pixelTerrainRoughness5x5Function`. The latter computes a 5x5 neighborhood-based local terrain roughness metric by calculating the standard deviation of valid elevation values within a local window.

The 5×5 window size matches the operator deployed in the source production system for terrain roughness derivation and corresponds to a 25-pixel neighborhood read per output pixel. This window is large enough to make per-pixel computation non-trivial relative to I/O while remaining within the `BufferRadius = 2` setting that GDAL evaluates without additional tile-boundary handling, making it a representative heavy operator for the lazy-evaluation overhead test.

### 4.5 Scope and limitations

The proposed mechanism is effective for selected classes of raster operations that can be expressed as pixel-level transformations or derived raster-band computations. However, it does not eliminate all forms of materialization. Final outputs still need to be written to persistent formats, and some thematic extraction or vector post-processing steps require intermediate shapefiles or raster products. Furthermore, neighborhood-based operators that require `BufferRadius` in VRT incur additional overhead because GDAL must read extended tiles to supply the pixel function with its required context window. The framework is therefore a lightweight chained processing mechanism, not a fully materialization-free universal raster engine.

In the source production system, the workflow nodes whose outputs were converted to VRT-derived bands are pixel decimal adjustment, DSM/DEM difference processing, slope-related computation, and 5×5 neighborhood roughness. Other nodes — tile cutting, projection transform, resampling, raster-to-vector conversion, and the various map-sheet inspection nodes (edge match, range, gross-error, invalid-value, DEM precision and horizontal checks) — continue to write physical raster or vector files because their outputs feed into vector-side post-processing or are required as durable deliverables. The framework therefore reduces but does not eliminate intermediate materialization across a complete production chain.

## 5. System Implementation and Representative Modules

### 5.1 Raster processing modules

Several raster processing modules in the system demonstrate the integration of VRT-derived computation with workflow execution. Pixel decimal adjustment uses a VRT-based output as an intermediate result, while DSM/DEM difference processing constructs derived raster computation for difference handling and nodata-related logic. These modules show how pixel functions can be used beyond static virtual raster description.

The DSM/DEM difference processor is a representative example of a medium-complexity pixel function that operates on two source bands. The function reads the primary surface model and the reference elevation model, applies nodata masking, performs conditional filling where one source has valid data and the other does not, and then applies a threshold-based replacement rule to handle cases where the difference exceeds a specified tolerance. The corresponding VRT structure includes two `SimpleSource` elements, one for each input band, and the pixel function receives both sources as elements of the `in_ar` array.

### 5.2 Heavy-load neighborhood operator

The `terrainRoughness5x5` method further extends the workload range of the framework. The corresponding pixel function accesses a 5x5 local neighborhood of a single-band DEM or DSM raster, ignores nodata cells, checks the minimum valid sample count, computes the local mean, and then derives the standard deviation as a roughness indicator. The VRT band uses `<BufferRadius>2</BufferRadius>` to instruct GDAL to supply a 2-pixel border around each processing block, which enables the 5x5 window to be evaluated without explicit tile-boundary handling by the pixel function itself.

Although the algorithm remains local and compatible with VRT-derived computation, its per-pixel workload is substantially higher than simple value replacement and conditional pixel selection. Each output pixel requires iterating over up to 25 input pixels, filtering nodata, and computing the standard deviation. This makes it suitable as a heavier representative in the evaluation protocol and a stress test for the VRT lazy-evaluation overhead.

### 5.3 Thematic extraction workflow

The vegetation extraction workflow represents another important capability of the project. It combines raster processing, raster splitting, raster-to-vector conversion, dissolve operations, and polygon cleaning. Although not all steps are VRT-based, this workflow illustrates how lightweight raster computation can be integrated with later-stage vector post-processing in a complete production chain. This module is described to show the system's production coverage but was not included in the quantitative evaluation in Section 6.

### 5.4 Quality inspection and output generation

The system also supports production-oriented quality inspection tasks such as adjacent sheet difference checking, extent checking, and invalid value inspection. These functions demonstrate the practical role of the platform in surveying and remote sensing production. Output modules further convert intermediate results into final deliverables, including raster products, vector products, metadata files, and Word or Excel business documents.

## 6. Experiments and Results

### 6.1 Experimental design

The evaluation protocol uses a two-layer design. The first layer is a mechanism experiment that isolates the effect of using VRT instead of GeoTIFF as the intermediate representation. The second layer is a production-like workflow experiment that evaluates longer chains that more closely resemble practical processing sequences.

Three intermediate-storage strategies are compared. In the VRT mode, intermediate results remain as virtual VRT files (KB-sized XML descriptors) and computation is deferred until the final output stage materializes the chain via in-process `GDALTranslate()`. The GeoTIFF mode, by contrast, materializes every intermediate step to an on-disk GeoTIFF file — the conventional workflow approach against which the proposed strategy is benchmarked. A third `/vsimem/` mode materializes each intermediate to GDAL's in-memory virtual filesystem as an uncompressed GeoTIFF, replacing on-disk writes with memory-based writes while preserving the raster encode-decode cycle; this isolates encode-decode cost from disk I/O.

The benchmark runner uses compiled C++ pixel functions registered via `GDALAddDerivedBandPixelFunc()`, matching the production ImageProcessFactory implementation. All GDAL operations execute in-process through the GDAL C API rather than through external subprocess calls, which enables the `/vsimem/` baseline (the `/vsimem/` filesystem is process-local and cannot be shared across subprocesses).

To strengthen the interpretability of the comparison, the benchmark runner records **per-step timing** for each mode. In GeoTIFF mode, each step's wall-clock time includes VRT construction (building the XML descriptor) followed by `GDALTranslate` materialization. Because VRT construction is negligible (typically <100 ms total across all steps), the GeoTIFF step execution time is dominated by materialization. The vsimem mode measures the cost of materializing each intermediate to an in-memory GeoTIFF; it retains raster encode-decode overhead but replaces disk I/O with memory-based writes. The difference between GeoTIFF time and vsimem time therefore reflects the on-disk I/O component of each GeoTIFF step.

To improve statistical reliability, DEM-scale and heavy cases were repeated ten times, and optical cases five times. Standard deviations are reported alongside all mean values. Statistical significance of the runtime differences was assessed using the one-sided Wilcoxon signed-rank test (H1: VRT runtime < GeoTIFF runtime, α = 0.05) with repetitions as paired observations; results are reported in Table 5.

### 6.2 Experimental environment and datasets

![Figure 5. Two-layer evaluation protocol: mechanism experiment (Layer 1, isolating VRT vs GeoTIFF intermediate storage) and production-like workflow experiment (Layer 2, evaluating end-to-end three-step chains).](figures/05_experiment_protocol.png)

The two-layer evaluation protocol is shown in Figure 5, and the experimental environment and dataset summary are provided in Table 1. The benchmark was executed on a Windows desktop aligned with the historical software environment. The host machine was `LONG_ALIENWARE`, running Windows 11 Pro (build 26200) with an Intel Core i9-12900H processor, 34.0 GB memory, and an NVMe Samsung SSD 990 PRO 4TB. The raster runtime remained the legacy OSGeo4W64 stack with `GDAL 2.2.4`, ensuring consistency with the source project rather than introducing behavioral changes from a newer runtime. The implications of this version choice are discussed in Section 7.4.

The benchmark executable was compiled with MSVC cl.exe from Visual Studio 2022 C++ Build Tools using the `/O2` speed-optimization flag, and linked against `gdal_i.lib` from the OSGeo4W64 distribution. Elapsed time was measured using the Windows `QueryPerformanceCounter` API at approximately 100 ns resolution. All materialized intermediate outputs — both GeoTIFF (on-disk) and vsimem (in-memory) — were written as uncompressed GeoTIFF (`-of GTiff -co COMPRESS=NONE`). This choice is deliberate: using uncompressed format isolates the raster encode-decode cycle cost from compression overhead, so that the GeoTIFF mode timing reflects the pure cost of materializing raster data rather than an inflated cost attributable to a compression algorithm. To reduce systematic bias from OS-level disk and memory caching, the execution order of the three modes was randomized within each repetition using a seeded Mersenne Twister (seed = 42), producing a reproducible but non-systematic schedule.

Two categories of real data were used. The main benchmark data were matched DEM/DSM map-sheet products from the `3D test data` directory. Three elevation scales were organized through VRT mosaics: a single-sheet small case (`2697 x 1957`), a `2 x 2` medium case (`5299 x 3816`), and a `2 x 3` large case (`7891 x 3835`). All elevation rasters were single-band `Float32` products with `10 m` pixel size and `NoData = -9999`. A supplementary optical case used a `1.9 GB` JiLin-1 panchromatic product (satellite `JL1KF02B02`, sensor `PMS09`, product level `L1`) acquired on `2025-03-27` at `0.5 m` ground sample distance. The scene covers approximately `30.09°–30.23°N / 103.89°–104.08°E` in the Sichuan production area and is supplied as a single-band `UInt16` raster with `30839 x 30948` pixels, `14`-bit radiometric depth, and `0%` cloud coverage; RPC metadata accompanies the product. This case verifies that the chained VRT strategy also applies to high-resolution single-band imagery.

**Table 1. Experimental environment and dataset summary**

| Item | Description |
|---|---|
| Host machine | LONG_ALIENWARE |
| OS | Windows 11 Pro, build 26200 |
| CPU | 12th Gen Intel Core i9-12900H |
| Memory | 34.0 GB |
| Storage | NVMe Samsung SSD 990 PRO 4TB |
| GDAL runtime | GDAL 2.2.4 (OSGeo4W64 legacy stack) |
| Benchmark compiler | MSVC cl.exe (Visual Studio 2022 C++ Build Tools), /O2 optimization |
| Intermediate file format | Uncompressed GeoTIFF (-of GTiff -co COMPRESS=NONE) |
| Timer | Windows QueryPerformanceCounter (~100 ns resolution) |
| Mode execution order | Randomized per repetition (Mersenne Twister, seed = 42) |
| Comparison modes | VRT (deferred), GeoTIFF (on-disk materialized), /vsimem/ (in-memory materialized), with per-step timing decomposition |
| Main elevation data | 3D test data DEM/DSM, single-band Float32, 10 m, NoData = -9999 |
| Small elevation case | 1 map sheet, 2697 x 1957 pixels |
| Medium elevation case | 2 x 2 map-sheet VRT mosaic, 5299 x 3816 pixels |
| Large elevation case | 2 x 3 map-sheet VRT mosaic, 7891 x 3835 pixels |
| Supplementary optical case | JL1KF02B02 / PMS09 panchromatic image; 0.5 m GSD; 30839 x 30948 pixels; single-band UInt16 (14-bit); acquired 2025-03-27; Sichuan production area (scene centre ≈ 30.146°N, 103.984°E); 0% cloud; RPC metadata |
| Repetition strategy | DEM-scale mechanism cases: 10; optical mechanism case: 5; production DEM chains: 10; optical production case: 5 |

### 6.3 Mechanism experiment: VRT versus GeoTIFF with timing decomposition

The mechanism experiment focused on representative two-step chains. Seven cases were selected: three light, two medium, and two heavy. The large DEM mosaic (7891 x 3835) was omitted because its pixel count is close to the medium case (5299 x 3816); the optical large case (30839 x 30948) provides a more meaningful scale contrast. Light cases combined decimal adjustment with pixel modification; medium cases combined DSM/DEM difference processing with decimal adjustment; heavy cases combined decimal adjustment with the `roughness_5x5` neighborhood operator.

![Figure 6. Measured intermediate storage comparison in the mechanism experiment.](figures/06_storage_comparison_r1.png)

![Figure 7. Measured runtime comparison in the mechanism experiment.](figures/07_runtime_comparison_r1.png)

![Figure 8. Workload-level summary of the mechanism experiment.](figures/08_cross_algorithm_r1.png)

Intermediate storage and runtime results are shown in Figures 6–8. Across all seven mechanism cases, the VRT mode replaced full raster intermediates with KB-sized XML descriptors, eliminating physical intermediate materialization in all cases (measured storage reduction: 99.995–100.000%, as reported in Table 3). This outcome follows directly from the mechanism: a VRT file describes computation without allocating raster data, whereas the GeoTIFF mode generates full raster arrays of 21 MB (small DEM), 81 MB (medium DEM), or 1.9 GB (optical large case) per intermediate; all were replaced by VRT descriptors of 638–1,071 bytes.

Runtime behavior showed consistent positive gains across all workload types. For the DEM-scale light cases (small and medium), VRT achieved runtime reductions of 14.0% and 11.8%, respectively. The large optical case (1.9 GB PAN image) showed a 9.4% runtime reduction, indicating that the benefit also extends to larger single-raster contexts, though this single-case observation does not constitute a systematic scaling analysis. The medium-workload cases achieved consistent reductions of 12.1% and 10.2%. The heavy cases, which use compiled C++ neighborhood-based roughness computation, also showed positive gains of 7.4% and 10.4%, confirming that even in compute-intensive scenarios VRT's avoidance of intermediate encode-decode cycles provides measurable benefit.

The three-mode comparison (Table 2) provides direct evidence for this interpretation. The vsimem mode, which materializes every intermediate to GDAL's in-memory filesystem as an uncompressed GeoTIFF, serves as an in-memory materialization baseline. Unexpectedly, vsimem was consistently slower than on-disk GeoTIFF across all cases, indicating that on the NVMe SSD test system (Samsung 990 PRO), GDAL 2.2.4's `/vsimem/` implementation incurs memory-allocation overhead that exceeds the disk I/O cost. This finding is consistent with the interpretation that VRT's runtime advantage does not arise primarily from faster disk access; rather, it appears to derive from avoiding the redundant read-decode-compute-encode-write cycle that materializing each GeoTIFF intermediate requires. This mechanistic explanation has not been independently confirmed by profiling and is noted as a limitation in Section 7.7.

**Table 2. Three-mode timing comparison**

| Case | GeoTIFF (ms) | vsimem (ms) | VRT (ms) | VRT red. (%) | vsimem vs GeoTIFF |
|---|---:|---:|---:|---:|---|
| mech_light_small | 269 | 371 | 232 | 14.0 | vsimem 38% slower |
| mech_light_medium | 968 | 1430 | 854 | 11.8 | vsimem 48% slower |
| mech_light_optical | 29209 | 41604 | 26476 | 9.4 | vsimem 42% slower |
| mech_medium_small | 333 | 462 | 292 | 12.1 | vsimem 39% slower |
| mech_medium_medium | 1284 | 1772 | 1152 | 10.2 | vsimem 38% slower |
| mech_heavy_small | 559 | 732 | 517 | 7.4 | vsimem 31% slower |
| mech_heavy_medium | 2302 | 3443 | 2063 | 10.4 | vsimem 50% slower |

**Table 3. Mechanism experiment summary (C++ pixel functions, 10/5 repetitions)**

| Case | Reps | VRT runtime (ms) | VRT std | GeoTIFF runtime (ms) | GeoTIFF std | Storage red. (%) | Runtime red. (%) | Checksum |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| mech_light_small | 10 | 232 | 10 | 269 | 17 | 99.997 | 14.0 | True |
| mech_light_medium | 10 | 854 | 25 | 968 | 39 | 99.999 | 11.8 | True |
| mech_light_optical_large | 5 | 26476 | 685 | 29209 | 911 | 100.000 | 9.4 | True |
| mech_medium_small | 10 | 292 | 6 | 333 | 13 | 99.995 | 12.1 | True |
| mech_medium_medium | 10 | 1152 | 22 | 1284 | 34 | 99.999 | 10.2 | True |
| mech_heavy_small | 10 | 517 | 10 | 559 | 17 | 99.997 | 7.4 | * |
| mech_heavy_medium | 10 | 2063 | 171 | 2302 | 158 | 99.999 | 10.4 | True |

\* Checksum not bitwise identical: VRT evaluates the chain in a single fused pass whereas GeoTIFF mode materializes an intermediate first; different evaluation orders produce LSB-level differences under IEEE 754 floating-point arithmetic [19] (absolute mean diff. < 1.2 × 10⁻⁶, max diff. < 9.1 × 10⁻⁵, within standard DEM accuracy tolerances for 10 m products). This reflects expected floating-point behavior, not algorithmic divergence.

Wilcoxon signed-rank tests confirmed that all seven runtime reductions are statistically significant (Table 5). For the six DEM-scale cases (n = 10 repetitions each), the test statistic W = 55 — the maximum possible for n = 10 — indicating VRT was faster than GeoTIFF in every single repetition without exception. The optical large case (n = 5) yielded W = 15 (the maximum for n = 5), p = 0.031. Effect sizes r ≥ 0.833 across all seven mechanism cases qualify as large effects by standard conventions.

### 6.4 Production-like workflow experiment

The second experimental layer addresses a limitation of the earlier benchmark design: the core mechanism had been validated, but mostly on short chains. To better reflect practical use, three workflow-like chains were constructed. An optical light chain consisted of three successive pixel-level steps; a DEM/DSM production-style chain combined difference processing, decimal adjustment, and a terminal value-normalization step; a heavier production-style chain combined decimal adjustment, neighborhood roughness derivation, and final decimal formatting.

![Figure 9. Production-like workflow runtime comparison.](figures/09_production_runtime_r1.png)

Runtime results for the production-like chains are shown in Figure 9. The proposed VRT strategy consistently improves end-to-end time across all tested chains (Table 4). `prod_medium_chain` achieved a 19.2% runtime reduction while reducing intermediate materialization by 99.996% (from 42 MB of intermediate GeoTIFFs to a 1.8 KB VRT descriptor chain). `prod_heavy_chain` achieved a comparable 18.7% runtime reduction (from 243 MB to 1.3 KB), demonstrating that the VRT advantage extends to three-step chains with neighborhood-based operators. `prod_light_optical` achieved a 13.4% runtime reduction with 100.000% intermediate reduction (from 3.8 GB to 1.5 KB), though with higher variance due to the large raster size.

**Table 4. Production-like workflow experiment summary (C++ pixel functions, 10/5 repetitions)**

| Case | Reps | VRT runtime (ms) | VRT std | GeoTIFF runtime (ms) | GeoTIFF std | Storage red. (%) | Runtime red. (%) | Checksum |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| prod_light_optical | 5 | 40162 | 5175 | 46381 | 8886 | 100.000 | 13.4 | True |
| prod_medium_chain | 10 | 343 | 16 | 425 | 76 | 99.996 | 19.2 | True |
| prod_heavy_chain | 10 | 2136 | 101 | 2627 | 127 | 99.999 | 18.7 | True |

Statistical testing confirmed significance for all three production chains (Table 5). The one-sided Wilcoxon test yielded p ≤ 0.031 for every chain, with W equal to the maximum possible for the given sample size — VRT was faster than GeoTIFF in every single repetition across all three production chains.

**Table 5. Wilcoxon signed-rank test: VRT vs GeoTIFF runtime (one-sided, H1: VRT < GeoTIFF)**

| Case | N | VRT (ms) | GeoTIFF (ms) | Red. (%) | W | p | r |
|---|---:|---:|---:|---:|---:|---:|---:|
| mech_light_small | 10 | 232 | 269 | 14.0 | 55 | 0.001 | 0.979 |
| mech_light_medium | 10 | 854 | 968 | 11.8 | 55 | 0.001 | 0.979 |
| mech_light_optical_large | 5 | 26476 | 29209 | 9.4 | 15 | 0.031 | 0.833 |
| mech_medium_small | 10 | 292 | 333 | 12.1 | 55 | 0.001 | 0.979 |
| mech_medium_medium | 10 | 1152 | 1284 | 10.2 | 55 | 0.001 | 0.979 |
| mech_heavy_small | 10 | 517 | 559 | 7.4 | 55 | 0.001 | 0.979 |
| mech_heavy_medium | 10 | 2063 | 2302 | 10.4 | 55 | 0.001 | 0.979 |
| prod_light_optical | 5 | 40162 | 46381 | 13.4 | 15 | 0.031 | 0.833 |
| prod_medium_chain | 10 | 344 | 425 | 19.2 | 55 | 0.001 | 0.979 |
| prod_heavy_chain | 10 | 2136 | 2627 | 18.7 | 55 | 0.001 | 0.979 |

W: Wilcoxon test statistic; maximum W = N(N+1)/2 (W = 55 for N = 10; W = 15 for N = 5), indicating VRT was faster in every repetition. r: effect size r = Z/√N; r > 0.5 is large by Cohen's (1988) convention. All p-values are exact one-sided.

### 6.5 Main findings

Across all tested cases, VRT intermediates replaced 21–81 MB (DEM-scale) or 1.9–3.8 GB (optical) GeoTIFF intermediates with KB-sized XML descriptors, yielding storage reductions of 99.995–100.000%. Runtime gains of 7.4–19.2% were observed across all tested configurations, including heavy neighborhood-based operators implemented in compiled C++, indicating that even compute-intensive cases show measurable benefit from reduced intermediate materialization.

The three-mode comparison provided a mechanistic explanation. The `/vsimem/` in-memory baseline was consistently slower than disk-based GeoTIFF across all cases (31–50% overhead), indicating that on NVMe SSD, GDAL's `/vsimem/` implementation incurs memory-allocation costs exceeding disk write costs. This rules out I/O avoidance as the primary driver of VRT's advantage. Per-step timing decomposition (supplementary `timing_decomposition.csv`) shows that materialization — the read-decode-compute-encode-write cycle for each intermediate — accounts for 75–89% (mean 83%) of GeoTIFF mode total time across the seven mechanism cases. The benefit therefore comes from fusing multi-step computation into a single deferred pass that bypasses materialization at every step except the final output.

The three-step production chains produced larger reductions (18.7–19.2%) than the two-step mechanism cases (mean 10.8% across DEM-scale cases), consistent with a benefit that grows with chain length. The source production system in operational use applies pipelines with substantially more stages, where the additive savings of avoiding intermediate materialization at each non-terminal step are expected to dominate; rigorous benchmarking on chains of four or more steps in this study is left as follow-up.

## 7. Discussion and Limitations

### 7.1 Timing decomposition and interpretability

Without the three-mode comparison, VRT-versus-GeoTIFF numbers show that something changed but not why. The vsimem baseline provides the key evidence: since in-memory intermediates are consistently slower than disk intermediates on the NVMe SSD test system, VRT's advantage is unlikely to come from faster storage access alone. The timing data are consistent with the hypothesis that gains arise from avoiding the redundant encode-decode overhead of intermediate materialization, though independent profiling would be needed to confirm this mechanism.

### 7.2 Relationship between workload, workflow length, and VRT benefit

The results show that VRT benefit is consistent across all tested workload types (7.4-19.2%). Light and medium DEM cases show 10-14% gains from avoided intermediate processing overhead. Heavy neighborhood-based operators, which with compiled C++ pixel functions are no longer compute-dominated in the way they were under interpreted Python, also show 7-10% gains. Production-like three-step chains achieve the strongest reductions (13-19%), suggesting that the benefit accumulates with chain length.

### 7.3 Practical significance for production workflows

In the source project, a typical map-sheet production run writes three to five intermediate GeoTIFFs before the final deliverable. For example, a 5-step workflow on a 120 MB DEM generates approximately 600 MB of intermediate GeoTIFFs; across a batch of 50 map sheets, this amounts to roughly 30 GB of temporary files that operators must manage and delete manually. Replacing those intermediates with VRT descriptors eliminates this cleanup step entirely and removes a common source of operator error (accidentally deleting or overwriting an intermediate that is still needed). The runtime question matters less in this context than the workflow simplification.

### 7.4 Legacy GDAL version and generalizability

The experiments were intentionally run under GDAL 2.2.4 to match the source system. The storage-reduction result is expected to be version-independent, since it follows from the structure of VRT as a descriptor format rather than from any specific runtime optimization; however, it has only been confirmed under GDAL 2.2.4. Runtime magnitudes may shift under newer GDAL releases, especially where later VRT multithreading and improved Python pixel-function support could affect heavy cases. The use of a high-performance NVMe SSD likely reduces the observable I/O advantage of VRT compared to slower storage media (HDDs, network drives), where intermediate materialization costs would be proportionally higher. Future work should rerun the protocol under GDAL 3.x and evaluate the effect of different storage configurations.

### 7.5 Comparison with other lazy-evaluation approaches

GEE and Dask/xarray [6, 7, 15] operate at the cloud-service or Python task-graph level, respectively; the proposed method operates at the GDAL driver level. This makes it directly embeddable in existing GDAL-based C/C++ production software without migrating the processing stack, but limits the operator vocabulary to per-pixel and fixed-neighborhood computations.

### 7.6 Benchmark fidelity to production implementation

The benchmark runner uses compiled C++ pixel functions registered through `GDALAddDerivedBandPixelFunc()`, which matches the production `ImageProcessFactory` implementation (Section 6.1). This alignment ensures that the measured I/O-to-computation ratios reflect the actual balance in the deployed system rather than those of a compute-inflated prototype.

Earlier development stages used embedded Python pixel functions for rapid iteration. Neighborhood operators such as `roughness_5x5`, when implemented with Python explicit loops, are substantially slower than their C++ counterparts — heavy cases that complete in under three seconds with C++ take tens of seconds with Python. This performance gap inverts the I/O-to-computation ratio and narrows the observable VRT benefit for heavy operators, not because VRT provides less structural advantage but because computation cost dominates the measurement. By using C++ in the current evaluation, the heavy-case gains (7.4-10.4% in mechanism experiments, 18.7% in the production chain) reflect the actual deployment scenario and should not be discounted as compute-dominated artifacts.

### 7.7 Additional limitations and future work

The current evaluation does not cover every branch of the full production system. Raster-to-vector stages, document-generation outputs, and multi-band imagery remain outside the benchmark runner. Extending coverage to these modules, adding multi-band test cases, and adding throughput-oriented indicators (map sheets per hour, pixels per second) would strengthen the generalizability claim.

The five-repetition optical cases (`mech_light_optical_large`, `prod_light_optical`) showed higher run-to-run variance than the DEM-scale cases — coefficients of variation of 13–19% for GeoTIFF versus 3–5% for DEM-scale runs. The 13.4% runtime reduction for `prod_light_optical` should therefore be interpreted as indicative rather than tightly bounded; increasing the repetition count on a quiescent system would narrow the confidence interval for large-raster cases.

The mechanistic explanation for VRT's runtime advantage — that it comes from avoiding redundant encode-decode cycles rather than faster storage access — is supported by the vsimem baseline data but has not been independently confirmed by sub-step profiling. Detailed profiling to attribute runtime savings to specific phases (memory allocation, GDAL I/O driver overhead, encoding) is identified as a direction for future work.

The within-repetition randomization described in Section 6.2 reduces systematic bias from OS-level disk and memory caching but does not eliminate warm-cache contribution. All ten cases were executed back-to-back under steady-state OS caches rather than from cold boot, so first-invocation cache-miss costs — which could shift the VRT-versus-GeoTIFF margin on cold-start deployments or on HDD-backed storage — are not isolated by the present protocol. A cold-cache replication under controlled power-cycle conditions is identified as follow-up work.

Memory consumption was not separately profiled in this evaluation. The benchmark host carried 34 GB of RAM and informal observation indicated peak working-set memory remained well below the host capacity in all tested cases, but no per-mode RAM-peak measurement is reported. Given that VRT's lazy evaluation defers raster decoding until terminal materialization, a follow-up evaluation with per-mode peak-memory instrumentation (for example via Windows Performance Toolkit or `psutil`) would clarify whether VRT shifts working-set pressure rather than removing it.

In production workflow graphs, branches typically diverge after a materialized output rather than after an intermediate VRT band, which avoids redundant pixel-function evaluation across branches. When a VRT-derived raster band is itself referenced by multiple downstream branches without an intervening materialization, GDAL evaluates the pixel function independently for each reference; in such topologies the storage advantage trades off against repeated computation. The linear chains evaluated here and post-materialization branching as observed in operational use both avoid this redundancy. Topologies that explicitly share intermediate VRT bands across branches are a remaining direction for follow-up.

## 8. Conclusion

This paper presented and evaluated a lightweight chained raster computation framework that embeds GDAL VRT pixel functions as executable intermediate representations in a visual geospatial workflow system.

Using compiled C++ pixel functions matching the production system and a three-mode comparison (VRT, GeoTIFF, and `/vsimem/` in-memory baseline), the evaluation showed consistent runtime gains across all tested cases (7.4–19.2%). The `/vsimem/` baseline produced an unexpected finding: in-memory materialization was 31–50% slower than on-disk GeoTIFF on the NVMe SSD test system. Together with per-step timing decomposition showing that materialization accounts for 75–89% of GeoTIFF mode total time, this indicates that VRT's runtime advantage comes from avoiding redundant encode-decode cycles rather than from replacing slow disk writes.

For production environments where operators routinely manage and clean up temporary intermediate files across batch runs, the combined benefits of suppressing intermediate raster materialization by over 99.99% and achieving consistent runtime acceleration across all tested cases make VRT chaining a practical improvement over conventional multi-step workflows.

## Supplementary Materials

The following supporting files are provided with the manuscript: aggregated case summaries (`r1_mechanism_cpp/case_summary.csv`, `r1_production_cpp/case_summary.csv`), per-run benchmark records (`raw_runs.csv` for both subsets), timing-decomposition tables (`timing_decomposition.csv` for both subsets), and the Wilcoxon paired-test script (`wilcoxon_paired.py`) used to reproduce the statistical results reported in Section 6.

## Author Contributions

Conceptualization, Y.L.; methodology, Y.L.; software, Y.L.; validation, Y.L. and Z.W.; formal analysis, Y.L.; investigation, Y.L.; resources, Z.W.; data curation, Y.L.; writing—original draft preparation, Y.L.; writing—review and editing, Y.L. and Z.W.; visualization, Y.L.; supervision, Z.W.; project administration, Z.W.; funding acquisition, Z.W. All authors have read and agreed to the published version of the manuscript.

## Funding

This research received no external funding. The production data environment and internal technical resources were provided by Sichuan Surveying and Mapping Technology Service Center Co., Ltd.

## Data Availability Statement

The benchmark runner, experiment configurations, figure-generation scripts, derived CSV/JSON result files, and statistical analysis script are available at: [GITHUB_URL_PENDING]. The source raster datasets were derived from restricted production data and cannot be publicly redistributed. The supplementary materials include aggregated and per-run benchmark outputs sufficient to reproduce the reported tables and statistical tests.

## Acknowledgments

The authors thank the Sichuan Surveying and Mapping Geographic Information Bureau Surveying and Mapping Technology Service Center for providing the production-scale DEM/DSM and optical raster datasets and the operational geospatial processing environment in which the source system was developed and evaluated.

## Conflicts of Interest

The authors declare no conflict of interest.

## References

[1] Altintas, I.; Ludascher, B.; Klasky, S.; Vouk, M.A. Introduction to Scientific Workflow Management and the Kepler System. In *Proceedings of the 2006 ACM/IEEE Conference on Supercomputing*; ACM: New York, NY, USA, **2006**. https://doi.org/10.1145/1188455.1188669.

[2] Zhang, F.; Xu, Y. Geospatially Constrained Workflow Modeling and Implementation. *Information* **2016**, *7*, 30. https://doi.org/10.3390/info7020030.

[3] Neteler, M.; Bowman, M.H.; Landa, M.; Metz, M. GRASS GIS: A Multi-Purpose Open Source GIS. *Environ. Model. Softw.* **2012**, *31*, 124–130. https://doi.org/10.1016/j.envsoft.2011.11.014.

[4] Holmes, C. Cloud Optimized GeoTIFF: An Imagery Format for Cloud-Native Geospatial Processing. Available online: https://www.cogeo.org/ (accessed 28 March 2026).

[5] Henderson, P.; Morris, J.H. A Lazy Evaluator. In *Proceedings of the 3rd ACM SIGACT-SIGPLAN Symposium on Principles of Programming Languages (POPL'76)*; ACM: New York, NY, USA, **1976**; pp. 95–103. https://doi.org/10.1145/800168.811543.

[6] Gorelick, N.; Hancher, M.; Dixon, M.; Ilyushchenko, S.; Thau, D.; Moore, R. Google Earth Engine: Planetary-Scale Geospatial Analysis for Everyone. *Remote Sens. Environ.* **2017**, *202*, 18–27. https://doi.org/10.1016/j.rse.2017.06.031.

[7] Rocklin, M. Dask: Parallel Computation with Blocked Algorithms and Task Scheduling. In *Proceedings of the 14th Python in Science Conference (SciPy 2015)*; **2015**; pp. 126–132. https://doi.org/10.25080/Majora-7b98e3ed-013.

[8] GDAL/OGR Contributors. GDAL Documentation: VRT—Virtual Format. Available online: https://gdal.org/en/latest/drivers/raster/vrt.html (accessed 28 March 2026).

[9] Oinn, T.; Addis, M.; Ferris, J.; et al. Taverna: A Tool for the Composition and Enactment of Bioinformatics Workflows. *Bioinformatics* **2004**, *20*, 3045–3054. https://doi.org/10.1093/bioinformatics/bth361.

[10] Graser, A.; Olaya, V. Processing: A Python Framework for the Seamless Integration of Geoprocessing Tools in QGIS. *ISPRS Int. J. Geo-Inf.* **2015**, *4*, 2219–2245. https://doi.org/10.3390/ijgi4042219.

[11] Allen, D.W. *Getting to Know ArcGIS ModelBuilder*; Esri Press: Redlands, CA, USA, **2011**.

[12] Inglada, J.; Christophe, E. The Orfeo Toolbox Remote Sensing Image Processing Software. In *Proceedings of the 2009 IEEE International Geoscience and Remote Sensing Symposium (IGARSS)*; IEEE: Piscataway, NJ, USA, **2009**; Vol. 4, pp. 733–736. https://doi.org/10.1109/IGARSS.2009.5417481.

[13] Pakdil, M.E.; Celik, R.N. Serverless Geospatial Data Processing Workflow System Design. *ISPRS Int. J. Geo-Inf.* **2022**, *11*, 20. https://doi.org/10.3390/ijgi11010020.

[14] Yue, P.; Gong, J.; Di, L.; Yuan, J.; Sun, L.; Sun, Z.; Wang, Q. GeoPW: Laying Blocks for the Geospatial Processing Web. *Trans. GIS* **2010**, *14*, 755–772. https://doi.org/10.1111/j.1467-9671.2010.01232.x.

[15] Hoyer, S.; Hamman, J. xarray: N-D Labeled Arrays and Datasets in Python. *J. Open Res. Softw.* **2017**, *5*, 10. https://doi.org/10.5334/jors.148.

[16] Gillies, S.; et al. Rasterio: Geospatial Raster I/O for Python Programmers. Available online: https://github.com/rasterio/rasterio (accessed 28 March 2026).

[17] Killough, B. Overview of the Open Data Cube Initiative. In *Proceedings of the 2018 IEEE International Geoscience and Remote Sensing Symposium (IGARSS)*; IEEE: Piscataway, NJ, USA, **2018**; pp. 8629–8632. https://doi.org/10.1109/IGARSS.2018.8517694.

[18] Rouault, E.; et al. GDAL Source Code: frmts/vrt/vrtderivedrasterband.cpp. Available online: https://github.com/OSGeo/gdal/blob/master/frmts/vrt/vrtderivedrasterband.cpp (accessed 28 March 2026).

[19] Goldberg, D. What Every Computer Scientist Should Know About Floating-Point Arithmetic. *ACM Comput. Surv.* **1991**, *23*, 5–48. https://doi.org/10.1145/103162.103163.

[20] Georges, A.; Buytaert, D.; Eeckhout, L. Statistically Rigorous Java Performance Evaluation. *ACM SIGPLAN Not.* **2007**, *42*, 57–76. https://doi.org/10.1145/1297105.1297033.

[21] Xie, Q. The Design of a High Performance Earth Imagery and Raster Data Management and Processing Platform. *Int. Arch. Photogramm. Remote Sens. Spatial Inf. Sci.* **2016**, *XLI-B4*, 551–557. https://doi.org/10.5194/isprs-archives-XLI-B4-551-2016.

[22] Baumann, P. Management of Multidimensional Discrete Data. *VLDB J.* **1994**, *3*, 401–444. https://doi.org/10.1007/BF01231603.

[23] Wagemann, J.; Clements, O.; Marco Figuera, R.; Rosber, A.P.; Mantovani, S. Geospatial Data Processing in the Cloud—A Review of State-of-the-Art Approaches. *Open Geospatial Data Softw. Stand.* **2021**, *6*, 6. https://doi.org/10.1186/s40965-021-00080-0.

[24] Radosevic, V.; Duckham, M.; Liu, Y.; et al. Hydro KNIME: Scientific Workflows for Reproducible Decision Support. *Appl. Comput. Geosci.* **2026**, *30*, 100348. https://doi.org/10.1016/j.acags.2026.100348.

[25] Haynes, D.; Ray, S.; Manson, S.M.; Soni, A. Developing the Raster Big Data Benchmark: A Comparison of Raster Analysis on Big Data Platforms. *ISPRS Int. J. Geo-Inf.* **2020**, *9*, 690. https://doi.org/10.3390/ijgi9110690.

[26] Kale, A.; Sun, Z.; Fan, F.; et al. Geoweaver_cwl: Transforming Geoweaver AI Workflows to Common Workflow Language. *Appl. Comput. Geosci.* **2023**, *18*, 100126. https://doi.org/10.1016/j.acags.2023.100126.
