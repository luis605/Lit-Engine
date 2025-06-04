@echo off
setlocal

REM ===================================================================
REM Common Configuration
REM ===================================================================
SET "CMAKE_GENERATOR=Visual Studio 17 2022"
SET "CMAKE_GENERATOR_PLATFORM=x64"
SET "SCRIPT_DIR=%~dp0"
REM LLVM_MINGW_BIN_PATH is not used for MSVC builds, so it's removed.

REM Resolve project root (assuming this script is in a 'scripts' subdirectory)
cd /D "%SCRIPT_DIR%"
SET "PROJECT_ROOT=%SCRIPT_DIR%.."
for %%i in ("%PROJECT_ROOT%") do SET "PROJECT_ROOT=%%~fi"

echo "Building dependencies with MSVC (Release, Static Runtime /MT)..."
echo "CMake Generator: %CMAKE_GENERATOR%"
echo "CMake Platform: %CMAKE_GENERATOR_PLATFORM%"
echo "Script Directory: %SCRIPT_DIR%"
echo "Resolved Project Root: %PROJECT_ROOT%"

SET CMAKE_RELEASE_MT_FLAGS=-G "%CMAKE_GENERATOR%" -A %CMAKE_GENERATOR_PLATFORM% -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DBUILD_SHARED_LIBS=OFF

echo "Build command: %CMAKE_RELEASE_MT_FLAGS%"

REM Navigate to project root
cd /D "%PROJECT_ROOT%"
echo "Current Directory: %CD%"
if not "%CD%\"=="%PROJECT_ROOT%\" (
    echo "Failed to change directory to project root: %PROJECT_ROOT%"
    echo "Current directory is: %CD%"
    goto :error_exit
)

REM ===================================================================
REM Build NodeEditor (Release, /MT)
REM ===================================================================
echo.
echo "Building NodeEditor..."
SET "NODEEDITOR_DIR=%PROJECT_ROOT%\Include\NodeEditor"
SET "NODEEDITOR_BUILD_DIR=%NODEEDITOR_DIR%\build_release_mt"

IF NOT EXIST "%NODEEDITOR_DIR%\examples\CMakeLists.txt" (
    echo "NodeEditor examples CMakeLists.txt not found at %NODEEDITOR_DIR%\examples"
    goto :error_exit
)
cd "%NODEEDITOR_DIR%"
IF EXIST "%NODEEDITOR_BUILD_DIR%" (
    echo "Cleaning previous NodeEditor build directory..."
    rmdir /S /Q "%NODEEDITOR_BUILD_DIR%"
)
mkdir "%NODEEDITOR_BUILD_DIR%"
cd "%NODEEDITOR_BUILD_DIR%"

echo "Configuring NodeEditor..."
cmake ..\examples %CMAKE_RELEASE_MT_FLAGS%
IF %ERRORLEVEL% NEQ 0 ( echo "CMake configure for NodeEditor failed." & goto :error_exit )

echo Building NodeEditor...
cmake --build . --config Release
IF %ERRORLEVEL% NEQ 0 ( echo "CMake build for NodeEditor failed." & goto :error_exit )
cd "%PROJECT_ROOT%"

REM ===================================================================
REM Build Raylib (Release, /MT)
REM ===================================================================
echo.
echo Building Raylib...
SET "RAYLIB_SOURCE_DIR=%PROJECT_ROOT%\Include\raylib"
SET "RAYLIB_BUILD_DIR=%RAYLIB_SOURCE_DIR%\build_release_mt"

IF NOT EXIST "%RAYLIB_SOURCE_DIR%\CMakeLists.txt" (
    echo "ERROR: raylib CMakeLists.txt not found in %RAYLIB_SOURCE_DIR%"
    goto :error_exit
)

cd "%RAYLIB_SOURCE_DIR%"
IF EXIST "%RAYLIB_BUILD_DIR%" (
    echo "Removing previous Raylib build directory %RAYLIB_BUILD_DIR%"...
    rmdir /S /Q "%RAYLIB_BUILD_DIR%"
)
echo Creating Raylib build directory "%RAYLIB_BUILD_DIR%"...
mkdir "%RAYLIB_BUILD_DIR%"
IF %ERRORLEVEL% NEQ 0 (
    echo "ERROR: Failed to create %RAYLIB_BUILD_DIR%"
    goto :error_exit
)
cd "%RAYLIB_BUILD_DIR%"

echo "Configuring Raylib for MSVC (Static Release)..."
cmake .. %CMAKE_RELEASE_MT_FLAGS% ^
  -DBUILD_EXAMPLES=OFF ^
  -DUSE_MSVC_RUNTIME_LIBRARY_DLL=OFF ^
  -DBUILD_SHARED_LIBS=OFF

