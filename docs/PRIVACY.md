# Privacy Policy — Mosaico

_Last updated: 2026-10-06_

Mosaico ("the app") is a Samsung Smart TV application that shows video
from RTSP cameras and DVRs on the user's own network. This policy explains
what the app does with information. The app is open source; its behavior can
be verified in the source code.

## Summary

- The app does **not** collect, sell or share personal data.
- The app has **no** accounts, analytics, advertising or tracking.
- The developer does **not** run any server for the app and receives **no**
  data from it.

## Information stored on the TV

To connect to the cameras configured by the user, the app stores on the TV,
in the app's local storage:

- camera names and RTSP addresses (IP address or host name, port and path);
- the user name and password of each camera, as entered by the user;
- display preferences (layout, page) and a performance profile of the TV
  (decoding speed, number of processor cores, model and firmware version),
  used to decide which layouts the TV can handle.

This information never leaves the TV except to authenticate with the cameras
themselves. Passwords are masked in the app's diagnostic messages. Removing a
camera in the app, resetting the app data or uninstalling the app deletes
this information.

## Network access

The app connects only to:

- the cameras and DVRs configured by the user, over RTSP, and — for
  Hikvision devices — over HTTP to request a key frame when a camera is
  opened in full screen;
- the local network, when the user starts an ONVIF search, to discover
  cameras (WS-Discovery messages sent to the local subnet) and to read their
  channel list after the user enters the device's credentials;
- the DNS servers configured on the TV, to resolve camera host names.

Video is played directly on the TV and is not recorded or uploaded.

## Children

The app is a utility for viewing the user's own cameras and does not target
children.

## Changes

Changes to this policy will be published in the project repository with a new
date at the top.

## Contact

Questions about this policy: mosaicotv.talk@outlook.com
