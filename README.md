# OTA Suite

Reusable ESP-IDF components for secure-by-default, **manually installed**
firmware updates. The package also includes a Wi-Fi provisioning/web UI
starter, an optional WS2812 status LED, a firmware publication script, and an
ESP32-S3 starter application. It is self-contained and independent of other
application checkouts.

## Contents

| Path | Purpose |
| --- | --- |
| `components/ota_manager` | Manual OTA download, image validation, rollback and boot confirmation |
| `components/update_check` | Strict manifest parsing, semantic numeric versions, scheduled checks |
| `components/network_manager` | Wi-Fi setup AP/station, NVS credentials and SNTP |
| `components/web_interface` | Authenticated, CSRF-protected local status, provisioning and update UI |
| `components/device_app` | Convenience startup sequence for the components above |
| `components/status_led` | Optional WS2812 OTA state indicator |
| `cmake/publish_firmware.cmake` | Explicit immutable-binary and manifest publisher |
| `examples/esp32s3-starter` | Minimal ESP-IDF application using `device_app_start()` |
| `tests` | Browser behavior and release-publisher tests; C manifest test source |

Requires ESP-IDF 6.0 or later. The included starter partition table is for
ESP32-S3 boards with at least 4 MiB flash; each OTA slot is 0x1e0000 bytes.
Check the board's actual flash and partition layout before flashing. For other
chips, adjust the target, partition table, LED GPIO, and hardware-specific
settings in the consuming application.

## Build the starter

Open `examples/esp32s3-starter` as the ESP-IDF project, select target
`esp32s3`, and build with the ESP-IDF extension. Its defaults enable rollback
and certificate/date validation. The default manifest is the reserved
placeholder `https://updates.example.org/firmware.json`; set
**OTA Suite update checks → Fixed firmware manifest URL** to your real
HTTPS manifest before deployment. For local HTTP-only testing, explicitly
enable **OTA Suite firmware updates → Allow unencrypted HTTP OTA**; use only a
trusted test network.

The setup AP name starts with `OTASuite-`. A random admin/AP password is
generated and stored when no configured password is provided, and is printed
to the serial log for first setup. Connect to that AP and open
`http://192.168.4.1`; the login is `admin` with the AP password. HTTP Basic
authentication is not encrypted: do not expose the web server outside a
trusted LAN or protected AP. The AP password is also an admin credential.

The status LED is disabled by default. Enable
**OTA Suite status LED → Enable the OTA status LED** and set its GPIO to match
the board before use. The default GPIO is a common ESP32-S3 DevKitC-1 value,
not a universal board setting.

## Integrate in an existing application

Add/copy the components directory to the project or add it to
`EXTRA_COMPONENT_DIRS`; ensure ESP-IDF component-manager manifests are
resolved, then make the application's `main` component require `device_app`.
Call `device_app_start()` once after initializing any application-specific
services whose health must be considered during the OTA boot-confirmation
window:

```c
#include "device_app.h"
#include "esp_err.h"

void app_main(void)
{
    /* Initialize application-specific critical services first. */
    ESP_ERROR_CHECK(device_app_start());
}
```

This starts rollback-aware OTA, networking, update checks, the web UI and
optional status LED, then confirms a pending boot after ten seconds. Leave
rollback enabled. The complete application should provide its own health
checks before confirmation if service initialization alone is not sufficient.
Ensure its partition table has `otadata` and two OTA app slots large enough
for the firmware. HTTPS updates require the ESP certificate bundle, valid
certificate dates (therefore synchronized time), and a server certificate
trusted by that bundle.

To start components individually instead, use the public headers in each
component's `include` directory and preserve the initialization order in
`components/device_app/device_app.c`. Manual installation is initiated only
by the authenticated web UI after a user confirms the offered version; update
availability checks never download or install an image.

## Release publishing (explicit only)

The publisher is a standalone CMake script and is **not attached to the
example's build**. It has no built-in network share, path or private origin.
Create/choose an existing writable destination and supply project, version,
target and public origin explicitly. Example from the package root after
building the starter; replace the placeholders with deployment values:

```powershell
cmake "-DSOURCE_FILE=examples\esp32s3-starter\build\ota_suite_starter.bin" `
  "-DDESTINATION_DIR=D:\firmware-releases\" `
  "-DFIRMWARE_VERSION=0.1.0" `
  "-DFIRMWARE_PROJECT=ota_suite_starter" `
  "-DFIRMWARE_TARGET=esp32s3" `
  "-DFIRMWARE_ORIGIN=https://updates.example.org" `
  -P cmake\publish_firmware.cmake
```

Publishing explicitly copies and verifies the actual `.bin`, refuses to
replace an immutable version with different bytes, updates a compatibility
`<project>.bin`, then replaces `firmware.json` last using same-directory
temporary files and rename. The publication origin must match the configured
device manifest origin/directory. Use HTTPS in production. The manifest binds
project, target, numeric `major.minor.patch` version, exact binary URL, size,
SHA-256, and notes. Run one publisher at a time; remote filesystem rename
semantics determine the atomicity guarantees.

## Update guarantees and limits

- Only the manifest is checked automatically (when connected and on schedule);
  binary download and installation require an authenticated, CSRF-protected
  user action and confirmation.
- HTTPS validates the CA chain, hostname and certificate dates. Redirects are
  rejected. HTTP is disabled unless explicitly opted in, and does not protect
  firmware authenticity in transit.
- Download size and SHA-256 are checked against the manifest before the OTA
  image is finalized or selected for boot. ESP-IDF also validates image
  integrity and target. SHA-256 alone is not a digital signature; use signed
  firmware/secure boot where the threat model requires authenticity.
- A pending image is confirmed only after local services survive the startup
  window; earlier crash/reset allows bootloader rollback. Network access is
  not part of boot health.
- NVS and flash encryption, secure boot, and protection of physical/serial
  access are deployment responsibilities. The local administration server
  uses HTTP; never expose it to an untrusted network.

## Tests

With Node.js and CMake available, run the browser and release-publisher tests:

```powershell
npm test
```

`tests/update_manifest.test.c` is the native C manifest/version test and is
intended to be built in an ESP-IDF test/application environment with the
`update_check` component and cJSON dependency.
