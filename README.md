# Mosaico — IP cameras on Samsung TVs

[![CI](https://github.com/ervinnotari/mosaico-tv/actions/workflows/ci.yml/badge.svg)](https://github.com/ervinnotari/mosaico-tv/actions/workflows/ci.yml)
[![Quality Gate](https://sonarcloud.io/api/project_badges/measure?project=ervinnotari_mosaico-tv&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=ervinnotari_mosaico-tv)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=ervinnotari_mosaico-tv&metric=coverage)](https://sonarcloud.io/summary/new_code?id=ervinnotari_mosaico-tv)

A Samsung Tizen TV app that plays RTSP cameras and DVRs **directly on the
TV**, with no PC, Raspberry Pi, Docker, proxy, transcoding server or cloud in
between.

- 1, 4, 8 and 16-camera layouts (mosaic), full screen with one click.
- ONVIF discovery (WS-Discovery + GetProfiles/GetStreamUri) and manual setup
  (Hikvision, Intelbras/Dahua or any RTSP path; IP address or host name).
- H.264 everywhere; H.265 in full screen.
- Remote-control navigation, automatic reconnection, pause in background.
- Portuguese and English UI, following the TV language.

```
HTML/JS UI ─ WebAssembly (C++) ─ Tizen Sockets ─ RTSP/RTP over TCP ─ depacketizer
   ├─ native TV player (Samsung WASM Player)  → full screen and the large 1:8 tile
   └─ OpenH264 compiled to WASM + WebGL        → the other mosaic tiles
```

Licensed under the [Apache License 2.0](LICENSE). Redistributions and
derivative works must keep the credits in [NOTICE](NOTICE). Third-party
components that require attribution are listed in [CREDITS.md](CREDITS.md)
(editable; shown on the app's **About** screen), and their full license texts
are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). These files at the
repository root are the ones to edit: `scripts\package.bat` (and
`scripts/ci/package.sh`) copies them into `app/licenses/` for the package.

## Status

Working on the reference TV (Samsung The Frame 2021, QN55LS03AAGXZD, Tizen 6.0)
against a 4-channel Hikvision DVR.

### Compatibility

| TV year | Tizen | Expected |
|---|---|---|
| up to 2019 | ≤ 5.0 | Not supported (no WebAssembly) |
| 2020 | 5.5 | Probably works (`config.xml` currently requires 6.0; untested) |
| 2021 | 6.0 | Tested (see below) |
| 2022–2026 | 6.5–10.0 | Same APIs; not tested yet |

Tested TVs (end-to-end test, `scripts\test-tv.bat`):

| TV | Benchmark | Layouts with DVR substreams (352×240 @ 25 fps) | Notes |
|---|---|---|---|
| The Frame 2021, QN55LS03A | 53–68 Mpx/s | 1, 4, 8; 16 with up to 12 cameras | Reference TV |
| Crystal UHD 2021, UN50AU7700 | 20–27 Mpx/s | 1 (4 at the limit, depending on the measurement) | Full screen (TV decoder, H.264 and H.265) works fully; the mosaic needs a faster TV. The TV restarts the app when it comes back from the background; it reconnects by itself |

The software mosaic depends on the TV processor, so the app measures it on
first launch and only enables the layouts that fit.

### How it works

- **One native player at a time.** The TV allows only one Samsung WASM Player
  (EMSS) playback at once: two or more players keep closing each other,
  whatever the decoder or rendering mode. So full screen (1:1) and the large
  1:8 tile use the native player with the main stream, and the other tiles
  decode the camera substream with OpenH264 in WebAssembly, drawn with WebGL.
- **Performance profile** (`app/js/capability.js`, `wasm/src/bench.cpp`). On
  first launch (and after each firmware update) the app decodes a synthetic
  reference clip (`app/bench/ref_352x240.bin`) on all cores for six 1-second
  windows and keeps the best one. A layout is enabled if the software
  decoding cost of the cameras (real substream resolution × fps) fits in the
  measured capacity; otherwise its button is crossed out and explains why.
  Reference TV: ~66 Mpx/s on the clip, ~33 Mpx/s real capacity, so 1:16
  allows 16 substreams of 352×240 at 25 fps.
- **Load governor (economy mode).** Every 2 s the app adds up the real
  decoding load. Above 90% for two cycles, the last tile that is not focused
  goes to economy mode: only key frames are decoded, so it refreshes once per
  GOP (1–2 s) at a fraction of the cost, marked by a clock after its name.
  When the load stays below 75% and the tile's live cost fits, it returns to
  live; a tile that has to go back soon waits twice as long next time. Full
  screen always plays live on the TV decoder.
- **H.265** plays in the native player. In the mosaic (H.264-only software
  decoder) the tile says "H.265 only in full screen" and does not retry. Cameras
  that record in H.265 usually offer an H.264 substream.
- **Background.** When the app leaves the screen (Home, another source or
  app) every camera disconnects and the native player is released; it
  reconnects when the app comes back.
- **Fast full screen.** On Hikvision devices the app requests a key frame
  (ISAPI `requestKeyFrame`); video shows up in ~100–200 ms. If the native
  player does not start within 6 s, the session restarts by itself.
- **Exit.** Back on the mosaic asks "Exit the app?" (focus starts on Cancel).

### Remote control

| Key | Mosaic | Full screen |
|---|---|---|
| Arrows | move between tiles; ▲ on the first row opens the menu | ◀ ▶ previous/next camera |
| OK | opens the camera in full screen | — |
| CH▲ / CH▼ | next/previous page | next/previous camera |
| Back | asks to exit (in the menu: back to the tiles) | back to the mosaic |

### Languages

The UI is in Portuguese and English: one JSON file per language in
`app/i18n/` (`en.json`, `pt.json`), loaded by `app/js/i18n.js`. The language comes
from the TV menu language (`tizen.systeminfo` LOCALE, or the browser
language): any Portuguese variant uses Portuguese, every other language uses
English. It is checked again when the app comes back to the foreground.

To translate a text, add the same key to every file in `app/i18n/`; in
`index.html` mark the element with `data-i18n="key"` (or
`data-i18n-title`, `data-i18n-aria-label`, `data-i18n-placeholder`). The
WASM module never sends user-facing text: errors carry a code (`err.<code>`
in the dictionaries) and an English technical detail. The tests fail if a key
is missing in one language or if the HTML has a fixed text.

To add a language, copy `en.json` to `app/i18n/<code>.json` (e.g. `es.json`),
translate the values, add the code to `LANGUAGES` in `i18n.js` and a
`description` with `xml:lang` in `app/config.xml`. English is the fallback
for any TV language without a file.

## Building

OpenH264 is a git submodule (`wasm/third_party/openh264`, pinned to the
official `v2.4.1` tag), so clone with:

```
git clone --recursive https://github.com/ervinnotari/mosaico-tv.git
```

In an existing clone: `git submodule update --init`.

Requirements (Windows):

- Samsung Emscripten SDK (fastcomp 1.39.4.7), Tizen CLI and `sdb` (the Tizen
  extension for Visual Studio installs them); paths are in `scripts\env.bat`.
- A Samsung TV certificate profile named `mosaico-tv` (Tizen Certificate
  Manager, with your TV's DUID). Keep `author.p12` and its password: store
  updates must be signed with the same author certificate.
- Node 22+ for tests and tools; ffmpeg for test media.

```
scripts\build-openh264.bat   builds OpenH264 once (build\openh264\libopenh264dec.a)
scripts\build-wasm.bat       builds the player into app\wasm\
scripts\package.bat          signs with the "mosaico-tv" profile → out\Mosaico.wgt
scripts\install.bat <ip>     installs and launches on the TV (Developer Mode)
scripts\duid.bat <ip>        prints the TV DUID (needed for the certificate)
```

The app id is defined only in `app/config.xml`; the scripts and the TV test
read it from there.

## Tests

| Level | Runs on | Covers |
|---|---|---|
| `tests/js` (71 tests) | Node, no TV | `store.js` (URLs per brand, special characters in passwords, persistence), `capability.js` (calibration, capacity rule on both test TVs, real cost, cache, load governor), `player.js` (slot lifecycle, single native player, reconnection, permanent errors, 6 s watchdog, ISAPI), ONVIF (SHA-1, WS-Security, discovery replies and the channel search, with a small XML test parser), Markdown renderer (incl. escaping), i18n (language detection, every language file has the same keys and parameters as `en.json`, every key used by the HTML, JS and WASM error codes exists) |
| `wasm/tests` | Node (WASM); natively with g++ in CI | MD5, Digest (RFC 2617), SDP, URLs, RTP, SPS (incl. synthetic High profile and H.265 variants) and H.264/H.265 depacketizers |
| `tools/e2e` | Real TV | H.264 mosaic live, RTSP command order (OPTIONS → DESCRIBE → SETUP → PLAY), H.265 in the mosaic (message, no retry), H.265 full screen, Back, exit dialog, About screen, background and resume, language switch, ONVIF discovery (optional) |

```
scripts\test.bat             all tests that do not need the TV
scripts\test-tv.bat <ip>     end-to-end on the TV (add --onvif for discovery)
```

On Linux: `node --test "tests/js/*.test.js"` and `sh scripts/ci/cpp-tests.sh`.
Add `--experimental-test-coverage` to the Node command for a coverage
report; CI publishes coverage and code analysis to SonarQube Cloud
([docs/SONAR.md](docs/SONAR.md)).

The TV test starts two RTSP test servers on the PC, drives the app through
the DevTools protocol like a remote control, and restores the cameras that
were saved on the TV.

### Testing without a camera (including H.265)

```
ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=25 -t 20 -c:v libx265 ^
  -x265-params keyint=25:repeat-headers=1:aud=1 -f hevc test.hevc
node tools\rtsp-test-server\server.js test.hevc 8554
```

Then add `rtsp://<pc-ip>:8554/test` as a camera ("Other (enter the path)").

## Project layout

```
app/                       widget (.wgt)
  licenses/                copies of LICENSE, NOTICE, CREDITS.md and THIRD_PARTY_NOTICES.md
                           made by the packaging for the About screen (generated, not in git)
  js/app.js                screens: mosaic, camera list, form, search, about
  i18n/en.json, pt.json    UI texts, one file per language
  js/i18n.js               loads the texts and detects the TV language
  js/markdown.js           minimal Markdown renderer for the About screen
  js/player.js             WASM bridge; keeps each slot playing, reconnects
  js/capability.js         performance profile and layout rules
  js/nav.js                spatial navigation for the remote control
  js/onvif.js              ONVIF client (SOAP + WS-UsernameToken)
  js/store.js              cameras and preferences (localStorage)
wasm/src/
  player_main.cpp          RTSP sessions (one pthread per slot) and exports
  rtsp_connection.*        TCP socket, DNS, requests, Digest/Basic auth
  rtsp_protocol.*          responses, SDP, authentication (no network, testable)
  video.*                  shared parts: access unit, RTP, bit reader
  h264.*, h265.*           RTP → Annex B access units, SPS parsing
  native_player.*          ElementaryMediaStreamSource (low latency)
  soft_decoder.*           OpenH264
  soft_renderer.*          YUV → WebGL, one rectangle per slot
  discovery.cpp            WS-Discovery over UDP (multicast + subnet sweep)
  bench.cpp                decoder benchmark
wasm/third_party/openh264  OpenH264 2.4.1 (git submodule, BSD 2-Clause)
tests/js                   app JavaScript tests (harness with test doubles)
tools/gen_clip             generates the benchmark clip (synthetic scene)
tools/rtsp-test-server     minimal RTSP server (Node) for testing
tools/e2e                  end-to-end test on the TV
tools/render-icon          renders store/icon.svg into the app and store PNGs
scripts/build.sh           Linux/macOS build (used by CI)
scripts/ci/                CI helpers: SDK and Tizen CLI setup, native C++ tests, signing
.github/workflows/         CI (tests + build), release (signed .wgt) and Sonar analysis
sonar-project.properties   SonarQube Cloud settings
store/                     icon source (icon.svg) and store icons (1920×1080, 512×423)
docs/                      privacy policy, Samsung certification, releasing, Sonar, spec
```

## CI and releases

GitHub Actions runs the tests and the WebAssembly build on every push and
pull request, the SonarQube Cloud analysis with test coverage
([docs/SONAR.md](docs/SONAR.md)), and pushing a tag `vX.Y.Z` publishes a signed `.wgt` as a
GitHub Release. Setup (certificate secrets) and steps are in
[docs/RELEASING.md](docs/RELEASING.md). On Linux/macOS the build is
`SAMSUNG_EMSDK=<emsdk> scripts/build.sh`.

## Publishing

See [docs/SAMSUNG_CERTIFICATION.md](docs/SAMSUNG_CERTIFICATION.md) for what
Samsung TV Seller Office requires and how reviewers can test the app, and
[docs/PRIVACY.md](docs/PRIVACY.md) for the privacy policy.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) (license of contributions, sign-off,
tests and code guidelines) and the [Code of Conduct](CODE_OF_CONDUCT.md).
Report security problems privately as described in [SECURITY.md](SECURITY.md).

## Support

mosaicotv.talk@outlook.com

## Security and privacy

Camera URLs, including user names and passwords, are stored only in the app's
local storage on the TV. Passwords are masked in logs. The app talks only to
the cameras configured by the user and, for discovery, to the local network.
