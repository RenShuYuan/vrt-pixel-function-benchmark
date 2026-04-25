/**
 * C++ VRT Pixel Function Benchmark — Three-Mode Edition
 *
 * Compares VRT-chained vs GeoTIFF-materialized vs /vsimem/ workflows
 * using native C++ pixel functions registered via GDALAddDerivedBandPixelFunc().
 *
 * Three modes:
 *   geotiff  – materialize every intermediate to on-disk GeoTIFF
 *   vrt      – chain VRT intermediates, materialize only the final output
 *   vsimem   – materialize every intermediate to in-memory /vsimem/ filesystem
 *
 * The vsimem mode provides a pure-computation baseline:
 *   Disk I/O overhead  = GeoTIFF_time - vsimem_time
 *   VRT I/O avoidance  = (GeoTIFF_time - VRT_time) / (GeoTIFF_time - vsimem_time)
 *
 * Build (MSVC + GDAL 2.2.4):
 *   set GDAL_ROOT=C:\OSGeo4W64    (or your local install root)
 *   cl /EHsc /O2 /I "%GDAL_ROOT%\include" benchmark_cpp.cpp
 *      /link /LIBPATH:"%GDAL_ROOT%\lib" gdal_i.lib /OUT:benchmark_cpp.exe
 *   (or just run build.bat with GDAL_ROOT set)
 *
 * Usage:
 *   benchmark_cpp.exe <config.json>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <random>
#include <iomanip>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <direct.h>
#else
#include <sys/time.h>
#include <sys/stat.h>
#endif

#include "gdal.h"
#include "gdal_priv.h"
#include "gdal_vrt.h"
#include "gdal_alg.h"
#include "gdal_utils.h"
#include "cpl_string.h"
#include "cpl_conv.h"
#include "cpl_vsi.h"

#include "json.hpp"
using njson = nlohmann::json;

// ============================================================
// High-resolution timer
// ============================================================
static double now_ms() {
#ifdef _WIN32
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart / (double)freq.QuadPart * 1000.0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
#endif
}

// ============================================================
// Global parameters for pixel functions (mirrors production code)
// ============================================================
static int    g_IPF_DECIMAL = 100;
static double g_IPF_DSM_NODATA = -9999.0;
static double g_IPF_DEM_NODATA = -9999.0;
static double g_IPF_THRESHOLD  = 1.0;
static bool   g_IPF_FILLNODATA = true;
static int    g_IPF_DSMDEM_IS_DSM = 1;
static double g_IPF_TERRAIN_NODATA = -9999.0;
static int    g_IPF_TERRAIN_MIN_VALID = 9;
static double g_IPF_MODIFY_OLD = 0.0;
static double g_IPF_MODIFY_NEW = 100.0;

// ============================================================
// C++ Pixel Functions (from ImageProcessFactory)
// ============================================================

static CPLErr pf_decimal(void **papoSources, int nSources, void *pData,
    int nXSize, int nYSize, GDALDataType eSrcType, GDALDataType eBufType,
    int nPixelSpace, int nLineSpace)
{
    if (nSources != 1) return CE_Failure;
    for (int iLine = 0; iLine < nYSize; ++iLine) {
        for (int iCol = 0; iCol < nXSize; ++iCol) {
            int ii = iLine * nXSize + iCol;
            double x0 = SRCVAL(papoSources[0], eSrcType, ii);
            if (x0 != 0)
                x0 = ((double)((long long)(x0 * g_IPF_DECIMAL))) / g_IPF_DECIMAL;
            GDALCopyWords(&x0, GDT_Float64, 0,
                ((GByte*)pData) + nLineSpace * iLine + iCol * nPixelSpace,
                eBufType, nPixelSpace, 1);
        }
    }
    return CE_None;
}

static CPLErr pf_modify(void **papoSources, int nSources, void *pData,
    int nXSize, int nYSize, GDALDataType eSrcType, GDALDataType eBufType,
    int nPixelSpace, int nLineSpace)
{
    if (nSources != 1) return CE_Failure;
    for (int iLine = 0; iLine < nYSize; ++iLine) {
        for (int iCol = 0; iCol < nXSize; ++iCol) {
            int ii = iLine * nXSize + iCol;
            double x0 = SRCVAL(papoSources[0], eSrcType, ii);
            if (x0 == g_IPF_MODIFY_OLD)
                x0 = g_IPF_MODIFY_NEW;
            GDALCopyWords(&x0, GDT_Float64, 0,
                ((GByte*)pData) + nLineSpace * iLine + iCol * nPixelSpace,
                eBufType, nPixelSpace, 1);
        }
    }
    return CE_None;
}

static CPLErr pf_dsmdem_diff(void **papoSources, int nSources, void *pData,
    int nXSize, int nYSize, GDALDataType eSrcType, GDALDataType eBufType,
    int nPixelSpace, int nLineSpace)
{
    if (nSources != 2) return CE_Failure;
    for (int iLine = 0; iLine < nYSize; ++iLine) {
        for (int iCol = 0; iCol < nXSize; ++iCol) {
            int idx = iLine * nXSize + iCol;
            double b1 = SRCVAL(papoSources[0], eSrcType, idx);
            double b2 = SRCVAL(papoSources[1], eSrcType, idx);
            double x0;
            if (g_IPF_DSMDEM_IS_DSM) {
                if (b1 == g_IPF_DSM_NODATA) {
                    x0 = (b2 != g_IPF_DEM_NODATA && g_IPF_FILLNODATA) ? b2 : b1;
                } else if (b2 == g_IPF_DEM_NODATA) {
                    x0 = b1;
                } else {
                    double d = b1 - b2;
                    x0 = (d < 0 && fabs(d) < g_IPF_THRESHOLD) ? b2 : b1;
                }
            } else {
                if (b1 == g_IPF_DEM_NODATA) {
                    x0 = (b2 != g_IPF_DSM_NODATA && g_IPF_FILLNODATA) ? b2 : b1;
                } else if (b2 == g_IPF_DSM_NODATA) {
                    x0 = b1;
                } else {
                    double d = b2 - b1;
                    x0 = (d < 0 && fabs(d) < g_IPF_THRESHOLD) ? b2 : b1;
                }
            }
            GDALCopyWords(&x0, GDT_Float64, 0,
                ((GByte*)pData) + nLineSpace * iLine + iCol * nPixelSpace,
                eBufType, nPixelSpace, 1);
        }
    }
    return CE_None;
}

static CPLErr pf_roughness5x5(void **papoSources, int nSources, void *pData,
    int nXSize, int nYSize, GDALDataType eSrcType, GDALDataType eBufType,
    int nPixelSpace, int nLineSpace)
{
    if (nSources != 1) return CE_Failure;
    const bool ndIsNan = std::isnan(g_IPF_TERRAIN_NODATA);
    const int minValid = std::max(1, std::min(g_IPF_TERRAIN_MIN_VALID, 25));
    for (int iLine = 0; iLine < nYSize; ++iLine) {
        for (int iCol = 0; iCol < nXSize; ++iCol) {
            double x0 = g_IPF_TERRAIN_NODATA;
            if (iLine >= 2 && iLine < nYSize - 2 &&
                iCol  >= 2 && iCol  < nXSize - 2) {
                double vals[25];
                int cnt = 0;
                double sum = 0.0;
                for (int wy = -2; wy <= 2; ++wy) {
                    for (int wx = -2; wx <= 2; ++wx) {
                        int idx = (iLine + wy) * nXSize + (iCol + wx);
                        double v = SRCVAL(papoSources[0], eSrcType, idx);
                        if (std::isnan(v)) continue;
                        if (!ndIsNan && v == g_IPF_TERRAIN_NODATA) continue;
                        vals[cnt++] = v;
                        sum += v;
                    }
                }
                if (cnt >= minValid) {
                    double m = sum / cnt;
                    double var = 0.0;
                    for (int i = 0; i < cnt; ++i) {
                        double d = vals[i] - m;
                        var += d * d;
                    }
                    x0 = sqrt(var / cnt);
                }
            }
            GDALCopyWords(&x0, GDT_Float64, 0,
                ((GByte*)pData) + nLineSpace * iLine + iCol * nPixelSpace,
                eBufType, nPixelSpace, 1);
        }
    }
    return CE_None;
}

static void register_pixel_functions() {
    GDALAddDerivedBandPixelFunc("pf_decimal",      pf_decimal);
    GDALAddDerivedBandPixelFunc("pf_modify",       pf_modify);
    GDALAddDerivedBandPixelFunc("pf_dsmdem_diff",  pf_dsmdem_diff);
    GDALAddDerivedBandPixelFunc("pf_roughness5x5", pf_roughness5x5);
}

// ============================================================
// Utility
// ============================================================
static std::string read_file(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) { fprintf(stderr, "Cannot open %s\n", path); exit(1); }
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

static void write_text(const char* path, const std::string& text) {
    std::ofstream f(path);
    f << text;
}

static void mkdirs(const std::string& path) {
    for (size_t pos = 0; (pos = path.find_first_of("/\\", pos + 1)) != std::string::npos; )
        VSIMkdir(path.substr(0, pos).c_str(), 0755);
    VSIMkdir(path.c_str(), 0755);
}

static long long file_size_bytes(const char* path) {
    VSIStatBufL st;
    if (VSIStatL(path, &st) == 0) return st.st_size;
    return 0;
}

static std::string timestamp_str() {
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", lt);
    return buf;
}

// ============================================================
// Raster helpers
// ============================================================
struct RasterMeta {
    int xsize, ysize;
    GDALDataType dtype;
    int bx, by;
    double nodata;
    bool has_nodata;
};

static RasterMeta get_meta(const char* path) {
    GDALDatasetH ds = GDALOpen(path, GA_ReadOnly);
    if (!ds) { fprintf(stderr, "Cannot open %s\n", path); exit(1); }
    RasterMeta m;
    m.xsize = GDALGetRasterXSize(ds);
    m.ysize = GDALGetRasterYSize(ds);
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    m.dtype = GDALGetRasterDataType(band);
    GDALGetBlockSize(band, &m.bx, &m.by);
    int ok = 0;
    m.nodata = GDALGetRasterNoDataValue(band, &ok);
    m.has_nodata = (ok != 0);
    GDALClose(ds);
    return m;
}

static int get_checksum(const char* path) {
    GDALDatasetH ds = GDALOpen(path, GA_ReadOnly);
    if (!ds) return -1;
    GDALRasterBandH band = GDALGetRasterBand(ds, 1);
    int xs = GDALGetRasterXSize(ds);
    int ys = GDALGetRasterYSize(ds);
    int ck = GDALChecksumImage(band, 0, 0, xs, ys);
    GDALClose(ds);
    return ck;
}

// ============================================================
// VRT XML builders
// ============================================================
static std::string build_vrt_xml(const char* func_name,
    const char* src_path, const RasterMeta& m, int buffer_radius,
    GDALDataType out_type)
{
    const char* dt_str = GDALGetDataTypeName(out_type);
    const char* src_dt = GDALGetDataTypeName(m.dtype);
    std::ostringstream os;
    os << "<VRTDataset rasterXSize=\"" << m.xsize << "\" rasterYSize=\"" << m.ysize << "\">\n"
       << "<VRTRasterBand dataType=\"" << dt_str << "\" band=\"1\" subClass=\"VRTDerivedRasterBand\">\n";
    if (m.has_nodata && out_type == GDT_Float64)
        os << "<NoDataValue>" << m.nodata << "</NoDataValue>\n";
    os << "<PixelFunctionType>" << func_name << "</PixelFunctionType>\n";
    if (buffer_radius > 0)
        os << "<BufferRadius>" << buffer_radius << "</BufferRadius>\n";
    os << "<SimpleSource>\n"
       << "<SourceFilename relativeToVRT=\"0\">" << src_path << "</SourceFilename>\n"
       << "<SourceBand>1</SourceBand>\n"
       << "<SourceProperties RasterXSize=\"" << m.xsize << "\" RasterYSize=\"" << m.ysize
       << "\" DataType=\"" << src_dt << "\" BlockXSize=\"" << m.bx
       << "\" BlockYSize=\"" << m.by << "\"/>\n"
       << "<SrcRect xOff=\"0\" yOff=\"0\" xSize=\"" << m.xsize << "\" ySize=\"" << m.ysize << "\"/>\n"
       << "<DstRect xOff=\"0\" yOff=\"0\" xSize=\"" << m.xsize << "\" ySize=\"" << m.ysize << "\"/>\n"
       << "</SimpleSource>\n"
       << "</VRTRasterBand>\n"
       << "</VRTDataset>";
    return os.str();
}

static std::string build_vrt_xml_2src(const char* func_name,
    const char* src1, const char* src2,
    const RasterMeta& m1, const RasterMeta& m2)
{
    const char* dt = GDALGetDataTypeName(m1.dtype);
    std::ostringstream os;
    os << "<VRTDataset rasterXSize=\"" << m1.xsize << "\" rasterYSize=\"" << m1.ysize << "\">\n"
       << "<VRTRasterBand dataType=\"" << dt << "\" band=\"1\" subClass=\"VRTDerivedRasterBand\">\n"
       << "<PixelFunctionType>" << func_name << "</PixelFunctionType>\n"
       << "<SimpleSource>\n"
       << "<SourceFilename relativeToVRT=\"0\">" << src1 << "</SourceFilename>\n"
       << "<SourceBand>1</SourceBand>\n"
       << "<SourceProperties RasterXSize=\"" << m1.xsize << "\" RasterYSize=\"" << m1.ysize
       << "\" DataType=\"" << dt << "\" BlockXSize=\"" << m1.bx << "\" BlockYSize=\"" << m1.by << "\"/>\n"
       << "<SrcRect xOff=\"0\" yOff=\"0\" xSize=\"" << m1.xsize << "\" ySize=\"" << m1.ysize << "\"/>\n"
       << "<DstRect xOff=\"0\" yOff=\"0\" xSize=\"" << m1.xsize << "\" ySize=\"" << m1.ysize << "\"/>\n"
       << "</SimpleSource>\n"
       << "<SimpleSource>\n"
       << "<SourceFilename relativeToVRT=\"0\">" << src2 << "</SourceFilename>\n"
       << "<SourceBand>1</SourceBand>\n"
       << "<SourceProperties RasterXSize=\"" << m2.xsize << "\" RasterYSize=\"" << m2.ysize
       << "\" DataType=\"" << dt << "\" BlockXSize=\"" << m2.bx << "\" BlockYSize=\"" << m2.by << "\"/>\n"
       << "<SrcRect xOff=\"0\" yOff=\"0\" xSize=\"" << m2.xsize << "\" ySize=\"" << m2.ysize << "\"/>\n"
       << "<DstRect xOff=\"0\" yOff=\"0\" xSize=\"" << m1.xsize << "\" ySize=\"" << m1.ysize << "\"/>\n"
       << "</SimpleSource>\n"
       << "</VRTRasterBand>\n"
       << "</VRTDataset>";
    return os.str();
}

// ============================================================
// GDALTranslate wrapper
// ============================================================
static double translate_to(const char* src, const char* dst) {
    double t0 = now_ms();
    GDALDatasetH hSrc = GDALOpen(src, GA_ReadOnly);
    if (!hSrc) { fprintf(stderr, "Cannot open %s for translate\n", src); return -1; }
    char** opts = NULL;
    opts = CSLAddString(opts, "-of");
    opts = CSLAddString(opts, "GTiff");
    opts = CSLAddString(opts, "-co");
    opts = CSLAddString(opts, "COMPRESS=NONE");
    GDALTranslateOptions* tOpts = GDALTranslateOptionsNew(opts, NULL);
    CSLDestroy(opts);
    int err = 0;
    GDALDatasetH hDst = GDALTranslate(dst, hSrc, tOpts, &err);
    GDALTranslateOptionsFree(tOpts);
    GDALClose(hSrc);
    if (hDst) GDALClose(hDst);
    return now_ms() - t0;
}

// ============================================================
// Data structures
// ============================================================
struct Step {
    std::string op;
    std::string input;       // dataset key or "prev"
    std::string input2;      // second input for dsmdem_diff
    int decimal;
    double val_old, val_new;
    double threshold;
    bool fill_nodata;
    int min_valid;
    Step() : decimal(2), val_old(0), val_new(100), threshold(1.0),
             fill_nodata(true), min_valid(9) {}
};

struct Case {
    std::string id;
    std::string workload;
    int reps;
    std::vector<Step> steps;
    bool enabled;
    Case() : reps(3), enabled(true) {}
};

struct StepResult {
    std::string op;
    double vrt_construct_ms;
    double materialize_ms;
    long long output_bytes;
    bool materialized;
    bool is_last;
};

struct RunResult {
    std::string case_id, workload, mode;
    int rep;
    bool success;
    std::string error;
    double total_ms;
    long long intermediate_bytes;
    int intermediate_count;
    long long final_bytes;
    int checksum;
    double final_mat_ms;
    std::vector<StepResult> steps;
};

// ============================================================
// Case runner (three-mode)
// ============================================================
static RunResult run_case_mode(const Case& c,
    const std::map<std::string, std::string>& datasets,
    const std::map<std::string, double>& nodatas,
    const std::string& mode, int rep, const std::string& outdir)
{
    RunResult r;
    r.case_id = c.id; r.workload = c.workload; r.mode = mode; r.rep = rep;
    r.success = false; r.total_ms = 0;
    r.intermediate_bytes = 0; r.intermediate_count = 0;
    r.final_bytes = 0; r.checksum = -1; r.final_mat_ms = 0;

    char repdir[1024];
    snprintf(repdir, sizeof(repdir), "%s/%s/%s/rep_%02d",
             outdir.c_str(), c.id.c_str(), mode.c_str(), rep);
    mkdirs(repdir);

    // vsimem prefix for in-memory intermediates
    char vmem_prefix[256];
    snprintf(vmem_prefix, sizeof(vmem_prefix), "/vsimem/%s_%s_%02d", c.id.c_str(), mode.c_str(), rep);

    std::vector<std::string> vsimem_files; // track for cleanup

    std::string cur_path;
    double t0 = now_ms();

    try {
        for (int si = 0; si < (int)c.steps.size(); ++si) {
            const Step& st = c.steps[si];
            bool is_last = (si == (int)c.steps.size() - 1);

            // Resolve input
            std::string in1;
            if (st.input == "prev") {
                in1 = cur_path;
            } else {
                auto it = datasets.find(st.input);
                if (it != datasets.end()) in1 = it->second;
                else throw std::runtime_error("Dataset not found: " + st.input);
            }

            char vrt_path[1024], tif_path[1024], vmem_path[256];
            snprintf(vrt_path, sizeof(vrt_path), "%s/step_%02d_%s.vrt", repdir, si+1, st.op.c_str());
            snprintf(tif_path, sizeof(tif_path), "%s/step_%02d_%s.tif", repdir, si+1, st.op.c_str());
            snprintf(vmem_path, sizeof(vmem_path), "%s/step_%02d.tif", vmem_prefix, si+1);

            // Build VRT
            StepResult sr;
            sr.op = st.op;
            sr.is_last = is_last;
            double tv0 = now_ms();

            if (st.op == "pixel_decimal") {
                g_IPF_DECIMAL = (int)pow(10.0, st.decimal);
                RasterMeta m = get_meta(in1.c_str());
                write_text(vrt_path, build_vrt_xml("pf_decimal", in1.c_str(), m, 0, m.dtype));
            }
            else if (st.op == "pixel_modify") {
                g_IPF_MODIFY_OLD = st.val_old;
                g_IPF_MODIFY_NEW = st.val_new;
                RasterMeta m = get_meta(in1.c_str());
                write_text(vrt_path, build_vrt_xml("pf_modify", in1.c_str(), m, 0, m.dtype));
            }
            else if (st.op == "dsmdem_diff") {
                std::string in2;
                auto it2 = datasets.find(st.input2);
                if (it2 != datasets.end()) in2 = it2->second;
                else in2 = st.input2;
                g_IPF_DSMDEM_IS_DSM = 1;
                g_IPF_THRESHOLD = st.threshold;
                g_IPF_FILLNODATA = st.fill_nodata;
                auto nd1 = nodatas.find(st.input);
                auto nd2 = nodatas.find(st.input2);
                g_IPF_DSM_NODATA = (nd1 != nodatas.end()) ? nd1->second : -9999.0;
                g_IPF_DEM_NODATA = (nd2 != nodatas.end()) ? nd2->second : -9999.0;
                RasterMeta m1 = get_meta(in1.c_str());
                RasterMeta m2 = get_meta(in2.c_str());
                write_text(vrt_path, build_vrt_xml_2src("pf_dsmdem_diff",
                    in1.c_str(), in2.c_str(), m1, m2));
            }
            else if (st.op == "roughness_5x5") {
                auto nd = nodatas.find(st.input);
                g_IPF_TERRAIN_NODATA = (nd != nodatas.end()) ? nd->second : -9999.0;
                g_IPF_TERRAIN_MIN_VALID = st.min_valid;
                RasterMeta m = get_meta(in1.c_str());
                // BufferRadius=0: GDAL 2.2.4 does not support BufferRadius for native
                // C++ pixel functions (only Python). Edge pixels get nodata, consistent
                // with the production ImageProcessFactory behavior.
                write_text(vrt_path, build_vrt_xml("pf_roughness5x5", in1.c_str(), m, 0, GDT_Float64));
            }
            else {
                throw std::runtime_error("Unknown op: " + st.op);
            }
            sr.vrt_construct_ms = now_ms() - tv0;

            // Materialize based on mode
            sr.materialize_ms = 0;
            sr.materialized = false;
            if (mode == "geotiff") {
                sr.materialize_ms = translate_to(vrt_path, tif_path);
                VSIUnlink(vrt_path);
                cur_path = tif_path;
                sr.materialized = true;
            } else if (mode == "vsimem") {
                sr.materialize_ms = translate_to(vrt_path, vmem_path);
                VSIUnlink(vrt_path);
                cur_path = vmem_path;
                vsimem_files.push_back(vmem_path);
                sr.materialized = true;
            } else { // vrt
                cur_path = vrt_path;
            }

            sr.output_bytes = file_size_bytes(cur_path.c_str());
            r.steps.push_back(sr);

            if (!is_last) {
                r.intermediate_bytes += sr.output_bytes;
                r.intermediate_count++;
            }
        }

        // Final materialization
        if (mode == "vrt") {
            char final_path[1024];
            snprintf(final_path, sizeof(final_path), "%s/final_output.tif", repdir);
            r.final_mat_ms = translate_to(cur_path.c_str(), final_path);
            r.final_bytes = file_size_bytes(final_path);
            r.checksum = get_checksum(final_path);
        } else if (mode == "vsimem") {
            // Final output to disk for checksum
            char final_path[1024];
            snprintf(final_path, sizeof(final_path), "%s/final_output.tif", repdir);
            r.final_mat_ms = translate_to(cur_path.c_str(), final_path);
            r.final_bytes = file_size_bytes(final_path);
            r.checksum = get_checksum(final_path);
        } else { // geotiff
            r.final_bytes = file_size_bytes(cur_path.c_str());
            r.checksum = get_checksum(cur_path.c_str());
            r.final_mat_ms = 0;
        }

        r.success = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }

    r.total_ms = now_ms() - t0;

    // Cleanup vsimem files
    for (const auto& vf : vsimem_files)
        VSIUnlink(vf.c_str());

    return r;
}

// ============================================================
// Statistics
// ============================================================
static double vec_mean(const std::vector<double>& v) {
    if (v.empty()) return 0;
    double s = 0; for (double x : v) s += x; return s / v.size();
}
static double vec_stdev(const std::vector<double>& v) {
    if (v.size() < 2) return 0;
    double m = vec_mean(v), s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return sqrt(s / (v.size() - 1));
}

// ============================================================
// Build mode summary JSON
// ============================================================
static njson build_mode_summary(const std::vector<RunResult>& runs) {
    njson j;
    j["run_count"] = (int)runs.size();
    std::vector<double> times, ibytes, fmat;
    int ok = 0;
    std::set<int> cks;
    for (const auto& r : runs) {
        if (!r.success) continue;
        ok++;
        times.push_back(r.total_ms);
        ibytes.push_back((double)r.intermediate_bytes);
        fmat.push_back(r.final_mat_ms);
        cks.insert(r.checksum);
    }
    j["success_count"] = ok;
    j["avg_total_elapsed_ms"] = vec_mean(times);
    j["std_total_elapsed_ms"] = vec_stdev(times);
    j["avg_intermediate_bytes"] = vec_mean(ibytes);
    j["avg_final_materialize_ms"] = vec_mean(fmat);
    j["consistent_checksum"] = (cks.size() == 1 && ok > 0);
    std::vector<int> ckv(cks.begin(), cks.end());
    j["checksums"] = ckv;
    return j;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config.json>\n", argv[0]);
        return 1;
    }

    GDALAllRegister();
    register_pixel_functions();

    // Parse config
    njson cfg = njson::parse(read_file(argv[1]));
    std::string outdir = cfg.value("output_root", "cpp_results");
    int default_reps = cfg.value("repetitions", 3);
    mkdirs(outdir);

    // Parse datasets
    std::map<std::string, std::string> datasets;
    std::map<std::string, double> nodatas;
    for (auto it = cfg["datasets"].begin(); it != cfg["datasets"].end(); ++it) {
        const std::string& key = it.key();
        const njson& val = it.value();
        if (val.is_object()) {
            datasets[key] = val["path"].get<std::string>();
            nodatas[key] = val.value("nodata", -9999.0);
        } else {
            datasets[key] = val.get<std::string>();
            nodatas[key] = -9999.0;
        }
    }

    // Parse cases
    std::vector<Case> cases;
    for (auto& jc : cfg["cases"]) {
        if (!jc.value("enabled", true)) continue;
        Case c;
        c.id = jc["id"].get<std::string>();
        c.workload = jc.value("workload", "unknown");
        c.reps = jc.value("repetitions", default_reps);
        for (auto& js : jc["steps"]) {
            if (!js.value("enabled", true)) continue;
            Step s;
            s.op = js["op"].get<std::string>();
            if (js.contains("inputs")) {
                s.input = js["inputs"][0].get<std::string>();
                if (js["inputs"].size() > 1) s.input2 = js["inputs"][1].get<std::string>();
            } else {
                s.input = js.value("input", "prev");
            }
            auto pm = js.value("params", njson::object());
            s.decimal = pm.value("decimal", 2);
            s.val_old = pm.value("value_old", 0.0);
            s.val_new = pm.value("value_new", 100.0);
            s.threshold = pm.value("threshold", 1.0);
            s.fill_nodata = pm.value("fill_nodata", true);
            s.min_valid = pm.value("min_valid_count", 9);
            c.steps.push_back(s);
        }
        cases.push_back(c);
    }

    printf("C++ VRT Pixel Function Benchmark (Three-Mode)\n");
    printf("==============================================\n");
    printf("GDAL version: %s\n", GDALVersionInfo("RELEASE_NAME"));
    printf("Config: %s\n", argv[1]);
    printf("Output: %s\n", outdir.c_str());
    printf("Cases: %d   Datasets: %d\n\n", (int)cases.size(), (int)datasets.size());

    for (auto it = datasets.begin(); it != datasets.end(); ++it)
        printf("  %s -> %s\n", it->first.c_str(), it->second.c_str());
    printf("\n");

    // Open CSV files
    std::string raw_csv_path = outdir + "/raw_runs.csv";
    FILE* raw_csv = fopen(raw_csv_path.c_str(), "w");
    fprintf(raw_csv, "case_id,workload,mode,repetition,success,"
        "total_elapsed_ms,intermediate_bytes,intermediate_count,"
        "final_output_bytes,final_checksum,final_materialize_ms,error\n");

    // JSON report
    njson report;
    report["config_path"] = std::string(argv[1]);
    report["generated_at"] = timestamp_str();
    report["output_root"] = outdir;
    report["repetitions"] = default_reps;
    report["runner_version"] = "cpp_r1_3mode";
    report["gdal_version"] = GDALVersionInfo("RELEASE_NAME");
    report["cases"] = njson::array();

    std::mt19937 rng(42);

    for (const Case& c : cases) {
        printf("[%s] %d reps, 3 modes ...\n", c.id.c_str(), c.reps);

        std::vector<RunResult> vrt_runs, gt_runs, vm_runs;

        for (int rep = 1; rep <= c.reps; ++rep) {
            std::vector<std::string> modes = {"vrt", "geotiff", "vsimem"};
            std::shuffle(modes.begin(), modes.end(), rng);

            for (const auto& mode : modes) {
                RunResult r = run_case_mode(c, datasets, nodatas, mode, rep, outdir);

                fprintf(raw_csv, "%s,%s,%s,%d,%s,%.3f,%lld,%d,%lld,%d,%.3f,%s\n",
                    r.case_id.c_str(), r.workload.c_str(), r.mode.c_str(), r.rep,
                    r.success ? "True" : "False",
                    r.total_ms, r.intermediate_bytes, r.intermediate_count,
                    r.final_bytes, r.checksum, r.final_mat_ms,
                    r.error.c_str());

                if (mode == "vrt") vrt_runs.push_back(r);
                else if (mode == "geotiff") gt_runs.push_back(r);
                else vm_runs.push_back(r);
            }
        }

        // Build case report
        njson cj;
        cj["id"] = c.id;
        cj["workload"] = c.workload;
        cj["repetitions_used"] = c.reps;

        // Build run arrays for JSON
        auto runs_to_json = [](const std::vector<RunResult>& runs) {
            njson arr = njson::array();
            for (const auto& r : runs) {
                njson rj;
                rj["case_id"] = r.case_id; rj["mode"] = r.mode;
                rj["repetition"] = r.rep; rj["success"] = r.success;
                rj["total_elapsed_ms"] = r.total_ms;
                rj["intermediate_bytes"] = r.intermediate_bytes;
                rj["final_output_bytes"] = r.final_bytes;
                rj["final_checksum"] = r.checksum;
                rj["final_materialize_ms"] = r.final_mat_ms;
                njson steps = njson::array();
                for (const auto& s : r.steps) {
                    njson sj;
                    sj["op"] = s.op;
                    sj["vrt_construct_ms"] = s.vrt_construct_ms;
                    sj["materialize_ms"] = s.materialize_ms;
                    sj["output_bytes"] = s.output_bytes;
                    sj["materialized"] = s.materialized;
                    sj["is_last"] = s.is_last;
                    steps.push_back(sj);
                }
                rj["steps"] = steps;
                if (!r.error.empty()) rj["error"] = r.error;
                arr.push_back(rj);
            }
            return arr;
        };

        cj["vrt"] = { {"runs", runs_to_json(vrt_runs)}, {"summary", build_mode_summary(vrt_runs)} };
        cj["geotiff"] = { {"runs", runs_to_json(gt_runs)}, {"summary", build_mode_summary(gt_runs)} };
        cj["vsimem"] = { {"runs", runs_to_json(vm_runs)}, {"summary", build_mode_summary(vm_runs)} };

        // Comparison
        double vt = cj["vrt"]["summary"]["avg_total_elapsed_ms"].get<double>();
        double gt = cj["geotiff"]["summary"]["avg_total_elapsed_ms"].get<double>();
        double vm = cj["vsimem"]["summary"]["avg_total_elapsed_ms"].get<double>();
        double vi = cj["vrt"]["summary"]["avg_intermediate_bytes"].get<double>();
        double gi = cj["geotiff"]["summary"]["avg_intermediate_bytes"].get<double>();

        njson comp;
        comp["intermediate_byte_reduction_pct"] = gi > 0 ? (gi - vi) / gi * 100.0 : 0;
        comp["runtime_reduction_pct"] = gt > 0 ? (gt - vt) / gt * 100.0 : 0;
        comp["io_overhead_ms"] = gt - vm;
        comp["io_overhead_pct"] = gt > 0 ? (gt - vm) / gt * 100.0 : 0;
        comp["vrt_io_avoidance_pct"] = (gt - vm) > 0 ? (gt - vt) / (gt - vm) * 100.0 : 0;
        comp["checksum_vrt_geotiff"] =
            (!vrt_runs.empty() && !gt_runs.empty() &&
             vrt_runs[0].success && gt_runs[0].success &&
             vrt_runs[0].checksum == gt_runs[0].checksum);
        comp["checksum_geotiff_vsimem"] =
            (!gt_runs.empty() && !vm_runs.empty() &&
             gt_runs[0].success && vm_runs[0].success &&
             gt_runs[0].checksum == vm_runs[0].checksum);
        cj["comparison"] = comp;

        // Timing decomposition (from geotiff mode)
        njson td;
        td["geotiff_total_ms"] = gt;
        td["vsimem_total_ms"] = vm;
        td["vrt_total_ms"] = vt;
        td["io_overhead_ms"] = gt - vm;
        td["io_overhead_pct"] = gt > 0 ? (gt - vm) / gt * 100.0 : 0;

        // Per-step materialization averages from geotiff runs
        std::vector<RunResult> ok_gt;
        for (const auto& r : gt_runs) if (r.success) ok_gt.push_back(r);
        if (!ok_gt.empty()) {
            double total_mat = 0;
            njson td_steps = njson::array();
            for (int si = 0; si < (int)ok_gt[0].steps.size(); ++si) {
                std::vector<double> sv, sm;
                for (const auto& r : ok_gt) {
                    sv.push_back(r.steps[si].vrt_construct_ms);
                    sm.push_back(r.steps[si].materialize_ms);
                }
                double avg_mat = vec_mean(sm);
                total_mat += avg_mat;
                njson sj;
                sj["step"] = si + 1;
                sj["op"] = ok_gt[0].steps[si].op;
                sj["avg_vrt_construct_ms"] = vec_mean(sv);
                sj["avg_materialize_ms"] = avg_mat;
                td_steps.push_back(sj);
            }
            td["total_materialize_ms"] = total_mat;
            td["materialize_pct"] = gt > 0 ? total_mat / gt * 100.0 : 0;
            td["vrt_final_materialize_ms"] = cj["vrt"]["summary"]["avg_final_materialize_ms"];
            td["steps"] = td_steps;
        }
        cj["timing_decomposition"] = td;
        report["cases"].push_back(cj);

        // Progress output
        double rt_red = comp["runtime_reduction_pct"].get<double>();
        double io_pct = td.value("io_overhead_pct", 0.0);
        printf("  VRT=%.0fms  GeoTIFF=%.0fms  vsimem=%.0fms  "
               "rt_red=%.1f%%  io_overhead=%.1f%%\n",
               vt, gt, vm, rt_red, io_pct);
    }

    fclose(raw_csv);

    // Write JSON report
    std::string rpt_path = outdir + "/benchmark_report_r1.json";
    std::ofstream rpt_file(rpt_path);
    rpt_file << report.dump(2);
    rpt_file.close();

    // Write case_summary.csv
    std::string sum_path = outdir + "/case_summary.csv";
    FILE* sum_csv = fopen(sum_path.c_str(), "w");
    fprintf(sum_csv, "case_id,workload,repetitions,"
        "vrt_avg_total_elapsed_ms,vrt_std_total_elapsed_ms,"
        "geotiff_avg_total_elapsed_ms,geotiff_std_total_elapsed_ms,"
        "vsimem_avg_total_elapsed_ms,vsimem_std_total_elapsed_ms,"
        "vrt_avg_intermediate_bytes,geotiff_avg_intermediate_bytes,"
        "checksum_vrt_geotiff,checksum_geotiff_vsimem,"
        "intermediate_byte_reduction_pct,runtime_reduction_pct,"
        "io_overhead_pct,vrt_io_avoidance_pct\n");
    for (const auto& cj : report["cases"]) {
        auto vs = cj["vrt"]["summary"];
        auto gs = cj["geotiff"]["summary"];
        auto ms = cj["vsimem"]["summary"];
        auto cp = cj["comparison"];
        fprintf(sum_csv, "%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%s,%s,%.3f,%.3f,%.3f,%.3f\n",
            cj["id"].get<std::string>().c_str(),
            cj["workload"].get<std::string>().c_str(),
            cj["repetitions_used"].get<int>(),
            vs["avg_total_elapsed_ms"].get<double>(), vs["std_total_elapsed_ms"].get<double>(),
            gs["avg_total_elapsed_ms"].get<double>(), gs["std_total_elapsed_ms"].get<double>(),
            ms["avg_total_elapsed_ms"].get<double>(), ms["std_total_elapsed_ms"].get<double>(),
            vs["avg_intermediate_bytes"].get<double>(), gs["avg_intermediate_bytes"].get<double>(),
            cp["checksum_vrt_geotiff"].get<bool>() ? "True" : "False",
            cp["checksum_geotiff_vsimem"].get<bool>() ? "True" : "False",
            cp["intermediate_byte_reduction_pct"].get<double>(),
            cp["runtime_reduction_pct"].get<double>(),
            cp["io_overhead_pct"].get<double>(),
            cp["vrt_io_avoidance_pct"].get<double>());
    }
    fclose(sum_csv);

    // Write timing_decomposition.csv
    std::string td_path = outdir + "/timing_decomposition.csv";
    FILE* td_csv = fopen(td_path.c_str(), "w");
    fprintf(td_csv, "case_id,geotiff_total_ms,vsimem_total_ms,vrt_total_ms,"
        "io_overhead_ms,io_overhead_pct,total_materialize_ms,materialize_pct,"
        "vrt_final_materialize_ms\n");
    for (const auto& cj : report["cases"]) {
        auto td = cj["timing_decomposition"];
        fprintf(td_csv, "%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
            cj["id"].get<std::string>().c_str(),
            td.value("geotiff_total_ms", 0.0),
            td.value("vsimem_total_ms", 0.0),
            td.value("vrt_total_ms", 0.0),
            td.value("io_overhead_ms", 0.0),
            td.value("io_overhead_pct", 0.0),
            td.value("total_materialize_ms", 0.0),
            td.value("materialize_pct", 0.0),
            td.value("vrt_final_materialize_ms", 0.0));
    }
    fclose(td_csv);

    printf("\nDone. Results in %s\n", outdir.c_str());
    return 0;
}
