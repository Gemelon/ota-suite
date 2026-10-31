<!-- SPDX-FileCopyrightText: 2026 Thomas Stoll
     SPDX-License-Identifier: Apache-2.0 -->

# OTA Suite

**A reusable foundation for ESP32 projects: configure Wi-Fi, operate a device
in the browser, and update firmware deliberately.**

OTA Suite was created for [EBCTester](https://github.com/Gemelon/EBCTester).
A battery tester should work without a continuously running PC and provide a
local interface without internet access. Updates must not interrupt a running
test without user action.

Networking, web interfaces, and updates are useful beyond battery testing.
They are independent components here so new projects can reuse them instead
of implementing the same infrastructure again.

## Features

- Protected setup AP, credentials in NVS, and Wi-Fi reconnection.
- Local Bootstrap web interface without a CDN.
- English/German UI, English fallback, and extensible interface.
- Automatic **availability checks**, but **manual installation** only after
  confirmation.
- Manifest, target, version, download size, and SHA256 checks.
- HTTPS certificate validation and bootloader rollback.
- Optional WS2812 status LED and explicit firmware publisher.

No SD card or EBCTester code is required. Applications can add tabs, settings
cards, and SD language providers.

## Architecture and related repositories

| Package | Responsibility |
| --- | --- |
| [EBCTester](https://github.com/Gemelon/EBCTester) | Battery testing, recording, charts, DCIR, and MariaDB |
| **ota-suite** | Shared networking, web UI, and updates |
| [sd-card-manager](https://github.com/Gemelon/sd-card-manager) | Optional storage and file-management UI |

| Component / directory | Responsibility |
| --- | --- |
| `components/network_manager` | AP, Wi-Fi, NVS settings, SNTP |
| `components/web_interface` | Login, CSRF protection, local UI, extensions |
| `components/update_check` | Manifest, numeric versions, schedule |
| `components/ota_manager` | Manual download, image validation, rollback |
| `components/device_app` | Startup sequence and boot confirmation |
| `components/status_led` | Optional RGB indicator |
| `cmake/publish_firmware.cmake` | Versioned image and atomic manifest |
| `examples/esp32s3-starter` | Minimal example application |

Development content is always English; requested UI translations are retained.
See [AGENTS.md](AGENTS.md).

## Download and build the example

```powershell
git clone https://github.com/Gemelon/ota-suite.git
```

This repository is public. Tested with **ESP-IDF 6.0.1**, VS Code, and the
ESP-IDF extension. The first build needs internet for managed components.

1. Open `examples/esp32s3-starter` as the ESP-IDF project.
2. Select `esp32s3` and configure the actual board.
3. Set your HTTPS manifest URL under **OTA Suite update checks**; the default
   is only a placeholder.
4. Use **ESP-IDF: Build your project**. For initial installation, flash the
   bootloader, partition table, and application serially.

The example requires at least **4 MiB flash**, with `0x1e0000`-byte OTA slots.
This is not EBCTester's 16-MiB partition table. Check board, image size, and
layout before flashing. Partition-table changes cannot be installed by OTA.

The default AP is `OTASuite-XXXXXX`. Without a configured password, a random
one is generated once, stored in NVS, and printed to serial. Connect using it,
open `http://192.168.4.1`, and log in as `admin`. The AP password is also the
admin password. The status LED is initially disabled; check its GPIO when enabling.
Do not commit local `sdkconfig`, credentials, editor settings, or build outputs.

## Integrating into an application

Add the package's `components` directory to `EXTRA_COMPONENT_DIRS`, and require
`device_app` from the application component. Initialize application-specific
services before shared startup:

```c
#include "device_app.h"
#include "esp_err.h"

void app_main(void)
{
    ESP_ERROR_CHECK(device_app_start());
}
```

`device_app_start()` starts services and confirms a pending image after ten
seconds. Keep rollback enabled. Critical applications need their own health
checks; the window is not a complete hardware self-test. Connectivity is not
required for boot confirmation.

For individual startup, use public headers and preserve the sequence in
`components/device_app/device_app.c`. Applications need `otadata`, two
sufficient OTA slots, the ESP certificate bundle, and valid time for HTTPS.

### UI extensions and languages

Register extensions with `web_interface_register_extension()` before server
startup. `placement="settings"` creates a settings card; otherwise a tab is
created. Up to 12 extensions and 80 total HTTP handlers are supported.

- `CONFIG_OTA_SUITE_WEB_TAB_ORDER`: Tab order; the first opens initially.
- `CONFIG_OTA_SUITE_WEB_PRIMARY_TABS`: Optional testing/administration grouping.
- `CONFIG_OTA_SUITE_WEB_ACTIVITY_URL`: Local status endpoint with
  `link.connected`, `framesOk`, `framesBad`, and `ageMs` for the RX indicator.
- Browser title and header come from the application's project name.

Catalogs are `components/web_interface/assets/en.json` and `de.json`.
Use `data-i18n` for static text and `I18n.t/f` for dynamic text.
Use `I18n.message` only for protocol messages, never user data.
Call `I18n.apply(pane)` on loaded extensions.

SD languages are disabled by default. An application can enable
`CONFIG_OTA_SUITE_WEB_SD_LANGUAGES` and provide authenticated
`/api/i18n/languages` and `/api/i18n/language?code=...` endpoints.
EBCTester supplies this provider with shared SD locking. Language changes
reload the page but do not issue device commands.

### Login sessions and settings confirmation

The login page at `/login` uses `admin` and the AP password. Login creates a
random 256-bit persistent `HttpOnly; SameSite=Lax` cookie. Under Settings,
select daily (24 hours, default) or weekly (7 days) login. The choice is saved
in NVS and applies to new logins. Sessions have fixed monotonic expiry without
renewal and are invalidated by device restart or logout. Eight concurrent
browser sessions are supported; a ninth login returns HTTP 429.

`POST /api/auth/login` accepts JSON `username`/`password` with `X-Web-Login: 1`.
Protected APIs require the cookie; mutations additionally require
`X-CSRF-Token` from `/api/status`. Logout is `POST /api/auth/logout`.
Basic-auth credentials are no longer accepted. Bundled login/CSS/language
assets are public, but status and extension assets remain authenticated.

`CONFIG_OTA_SUITE_WEB_SETTINGS_REQUIRE_PASSWORD` defaults to `y`. Every settings
save then requires the AP/admin password again, not just a valid session.
The built-in Wi-Fi and login-duration forms implement this. Application
settings endpoints must call `web_interface_authorize_settings_request(req)`
before parsing/applying data. Their UI must call
`await WebAuth.settingsHeaders()` and stop if it returns `null` (cancel).
Otherwise include its CSRF/password headers in that one save request. The UI
uses a masked dialog and does not remember the password for later saves.
Setting the option to `n` disables only the additional password check.
Operational commands still use `web_interface_authorize_request(req, true)`.

## Publishing firmware

The standalone publisher is **not attached to the example build**.
Supply an existing writable directory and your deployment values:

```powershell
cmake "-DSOURCE_FILE=examples\esp32s3-starter\build\ota_suite_starter.bin" `
  "-DDESTINATION_DIR=D:\firmware-releases" `
  "-DFIRMWARE_VERSION=0.1.0" `
  "-DFIRMWARE_PROJECT=ota_suite_starter" `
  "-DFIRMWARE_TARGET=esp32s3" `
  "-DFIRMWARE_ORIGIN=https://updates.example.org" `
  "-DFIRMWARE_URL_PATH=/my-device" `
  -P cmake\publish_firmware.cmake
```

`FIRMWARE_ORIGIN` is scheme, host, and optional port. `FIRMWARE_URL_PATH`
is empty or a path such as `/my-device`, without a trailing slash. The local
directory accepts either form of trailing separator. The device's manifest
URL must match the server directory.

The publisher verifies copied size/SHA256, preserves immutable
`<project>-<version>.bin`, replaces the compatibility name, and commits
`firmware.json` last by rename. A version with different bytes is rejected:
increase the version instead of overwriting a release. SMB/server semantics
determine atomicity; use one publisher at a time. Publishing does not install.

Firmware distributions must also include `LICENSE`, `NOTICE`, third-party
notices, and their license texts.

## Safety, limitations, and tests

- Only manual actions install updates; checks install nothing.
- Opening the web UI requests one availability check as soon as networking
  and the required HTTPS clock are ready, or reuses a check already in progress.
  It needs no settings-password confirmation and never installs firmware.
  Status polling does not repeat the request; failures remain visible for
  manual retry with **Check updates**.
- HTTPS checks CA, hostname, and dates; no redirects or HTTP fallback.
- HTTP requires explicit opt-in for a trusted test network and does not
  authenticate the server.
- SHA256 is not a signature. Secure Boot, signed images, and NVS/flash
  encryption are separate deployment responsibilities.
- Administration uses cookie sessions over unencrypted HTTP. Passwords and
  cookies can be intercepted; use only a trusted network. Do not expose it to the
  internet; protect serial access and stored credentials.
- Errors are reported; installation is not retried automatically.

With Node.js and CMake:

```powershell
npm test
```

Set `CMAKE_EXE` if necessary. `tests/update_manifest.test.c` tests the actual
parser with `update_manifest.c`, `update_policy.c`, and cJSON using a host
C compiler. Host tests do not replace power-loss, TLS, and rollback hardware
tests. Reports and contributions with sanitized logs and reproduction steps
are welcome; never publish secrets.

## License and authorship

**Copyright 2026 Thomas Stoll.** Original code and documentation are licensed
under **[Apache-2.0](LICENSE)**, including commercial use and sale under its
terms. See [NOTICE](NOTICE). SPDX headers cover source files; commentless
formats are attributed in [.reuse/dep5](.reuse/dep5). Mark redistributed
modified files and retain existing notices. Third-party components keep their
own licenses.

## Third-party components and licenses

| Component | Author / origin | License and notices |
| --- | --- | --- |
| Bootstrap 5.3.3 | The Bootstrap Authors | MIT - [original text](components/web_interface/assets/BOOTSTRAP-LICENSE); unmodified CSS |
| ESP-IDF 6.0.1 | Espressif Systems and contributors | Apache-2.0 for original Espressif code - [SDK license](third_party/licenses/esp-idf/LICENSE) |
| cJSON | Dave Gamble and cJSON contributors | MIT - [original text](third_party/licenses/cjson/LICENSE) |
| led_strip | Espressif Systems | Apache-2.0 - [original text](third_party/licenses/led_strip/LICENSE) |
| FreeRTOS-Kernel | Amazon.com and contributors | MIT - [original text](third_party/licenses/esp-idf/FreeRTOS-LICENSE.md) |
| lwIP | Swedish Institute of Computer Science and other authors | BSD-3-Clause - [original text](third_party/licenses/esp-idf/lwip-COPYING) |
| Mbed TLS / TF-PSA-Crypto | Upstream authors | Apache-2.0 option where dual-licensed - [Mbed TLS](third_party/licenses/esp-idf/mbedtls-LICENSE), [TF-PSA-Crypto](third_party/licenses/esp-idf/tf-psa-crypto-LICENSE); file-specific notices apply |
| wpa_supplicant | Jouni Malinen and contributors | BSD-3-Clause - [license](third_party/licenses/esp-idf/wpa_supplicant-README), [copyright](third_party/licenses/esp-idf/wpa_supplicant-COPYING) |
| http_parser | Igor Sysoev, Joyent, and Node contributors | MIT - [original text](third_party/licenses/esp-idf/http_parser-LICENSE.txt) |
| C runtime: Newlib / Picolibc | Multiple authors | Individual terms - [Newlib](third_party/licenses/esp-idf/COPYING.NEWLIB), [Picolibc](third_party/licenses/esp-idf/COPYING.picolibc) |
| GCC runtime | Free Software Foundation and contributors | GPL-3.0 with GCC Runtime Library Exception 3.1 - [GPL](third_party/licenses/toolchain/COPYING3), [exception](third_party/licenses/toolchain/COPYING.RUNTIME) |
| TLSF | Matthew Conte | BSD-3-Clause - [original notice](third_party/licenses/esp-idf/TLSF-NOTICE.txt) |
| Argtable3 / linenoise | Stewart Heitmann; Salvatore Sanfilippo and Pieter Noordhuis | [Argtable3](third_party/licenses/esp-idf/argtable3-LICENSE) / [linenoise](third_party/licenses/esp-idf/linenoise-LICENSE), BSD terms |
| protobuf-c / SPIFFS, optional SDK scope | Dave Benson and protobuf-c authors; Peter Andersson | [BSD-2-Clause](third_party/licenses/esp-idf/protobuf-c-LICENSE) / [MIT](third_party/licenses/esp-idf/spiffs-LICENSE) |
| Other SDK files | Authors in original headers | [Reference-build notices](third_party/licenses/esp-idf/SOURCE-NOTICES.txt), conservative scope; [standard texts](third_party/licenses/standard/) |

Component-manager manifests and the consuming application's lockfile resolve
versions. Other SDK texts are in [third_party/licenses/](third_party/licenses/).
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for provenance and
distribution obligations. New dependencies or SDK versions need review;
upstream copyrights and license notices must not be removed.
