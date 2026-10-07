# Samsung TV Seller Office — publishing notes

What is needed to publish Mosaico on the Samsung TV app store, what is
already done, and how Samsung reviewers can test the app without access to
the developer's cameras.

## Seller account

| Seller type | How | Where the app can be published |
|---|---|---|
| Public Seller | Sign up in TV Seller Office | United States only |
| Partner Seller | Offline contract with Samsung HQ or a local subsidiary, then a partnership request approved by a Samsung Content Manager | Countries agreed in the contract |

Publishing in Brazil therefore requires a **partnership with Samsung**, plus an
**age rating certificate** for that country. Sources:
[Becoming Seller Office Members](https://developer.samsung.com/tv-seller-office/guides/membership/becoming-seller-office-member.html),
[Registering Applications](https://developer.samsung.com/tv-seller-office/guides/applications/registering-application.html),
[Launch Checklist](https://developer.samsung.com/tv-seller-office/checklists-for-distribution/launch-checklist.html).

## Checklist

| Item | Status |
|---|---|
| Signed package with `config.xml`, `author-signature.xml`, `signature1.xml` | Done (`scripts\package.bat` → `out\Mosaico.wgt`) |
| Unique Tizen ID and version `x.y.z` | `MosaicoTv1.Mosaico` 1.0.0 — change in `app/config.xml` if Seller Office assigns another ID |
| App title matching the default language | "Mosaico" in every language; UI in Portuguese and English (follows the TV language, English fallback); description in English and Portuguese in `config.xml` |
| Icons 512×423 and 1920×1080 | Done: `store/icon_512x423.png`, `store/icon_1920x1080.png` |
| Four screenshots, 1280×720 or 1920×1080, under 500 kB each | **To do**: capture on the TV (the DevTools screenshot API does not work on Tizen 6.0) |
| Only required privileges | `internet`, `tv.inputdevice` |
| Privacy policy URL | `docs/PRIVACY.md` — publish it (e.g. GitHub Pages) and set the support e-mail |
| Support e-mail | mosaicotv.talk@outlook.com |
| Back/Exit keys and resume after multitasking | Back asks before exiting; cameras pause in background and reconnect on return |
| Credits and licenses | "About" screen in the app; `NOTICE`, `THIRD_PARTY_NOTICES.md` |
| Same author certificate for every update | Keep `author.p12` and its password safe |

## Test instructions for reviewers

Reviewers do not have the developer's cameras, so the submission must include
a camera stream reachable from the internet. The app accepts host names, so a
small public test server is enough.

### Setting up a public test stream

On any internet-facing machine with Node 22+ and ffmpeg (for example a small
cloud VM), with TCP port 8554 open and a DNS name pointing to it:

```
ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=25 -t 60 -c:v libx264 \
  -x264-params keyint=25:aud=1 -pix_fmt yuv420p -f h264 test_h264.h264
ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=25 -t 60 -c:v libx265 \
  -x265-params keyint=25:repeat-headers=1:aud=1 -f hevc test_h265.hevc
node tools/rtsp-test-server/server.js test_h264.h264 8554 &
node tools/rtsp-test-server/server.js test_h265.hevc 8556 &
```

The test server has no authentication; the app works with or without a user
name and password.

### Text for the "Test information" field

> The app plays RTSP cameras. To test without a camera, use these public test
> streams:
>
> - H.264: `rtsp://<test-host>:8554/test`
> - H.265 (full screen only): `rtsp://<test-host>:8556/test`
>
> Steps:
> 1. On first launch the app measures the TV performance for about 6 seconds.
> 2. Choose "Add manually", set "Model" to "Other (enter the path)" with
>    ◀ ▶, enter the host in "IP or address", port 8554 and path `/test`, and
>    save. Repeat with port 8556 for H.265.
> 3. The mosaic shows the H.264 camera live. The H.265 tile shows a message
>    that H.265 plays only in full screen.
> 4. Press OK on a tile to open it in full screen (H.265 included). Back
>    returns to the mosaic.
> 5. ▲ on the first row opens the menu: layouts 1/4/8/16, pages and settings
>    (gear icon) with the camera list and the "About" (licenses) screen.
> 6. Back on the mosaic asks before exiting.
>
> The UI follows the TV language: Portuguese for Portuguese, English for any
> other language. The names above are the English ones.
>
> The ONVIF search ("Search the network") finds cameras on the local network
> only, so it cannot find the public test streams.

## Before submitting

- Run `scripts\test.bat` and `scripts\test-tv.bat <tv-ip>`.
- Bump the version in `app/config.xml` for every new upload.
- Review the H.264/H.265 patent note in `THIRD_PARTY_NOTICES.md`.
