@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Shows the TV DUID (needed for the Samsung distributor certificate).
rem Usage: duid.bat <tv-ip>
setlocal
call "%~dp0env.bat"
if "%~1"=="" (
  echo Usage: duid.bat ^<tv-ip^>
  exit /b 1
)
"%SDB%" connect %~1
"%SDB%" -s %~1:26101 shell 0 getduid
