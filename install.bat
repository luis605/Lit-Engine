@echo off
setlocal EnableDelayedExpansion

:: Set Python version
set PYTHON_VERSION=3.11.0

:: Set download URL
set DOWNLOAD_URL=https://www.python.org/ftp/python/%PYTHON_VERSION%/python-%PYTHON_VERSION%-amd64.exe

:: Set temporary file names
set TEMP_EXE=python_installer.exe

:: Download Python installer
echo Downloading Python installer...
curl -o %TEMP_EXE% %DOWNLOAD_URL%

:: Run Python installer with admin privileges
echo Installing Python...
powershell Start-Process -FilePath "%TEMP_EXE%" -ArgumentList "/quiet", "/passive", "/norestart" -Verb RunAs

:: Cleanup temporary files
echo Cleaning up...
del %TEMP_EXE%

echo Python %PYTHON_VERSION% has been installed on your system.

:end

