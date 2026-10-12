<!-- SPDX-FileCopyrightText: 2026 Thomas Stoll
     SPDX-License-Identifier: Apache-2.0 -->

# OTA Suite

**Ein wiederverwendbares Fundament für ESP32-Projekte: WLAN einrichten,
das Gerät im Browser bedienen und Firmware bewusst aktualisieren.**

OTA Suite entstand für [EBCTester](https://github.com/Gemelon/EBCTester).
Ein Batterietester soll auch ohne dauerhaften PC auskommen und seine lokale
Oberfläche ohne Internet bereitstellen. Updates dürfen dabei nicht ungefragt
einen laufenden Test unterbrechen.

WLAN-Einrichtung, Webinterface und Updates sind aber keine Besonderheiten eines
Batterietesters. Deshalb sind sie hier als eigenständige Komponenten
zusammengefasst: In einem neuen Projekt sollen sie wiederverwendet werden,
statt jedes Mal neu geschrieben zu werden.

## Funktionen

- Geschützter Einrichtungs-AP, WLAN-Zugangsdaten in NVS und Wiederverbindung.
- Lokales Bootstrap-Webinterface, ohne CDN oder Internetabhängigkeit.
- Deutsch und Englisch, englische Rückfallsprache und erweiterbare Oberfläche.
- Automatische **Verfügbarkeitsprüfung**, aber ausschließlich **manuelle
  Firmwareinstallation** nach Bestätigung.
- Prüfung von Manifest, Zielgerät, Version, Downloadgröße und SHA256.
- HTTPS-Zertifikatsprüfung und Bootloader-Rollback für neue Images.
- Optionale WS2812-Status-LED und explizites Firmware-Publishing.

OTA Suite benötigt keine SD-Karte und keinen EBCTester-Code.
Die Anwendung kann eigene Tabs, Einstellungsseiten und SD-Sprachen ergänzen.

## Architektur und verwandte Repositories

| Paket | Aufgabe |
| --- | --- |
| [EBCTester](https://github.com/Gemelon/EBCTester) | Anwendung: Batterietests, Aufzeichnung, Diagramme, DCIR und MariaDB |
| **ota-suite** | Gemeinsame WLAN-, Web- und Update-Dienste |
| [sd-card-manager](https://github.com/Gemelon/sd-card-manager) | Optionale SD-Verwaltung als Erweiterung der gemeinsamen Oberfläche |

| Komponente / Verzeichnis | Aufgabe |
| --- | --- |
| `components/network_manager` | AP, WLAN, NVS-Einstellungen und SNTP |
| `components/web_interface` | Anmeldung, CSRF-Schutz, lokale UI und Erweiterungen |
| `components/update_check` | Manifest, numerische Versionen und Zeitplan |
| `components/ota_manager` | Manueller Download, Imageprüfung und Rollback |
| `components/device_app` | Startreihenfolge und Bootbestätigung |
| `components/status_led` | Optionale RGB-Anzeige |
| `cmake/publish_firmware.cmake` | Versionierte Binärdatei und atomisches Manifest |
| `examples/esp32s3-starter` | Kleine Beispielanwendung |

## Herunterladen und Beispiel kompilieren

```powershell
git clone https://github.com/Gemelon/ota-suite.git
```

Das Repository ist derzeit privat; ein berechtigtes GitHub-Konto ist nötig.
Getestet wird mit **ESP-IDF 6.0.1**, VS Code und ESP-IDF-Erweiterung.
Der erste Build benötigt Internet für verwaltete Komponenten.

1. `examples/esp32s3-starter` als ESP-IDF-Projekt öffnen.
2. Ziel `esp32s3` wählen und das tatsächliche Board konfigurieren.
3. Manifest-Adresse in **OTA Suite update checks** auf den eigenen
   HTTPS-Server einstellen; die vorhandene Adresse ist nur ein Beispiel.
4. Mit **ESP-IDF: Build your project** bauen und für die Erstinstallation
   Bootloader, Partitionstabelle und Anwendung seriell flashen.

Die Beispielpartitionen verlangen mindestens **4 MiB Flash**; jedes OTA-Slot
ist `0x1e0000` Bytes groß. Das ist nicht die 16-MiB-Partitionstabelle von
EBCTester. Board, Imagegröße und Partitionen vor dem Flashen prüfen.
Eine geänderte Partitionstabelle lässt sich nicht per OTA installieren.

Der AP heißt standardmäßig `OTASuite-XXXXXX`. Ohne konfiguriertes Passwort
wird einmalig ein zufälliges erzeugt, in NVS gespeichert und seriell ausgegeben.
Mit diesem Passwort verbinden und unter `http://192.168.4.1` als `admin`
anmelden. Das AP-Passwort ist gleichzeitig das Admin-Passwort.
Die Status-LED ist standardmäßig deaktiviert; GPIO bei Aktivierung prüfen.

Lokale `sdkconfig`, Zugangsdaten, Editor- und Build-Dateien nicht committen.

## In eigene Anwendungen einbinden

Das `components`-Verzeichnis über `EXTRA_COMPONENT_DIRS` einbinden und im
Anwendungskomponent `device_app` als Abhängigkeit angeben.
Anwendungsspezifische Dienste vor dem gemeinsamen Start initialisieren:

```c
#include "device_app.h"
#include "esp_err.h"

void app_main(void)
{
    ESP_ERROR_CHECK(device_app_start());
}
```

`device_app_start()` startet die Dienste und bestätigt ein anstehendes Image
nach zehn Sekunden. Rollback aktiv lassen. Anwendungen mit kritischen Diensten
müssen eigene Gesundheitsprüfungen ergänzen; das Zeitfenster ist kein
vollständiger Hardware-Selbsttest. Netzwerkkonnektivität ist keine Voraussetzung
für die Bootbestätigung.

Zum Einzelstart die öffentlichen Header und die Reihenfolge in
`components/device_app/device_app.c` verwenden.
Die Anwendung benötigt `otadata`, zwei ausreichend große OTA-Slots, das
ESP-Zertifikatsbundle und für HTTPS eine gültige Uhrzeit.

### Eigene Oberfläche und Sprachen

Erweiterungen vor Serverstart über `web_interface_register_extension()`
registrieren. `placement="settings"` fügt eine Einstellungskarte hinzu;
ansonsten entsteht ein Tab. Bis zu acht Erweiterungen und insgesamt
72 HTTP-Handler sind vorgesehen.

- `CONFIG_OTA_SUITE_WEB_TAB_ORDER`: Reihenfolge der Tabs; der erste öffnet initial.
- `CONFIG_OTA_SUITE_WEB_PRIMARY_TABS`: Optionale Gruppierung der Tester-/Verwaltungstabs.
- `CONFIG_OTA_SUITE_WEB_ACTIVITY_URL`: Optionaler lokaler Status-Endpunkt
  mit `link.connected`, `framesOk`, `framesBad` und `ageMs` für die RX-Anzeige.
- Name und Browser-Titel kommen aus dem Anwendungsnamen, nicht aus „OTA Suite“.

Die Kataloge liegen in `components/web_interface/assets/en.json` und `de.json`.
Statische Texte verwenden `data-i18n`, dynamische Texte `I18n.t/f`.
`I18n.message` nur für Protokollmeldungen verwenden, niemals für Benutzerdaten.
Bei geladenen Erweiterungen `I18n.apply(pane)` aufrufen.

SD-Sprachen sind standardmäßig deaktiviert. Eine Anwendung kann
`CONFIG_OTA_SUITE_WEB_SD_LANGUAGES` aktivieren und die authentifizierten
Endpunkte `/api/i18n/languages` und `/api/i18n/language?code=...` bereitstellen.
EBCTester implementiert dies mit der SD-Speichersperre.
Ein Sprachwechsel lädt den Browser neu, führt aber keine Gerätebefehle aus.

## Firmware veröffentlichen

Der Publisher ist **nicht** automatisch an den Beispielbuild angehängt.
Ein vorhandenes, beschreibbares Zielverzeichnis und eigene Serverwerte angeben:

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

`FIRMWARE_ORIGIN` enthält Schema, Host und optionalen Port.
`FIRMWARE_URL_PATH` ist leer oder beispielsweise `/my-device`, ohne abschließenden
Slash. Das lokale Ziel darf mit oder ohne abschließenden Pfadtrenner angegeben
werden. Der Manifest-URL im Gerät muss zum Serververzeichnis passen.

Der Publisher prüft Größe und SHA256 nach dem Kopieren, erhält immutable
`<project>-<version>.bin`, ersetzt den Kompatibilitätsnamen und veröffentlicht
`firmware.json` zuletzt durch Umbenennen. Eine identische Version mit anderen
Bytes wird abgelehnt. Version erhöhen, niemals eine bestehende Veröffentlichung
überschreiben. SMB-/Serversemantik bestimmt die Atomizität; nur ein Publisher
zur gleichen Zeit. Publishing installiert keine Firmware.

Bei Firmwareweitergabe auch `LICENSE`, `NOTICE`, Drittanbieterhinweise und
die zugehörigen Lizenztexte mitliefern.

## Sicherheit, Grenzen und Tests

- Updates werden nur manuell gestartet; Prüfungen installieren nichts.
- HTTPS prüft CA, Hostname und Datum, ohne Redirects oder HTTP-Rückfall.
- HTTP ist nur nach ausdrücklicher Freigabe für ein vertrauenswürdiges
  Testnetz zulässig und schützt die Serveridentität nicht.
- SHA256 ist keine digitale Signatur. Secure Boot, signierte Images und
  NVS-/Flash-Verschlüsselung sind gesonderte Deployment-Aufgaben.
- Das Webinterface verwendet HTTP Basic Authentication; nicht ins Internet
  stellen. Seriellen Zugang und gespeicherte Zugangsdaten schützen.
- Fehler werden gemeldet; es gibt keinen automatischen Installations-Retry.

Mit Node.js und CMake die Hosttests ausführen:

```powershell
npm test
```

`CMAKE_EXE` kann einen abweichenden CMake-Pfad setzen.
`tests/update_manifest.test.c` prüft den tatsächlichen Parser mit
`update_manifest.c`, `update_policy.c` und cJSON über einen Host-C-Compiler.
Hosttests ersetzen keine Hardwaretests zu Stromausfall, TLS und Rollback.
Fehlerberichte und Beiträge mit bereinigten Logs und Reproduktionsschritten
sind willkommen; keine Geheimnisse veröffentlichen.

## Lizenz und Urheber

**Copyright 2026 Thomas Stoll.** Ursprünglicher Code und Dokumentation stehen
unter **[Apache-2.0](LICENSE)**, einschließlich gewerblicher Nutzung und Verkauf
unter den Lizenzbedingungen. Siehe [NOTICE](NOTICE).
SPDX-Hinweise kennzeichnen Quelldateien; Formate ohne Kommentare sind in
[.reuse/dep5](.reuse/dep5) zugeordnet. Geänderte Dateien bei Weitergabe
kennzeichnen und vorhandene Hinweise erhalten. Fremde Bestandteile werden
nicht unter die eigene Projektlizenz gestellt.

## Drittanbieter und ihre Lizenzen

| Bestandteil | Urheber / Herkunft | Lizenz und Hinweise |
| --- | --- | --- |
| Bootstrap 5.3.3 | The Bootstrap Authors | MIT – [Originaltext](components/web_interface/assets/BOOTSTRAP-LICENSE); CSS unverändert |
| ESP-IDF 6.0.1 | Espressif Systems und weitere Beitragende | Apache-2.0 für den ursprünglichen Espressif-Code – [SDK-Lizenz](third_party/licenses/esp-idf/LICENSE) |
| cJSON | Dave Gamble und cJSON contributors | MIT – [Originaltext](third_party/licenses/cjson/LICENSE) |
| led_strip | Espressif Systems | Apache-2.0 – [Originaltext](third_party/licenses/led_strip/LICENSE) |
| FreeRTOS-Kernel | Amazon.com und Beitragende | MIT – [Originaltext](third_party/licenses/esp-idf/FreeRTOS-LICENSE.md) |
| lwIP | Swedish Institute of Computer Science und weitere Autoren | BSD-3-Clause – [Originaltext](third_party/licenses/esp-idf/lwip-COPYING) |
| Mbed TLS / TF-PSA-Crypto | Upstream-Autoren | Apache-2.0 als gewählte Dual-License-Option – [Mbed TLS](third_party/licenses/esp-idf/mbedtls-LICENSE), [TF-PSA-Crypto](third_party/licenses/esp-idf/tf-psa-crypto-LICENSE); dateispezifische Hinweise bleiben gültig |
| wpa_supplicant | Jouni Malinen und Beitragende | BSD-3-Clause – [Lizenz](third_party/licenses/esp-idf/wpa_supplicant-README), [Copyright](third_party/licenses/esp-idf/wpa_supplicant-COPYING) |
| http_parser | Igor Sysoev, Joyent und Node contributors | MIT – [Originaltext](third_party/licenses/esp-idf/http_parser-LICENSE.txt) |
| C-Laufzeit: Newlib / Picolibc | Verschiedene Autoren | Eigene Bedingungen – [Newlib](third_party/licenses/esp-idf/COPYING.NEWLIB), [Picolibc](third_party/licenses/esp-idf/COPYING.picolibc) |
| GCC-Laufzeit | Free Software Foundation und Beitragende | GPL-3.0 mit GCC Runtime Library Exception 3.1 – [GPL](third_party/licenses/toolchain/COPYING3), [Ausnahme](third_party/licenses/toolchain/COPYING.RUNTIME) |
| TLSF | Matthew Conte | BSD-3-Clause – [Originalhinweis](third_party/licenses/esp-idf/TLSF-NOTICE.txt) |
| Argtable3 / linenoise | Stewart Heitmann; Salvatore Sanfilippo und Pieter Noordhuis | [Argtable3](third_party/licenses/esp-idf/argtable3-LICENSE) / [linenoise](third_party/licenses/esp-idf/linenoise-LICENSE), BSD-Bedingungen |
| protobuf-c / SPIFFS, optionaler SDK-Umfang | Dave Benson und protobuf-c authors; Peter Andersson | [BSD-2-Clause](third_party/licenses/esp-idf/protobuf-c-LICENSE) / [MIT](third_party/licenses/esp-idf/spiffs-LICENSE) |
| Weitere SDK-Dateien | Urheber laut ursprünglichen Headern | [Originalhinweise aus dem Referenzbuild](third_party/licenses/esp-idf/SOURCE-NOTICES.txt), konservativer Umfang; [Standardtexte](third_party/licenses/standard/) |

Die Versionsauflösung erfolgt über die Component-Manager-Manifeste und den
Lockfile der konsumierenden Anwendung. Weitere SDK-Lizenztexte liegen in
[third_party/licenses/](third_party/licenses/).
Herkunft und Weitergabepflichten stehen in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Neue Abhängigkeiten und andere
SDK-Versionen erfordern eine erneute Prüfung; upstream Urheber- und
Lizenzhinweise dürfen nicht entfernt werden.
