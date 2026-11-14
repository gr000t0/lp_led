# Validierung

Hosttests prüfen SSID-/Passwortgrenzen, fehlende Terminierung, PSK-Regeln, leeres Passwort,
RGB-Länge/Reihenfolge sowie Prepare-Write-Fragmente, Overflow, falsche Handles, Lücken,
Überlappungen und Cancel/Reset. AddressSanitizer und UndefinedBehaviorSanitizer sind aktiv;
LeakSanitizer ist wegen der Ausführungsumgebung deaktiviert. Parser allokiert nicht dynamisch.

Kein ESP32-C6 oder Smartphone verfügbar: folgende Tests sind **noch durchzuführen**:

| Test | Erwartung |
|---|---|
| Boot ohne NVS | grüne LED, Advertising, lpsd-Verbindungsversuch |
| WLAN mit Internet | IP und SNTP-Zeit im Status |
| WLAN aus/falsche Credentials | Timeout/Fehler, BLE weiterhin bedienbar |
| Pairing und RGB | verschlüsselter Write, korrekte Farben und Readback |
| SSID + Passwort + 01 | Verbindung, Speicherung erst nach GOT_IP |
| Neustart | gespeicherte Zugangsdaten geladen |
| Ungültige neue Daten | bisherige gespeicherte Daten erhalten |
| Passwort-Read | abgewiesen |
| Lange Werte / abgebrochener Long Write | vollständige Übernahme bzw. Verwerfen |
| Status mit MTU 23 | konsistenter Read-Blob-Snapshot |
| Disconnect beim Provisioning | beide Felder erneut erforderlich |
| Queue voll | ATT-Fehler, kein unbegrenztes Wachstum |
| Forget 02, Neustart | Standardzugangsdaten |
| Fallback an/aus | Verhalten gemäß Einstellung, keine NVS-Überschreibung |
| Light-Sleep wiederholt | Module stoppen und starten erneut |
| Deep-Sleep | Neustart und Timer-Wakeup-Bit |
| GPIO-Wakeup | externer Pegel weckt, ungültiger EXT1-Pin abgewiesen |
| Strommessung | aktive/schlafende Phase und Übergangskosten erfassen |

Sleep-Rollback ist bestmöglich, keine Garantie bei partiellen Treiberfehlern.
Serielle Fehler auswerten. Alte Smartphone-GATT-Caches/Bonds ggf. entfernen und neu entdecken.
