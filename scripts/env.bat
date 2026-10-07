@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
rem Tool paths. Override them with environment variables if they change.
rem The emsdk_env.bat of this installation does not put emcc in the PATH, so
rem we point directly to the fastcomp emscripten.

if not defined SAMSUNG_EMSDK set "SAMSUNG_EMSDK=C:\Samsung\emscripten-release-bundle\emsdk"
if not defined TIZEN_SDK_DATA set "TIZEN_SDK_DATA=%USERPROFILE%\.tizen-extension-platform\server\sdktools\data"

set "EMSCRIPTEN_DIR=%SAMSUNG_EMSDK%\fastcomp\emscripten"
set "EM_CONFIG=%USERPROFILE%\.emscripten"
if not defined EM_CACHE set "EM_CACHE=%USERPROFILE%\.emscripten_cache"
set "EMPP=%EMSCRIPTEN_DIR%\em++.bat"

set "TIZEN_CLI=%TIZEN_SDK_DATA%\tools\ide\bin\tizen.bat"
set "SDB=%TIZEN_SDK_DATA%\tools\sdb.exe"

rem App ID read from config.xml (the only place where it is defined).
for /f tokens^=2^ delims^=^" %%i in ('findstr /c:"tizen:application id=" "%~dp0..\app\config.xml"') do set "APP_ID=%%i"

set "PATH=%SAMSUNG_EMSDK%\python\2.7.13.1_64bit\python-2.7.13.amd64;%SAMSUNG_EMSDK%\node\12.9.1_64bit\bin;%EMSCRIPTEN_DIR%;%PATH%"
