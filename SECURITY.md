# Security policy

## Supported versions

Only the latest release published on GitHub (and in the Samsung store, once
available) receives security fixes.

## Reporting a vulnerability

Please report security problems privately, **not** in public issues:

- GitHub: Security → "Report a vulnerability" on this repository (private
  advisory), or
- e-mail: mosaicotv.talk@outlook.com, subject starting with `[security]`.

Include the app version, TV model, what an attacker can do and the steps to
reproduce. Do not include real camera passwords or addresses.

You will get an answer within 7 days. Once a fix is released, the advisory
is published and you are credited, unless you prefer otherwise.

## Scope

In scope: the app (`app/`, `wasm/`), its handling of camera credentials, the
RTSP/ONVIF parsers, and the build and release scripts.

Out of scope: vulnerabilities in the cameras or DVRs themselves, in Samsung
Tizen, or attacks that need physical access to an unlocked TV in Developer
Mode.
