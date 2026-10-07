@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Builds and runs the unit tests (no network) on the emsdk's Node.
setlocal
call "%~dp0env.bat"
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\tests"
if not exist "%OUT%" mkdir "%OUT%"

call "%EMPP%" -std=gnu++14 -O1 -Wall ^
  -I"%ROOT%\wasm\include" ^
  "%ROOT%\wasm\src\md5.cpp" ^
  "%ROOT%\wasm\src\rtsp_protocol.cpp" ^
  "%ROOT%\wasm\src\rtsp_url.cpp" ^
  "%ROOT%\wasm\src\video.cpp" ^
  "%ROOT%\wasm\src\h264.cpp" ^
  "%ROOT%\wasm\src\h265.cpp" ^
  "%ROOT%\wasm\tests\unit_tests.cpp" ^
  -o "%OUT%\unit_tests.js"
if errorlevel 1 (
  echo [test-wasm] build FAILED
  exit /b 1
)
rem The Samsung emscripten runtime uses `self`, which Node does not define.
node -e "global.self=global;require(process.argv[1])" "%OUT%\unit_tests.js"
