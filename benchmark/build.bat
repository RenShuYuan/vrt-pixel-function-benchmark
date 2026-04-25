@echo off
REM ============================================================
REM Build script for VRT Pixel Function Benchmark (C++)
REM
REM Requires:
REM   - Visual Studio 2022 (or 2019) C++ Build Tools
REM   - GDAL 2.2.4 development install (include/, lib/, bin/)
REM
REM Configure GDAL location via environment variable:
REM   set GDAL_ROOT=C:\OSGeo4W64
REM   (or set GDAL_ROOT=C:\path\to\your\gdal\install)
REM
REM Then run:
REM   build.bat
REM ============================================================

if "%GDAL_ROOT%"=="" (
    echo ERROR: GDAL_ROOT environment variable is not set.
    echo Example: set GDAL_ROOT=C:\OSGeo4W64
    exit /b 1
)

if not exist "%GDAL_ROOT%\include\gdal.h" (
    echo ERROR: gdal.h not found at %%GDAL_ROOT%%\include\gdal.h
    echo Verify GDAL_ROOT points to a directory containing include\ and lib\ subdirectories.
    exit /b 1
)

REM --- Step 1: Set up MSVC environment ---
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if %errorlevel% neq 0 (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
)
if %errorlevel% neq 0 (
    echo ERROR: Cannot find Visual Studio Build Tools.
    echo Install "Desktop development with C++" via the Visual Studio Installer.
    exit /b 1
)

REM --- Step 2: Compile ---
echo Compiling benchmark_cpp.cpp against GDAL at %GDAL_ROOT% ...
cl /EHsc /O2 /W3 /nologo ^
    /I "%GDAL_ROOT%\include" ^
    benchmark_cpp.cpp ^
    /link /LIBPATH:"%GDAL_ROOT%\lib" gdal_i.lib ^
    /OUT:benchmark_cpp.exe

if %errorlevel% neq 0 (
    echo Compilation failed.
    exit /b 1
)

echo.
echo Build successful: benchmark_cpp.exe
echo.
echo Usage:
echo   set PATH=%%GDAL_ROOT%%\bin;%%PATH%%
echo   benchmark_cpp.exe configs\r1_mechanism_cpp.json
