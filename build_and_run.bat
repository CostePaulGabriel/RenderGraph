@echo off
rem Configures, builds and runs the prototype with the windows-msvc-debug preset.
rem   build_and_run.bat            build, run tests, start the sample
rem   build_and_run.bat 600        same, but the sample exits after 600 frames
rem Output is mirrored to build_log.txt.

setlocal
cd /d "%~dp0"
set "LOG=%~dp0build_log.txt"
set "PRESET=windows-msvc-debug"
echo [%date% %time%] build started > "%LOG%"

where cl >nul 2>nul
if not errorlevel 1 goto :have_env

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :no_vs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_vs
rem VsDevCmd changes the current directory unless VSCMD_START_DIR is set.
set "VSCMD_START_DIR=%~dp0"
echo Setting up the Visual Studio environment...
rem Not redirected to the log: VsDevCmd can leave a child process holding the file open.
call "%VSPATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul 2>&1
cd /d "%~dp0"

:have_env
if not defined VCPKG_ROOT goto :no_vcpkg
echo VCPKG_ROOT=%VCPKG_ROOT% >> "%LOG%"

echo --- configure >> "%LOG%"
echo Configuring (first run downloads NVRHI + vcpkg packages, this takes a while)...
cmake --preset %PRESET% >> "%LOG%" 2>&1
if errorlevel 1 goto :fail

echo --- build >> "%LOG%"
echo Building...
cmake --build --preset %PRESET% >> "%LOG%" 2>&1
if errorlevel 1 goto :fail

echo --- tests >> "%LOG%"
"build\%PRESET%\bin\RenderGraphTests.exe" >> "%LOG%" 2>&1
if errorlevel 1 goto :fail

echo --- run >> "%LOG%"
pushd "build\%PRESET%\bin"
if "%~1"=="" (
    .\RenderGraphPrototype.exe >> "%LOG%" 2>&1
) else (
    .\RenderGraphPrototype.exe --frames %~1 >> "%LOG%" 2>&1
)
set "EXITCODE=%errorlevel%"
popd
echo --- exit code %EXITCODE% >> "%LOG%"
echo [%date% %time%] done >> "%LOG%"
exit /b %EXITCODE%

:no_vs
echo Visual Studio 2022 with the C++ workload was not found. >> "%LOG%"
exit /b 1

:no_vcpkg
echo VCPKG_ROOT is not set. Install vcpkg (or the VS vcpkg component) and set VCPKG_ROOT. >> "%LOG%"
exit /b 1

:fail
echo --- FAILED >> "%LOG%"
echo FAILED, see build_log.txt
exit /b 1
