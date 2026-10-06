# NOTICE: modifications to Tasmota

This repository is a modified version of **Tasmota** (https://github.com/arendst/Tasmota, GPLv3), based on upstream release **v15.0.1**. It is **not** the original Tasmota. This file lists the changes in accordance with GPLv3 section 5(a). The modified source files additionally carry a notice in their headers.

Modifications by nineti GmbH (stromleser / energieleser), 2025–2026.

## Modified upstream files

| File | Change |
|---|---|
| `tasmota/include/tasmota_version.h` | Version set to 15.0.1.3 |
| `tasmota/my_user_config.h` | Added `USE_STROMLESER_CLOUD` |
| `tasmota/include/tasmota.h` | Added setting `SET_STROMLESER_API_KEY` |
| `tasmota/include/i18n.h` | Added command name `SlKey` |
| `tasmota/tasmota.ino` | Calls to `CustomResetButton*()` and `StatusLed*()` in setup and main loop |
| `tasmota/tasmota_support/support_button_v4.ino` | Added custom reset button (GPIO 9, 7 s hold) and status LED (GPIO 2) |
| `tasmota/tasmota_xdrv_driver/xdrv_01_9_webserver.ino` | Added stromleser cloud client: config page (`/sc`), commands `SlKey`, `SlSync`, `SlCheckKey`, `SlStatus`, periodic upload to `https://api.energieleser.com/ingest`, key check at `/check-key`; the server certificate is verified against Tasmota's built-in trust anchors |
| `tasmota/tasmota_xdrv_driver/xdrv_10_scripter.ino` | Added mDNS / Shelly emulation support for the script engine (`USE_SCRIPT_MDNS`); default script text with stromleser instructions |
| `lib/libesp32/HttpClientLight/src/HttpClientLight.{h,cpp}` (LGPL-2.1 library) | Added `begin(url, trust anchors)` so HTTPS requests can verify the server certificate (2026-10) |
| `.gitignore` | No longer ignores `tasmota/user_config_override.h` and `platformio_tasmota_cenv.ini`, so they are part of the source; ignores `build_output` |

## Added files

| File | Purpose | Origin |
|---|---|---|
| `tasmota/user_config_override.h` | Feature selection, German locale, stromleser branding, GPIO template, `STROMLESER_NO_CLOUD` switch | Based on the published Tasmota configuration of ottelo (ottelo.jimdo.de), adapted by nineti GmbH |
| `platformio_tasmota_cenv.ini` | PlatformIO build environments (`*_ottelo`, `tasmota32c3_stromleser_nocloud`) | Based on the same ottelo configuration, adapted by nineti GmbH |
| `bins/` | Prebuilt firmware binaries | Built from this repository |
| `CHECKSUMS.txt`, `NOTICE.md`, `README.md` | Documentation | nineti GmbH |

`README.md` replaces the upstream README, which is kept as `README_TASMOTA_UPSTREAM.md`.

Removed from upstream: the GitHub Actions workflows in `.github/workflows/` (Tasmota's own CI and release jobs). They are not needed to build the firmware.

## Third-party code

All other files are unchanged upstream Tasmota and its bundled libraries (`lib/`), each under its own license as stated in the respective file or library folder. The SML smart meter driver is unmodified upstream code.

## Not covered by this repository

The stromleser cloud backend and apps are separate works that are not part of the firmware.
