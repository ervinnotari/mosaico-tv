# Releasing

Three GitHub Actions workflows run on Ubuntu 24.04:

| Workflow | When | What it does |
|---|---|---|
| `ci.yml` | every push to `main`, every pull request, manual | JavaScript tests (Node 22), C++ tests compiled natively with g++, full WebAssembly build with the Samsung Emscripten SDK (artifact `app-wasm`) |
| `release.yml` | pushing a tag `vX.Y.Z` | the same tests and build, then signs the `.wgt` and publishes a GitHub Release with `Mosaico-X.Y.Z.wgt` and its `.sha256` |
| `sonar.yml` | every push to `main`, every pull request, manual | tests with coverage and the SonarQube Cloud analysis; see [SONAR.md](SONAR.md) |

The end-to-end test needs the TV on the local network, so it is run by hand
before a release: `scripts\test-tv.bat <tv-ip>`.

## One-time setup: repository secrets

The release signs the package with your Samsung certificates. They never go
into the repository; they are stored as secrets of the `release` environment
(Settings → Environments → `release` → Add secret), or as repository secrets.

| Secret | Value |
|---|---|
| `TIZEN_AUTHOR_P12_BASE64` | `author.p12` encoded in base64 |
| `TIZEN_AUTHOR_PASSWORD` | password of `author.p12` |
| `TIZEN_DISTRIBUTOR_P12_BASE64` | `distributor.p12` encoded in base64 |
| `TIZEN_DISTRIBUTOR_PASSWORD` | password of `distributor.p12` |

The certificate files are created by the Samsung Certificate Manager, by
default in `%USERPROFILE%\SamsungCertificate\<profile>\`.

To copy a certificate as base64 to the clipboard (PowerShell):

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("$env:USERPROFILE\SamsungCertificate\mosaico-tv\author.p12")) | Set-Clipboard
```

To check a password before saving it (needs a JDK):

```
keytool -list -storetype pkcs12 -keystore author.p12 -storepass <password>
```

Using an environment named `release` also lets you require a manual
approval before the signing job runs (Settings → Environments → `release`
→ Required reviewers).

## Publishing a version

1. Update `version` in `app/config.xml` (format `x.y.z`; Samsung requires a
   higher version for every upload).
2. Run the tests locally, including the TV test.
3. Commit, then tag and push:

   ```
   git tag v1.0.1
   git push origin v1.0.1
   ```

The workflow refuses tags that do not match the version in
`app/config.xml`.

## What the signed package can do

The `.wgt` is signed with your author certificate and your Samsung
distributor certificate. A distributor certificate created for development
lists the DUIDs of your TVs, so the package installs only on those TVs (in
Developer Mode). For the Samsung store, upload the same package to TV Seller
Office; see [SAMSUNG_CERTIFICATION.md](SAMSUNG_CERTIFICATION.md).

## How the build works on Linux

- `scripts/ci/setup-emsdk.sh` downloads the Samsung Emscripten SDK 1.39.4.7
  from the public Samsung download page (cached between runs).
  Emscripten 1.39 needs Python 3.11 (it uses `distutils`, removed in 3.12)
  and `libxml2`.
- OpenH264 is a git submodule; the build jobs check it out with
  `submodules: true`. To update it, check out the new tag inside
  `wasm/third_party/openh264`, commit the new submodule commit and update the
  version in `CREDITS.md`.
- `scripts/build.sh` is the Linux equivalent of `build-openh264.bat` +
  `build-wasm.bat`; keep their source lists and flags in sync.
- `scripts/ci/setup-tizen-cli.sh` installs the Tizen Studio 6.1 CLI; the
  installer only accepts a destination inside the home directory.
- `scripts/ci/package.sh` writes the signing profile itself instead of using
  `tizen security-profiles add`: on Linux that command stores passwords in the
  desktop keyring, which does not exist on a CI runner, and signing then fails
  with "Invalid password". The profile and the certificates live in a
  temporary directory removed at the end. The script also checks that both
  signatures are in the package, because the Tizen CLI can produce an
  unsigned package without reporting an error.
