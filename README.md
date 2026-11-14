# Modulares LPSD-Projekt für ESP32-C6

Erweiterung des ursprünglichen LED-Projekts für ESP-IDF 6.0.x. BLE verwendet
**Bluedroid mit esp_ble_gap_* / esp_ble_gatts_*, kein NimBLE**. GPIO 8 bleibt LED-Pin.

## Bauen und starten

In einen neuen Ordner entpacken und die ESP-IDF-Umgebung aktivieren:

```sh
. ~/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Port anpassen; Monitor mit Ctrl+] verlassen. Die ursprüngliche Konfiguration liegt
in `original-config/`. Nicht deren sdkconfig zurückkopieren: BLE und PM waren dort aus.
Die neue Konfiguration entsteht aus `sdkconfig.defaults`. Spätere Änderungen daran
überschreiben bestehende sdkconfig-Werte nicht; menuconfig nutzen oder sdkconfig neu erzeugen.
Die BLE-4.2-Feature-Auswahl aktiviert die verwendeten Legacy-Advertising-APIs; die
separate BLE-5-Feature-Auswahl ist ausgeschaltet. Host bleibt Bluedroid.
2 MB Flash, eine Factory-Partition, kein OTA.

## Verhalten

LED startet gedimmt grün. BLE wirbt als **LPSD-ESP32-C6**. WLAN verwendet gespeicherte
Zugangsdaten, andernfalls **lpsd / lpsd2024**. Nach Erhalt einer IP startet SNTP mit
pool.ntp.org. Systemzeit ist UTC; lokale Anzeige berücksichtigt österreichische Sommer-/Winterzeit.
Ohne erreichbaren NTP-Server steht im Status `not-synchronized`; eine IP garantiert keinen Internetzugang.
BLE bleibt auch bei fehlgeschlagener WLAN-Verbindung erreichbar.

## Architektur

| Klasse / Ordner | Verantwortung |
|---|---|
| led / led-manager | init, deinit, set_color, turn_on, turn_off, blink |
| BleManager / ble-manager | Bluedroid, start/stop, Advertising, GAP/GATTS-Callbacks, Pairing |
| DeviceService / device-service | GATT-Tabelle, Protokoll, Eingabeprüfung, Befehlsqueue, Status |
| WifiManager / wifi-manager | connect, stop, Retries, Zustände, set_power_save |
| TimeManager / time-manager | SNTP, Zeitzone, Systemzeit und formatierte Anzeige |
| PowerManager / power-manager | Frequenzskalierung, Sleep/Wakeup, Duty-Cycle-/Break-even-Helfer |
| SettingsManager / settings-manager | Zugangsdaten laden, speichern, vergessen (NVS) |
| Application / main | Module verbinden, Befehle ausführen, Fallback- und Speicherpolitik |

`main/main.cpp` enthält nur Einstieg und Start. **main/app_config.hpp** enthält die
leicht veränderbaren Einstellungen. Der Ablauf in Application ist eine Beispielanwendung;
die Manager bieten unabhängig aufrufbare Methoden. Objekte leben während der gesamten
Firmware-Laufzeit. Mutationen gehören in einen Anwendungstask, nicht parallel in mehrere Tasks.
BLE-Callbacks stellen Befehle in eine Queue, statt WLAN/NTP abzuwarten oder zu schlafen.

## nRF Connect

Scannen, **LPSD-ESP32-C6** verbinden, Services entdecken. Standardmäßig verschlüsselte
Verbindung per LE Secure Connections / Just Works; ggf. Pairing anstoßen und abwarten.
Schreiben als **Write Request (mit Antwort)**.
Service: `7c2a0000-6e3b-4a2e-9f71-8b6d20c14590`.

| Characteristic UUID | Funktion und Format |
|---|---|
| 7c2a0001-6e3b-4a2e-9f71-8b6d20c14590 | SSID: UTF-8/Text, 1–32 Bytes |
| 7c2a0002-6e3b-4a2e-9f71-8b6d20c14590 | Passwort: Text, 8–63 Bytes oder 64 Hex-Zeichen PSK; leer für offenes Netz |
| 7c2a0003-6e3b-4a2e-9f71-8b6d20c14590 | Aktion: Hex 01 anwenden, Hex 02 gespeicherte Daten vergessen |
| 7c2a0004-6e3b-4a2e-9f71-8b6d20c14590 | RGB: genau drei Bytes R G B, lesbar/schreibbar |
| 7c2a0005-6e3b-4a2e-9f71-8b6d20c14590 | Status: UTF-8 lesen, keine Notifications |

Beispiel: SSID als Text `lpsd`, Passwort als Text `lpsd2024`, dann Aktion als **Hex 01**
(ein Byte, nicht Text "01"). Anschließend Status lesen. LED rot: **Hex FF 00 00**;
grün 00 FF 00, blau 00 00 FF, aus 00 00 00. BLE benutzt RGB, der Treiber übernimmt WS2812-GRB.

SSID und Passwort müssen in derselben Verbindung gesetzt werden. Disconnect verwirft
unvollständige Eingaben. Zugangsdaten werden erst nach GOT_IP gespeichert; ein Fehlversuch
überschreibt den bisherigen gespeicherten Datensatz nicht. ATT-Erfolg bedeutet nur
"Befehl angenommen", nicht WLAN-Erfolg. Passwort ist nicht lesbar und wird nicht geloggt.
02 entfernt gespeicherte Daten und eine ausstehende Speicheranforderung, trennt aber nicht
sofort das aktuelle WLAN. Nach Neustart wird ohne gespeicherte Daten lpsd verwendet.

Für lange Strings MTU 185 anfordern (Android; iOS verwaltet MTU selbst), alternativ
Prepare Write + Execute Write. Je SSID/Passwort eine separate Transaktion. Lücken,
Überlappungen, Überlänge und mehrere Attribute pro Prepare-Transaktion werden abgewiesen.
Status-Read-Blob verwendet einen Snapshot, damit zwischen Fragmenten aktualisierte Zeit
keinen inkonsistenten Text ergibt. Typische ATT-Fehler: 0x0D Länge, 0x07 Offset,
0x11 Queue voll, 0x80 ungültige/unvollständige Zugangsdaten oder unbekannte Aktion.

## Eigene Erweiterungen und Fallback

In `main/app_config.hpp`: WLAN-Retries/Timeout, Laden/Speichern von Zugangsdaten,
`fallback_to_default_wifi` (standardmäßig false), `ble_advertise_after_disconnect`,
`ble_encrypt_writes`, NTP, Zeitzone, LED und Sleep-Modus. Aktivierter WLAN-Fallback versucht
nach fehlgeschlagenen anderen Zugangsdaten einmal lpsd; er überschreibt NVS nicht.
Advertising-Intervalle stehen in BleConfig (Einheiten 0,625 ms).

Eigene Pairing-/Verbindungsparameter in BleManager::on_gap_event/on_gatts_event ergänzen.
Nicht aus dem Bluetooth-Callback schlafen oder BleManager::stop aufrufen.
Neue Funktion: DeviceCommand erweitern, in DeviceService validieren und einreihen,
in Application::handle die passende Manager-Methode aufrufen. Einen anderen Service über
die GattService-Schnittstelle übergeben. Aktuell ein Service und eine Verbindung.

## Power und Duty Cycling

```cpp
power.configure(40, 160, false);       // dynamische CPU-Frequenz
power.configure(40, 160, true);        // Auto-Light-Sleep bei geeigneter Idle-Phase
power.light_sleep_for(5'000'000);      // kehrt nach 5 s zurück
power.deep_sleep_for(5'000'000);       // nach Wakeup Neustart
power.light_sleep_until_button(gpio); // Level-Wakeup
power.deep_sleep_until_button(gpio);  // C6: geeigneter LP-GPIO für EXT1
```

Direkte PowerManager-Methoden stoppen keine anderen Module. Die Application-Methoden
`sleep_light_for`, `sleep_deep_for` und Button-Varianten koordinieren SNTP/WLAN/BLE-Stopp,
LED-Aus und RMT-Freigabe. Nach Light-Sleep starten die Module erneut; BLE neu verbinden.
Deep-Sleep verliert RAM-Zustand und nicht gespeicherte Zugangsdaten.

`mode` in app_config.hpp: Interactive (Standard), LightSleepTimerDemo oder DeepSleepTimerDemo.
Demos: ungefähr 10 Sekunden aktiv, 5 Sekunden Schlaf. Übergänge und Verbindungsaufbau
zählen zur aktiven Zeit; kein garantierter 2/3-Duty-Cycle. Timerwerte sind konfigurierbar.

Auto-Light-Sleep standardmäßig aus. Bei laufendem BLE verhindert ein ESP_PM_NO_LIGHT_SLEEP-Lock
System-Light-Sleep; diese Konfiguration verspricht keine BLE-Verbindung während des Schlafs.
WLAN nutzt WIFI_PS_MIN_MODEM. Für eigene Sensor-/WLAN-Versuche BLE stoppen und Auto-Sleep erproben.
GPIO nach Board-Schaltplan wählen, extern definierten Ruhepegel sicherstellen. Keine internen
Pulls werden gesetzt. Aktiver Pegel beim Einschlafen kann sofort wieder wecken. BOOT-Pin ist
nicht automatisch EXT1-fähig; GPIO 9 nicht blind für Deep-Sleep verwenden.

`duty_cycle(active_ms, period_ms)` liefert den Anteil (0,01 = 1 %).
`break_even_seconds(extra_j, awake_w, sleep_w)` liefert extra_j/(awake_w-sleep_w).
extra_j muss zusätzliche Übergangskosten gegenüber idealem Sleep im selben Zeitfenster enthalten.
Das ist ein vereinfachter Rechenhelfer, **kein automatisch energieoptimaler Scheduler**.
Strom und Übergangsenergie am Board messen; LED-Aus trennt die WS2812-Stromversorgung nicht.

## Grenzen und Prüfung

Just Works verschlüsselt, authentifiziert aber keinen Benutzer gegen aktive Angreifer.
Jeder Client in Reichweite kann grundsätzlich nach Pairing Einstellungen ändern. Für ein Produkt
Freigabe/Provisioning-Fenster oder authentifiziertes Pairing ergänzen. Kein dauerhaftes Bonding
angefordert. NVS ist ohne zusätzliche Flash-/NVS-Verschlüsselung ungeschützt.
WPA2-Personal/offenes WLAN, kein Enterprise-/eduroam-Provisioner.

`bash tests/run_host_tests.sh`: Parserprüfung mit Address-/UndefinedBehaviorSanitizer.
Buildnachweis und offene Hardwaretests stehen in docs/VALIDATION.md und docs/BUILD_REPORT.txt.
