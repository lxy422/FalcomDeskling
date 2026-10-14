@echo off
rem ============================================================
rem  FalcomDeskling build script
rem  usage:  build.cmd          Release (/O2 /MT, static CRT)
rem          build.cmd debug    Debug   (/Od /Zi /MTd)
rem  NOTE: keep this file ASCII-only. cmd.exe reads batch files
rem  in the system codepage, so any non-ASCII byte in a comment
rem  can decode into '(' and break the parser.
rem ============================================================
setlocal
set SRC=src
set OUT=build
set EXE=%OUT%\bin\FalcomDeskling.exe

rem ---------- pick a *complete* MSVC install ----------
rem This box has a VS2022 Community whose C++ component is half-installed
rem (cl.exe is present, vcvarsall.bat / include / lib are not). Probing by
rem file existence beats trusting isComplete from vswhere.
set VCVARS=
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -products * -property installationPath`) do call :tryvars "%%i"
)
if not defined VCVARS goto novc

call "%VCVARS%" >nul 2>&1
where cl.exe >nul 2>&1
if errorlevel 1 goto vcfail

set OPTS=/O2 /MT /DNDEBUG
if /i "%1"=="debug" set OPTS=/Od /Zi /MTd /DDEBUG

if not exist "%OUT%\obj" mkdir "%OUT%\obj"
if not exist "%OUT%\bin" mkdir "%OUT%\bin"

echo [..] compiling
cl /nologo /std:c++17 /EHsc /utf-8 /W3 /DUNICODE /D_UNICODE %OPTS% /Fo"%OUT%\obj\\" "%SRC%\main.cpp" "%SRC%\window.cpp" "%SRC%\wic.cpp" "%SRC%\runner.cpp" "%SRC%\tray.cpp" /link /SUBSYSTEM:WINDOWS /OUT:"%EXE%" user32.lib gdi32.lib shell32.lib windowscodecs.lib ole32.lib oleaut32.lib
if errorlevel 1 goto fail

if exist "resource\falcom.ico" copy /y "resource\falcom.ico" "%OUT%\bin\falcom.ico" >nul
echo [OK] %EXE%
endlocal
exit /b 0

rem ---------- helpers ----------
:tryvars
if defined VCVARS exit /b 0
if not exist "%~1\VC\Auxiliary\Build\vcvars64.bat" exit /b 0
if not exist "%~1\VC\Auxiliary\Build\vcvarsall.bat" exit /b 0
for /d %%d in ("%~1\VC\Tools\MSVC\*") do if exist "%%d\include" set VCVARS=%~1\VC\Auxiliary\Build\vcvars64.bat
exit /b 0

:novc
echo [X] No complete MSVC C++ toolset found. Open Visual Studio Installer and
echo     add "MSVC v143 - VS C++ x64/x86 build tools".
endlocal
exit /b 1

:vcfail
echo [X] vcvars64.bat ran but cl.exe is still not on PATH
endlocal
exit /b 1

:fail
echo [X] build failed
endlocal
exit /b 1
