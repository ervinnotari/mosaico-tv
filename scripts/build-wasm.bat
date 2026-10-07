@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Builds the WASM player into app\wasm\ (player.js, player.wasm, player.worker.js).
setlocal
call "%~dp0env.bat"
set "ROOT=%~dp0.."
set "OUT=%ROOT%\app\wasm"
set "H264LIB=%ROOT%\build\openh264\libopenh264dec.a"
if not exist "%OUT%" mkdir "%OUT%"

if not exist "%H264LIB%" (
  call "%~dp0build-openh264.bat"
  if errorlevel 1 exit /b 1
)

rem Flags from the official Samsung sample + export of the JS bridge.
rem PTHREAD_POOL_SIZE=18: 16 cameras + ONVIF search + spare.
call "%EMPP%" -std=gnu++14 -O3 -Wall ^
  -I"%ROOT%\wasm\include" ^
  -I"%ROOT%\wasm\third_party\openh264\codec\api\wels" ^
  "%ROOT%\wasm\src\md5.cpp" ^
  "%ROOT%\wasm\src\rtsp_url.cpp" ^
  "%ROOT%\wasm\src\rtsp_protocol.cpp" ^
  "%ROOT%\wasm\src\rtsp_connection.cpp" ^
  "%ROOT%\wasm\src\video.cpp" ^
  "%ROOT%\wasm\src\h264.cpp" ^
  "%ROOT%\wasm\src\h265.cpp" ^
  "%ROOT%\wasm\src\events.cpp" ^
  "%ROOT%\wasm\src\native_player.cpp" ^
  "%ROOT%\wasm\src\soft_decoder.cpp" ^
  "%ROOT%\wasm\src\soft_renderer.cpp" ^
  "%ROOT%\wasm\src\discovery.cpp" ^
  "%ROOT%\wasm\src\bench.cpp" ^
  "%ROOT%\wasm\src\player_main.cpp" ^
  "%H264LIB%" ^
  -s ENVIRONMENT_MAY_BE_TIZEN ^
  -pthread -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=18 ^
  -s TOTAL_MEMORY=268435456 ^
  -s NO_EXIT_RUNTIME=1 ^
  -s "EXPORTED_FUNCTIONS=['_main','_malloc','_free']" ^
  -s "EXTRA_EXPORTED_RUNTIME_METHODS=['ccall','UTF8ToString']" ^
  -o "%OUT%\player.js"
if errorlevel 1 (
  echo [build-wasm] FAILED
  exit /b 1
)
echo [build-wasm] OK: %OUT%
dir /b "%OUT%"
