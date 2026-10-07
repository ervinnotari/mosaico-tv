@echo off
rem SPDX-License-Identifier: Apache-2.0
rem Copyright 2026 Ervin Notari Junior
chcp 65001 >nul
rem Unit tests of the app JavaScript (store, capability, player, ONVIF, i18n).
rem They run on Node, without a TV, with test doubles for the browser and the WASM module.
node --test "%~dp0..\tests\js\*.test.js"
