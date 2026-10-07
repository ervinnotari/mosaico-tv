# Contributing to Mosaico

Thanks for helping. Bug reports, tests on other TV models, translations and
code are all welcome. This document explains how contributions are accepted.

By taking part you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).
Security problems are **not** reported in public issues; see
[SECURITY.md](SECURITY.md).

## License of contributions

Mosaico is licensed under the [Apache License 2.0](LICENSE). Under section 5
of that license, anything you submit for inclusion (code, documentation,
translations, images) is licensed under the same terms, with no additional
conditions.

Every commit must be signed off, certifying the
[Developer Certificate of Origin 1.1](https://developercertificate.org/):
you wrote the change, or have the right to submit it under the project
license. Add the sign-off with `git commit -s`, which appends:

```
Signed-off-by: Your Name <you@example.com>
```

Use your real name. Pull requests with unsigned commits are not merged.

Do not submit code copied from projects with incompatible licenses (for
example GPL code into the app, or code with no license at all). New
third-party components must be listed in [CREDITS.md](CREDITS.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) in the same pull request;
keep the `SPDX-License-Identifier` and copyright header of files you copy.

The attribution in [NOTICE](NOTICE) and on the app's About screen must stay
in place (section 4(d) of the license). Contributors are credited in the git
history; you may also add your name to the "Contributors" section of
[CREDITS.md](CREDITS.md).

## Reporting bugs

Open an issue with:

- TV model and year (Settings → Support → About This TV) and Tizen version;
- camera or DVR brand and model, codec (H.264/H.265) and resolution of the
  main and sub streams;
- what you did, what you expected and what happened;
- if possible, the console log (run the app in debug mode with
  `sdb shell 0 debug MosaicoTv1.Mosaico` and open the DevTools console).
  **Remove passwords and public IP addresses** before posting; the app masks
  passwords in its own logs, but check anyway.

Reports from TV models other than the 2021 The Frame (Tizen 6.0) are
especially useful, including "works fine" reports with the performance line
shown in Settings.

## Proposing changes

1. For anything larger than a small fix, open an issue first to agree on the
   approach. Features that need a server, a cloud service or extra
   privileges are out of scope: the app talks only to the user's cameras.
2. Fork, clone with `git clone --recursive` (OpenH264 is a submodule), create
   a branch from `main`, keep the change focused (one topic per pull
   request).
3. Run the tests (below) and update the documentation that your change
   affects.
4. Open the pull request using the template; CI must be green.

A maintainer reviews every pull request. Changes may be asked for or
declined; the maintainer has the final word on what is merged.

## Development setup

See [README.md](README.md#building) for the toolchain (Samsung Emscripten
SDK, Tizen CLI, Node 22+). Tests:

```
scripts\test.bat             JavaScript and C++ unit tests (no TV needed)
scripts\test-tv.bat <ip>     end-to-end on a TV in Developer Mode
```

On Linux the CI equivalents are `node --test tests/js/*.test.js`,
`scripts/ci/cpp-tests.sh` and `scripts/build.sh`.

Changes to playback, RTSP or the native player must pass the TV test; say in
the pull request which TV you used. If you have no TV, say so and a
maintainer will run it.

## Code guidelines

- **Match the surrounding code.** The app JavaScript (`app/js`) runs with
  no build step on the Tizen 6.0 browser (Chromium 76): `var` and functions,
  and nothing newer than that browser — no optional chaining (`?.`), `??`,
  `replaceAll` or `.at()` (those Sonar rules are disabled for `app/js`).
  Tools and tests run on Node 22. C++ is C++14 for the Samsung
  Emscripten 1.39 toolchain.
- Keep the code clean for SonarQube Cloud and CodeQL. When a rule does not
  apply, explain why in a comment next to a `// NOSONAR`, as in
  `app/js/onvif.js` (32-bit arithmetic in SHA-1).
- New source files start with the SPDX and copyright header used by the
  other files.
- **User-facing text goes through i18n.** Add every new text to both the
  language files in `app/i18n/` (`en.json`, `pt.json`); in `index.html` use
  `data-i18n`. The WASM module never sends user-facing text: it sends an
  error code (`err.<code>`) and an English technical detail. The tests fail
  when a key is missing.
- Never log or commit passwords, certificates (`*.p12`, `profiles.xml`) or
  real camera addresses. Test credentials in code are dummies.
- Add or update tests for what you change: `tests/js` for the app,
  `wasm/tests` for protocol and parsing code, `tools/e2e` for behavior on
  the TV.
- Documentation is in English.

## Translations

The UI is available in English and Portuguese, one JSON file per language in
`app/i18n/`. To add a language, copy `en.json` to `app/i18n/<code>.json`,
translate the values (keep the keys and the `{name}` parameters), add the code
to `LANGUAGES` in `app/js/i18n.js` and a `description` with `xml:lang` in
`app/config.xml`. The tests check that no key or parameter is missing.

## Commits

Write short, imperative subjects ("Fix reconnection after DVR reboot") with
a body explaining why when it is not obvious. Keep generated files
(`app/wasm`, `app/licenses`, `out/`, `build/`) out of commits.
