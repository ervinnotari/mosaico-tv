@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem End-to-end test on the TV with RTSP test servers (H.264 and H.265).
rem Usage: test-tv.bat [tv-ip] [--onvif]
rem Needs: app installed (install.bat), Developer Mode, ffmpeg and Node 22+
rem in the PATH. Does not call env.bat: it puts the emsdk's Node 12 first, which
rem has neither fetch nor WebSocket.
node "%~dp0..\tools\e2e\tv-e2e.js" %*
