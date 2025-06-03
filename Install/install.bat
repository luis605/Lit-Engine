@echo off
setlocal

set SCRIPT_DIR=%~dp0
set VCPKG_ROOT=%SCRIPT_DIR%bin\vcpkg
set VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe
set VCPKG_BOOTSTRAP_SCRIPT=%VCPKG_ROOT%\bootstrap-vcpkg.bat
set VCPKG_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

echo Current script directory: %SCRIPT_DIR%
echo Vcpkg root will be: %VCPKG_ROOT%

if not exist "%SCRIPT_DIR%bin" md "%SCRIPT_DIR%bin"

:: Check for Git
git --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Git is not installed or not in PATH. Please install Git and add it to your PATH.
    powershell -Command "Write-Host 'You can download Git from https://git-scm.com/download/win' -ForegroundColor Yellow"
    goto :eof
)

:: CMake Installation (Optional, user might have it)
cmake --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo CMake is not found. Attempting to download and install...
    powershell -Command "Invoke-WebRequest -Uri https://github.com/Kitware/CMake/releases/download/v3.29.3/cmake-3.29.3-windows-x86_64.msi -OutFile %SCRIPT_DIR%bin\cmake.msi"
    echo Starting CMake installer... Please follow the prompts.
    echo Ensure CMake is added to your PATH during installation.
    start /wait "" "%SCRIPT_DIR%bin\cmake.msi"
    cmake --version >nul 2>&1
    if %ERRORLEVEL% neq 0 (
        echo CMake installation failed or was cancelled. Please install CMake manually and ensure it's in PATH.
        goto :eof
    )
    echo CMake installed.
) else (
    echo CMake found.
)

:: Vcpkg Installation and Setup
if not exist "%VCPKG_BOOTSTRAP_SCRIPT%" (
    echo "Cloning vcpkg repository to %VCPKG_ROOT%..."
    git clone https://github.com/Microsoft/vcpkg.git "%VCPKG_ROOT%"
    if %ERRORLEVEL% neq 0 (
        echo "Failed to clone vcpkg. Please check your internet connection and Git setup."
        goto :eof
    )
) else (
    echo "Vcpkg directory already exists. Skipping clone."
)

if not exist "%VCPKG_EXE%" (
    echo "Bootstrapping vcpkg..."
    call "%VCPKG_BOOTSTRAP_SCRIPT%" -disableMetrics
    if %ERRORLEVEL% neq 0 (
        echo "Failed to bootstrap vcpkg."
        goto :eof
    )
) else (
    echo "Vcpkg already bootstrapped."
)

echo "Installing dependencies via vcpkg..."
echo "This may take a very long time."

:: Install for MSVC (static linkage)
echo "Installing libraries for MSVC (x64-windows-static)..."
"%VCPKG_EXE%" install bullet3:x64-windows-static --recurse
"%VCPKG_EXE%" install ffmpeg:x64-windows-static --recurse  --allow-unsupported


echo.
echo --- Installation Complete ---
echo ---   Please run cmake.   ---

endlocal