IF %ERRORLEVEL% NEQ 0 (
    echo "ERROR: CMake configuration for Raylib failed."
    goto :error_exit
)

echo "Building Raylib (Release x64, /MT)..."
cmake --build . --config Release -- /m
IF %ERRORLEVEL% NEQ 0 (
    echo "ERROR: Raylib build failed."
    goto :error_exit
)

echo "Raylib built successfully!"
echo "Output library expected at: %RAYLIB_BUILD_DIR%\raylib\Release\raylib.lib (or similar, check Raylib's output structure)"
echo.
cd "%PROJECT_ROOT%"

REM ===================================================================
REM Build meshoptimizer (Release, /MT)
REM ===================================================================
echo.
echo "Building meshoptimizer..."
SET "MESHOPTIMIZER_DIR=%PROJECT_ROOT%\Include\meshoptimizer"
SET "MESHOPTIMIZER_BUILD_DIR=%MESHOPTIMIZER_DIR%\build_release_mt"

IF NOT EXIST "%MESHOPTIMIZER_DIR%\CMakeLists.txt" (
    echo "meshoptimizer CMakeLists.txt not found at %MESHOPTIMIZER_DIR%"
    goto :error_exit
)
cd "%MESHOPTIMIZER_DIR%"
IF EXIST "%MESHOPTIMIZER_BUILD_DIR%" (
    echo "Cleaning previous meshoptimizer build directory..."
    rmdir /S /Q "%MESHOPTIMIZER_BUILD_DIR%"
)
mkdir "%MESHOPTIMIZER_BUILD_DIR%"
cd "%MESHOPTIMIZER_BUILD_DIR%"

echo "Configuring meshoptimizer..."
cmake .. %CMAKE_RELEASE_MT_FLAGS%
IF %ERRORLEVEL% NEQ 0 ( echo "CMake configure for meshoptimizer failed." & goto :error_exit )

echo "Building meshoptimizer..."
cmake --build . --config Release
IF %ERRORLEVEL% NEQ 0 ( echo CMake build for meshoptimizer failed. & goto :error_exit )
cd "%PROJECT_ROOT%"

REM ===================================================================
REM Build squish (Release, /MT)
REM ===================================================================
echo.
echo "Building squish..."
SET "SQUISH_DIR=%PROJECT_ROOT%\Include\squish"
SET "SQUISH_BUILD_DIR=%SQUISH_DIR%\build_release_mt"

IF NOT EXIST "%SQUISH_DIR%\CMakeLists.txt" (
    echo "squish CMakeLists.txt not found at %SQUISH_DIR%"
    goto :error_exit
)
cd "%SQUISH_DIR%"
IF EXIST "%SQUISH_BUILD_DIR%" (
    echo "Cleaning previous squish build directory..."
    rmdir /S /Q "%SQUISH_BUILD_DIR%"
)
mkdir "%SQUISH_BUILD_DIR%"
cd "%SQUISH_BUILD_DIR%"

echo "Configuring squish..."
cmake .. %CMAKE_RELEASE_MT_FLAGS%
IF %ERRORLEVEL% NEQ 0 ( echo "CMake configure for squish failed." & goto :error_exit )

echo Building squish...
cmake --build . --config Release
IF %ERRORLEVEL% NEQ 0 ( echo "CMake build for squish failed." & goto :error_exit )
cd "%PROJECT_ROOT%"

REM ===================================================================
REM Finished
REM ===================================================================
echo.
echo All specified dependencies built successfully for MSVC (Release, /MT).
echo.
echo IMPORTANT: Update your main CMakeLists.txt to find libraries in the
echo 'build_release_mt' subdirectories, for example:
echo   NodeEditor: (You are compiling sources directly, no .lib usually)
echo   Raylib:     %PROJECT_ROOT%\Include\raylib\build_release_mt\raylib\Release\raylib.lib
echo   Meshopt:    %PROJECT_ROOT%\Include\meshoptimizer\build_release_mt\Release\meshoptimizer.lib
echo   Squish:     %PROJECT_ROOT%\Include\squish\build_release_mt\Release\squish.lib
echo (Note: The exact path for raylib.lib inside its build dir might vary slightly depending on its CMake setup)
echo.
echo If runtime library mismatches persist for a dependency (e.g., squish),
echo its own CMakeLists.txt might be overriding CMAKE_MSVC_RUNTIME_LIBRARY.
echo You may need to investigate its CMake file for specific options to force /MT
echo or patch its CMakeLists.txt directly.
echo Use 'dumpbin /DIRECTIVES library.lib' to check how a .lib was compiled.
echo Look for '/DEFAULTLIB:"LIBCMT"' for /MT Release.

endlocal
goto :eof

:error_exit
echo.
echo ======================================================
echo SCRIPT FINISHED WITH ERRORS.
echo ======================================================
endlocal
exit /B 1