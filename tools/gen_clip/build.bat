@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Builds the reference clip generator (runs on Node) and writes the clips
rem to app\bench\. Needs the full OpenH264 source (with the encoder):
rem   build.bat <openh264-2.4.1 folder>
setlocal enabledelayedexpansion
call "%~dp0..\..\scripts\env.bat"
set "ROOT=%~dp0..\.."
set "OH=%~1"
if "%OH%"=="" (
  echo usage: build.bat ^<full openh264-2.4.1 folder^>
  exit /b 1
)
set "SRC=%OH%\codec"
set "OUT=%ROOT%\build\gen_clip"
if not exist "%OUT%\obj" mkdir "%OUT%\obj"

set INC=-I"%SRC%\api\wels" -I"%SRC%\common\inc" -I"%SRC%\encoder\core\inc" ^
  -I"%SRC%\encoder\plus\inc" -I"%SRC%\processing\interface" -I"%SRC%\processing\src\common"

if not exist "%OUT%\libopenh264enc.a" (
  del /q "%OUT%\obj\*.o" 2>nul
  for %%D in (common\src encoder\core\src encoder\plus\src processing\src\adaptivequantization ^
              processing\src\backgrounddetection processing\src\common processing\src\complexityanalysis ^
              processing\src\denoise processing\src\downsample processing\src\imagerotate ^
              processing\src\scenechangedetection processing\src\scrolldetection processing\src\vaacalc) do (
    for %%F in ("%SRC%\%%D\*.cpp") do (
      if /i not "%%~nxF"=="DllEntry.cpp" (
        echo [gen_clip] %%~nxF
        call "%EMPP%" -O2 -DNDEBUG -Wno-everything !INC! -c "%%F" -o "%OUT%\obj\%%~nF.o"
        if errorlevel 1 exit /b 1
      )
    )
  )
  rem emar does not expand *.o on Windows: add them one by one.
  for %%O in ("%OUT%\obj\*.o") do call "%EMSCRIPTEN_DIR%\emar.bat" q "%OUT%\libopenh264enc.a" "%%O"
  call "%EMSCRIPTEN_DIR%\emar.bat" s "%OUT%\libopenh264enc.a"
)

call "%EMPP%" -O2 -DNDEBUG %INC% "%~dp0gen_clip.cpp" "%OUT%\libopenh264enc.a" ^
  -s ALLOW_MEMORY_GROWTH=1 -s NODERAWFS=1 ^
  -s ERROR_ON_UNDEFINED_SYMBOLS=0 ^
  -o "%OUT%\gen_clip.js"
rem ERROR_ON_UNDEFINED_SYMBOLS=0: WelsThreadLib references pthread functions that
rem do not exist without -pthread; the encoder runs on one thread and never calls them.
if errorlevel 1 (
  echo [gen_clip] build FAILED
  exit /b 1
)

if not exist "%ROOT%\app\bench" mkdir "%ROOT%\app\bench"
rem Bitrate similar to real DVR substreams. The Samsung emscripten
rem runtime uses `self`, which Node does not define.
node -e "global.self=global;require(process.argv[1])" "%OUT%\gen_clip.js" 352 240 50 25 512 "%ROOT%\app\bench\ref_352x240.bin"
