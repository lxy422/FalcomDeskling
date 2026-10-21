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
cd /d "%~dp0"
set SRC=src
set OUT=build
if not "%~2"=="" set "OUT=%~2"
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

echo [..] compiling icon resource
rc /nologo /i resource /fo "%OUT%\obj\falcom.res" resource\falcom.rc
if errorlevel 1 goto fail

echo [..] compiling
cl /nologo /std:c++17 /EHsc /utf-8 /W3 /DUNICODE /D_UNICODE %OPTS% /Fo"%OUT%\obj\\" "%SRC%\main.cpp" "%SRC%\display.cpp" "%SRC%\window.cpp" "%SRC%\wic.cpp" "%SRC%\runner.cpp" "%SRC%\combat.cpp" "%SRC%\tray.cpp" "%SRC%\store.cpp" "%SRC%\emote.cpp" "%SRC%\charconf.cpp" "%SRC%\xml.cpp" "%OUT%\obj\falcom.res" /link /SUBSYSTEM:WINDOWS /OUT:"%EXE%" user32.lib gdi32.lib shell32.lib windowscodecs.lib ole32.lib oleaut32.lib
if errorlevel 1 goto fail

if exist "resource\falcom.ico" copy /y "resource\falcom.ico" "%OUT%\bin\falcom.ico" >nul

rem ---------- resource junctions ----------
rem chr\ and Enemy\ live OUTSIDE the exe: the program only looks next to it, so
rem the build has to put something there. mklink /J needs no admin rights.
rem
rem %~dp0 is this batch file's own directory, so the source is right no matter
rem what cwd the caller used. The art itself still lives in the sibling WpfTest
rem checkout - this repo keeps no copies of those 16MB.
set CHRSRC=%~dp0..\WpfTest\Resource\Chr
set ENEMYSRC=%~dp0..\WpfTest\Resource\Enemy
if not exist "%OUT%\bin\chr" (
  if exist "%CHRSRC%" (
    mklink /J "%OUT%\bin\chr" "%CHRSRC%" >nul
  ) else (
    echo [!] chr\ source not found at %CHRSRC% - the program will refuse to start
  )
)
if not exist "%OUT%\bin\Enemy" (
  if exist "%ENEMYSRC%" (
    mklink /J "%OUT%\bin\Enemy" "%ENEMYSRC%" >nul
  )
)
rem Alternate output directories reuse the split resources beside the main build.
for %%c in (Enemy1 Enemy2) do if not exist "%OUT%\bin\%%c" if exist "%~dp0build\bin\%%c" mklink /J "%OUT%\bin\%%c" "%~dp0build\bin\%%c" >nul
echo [OK] %EXE%
if /i "%1"=="test" (
  if not exist tests\regression.cpp goto notests
  cl /nologo /std:c++17 /EHsc /utf-8 /W3 /DUNICODE /D_UNICODE /O2 /MT /DNDEBUG /Fo"%OUT%\obj\regression.obj" tests\regression.cpp "%OUT%\obj\display.obj" "%OUT%\obj\runner.obj" "%OUT%\obj\combat.obj" "%OUT%\obj\window.obj" "%OUT%\obj\wic.obj" "%OUT%\obj\emote.obj" "%OUT%\obj\charconf.obj" "%OUT%\obj\xml.obj" "%OUT%\obj\store.obj" /link /SUBSYSTEM:CONSOLE /OUT:"%OUT%\bin\regression.exe" user32.lib gdi32.lib shell32.lib windowscodecs.lib ole32.lib oleaut32.lib psapi.lib
  if errorlevel 1 goto fail
  "%OUT%\bin\regression.exe" "%~dp0testdata\ChrBig"
  if errorlevel 1 goto fail
)
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

:notests
echo [X] Local tests are not included in the public source checkout. Use build.cmd for a release build.
endlocal
exit /b 1
