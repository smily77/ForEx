# ForEx v2.1-FR-SSL (Frankfurter)

Variante von ForEx_2_1, die Wechselkurse ueber die **Frankfurter API** bezieht
statt ueber exchangerate-api.com.

## Warum eine eigene Variante?

Die Frankfurter API (`api.frankfurter.app`) laeuft hinter **Cloudflare**.
Cloudflare blockiert den TLS-Fingerprint (JA3) des BK-7670-Moduls, weshalb
die eingebauten `AT+HTTP*`-Befehle dort nicht funktionieren.

## Architektur-Unterschied zu ForEx_2_1

| | ForEx_2_1 | ForEx_2_1_Frankfurter |
|---|---|---|
| **API** | `api.exchangerate-api.com` (Fastly) | `api.frankfurter.app` (Cloudflare) |
| **SSL/TLS** | BK-7670 intern (`AT+HTTP*`) | BearSSL auf dem ESP8266 |
| **TCP** | Modul macht alles | Modul oeffnet nur rohe TCP-Verbindung |
| **Antwortgroesse** | ~2500 Bytes (alle Waehrungen) | ~120 Bytes (nur CHF, USD, GBP) |
| **Zusaetzliche Lib** | keine | `ESP_SSLClient` (mobizt) |

### Datenfluss

```
BK-7670 LTE-Modul          ESP8266
   AT+CIPOPEN ──────> LTEClient (Arduino Client)
   (roher TCP-Socket)        |
                        ESP_SSLClient (BearSSL)
                             |
                     api.frankfurter.app:443
```

Das LTE-Modul oeffnet nur eine rohe TCP-Verbindung (`AT+CIPOPEN`).
Die TLS-Verschluesselung uebernimmt BearSSL auf dem ESP8266 -- dessen
JA3-Fingerprint wird von Cloudflare akzeptiert.

## Neue Komponenten

### LTEClient-Klasse (`X_InternetInfo.ino`)
Arduino-`Client`-Adapter fuer das BK-7670-Modul. Implementiert
`connect()`, `write()`, `read()`, `available()`, `stop()` ueber die
AT-Befehle `CIPOPEN`, `CIPSEND`, `CIPRXGET`, `CIPCLOSE`.

### ESP_SSLClient
BearSSL-Wrapper von mobizt, der auf einem beliebigen Arduino-`Client`
aufsetzt. Konfiguration:
- `setInsecure()` -- kein Zertifikatscheck (spart RAM)
- `setBufferSizes(4096, 512)` -- 4 KB RX, 512 Bytes TX

## Speicher

| Ressource | Wert |
|---|---|
| Freier Heap vor TLS | ~32 KB |
| BearSSL RX-Buffer | 4096 Bytes |
| BearSSL TX-Buffer | 512 Bytes |
| LTEClient RX-Buffer | 256 Bytes (statisch) |
| CPU-Takt | 160 MHz (fuer TLS-Handshake) |

## Weitere Aenderungen gegenueber ForEx_2_1

- **SIM-PIN wird nicht mehr im Serial-Log angezeigt** (Sicherheit)
- **`String.reserve()`** in allen AT-Funktionen (weniger Heap-Fragmentierung)
- **`ESP.wdtFeed()`** in allen Warte-Schleifen (verhindert WDT-Reset)
- **Statische Puffer** fuer LTEClient (spart Stack)

## Abhaengigkeit installieren

Arduino IDE: *Sketch > Bibliothek einbinden > Bibliotheken verwalten*
> "ESP_SSLClient" suchen und installieren.

## Hardware

Identisch mit ForEx_2_1 -- keine Hardware-Aenderungen noetig.
