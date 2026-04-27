@echo off
REM KADD Dynamic Library Build Script (Windows)
REM Purpose: Build all 4 dynamic libraries in one click
REM
REM Dependencies:
REM   - ONNX Runtime (required, for ML engine)
REM   - cJSON (optional, for SQLi engine and DFA Pro)
REM   - Google Test (optional, for testing only)
REM   - CMake 3.15+ (required)
REM   - MinGW (required)
REM
REM Dependency Lookup Strategy:
REM   1. Try system libraries first
REM   2. Fallback to third_party directory
REM   3. Use -DUSE_SYSTEM_LIBS=OFF to force third_party only
REM
REM For installation guide, see: docs/INSTALL.md

setlocal enabledelayedexpansion

REM Change to script directory
cd /d "%~dp0"

echo ==========================================
echo   KADD Dynamic Library Build Script
echo ==========================================
echo.
echo Current directory: %CD%
echo.

echo [SYSTEM] Dependency Lookup Strategy:
echo [INFO]   1. Try system libraries first
echo [INFO]   2. Fallback to third_party directory
echo [INFO]   3. See: cmake -DUSE_SYSTEM_LIBS=ON/OFF
echo.
echo [INFO] Dependencies:
echo [INFO]   Build Basic Engines (DFA/Feature): No external deps
echo [INFO]   Build ML Engine: ONNX Runtime
echo [INFO]   Build SQLi Engine: ONNX Runtime + cJSON
echo [INFO]   Run Tests: ONNX Runtime + cJSON + Google Test
echo [INFO]   Installation guide: docs\INSTALL.md
echo.

REM Check build directory
echo [SYSTEM] 1. Checking dependencies...
echo.
if not exist "build" (
    echo [INFO] Build directory does not exist, will create it
)

REM Check ONNX Runtime
echo [CHECK] Checking ONNX Runtime...
where onnxruntime.dll >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK]   System ONNX Runtime found
    set USE_SYSTEM_ONNX=1
) else if exist "third_party\lib\onnxruntime.dll" (
    echo [OK]   third_party ONNX Runtime found
    set USE_SYSTEM_ONNX=0
    echo [WARN] Will use third_party ONNX Runtime
) else (
    echo [ERROR] ONNX Runtime not found!
    echo [INFO] Please install ONNX Runtime or place it in third_party\lib\
    echo [INFO] See docs\INSTALL.md for installation guide
    pause
    exit /b 1
)

REM Check cJSON
echo.
echo [CHECK] Checking cJSON...
if exist "C:\Program Files\cJSON\include\cJSON.h" (
    echo [OK]   System cJSON found
    set USE_SYSTEM_CJSON=1
) else if exist "C:\msys64\mingw64\include\cJSON.h" (
    echo [OK]   System cJSON found (MSYS2)
    set USE_SYSTEM_CJSON=1
) else if exist "third_party\include\cJSON.h" (
    echo [OK]   third_party cJSON found
    set USE_SYSTEM_CJSON=0
    echo [WARN] Will use third_party cJSON
) else (
    echo [WARN] cJSON not found! SQLi engine will not be built
    set USE_SYSTEM_CJSON=0
)

echo.
echo [INFO] Dependency check completed
echo.

REM Clean old DLLs
echo [SYSTEM] 2. Cleaning old DLLs...
if exist build\src\*.dll (
    del /Q build\src\*.dll 2>nul
    echo [OK]   Old DLLs deleted
) else (
    echo [INFO] No old DLLs to clean
)
echo.

REM Configure CMake
echo [SYSTEM] 3. Configuring CMake...
cmake -H. -Bbuild -G "MinGW Makefiles"
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed
    pause
    exit /b 1
)
echo [OK]   CMake configuration completed
echo.

REM Build dynamic libraries
echo [SYSTEM] 4. Building dynamic libraries...
echo.

echo [INFO]   [1/4] Building DFA engine DLL...
mingw32-make -C build kadd_dfa_engine_dll
if exist build\src\libkadd_dfa_engine.dll (
    echo [OK]     libkadd_dfa_engine.dll built successfully
) else (
    echo [FAIL]   libkadd_dfa_engine.dll build failed
)
echo.

echo [INFO]   [2/4] Building Feature engine DLL...
mingw32-make -C build kadd_feature_engine_dll
if exist build\src\libkadd_feature_engine.dll (
    echo [OK]     libkadd_feature_engine.dll built successfully
) else (
    echo [FAIL]   libkadd_feature_engine.dll build failed
)
echo.

echo [INFO]   [3/4] Building ML engine DLL...
mingw32-make -C build kadd_ml_engine_dll
if exist build\src\libkadd_ml_engine.dll (
    echo [OK]     libkadd_ml_engine.dll built successfully
) else (
    echo [FAIL]   libkadd_ml_engine.dll build failed
)
echo.

if exist "third_party\include\cJSON.h" (
    echo [INFO]   [4/4] Building SQLi engine DLL...
    mingw32-make -C build kadd_sqli_engine_dll
    if exist build\src\libkadd_sqli_engine.dll (
        echo [OK]     libkadd_sqli_engine.dll built successfully
    ) else (
        echo [WARN]   libkadd_sqli_engine.dll build failed
    )
) else (
    echo [INFO]   [4/4] Skipping SQLi engine (cJSON not available)
)
echo.

REM Count results
echo [SYSTEM] 5. Build result statistics...
set DLL_COUNT=0
if exist build\src\libkadd_dfa_engine.dll set /a DLL_COUNT+=1
if exist build\src\libkadd_feature_engine.dll set /a DLL_COUNT+=1
if exist build\src\libkadd_ml_engine.dll set /a DLL_COUNT+=1
if exist build\src\libkadd_sqli_engine.dll set /a DLL_COUNT+=1

echo.
echo ==========================================
echo   Build Complete!
echo ==========================================
echo [SYSTEM] Platform: Windows
echo [INFO]   Total DLLs: %DLL_COUNT%
echo [INFO]   Output directory: build\src\
echo.
echo Generated DLLs:
dir /B build\src\*.dll 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo   (No DLL files found)
)
echo.
echo Usage:
echo   1. Copy DLLs to your project directory
echo   2. Link with -L<path\to\dll> -l<libname>
echo.
echo Integration Notes:
if %USE_SYSTEM_ONNX%==1 (
    echo [INFO]   ONNX Runtime: System library
) else (
    echo [INFO]   ONNX Runtime: third_party (copy onnxruntime.dll with your app)
)
if %USE_SYSTEM_CJSON%==1 (
    echo [INFO]   cJSON: System library
) else if exist "third_party\include\cJSON.h" (
    echo [INFO]   cJSON: third_party (statically linked)
) else (
    echo [INFO]   cJSON: Not used
)
echo [INFO]   Windows: Copy DLLs to same directory as your executable
echo [INFO]            Include onnxruntime.dll if using third_party
echo ==========================================

pause
