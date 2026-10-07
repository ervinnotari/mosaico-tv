@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Signs app\ and builds the .wgt in out\.
rem Usage: package.bat [profile]   (default: %TIZEN_PROFILE% or "mosaico-tv")
setlocal
call "%~dp0env.bat"
set "ROOT=%~dp0.."
set "PROFILE=%~1"
if "%PROFILE%"=="" set "PROFILE=%TIZEN_PROFILE%"
if "%PROFILE%"=="" set "PROFILE=mosaico-tv"
set "OUT=%ROOT%\out"

if not exist "%ROOT%\app\wasm\player.wasm" (
  echo [package] app\wasm\player.wasm missing. Run scripts\build-wasm.bat first.
  exit /b 1
)
rem The CLI produces an unsigned .wgt when the profile does not exist, without an error.
call "%TIZEN_CLI%" security-profiles list | findstr /b /c:"%PROFILE% " >nul
if errorlevel 1 (
  echo [package] Signing profile "%PROFILE%" does not exist. Available profiles:
  call "%TIZEN_CLI%" security-profiles list
  exit /b 1
)
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"

rem Licenses and credits go inside the app (About screen), as required by
rem Apache 2.0 (NOTICE) and the third-party licenses.
if not exist "%ROOT%\app\licenses" mkdir "%ROOT%\app\licenses"
copy /y "%ROOT%\LICENSE" "%ROOT%\app\licenses\LICENSE" >nul
copy /y "%ROOT%\NOTICE" "%ROOT%\app\licenses\NOTICE" >nul
copy /y "%ROOT%\THIRD_PARTY_NOTICES.md" "%ROOT%\app\licenses\THIRD_PARTY_NOTICES.md" >nul
copy /y "%ROOT%\CREDITS.md" "%ROOT%\app\licenses\CREDITS.md" >nul

call "%TIZEN_CLI%" package -t wgt -s "%PROFILE%" -o "%OUT%" -- "%ROOT%\app"
if errorlevel 1 (
  echo [package] FAILED. Available profiles:
  call "%TIZEN_CLI%" security-profiles list
  exit /b 1
)
rem The name comes from <name> in config.xml; the TV installer does not accept spaces.
for %%F in ("%OUT%\*.wgt") do if /i not "%%~nxF"=="Mosaico.wgt" move /y "%%F" "%OUT%\Mosaico.wgt" >nul
echo [package] OK:
dir /b "%OUT%\*.wgt"
