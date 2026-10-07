# Build Notes

> Original notes from the first phase. Current build steps are in the README
> and `scripts/env.bat`.

Known local environment:
- Samsung Emscripten: `C:\Samsung\emscripten-release-bundle\emsdk`
- `.emscripten`: `%USERPROFILE%\.emscripten`
- cache: `%USERPROFILE%\.emscripten_cache`
- emcc: 1.39.4.7

Activation on Windows:
`C:\Samsung\emscripten-release-bundle\emsdk\emsdk_env.bat`

Validation:
`emcc --version`
`em++ --version`

The final package must be a signed `.wgt`. A ZIP of the project cannot be
installed on the TV directly.
