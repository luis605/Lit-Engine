@echo off
if not exist "bin" md "bin"

echo Please ensure the path to this directory contains no spaces, otherwise ffmpeg will fail to build. Please use a directory without spaces in the name.

powershell -Command "Invoke-WebRequest -Uri https://github.com/Kitware/CMake/releases/download/v3.30.0/cmake-3.30.0-windows-x86_64.msi -OutFile bin/cmake.msi"
powershell -Command "Start-Process .\bin\cmake.msi"

curl -L -o "bin/mingw.zip" "https://github.com/mstorsjo/llvm-mingw/releases/download/20240805/llvm-mingw-20240805-msvcrt-x86_64.zip"
powershell -Command "Expand-Archive -Force bin/mingw.zip bin"
start "" "bin\llvm-mingw-20240805-msvcrt-x86_64\bin"

powershell -Command "Add-Type -assembly System.Windows.Forms; $main_form = New-Object System.Windows.Forms.Form; $main_form.Text ='GUI for my PowerShell script'; $Label = New-Object System.Windows.Forms.Label; $Label.Text = 'To add mingw to PATH, open the start menu, type `system variables`, and press Enter. Click `Environment Variables`. In the User section`s top table double click in `Path` to open Path`s window. We also opened a file explorer window inside mingw`s bin folder for you, please copy the full path, get back to the environment path window, click `New,` paste the copied path, and press Enter. Finally, click `OK` to exit and save all changes in all the windows, then restart the current shell and/or editor.'; $Label.AutoSize = $true; $Label.MaximumSize = New-Object System.Drawing.Size(580, 0); $Label.Location = New-Object System.Drawing.Point(0,10); $Label.AutoSize = $true; $main_form.Controls.Add($Label); $main_form.AutoSize = $true; $main_form.ShowDialog();"

setlocal

if exist .\bin\vcpkg (
    rd /s /q .\bin\vcpkg
)

set VCPKG_PATH=./bin/vcpkg

echo Cloning vcpkg repository...
git clone https://github.com/Microsoft/vcpkg.git %VCPKG_PATH%

echo Changing directory to vcpkg...
cd %VCPKG_PATH%

echo Bootstrapping vcpkg...
call bootstrap-vcpkg.bat

echo Integrating vcpkg...
.\vcpkg integrate install

echo Installing Bullet3...
.\vcpkg install bullet3 --triplet=x64-mingw-static --host-triplet=x64-mingw-static

echo Installing FFmpeg
.\vcpkg install ffmpeg --triplet=x64-mingw-static --host-triplet=x64-mingw-static

endlocal

cd ../..