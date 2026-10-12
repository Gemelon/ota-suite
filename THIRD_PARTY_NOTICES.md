<!-- SPDX-FileCopyrightText: 2026 Thomas Stoll
     SPDX-License-Identifier: Apache-2.0 -->

# Third-party notices

OTA Suite's original source and documentation: **Copyright 2026 Thomas Stoll,
Apache-2.0**, see [LICENSE](LICENSE) and [NOTICE](NOTICE).

## Bundled and managed dependencies

- Bootstrap **5.3.3**, https://github.com/twbs/bootstrap/tree/v5.3.3:
  Copyright 2011-2024 The Bootstrap Authors, MIT.
  The CSS bundle and [complete upstream license](components/web_interface/assets/BOOTSTRAP-LICENSE)
  are preserved unchanged.
- `espressif/cjson`, https://components.espressif.com/components/espressif/cjson:
  MIT, Dave Gamble and cJSON contributors.
  [License snapshot](third_party/licenses/cjson/LICENSE) from 1.7.19~2.
- `espressif/led_strip`, https://components.espressif.com/components/espressif/led_strip:
  Apache-2.0, original Espressif copyright headers remain in managed source.
  [License snapshot](third_party/licenses/led_strip/LICENSE) from 3.1.0~1.
  The consuming application's lockfile determines resolved versions.

## ESP-IDF and compiler runtime

License snapshots are copied verbatim from ESP-IDF **6.0.1** and the Xtensa
toolchain **esp-15.2.0_20251204**, under [third_party/licenses/](third_party/licenses/).
They are not a replacement license for SDK source.

| Upstream file / component | Snapshot and license |
| --- | --- |
| SDK `LICENSE` | [Apache-2.0](third_party/licenses/esp-idf/LICENSE) for Espressif's original code |
| FreeRTOS-Kernel `LICENSE.md` | [MIT](third_party/licenses/esp-idf/FreeRTOS-LICENSE.md) |
| lwIP `COPYING` | [BSD-3-Clause](third_party/licenses/esp-idf/lwip-COPYING) |
| Mbed TLS `LICENSE` and TF-PSA-Crypto `LICENSE` | [Mbed TLS](third_party/licenses/esp-idf/mbedtls-LICENSE), [TF-PSA-Crypto](third_party/licenses/esp-idf/tf-psa-crypto-LICENSE); Apache-2.0 option selected where dual-licensed |
| wpa_supplicant `README` and `COPYING` | [BSD terms](third_party/licenses/esp-idf/wpa_supplicant-README), [copyright/history](third_party/licenses/esp-idf/wpa_supplicant-COPYING) |
| http_parser `LICENSE.txt` | [MIT](third_party/licenses/esp-idf/http_parser-LICENSE.txt) |
| esp_libc `COPYING.NEWLIB`, `COPYING.picolibc` | [Newlib](third_party/licenses/esp-idf/COPYING.NEWLIB), [Picolibc](third_party/licenses/esp-idf/COPYING.picolibc), multiple authors/terms |
| esp_wifi / esp_phy / esp_coex `lib/LICENSE` | [Wi-Fi](third_party/licenses/esp-idf/esp_wifi-LICENSE), [PHY](third_party/licenses/esp-idf/esp_phy-LICENSE), [coexistence](third_party/licenses/esp-idf/esp_coex-LICENSE) |
| GCC `COPYING3`, `COPYING.RUNTIME`, `COPYING3.LIB` | [GPL-3.0](third_party/licenses/toolchain/COPYING3), [Runtime Library Exception](third_party/licenses/toolchain/COPYING.RUNTIME), [LGPL-3.0](third_party/licenses/toolchain/COPYING3.LIB) |
| Toolchain Newlib `COPYING.LIBGLOSS` | [libgloss terms](third_party/licenses/toolchain/COPYING.LIBGLOSS) |

File-specific BSD and other upstream notices remain applicable, including
inside Mbed TLS and libc. Build-tool licenses do not automatically become the
application license. Not every SDK component is linked into every application.
Additional components and other SDK/toolchain versions require their own review.

Additional unchanged SDK texts cover
[Argtable3](third_party/licenses/esp-idf/argtable3-LICENSE),
[linenoise](third_party/licenses/esp-idf/linenoise-LICENSE),
[protobuf-c](third_party/licenses/esp-idf/protobuf-c-LICENSE) and
[SPIFFS](third_party/licenses/esp-idf/spiffs-LICENSE).
[TLSF's original license block](third_party/licenses/esp-idf/TLSF-NOTICE.txt)
comes from https://github.com/mattconte/tlsf (`tlsf.h`).
[SOURCE-NOTICES.txt](third_party/licenses/esp-idf/SOURCE-NOTICES.txt) preserves
individual notices from the EBCTester reference build, a conservative SDK
superset; [standard license texts](third_party/licenses/standard/) accompany
SPDX-only notices. Optional components are not necessarily linked.

## Distribution

Keep the project's `LICENSE`, `NOTICE`, this document, the complete
`third_party/licenses` directory and Bootstrap license with redistributions.
For firmware, provide them as accompanying documentation; a bare `.bin` or
a remote link alone does not deliver all required notices. The publisher only
copies firmware and its manifest, not these documentation files.

Retain upstream copyright, permission, patent and attribution notices,
applicable upstream `NOTICE` files and BSD disclaimers. Preserve file headers
when redistributing SDK source. Mark changed Apache-covered files, and do not
imply upstream endorsement. The project license does not relicense dependencies.
