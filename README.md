# tasmota-stromleser: Tasmota firmware for stromleser / energieleser devices

This repository contains the source code of the firmware used on stromleser (energieleser) ESP32-C3 devices by **nineti GmbH**. It is a **modified version of [Tasmota](https://github.com/arendst/Tasmota)** (based on release 15.0.1; current version 15.0.1.3) and is licensed under the **GNU General Public License v3.0**, like Tasmota itself. See [LICENSE.txt](LICENSE.txt).

- What we changed compared to Tasmota: **[NOTICE.md](NOTICE.md)**
- Original Tasmota documentation and README: [README_TASMOTA_UPSTREAM.md](README_TASMOTA_UPSTREAM.md)
- Original copyright holders: see the headers of the individual source files (Theo Arends and Tasmota contributors, among others). They are unchanged.

## Releases and binaries

The binaries in [`bins/`](bins/) were built from this repository with the commands under [Building](#building). The source for a binary is the repository state at its git tag.

| Binary | Tag | Build environment | Cloud client |
|---|---|---|---|
| `bins/sl-tasmota-15.0.1.3-cloud-factory.bin` | `v15.0.1.3` | `tasmota32c3_ottelo` | included |
| `bins/sl-tasmota-15.0.1.3-cloud-ota.bin` | `v15.0.1.3` | `tasmota32c3_ottelo` | included |
| `bins/sl-tasmota-15.0.1.3-nocloud-factory.bin` | `v15.0.1.3` | `tasmota32c3_stromleser_nocloud` | not included |
| `bins/sl-tasmota-15.0.1.3-nocloud-ota.bin` | `v15.0.1.3` | `tasmota32c3_stromleser_nocloud` | not included |

`*-factory.bin` is the complete flash image (bootloader, partition table, Tasmota safeboot and firmware) for flashing at offset `0x0`. `*-ota.bin` is the firmware only, for the Tasmota web UI (*Firmware Upgrade*). The safeboot part of the factory image is the unmodified Tasmota `tasmota32c3-safeboot` environment, built from this repository too.

SHA-256 checksums: [CHECKSUMS.txt](CHECKSUMS.txt). Verify with `shasum -a 256 -c CHECKSUMS.txt`.

### Earlier builds

Earlier builds (labelled 15.0.1.1, 15.0.1.2, 15.1 and 15.1-v2) are no longer distributed. 15.0.1.3 has the same features and adds server certificate checking for the cloud connection. If your device runs one of the earlier builds, update it to the matching 15.0.1.3 build above (cloud or no-cloud). If you received one of those builds and want its source code, contact us at the address below.

## Installing your own firmware

The firmware does not restrict updates. You can flash a build you made yourself through the Tasmota web UI (*Firmware Upgrade*, upload an `*-ota.bin`) or over USB/serial with `esptool.py --chip esp32c3 write_flash 0x0 <factory.bin>`. No keys or signatures are needed.

## Building

Requirements: [PlatformIO](https://platformio.org/). The platform, framework and toolchain versions are pinned in the `platformio*.ini` files. Then, in the repository root (the path must not contain spaces):

```bash
platformio run -e tasmota32c3-safeboot
```

```bash
platformio run -e tasmota32c3_ottelo
```

```bash
platformio run -e tasmota32c3_stromleser_nocloud
```

Build the safeboot environment first: the factory image includes it. The results are written to `build_output/firmware/` (`tasmota32c3_ottelo.factory.bin` and `tasmota32c3_ottelo.bin`, and the same for the no-cloud environment). Builds are not bit-identical because of embedded timestamps.

The environments are defined in [`platformio_tasmota_cenv.ini`](platformio_tasmota_cenv.ini), the feature configuration in [`tasmota/user_config_override.h`](tasmota/user_config_override.h) (enabled via `-DTASMOTA32C3_OTTELO`). The cloud client is switched on by `USE_STROMLESER_CLOUD` in `tasmota/my_user_config.h`; the no-cloud environment sets `-DSTROMLESER_NO_CLOUD`, which removes it in `user_config_override.h`.

## License

Copyright (C) Theo Arends and Tasmota contributors; modifications Copyright (C) nineti GmbH.

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY. See [LICENSE.txt](LICENSE.txt).

Questions about licensing or source code: info@nineti.de
