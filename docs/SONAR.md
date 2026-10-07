# Code analysis (SonarQube Cloud)

`.github/workflows/sonar.yml` analyzes the code with
[SonarQube Cloud](https://sonarcloud.io) on every push to `main` and every
pull request: bugs, vulnerabilities, code smells, duplication and test
coverage for JavaScript and C++. The settings are in
[`sonar-project.properties`](../sonar-project.properties).

## One-time setup

1. Sign in to <https://sonarcloud.io> with GitHub and import the
   organization (free for public repositories).
2. Add the `mosaico-tv` repository as a project. In
   **Administration → Analysis Method**, turn **Automatic Analysis off**: the
   workflow runs the analysis (automatic analysis does not support C++ or
   coverage).
3. Create a token (**My Account → Security**) and save it in GitHub as the
   repository secret `SONAR_TOKEN` (Settings → Secrets and variables →
   Actions).
4. The workflow uses organization `ervinnotari` and project key
   `ervinnotari_mosaico-tv` (the defaults when importing from GitHub). If
   yours are different, set the repository **variables** `SONAR_ORGANIZATION`
   and `SONAR_PROJECT_KEY`, and update the badge in `README.md`.

Without `SONAR_TOKEN` (for example, pull requests from forks, which do not
receive secrets) the tests still run and the scan is skipped with a notice.

## What is analyzed

| Part | Analysis | Coverage |
|---|---|---|
| `app/js`, `app/css`, `app/index.html` | JavaScript, CSS, HTML | `node --test --experimental-test-coverage` (lcov) |
| Portable C++ (`rtsp_protocol`, `rtsp_url`, `md5`, `video`, `h264`, `h265`) | C++, with the compilation database from Build Wrapper around `scripts/ci/cpp-tests.sh` | `g++ --coverage` + `gcovr` |
| C++ that builds only with the Samsung SDK (`player_main`, `native_player`, `soft_*`, `discovery`, `bench`, `events`, `rtsp_connection`) | not in the compilation database, so skipped by the C++ analyzer | — |
| `tools/`, `scripts/` | analyzed | excluded |

`app.js`, `nav.js` and the TV-only C++ are excluded from the coverage
metric on purpose: they are tested end to end on a real TV
(`tools/e2e`), which cannot run in CI.

## Running the coverage locally

```
node --test --experimental-test-coverage "tests/js/*.test.js"
```

On Linux, the C++ coverage (needs `g++` and `gcovr`):

```
CXXFLAGS="--coverage -O0" OUT=build/cpp/unit_tests sh scripts/ci/cpp-tests.sh
gcovr --root . --filter wasm/src/ --filter wasm/include/ build/cpp
```
