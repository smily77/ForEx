# ForEx_1_1 - Optimierte Version

Vereinfachte Version des Desktop Widget Programms für ESP8266 mit ST7735 Display.

## Features

- **Aktuelle Uhrzeit und Datum** (lokale Zeitzone)
- **Weltuhren**: Zeigt die Zeit in 6 verschiedenen Städten
- **Wechselkurse**: CHF zu USD, EUR, GBP
- **Automatisches DST**: Korrekte Daylight Saving Time Berechnung für EU, US, Australien und Neuseeland
- **Minimale Updates**: Nur beim Start und einmal täglich um Mitternacht
- **Einfache Konfiguration**: Nur Airport-Codes ändern!

## Konfiguration

### Städte ändern

Die Konfiguration ist sehr einfach! Öffne `ForEx_1_1.ino` und ändere nur die Airport-Codes im Konfigurations-Bereich:

```cpp
// ============================================
// KONFIGURATION: Nur Airport-Codes ändern!
// ============================================
const char* AIRPORT_CODES[7] = {
  "ZRH",  // 0: Lokale Zeit (Zürich, Schweiz)
  "DXB",  // 1: Dubai, VAE
  "SIN",  // 2: Singapore
  "IAD",  // 3: Washington DC, USA
  "SYD",  // 4: Sydney, Australien
  "BLR",  // 5: Bangalore, Indien
  "SFO"   // 6: San Francisco, USA
};
// ============================================
```

**Wichtig**: Der erste Code (Index 0) ist deine lokale Zeitzone!

### Unterstützte Airport-Codes

Die Airport-Datenbank (`AirportDatabase.h`) enthält über 100 wichtige Flughäfen weltweit:

#### Europa
ZRH, GVA, LHR, CDG, FRA, AMS, MAD, BCN, FCO, MXP, VIE, CPH, OSL, ARN, HEL, IST, ATH, PRG, WAW

#### Naher Osten
DXB, DOH, AUH, CAI, TLV, RUH, JED

#### Asien-Pazifik
SIN, HKG, BKK, ICN, NRT, HND, PEK, PVG, DEL, BOM, BLR, KUL, CGK, MNL, TPE

#### Australien & Neuseeland
SYD, MEL, BNE, PER, AKL, CHC, ADL

#### Nord- & Mittelamerika
JFK, EWR, LGA, IAD, DCA, BOS, ORD, DFW, IAH, DEN, LAX, SFO, SEA, PDX, LAS, PHX, MIA, ATL, YYZ, YVR, YUL, MEX

#### Südamerika
GRU, GIG, EZE, SCL, LIM, BOG

#### Afrika
JNB, CPT, NBO, ADD, LOS, ALG, TUN, CMN

### Wechselkurse ändern

In `ForEx_1_1.ino` findest du:

```cpp
// Wechselkurs-Daten
String fxSym[4] = {"CHF", "USD", "EUR", "GBP"};
```

Ändere die Währungscodes nach Bedarf. Der erste Code (Index 0) ist die Basis-Währung.

## Hardware

- **ESP8266** (getestet mit NodeMCU, WeMos D1 Mini)
- **ST7735 TFT Display** (160x128 Pixel)
- **BMP180** Drucksensor (optional)

### Pin-Belegung

```
TFT_CS  = GPIO 15
TFT_DC  = GPIO 2
TFT_RST = GPIO 12
LED     = GPIO 0 (für Helligkeitssteuerung)
```

## Installation

1. Öffne `ForEx_1_1.ino` in der Arduino IDE
2. Erstelle eine Datei `Credentials.h` mit deinen WiFi-Zugangsdaten:

```cpp
// Credentials.h
const char* ssid1 = "DeinWiFiName";
const char* password1 = "DeinWiFiPasswort";
const char* ssid2 = "AlternativesWiFi";  // Optional
const char* password2 = "AlternativesPasswort";  // Optional
```

3. Wähle die Airport-Codes wie oben beschrieben
4. Kompiliere und lade das Programm auf den ESP8266 hoch

## Bibliotheken

Folgende Bibliotheken werden benötigt:

- `TimeLib` by Michael Margolis
- `ESP8266WiFi` (im ESP8266 Board Package enthalten)
- `Adafruit_GFX` by Adafruit
- `Adafruit_ST7735` by Adafruit
- `SFE_BMP180` by Sparkfun (wenn Drucksensor verwendet wird)

## Wie es funktioniert

### Beim Start
1. Verbindung mit WiFi
2. Laden der Timezone-Daten aus der Airport-Datenbank
3. NTP-Zeit synchronisieren
4. Wechselkurse von frankfurter.app API laden
5. Display aktualisieren

### Im Betrieb
- Display wird jede Minute aktualisiert
- Um Mitternacht (00:00): Automatische Aktualisierung von NTP-Zeit und Wechselkursen
- Watchdog-Timer schützt vor Hängern

### DST-Berechnung
Das Programm berechnet Daylight Saving Time automatisch basierend auf den regionalen Regeln:

- **EU**: Letzter Sonntag März bis letzter Sonntag Oktober
- **US/Kanada**: Zweiter Sonntag März bis erster Sonntag November
- **Australien**: Erster Sonntag Oktober bis erster Sonntag April
- **Neuseeland**: Letzter Sonntag September bis erster Sonntag April
- Viele Länder haben **kein DST** (z.B. Dubai, Singapore, Bangalore)

## Fehlerbehebung

### Airport-Code nicht gefunden
Wenn ein Airport-Code nicht in der Datenbank gefunden wird:
- Der Display zeigt "ERROR: XXX not found!"
- Der Airport wird mit UTC (Zeitzone 0) als Fallback verwendet
- Füge den Airport-Code in `AirportDatabase.h` hinzu

### Neue Airport-Codes hinzufügen
Bearbeite `AirportDatabase.h` und füge einen Eintrag hinzu:

```cpp
{"XXX", stdOffset, dstOffset, dstType},
```

Wobei:
- `XXX` = 3-Buchstaben IATA Code
- `stdOffset` = Standard UTC Offset in Sekunden (z.B. 3600 für UTC+1)
- `dstOffset` = DST UTC Offset in Sekunden (z.B. 7200 für UTC+2)
- `dstType` = 0 (kein DST), 1 (EU), 2 (US), 3 (AU), 4 (NZ)

Beispiel für Berlin (UTC+1/+2, EU DST):
```cpp
{"TXL", 3600, 7200, 1},  // Berlin Tegel
```

## Optimierungen gegenüber Original

1. **Keine Wetter-API mehr** für Timezone-Daten
2. **Keine TimezoneDB-API** mehr nötig
3. **Kein Web-Interface** (weniger Code, mehr RAM)
4. **Lokale DST-Berechnung** (schnell und zuverlässig)
5. **Einfache Konfiguration** (nur Airport-Codes)
6. **Seltener Updates** (nur beim Start und täglich)
7. **Airport-Datenbank im PROGMEM** (spart RAM)

## Lizenz

Basierend auf dem Original Desktop_Widget_II Projekt.

## Support

Bei Fragen oder Problemen siehe die Kommentare im Code oder öffne ein Issue.
