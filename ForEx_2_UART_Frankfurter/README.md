# ForEx_2_UART_Frankfurter

Diese Variante basiert auf **ForEx_2_1_Frankfurter** (Cloudflare/BearSSL-Ansatz unverändert),
setzt das BK-7670 jedoch **ausschliesslich über den Hardware-UART** des ESP8266 ein.

## Kernänderung

- **SoftwareSerial entfernt**.
- Modem-Kommunikation läuft über `Serial` (UART0) mit `19200 Baud`.
- `Serial` ist exklusiv für das LTE-Modul reserviert.
- **Keine Debug-/Log-Ausgaben über Serial** (`Serial.print*`, `Serial.printf`, etc. entfernt).

## Verdrahtung (ESP8266 ↔ BK-7670)

- `GPIO3 / RX0` (ESP8266)  ←  `TX` (BK-7670)
- `GPIO1 / TX0` (ESP8266)  →  `RX` (BK-7670)
- Gemeinsame Masse (`GND`) und passende Versorgungsspannung sicherstellen.

## Architektur

Die Netzwerk- und TLS-Logik bleibt identisch zur Frankfurter-Variante:

- BK-7670 öffnet nur den TCP-Socket (`AT+CIPOPEN`, `AT+CIPSEND`, `AT+CIPRXGET`)
- TLS läuft via **BearSSL** auf dem ESP8266 (`ESP_SSLClient`)
- Ziel bleibt `https://api.frankfurter.app`

## Hinweis zum Betrieb

Da `Serial` vollständig für das Modem verwendet wird, gibt es **keine serielle Monitoring-Ausgabe**.
Status und Fehler werden nur über das Display signalisiert.
