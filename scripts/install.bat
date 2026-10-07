@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Connects to the TV, installs the .wgt from out\ and opens the app.
rem Usage: install.bat <tv-ip>   (or set TV_IP)
setlocal
call "%~dp0env.bat"
set "ROOT=%~dp0.."
set "TV=%~1"
if "%TV%"=="" set "TV=%TV_IP%"
if "%TV%"=="" (
  echo Usage: install.bat ^<tv-ip^>
  exit /b 1
)
set "SERIAL=%TV%:26101"

"%SDB%" connect %TV%
"%SDB%" devices | findstr /c:"%SERIAL%" >nul
if errorlevel 1 (
  echo [install] TV %SERIAL% does not show up in sdb. Check Developer Mode and the PC IP configured in it.
  exit /b 1
)

set "WGT="
for %%F in ("%ROOT%\out\*.wgt") do set "WGT=%%~nxF"
if "%WGT%"=="" (
  echo [install] No .wgt in out\. Run scripts\package.bat first.
  exit /b 1
)

call "%TIZEN_CLI%" install -s %SERIAL% -n "%WGT%" -- "%ROOT%\out"
if errorlevel 1 exit /b 1
call "%TIZEN_CLI%" run -s %SERIAL% -p %APP_ID%
