@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem All tests that do not need the TV: C++ (WASM on Node) and JavaScript.
call "%~dp0test-wasm.bat"
if errorlevel 1 exit /b 1
call "%~dp0test-js.bat"
