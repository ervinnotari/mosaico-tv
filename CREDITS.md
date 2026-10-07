# Credits

**Mosaico** — IP cameras on Samsung TVs

© 2026 Ervin Notari Junior — licensed under the Apache License 2.0

Source code: https://github.com/ervinnotari/mosaico-tv

Support: mosaicotv.talk@outlook.com

## Third-party software

These components are distributed inside the app and require attribution.
Their full license texts are in `THIRD_PARTY_NOTICES.md`.

| Component | Author | License | Used for |
|---|---|---|---|
| [OpenH264 2.4.1](https://github.com/cisco/openh264) | Cisco Systems | BSD 2-Clause | Software H.264 decoder for the mosaic tiles |
| [Emscripten runtime](https://emscripten.org) | Emscripten authors | MIT / University of Illinois NCSA | WebAssembly runtime (JavaScript glue) |
| [Samsung TizenTV Emscripten extensions](https://developer.samsung.com/smarttv/develop/extension-libraries/webassembly/overview.html) | Samsung Electronics | MIT / University of Illinois NCSA | Native video player (WASM Player) and Tizen Sockets |
| [musl libc](https://musl.libc.org) | Rich Felker et al. | MIT | C library compiled into the WebAssembly module |
| [libc++](https://libcxx.llvm.org) | LLVM contributors | MIT / University of Illinois NCSA | C++ standard library compiled into the WebAssembly module |
| [Material Icons "settings"](https://fonts.google.com/icons) | Google | Apache 2.0 | Gear icon of the settings button |

## Development tools

Used to build and test the project; not distributed with the app, so they
need no attribution in it.

- Samsung Emscripten SDK and Tizen Studio tools — build and packaging.
- OpenH264 encoder — generates the synthetic benchmark clip.
- FFmpeg (x264, x265) — test media for the RTSP test server.
- Node.js — tests and tools.

## Contributors

Thanks to everyone who contributed code, tests, translations and bug reports
(see the git history). Contributors may add their name here in their pull
request, as described in `CONTRIBUTING.md`.

- Ervin Notari Junior — author and maintainer

## Keeping this file up to date

When a new library is added to the app:

1. Add a row to the table above (component, author, license, purpose).
2. Add its full license text to `THIRD_PARTY_NOTICES.md`.
3. Run `scripts\package.bat`: both files are copied into the app and shown on
   the **About** screen.
