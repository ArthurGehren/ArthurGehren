@echo off
REM ============================================================
REM Build gbhv.sys using Visual Studio + WDK
REM Run from a "Developer Command Prompt" or "x64 Native Tools"
REM ============================================================

echo Building GBHV driver...

REM Try VS 2022 first, then 2019
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat"
)

pushd ..\gbhv
msbuild gbhv.vcxproj /p:Configuration=Release /p:Platform=x64
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed. Make sure you have:
    echo   - Visual Studio with C++ workload
    echo   - Windows Driver Kit (WDK) installed
    echo   - Running from a Developer Command Prompt
    pause
    exit /b 1
)
popd

echo.
echo [OK] Driver built successfully.
echo Copy the .sys file from gbhv\x64\Release\ to this tools\ folder
echo before running setup.exe.
pause
