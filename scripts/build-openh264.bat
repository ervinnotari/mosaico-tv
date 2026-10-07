@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Builds the OpenH264 decoder (wasm\third_party\openh264) into
rem build\openh264\libopenh264dec.a. Only needs to run again if the
rem OpenH264 code changes.
setlocal enabledelayedexpansion
call "%~dp0env.bat"
set "ROOT=%~dp0.."
set "SRC=%ROOT%\wasm\third_party\openh264\codec"
if not exist "%SRC%\decoder" (
  echo [openh264] wasm\third_party\openh264 is empty: run "git submodule update --init" first.
  exit /b 1
)
set "OUT=%ROOT%\build\openh264"
if not exist "%OUT%" mkdir "%OUT%"
del /q "%OUT%\*.o" "%OUT%\*.a" 2>nul

set FLAGS=-O3 -DNDEBUG -pthread -s USE_PTHREADS=1 -Wno-everything ^
  -I"%SRC%\api\wels" -I"%SRC%\common\inc" ^
  -I"%SRC%\decoder\core\inc" -I"%SRC%\decoder\plus\inc"

set OBJS=
for %%D in (common\src decoder\core\src decoder\plus\src) do (
  for %%F in ("%SRC%\%%D\*.cpp") do (
    echo [openh264] %%~nxF
    call "%EMPP%" !FLAGS! -c "%%F" -o "%OUT%\%%~nF.o"
    if errorlevel 1 (
      echo [openh264] FAILED on %%~nxF
      exit /b 1
    )
    set OBJS=!OBJS! "%OUT%\%%~nF.o"
  )
)

call "%EMSCRIPTEN_DIR%\emar.bat" rcs "%OUT%\libopenh264dec.a" %OBJS%
if errorlevel 1 (
  echo [openh264] emar FAILED
  exit /b 1
)
echo [openh264] OK: %OUT%\libopenh264dec.a
