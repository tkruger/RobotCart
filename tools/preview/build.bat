@echo off
rem Builds sim.exe with MSVC. Run from a normal cmd prompt; it sets up the
rem compiler environment itself. Set GFX_DIR if your Arduino libraries live
rem somewhere other than Documents\Arduino\libraries.
setlocal
cd /d "%~dp0"

if "%GFX_DIR%"=="" set "GFX_DIR=%USERPROFILE%\Documents\Arduino\libraries\Adafruit_GFX_Library"

rem Find and load the Visual C++ compiler if it isn't already on the PATH.
rem (No if-blocks here: the ")" in "Program Files (x86)" breaks them.)
where cl >nul 2>nul
if not errorlevel 1 goto build
rem (Spelled out rather than %ProgramFiles(x86)%, which Git Bash doesn't pass on.)
set "VSWHERE=%SystemDrive%\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul

:build
if not exist build mkdir build
cl /nologo /DARDUINO=10819 /EHsc /O2 /std:c++17 /w /Imock /I"%GFX_DIR%" sim.cpp "%GFX_DIR%\Adafruit_GFX.cpp" /Fobuild\ /Febuild\sim.exe
