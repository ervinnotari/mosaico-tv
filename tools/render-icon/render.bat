@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Renders store\icon.svg (headless Chrome) and writes the app and store PNGs:
rem   app\icon.png and store\icon_512x423.png  (512x423, transparent background)
rem   store\icon_1920x1080.png               (icon on a dark background)
rem   store\icon_1024.png                    (square master)
rem Needs Chrome and ffmpeg.
setlocal
set "ROOT=%~dp0..\.."
set "CHROME=%ProgramFiles%\Google\Chrome\Application\chrome.exe"
if not exist "%CHROME%" set "CHROME=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
set "TMPDIR=%TEMP%\mosaico-icon"
if not exist "%TMPDIR%" mkdir "%TMPDIR%"

set "SVG=%ROOT%\store\icon.svg"
for %%I in ("%SVG%") do set "SVGURL=file:///%%~fI"
set "SVGURL=%SVGURL:\=/%"
> "%TMPDIR%\render.html" echo ^<!doctype html^>^<html^>^<body style="margin:0;background:transparent"^>^<img src="%SVGURL%" style="display:block;width:1024px;height:1024px"^>^</body^>^</html^>

"%CHROME%" --headless=new --disable-gpu --hide-scrollbars --default-background-color=00000000 --window-size=1024,1024 --screenshot="%ROOT%\store\icon_1024.png" "file:///%TMPDIR:\=/%/render.html"
if errorlevel 1 exit /b 1

ffmpeg -hide_banner -loglevel error -y -i "%ROOT%\store\icon_1024.png" -vf "format=rgba,scale=423:423:flags=lanczos,pad=512:423:(ow-iw)/2:0:color=black@0" -frames:v 1 "%ROOT%\app\icon.png"
copy /y "%ROOT%\app\icon.png" "%ROOT%\store\icon_512x423.png" >nul
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "color=c=0x081127:s=1920x1080" -i "%ROOT%\store\icon_1024.png" -filter_complex "[1:v]scale=860:860:flags=lanczos[i];[0:v][i]overlay=(W-w)/2:(H-h)/2:format=auto" -frames:v 1 "%ROOT%\store\icon_1920x1080.png"
echo [render-icon] OK
