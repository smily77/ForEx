# ForEx 2.1 – GSM/LTE Wechselkurs- und Weltuhren-Anzeige

ESP8266-basiertes Tischgerät mit ST7735-TFT-Display. Zeigt die lokale Uhrzeit (Zürich), sechs Weltuhren und drei CHF-Wechselkurse (USD, EUR, GBP) an. Datenverbindung und Zeitabfrage erfolgen ausschliesslich über das **BK-7670 LTE-Modul** – kein WLAN nötig.

---

## Hardware

| Bauteil | Modell | Bemerkung |
|---------|--------|-----------|
| Mikrocontroller | ESP8266 (NodeMCU / WeMos D1 Mini) | 3.3 V, 80 MHz |
| Display | ST7735 TFT 160 × 128 px | SPI |
| LTE-Modul | BK-7670 (AND Global) | UART, 19 200 Baud |
| Helligkeitssteuerung | Potentiometer oder LDR | an A0 |

---

## Pin-Belegung

| GPIO | Richtung | Funktion | Bauteil |
|:----:|----------|----------|---------|
| 0 | Ausgang | PWM – Hintergrundbeleuchtung | TFT LED |
| 2 | Ausgang | TFT Data/Command | ST7735 DC |
| 4 | **Eingang** | UART RX ← Modul TX | BK-7670 TX |
| 5 | **Ausgang** | UART TX → Modul RX | BK-7670 RX |
| 12 | Ausgang | TFT Reset | ST7735 RST |
| 13 | Ausgang | SPI MOSI | ST7735 |
| 14 | Ausgang | SPI CLK | ST7735 |
| 15 | Ausgang | TFT Chip-Select | ST7735 CS |
| A0 | Eingang | Analog – Helligkeit | Poti / LDR |

> **Spannungsebene:** ESP8266 arbeitet mit 3.3 V. Prüfen, ob der BK-7670 ebenfalls 3.3 V-UART-Logik verwendet; andernfalls Level-Shifter einsetzen.

---

## Dateistruktur

```
ForEx_2_1/
├── ForEx_2_1.ino         Hauptsketch: Konfiguration, setup(), loop(), Watchdog
├── X_InternetInfo.ino    Wechselkurse via LTE (AT+HTTPINIT / HTTPACTION)
├── x_subroutines.ino     AT-Kommunikation, LTE-Init, GSM-Zeit, DST-Berechnungen
├── x_printsubs.ino       Display-Funktionen (clearTFTScreen, displayMainScreen)
├── AirportDatabase.h     Zeitzonendatenbank (~40 Flughäfen, im PROGMEM)
├── schema.md             Schaltungsschema als Mermaid-Diagramme
└── README.md             Diese Datei
```

---

## Konfiguration

### Provider / APN

In `ForEx_2_1.ino`:

```cpp
#define ACTIVE_PROVIDER  1   // 0 = M-Budget Mobile (Swisscom)
                             // 1 = Digital Republic (Salt)
```

| Wert | Provider | Netz | APN |
|:----:|----------|------|-----|
| `0` | M-Budget Mobile | Swisscom | `gprs.swisscom.ch` |
| `1` | Digital Republic | Salt | `internet` |

Anderen Provider: `LTE_APN` und `PROVIDER_NAME` direkt als String eintragen.

### Weltuhren (Airport-Codes)

In `ForEx_2_1.ino`:

```cpp
const char* AIRPORT_CODES[7] = {
  "ZRH",  // 0: Lokale Zeit (immer Zürich)
  "DXB",  // 1: Dubai
  "SIN",  // 2: Singapore
  "IAD",  // 3: Washington DC
  "SYD",  // 4: Sydney
  "BLR",  // 5: Bangalore
  "SFO"   // 6: San Francisco
};
```

Index 0 ist immer die Heimatzone (ZRH). Die übrigen sechs können frei aus `AirportDatabase.h` gewählt werden. DST wird automatisch berechnet (EU, US, AU, NZ).

---

## Funktionsweise

### Startup-Ablauf

```
1. TFT initialisieren
2. BK-7670 initialisieren (AT, APN, PDP-Kontext)
3. Zeitzonen aus AirportDatabase.h laden (kein Datenvolumen)
4. Zeit vom GSM-Netz (NITZ / AT+CCLK?) → kein Datenvolumen
5. Wechselkurse per HTTP/LTE abrufen
6. Display rendern, Watchdog starten
```

### Hauptschleife

- Jede Minute: Display neu rendern
- Täglich um **17:00 Uhr**: Zeit re-synchronisieren + neue Kurse laden
- A0-Analogwert → PWM-Helligkeit (im DEBUG-Modus deaktiviert)
- Watchdog: 60 s Timeout → `ESP.reset()`

### Wechselkurs-Abfrage (`X_InternetInfo.ino`)

**API:** `https://api.exchangerate-api.com/v4/latest/EUR`
**Kein API-Key** nötig. Fastly CDN – breite TLS-Kompatibilität mit dem BK-7670.

```
Response (~2500 Bytes bis USD, alphabetische Reihenfolge):
{"base":"EUR","rates":{...,"CHF":0.9560,...,"GBP":0.8575,...,"USD":1.0853,...}}
```

**Berechnung:** Alle Kurse sind EUR-relativ. CHF/USD ergibt sich aus:

```
CHF/USD = rates["CHF"] / rates["USD"]  =  0.956 / 1.085  =  0.881
```

Analoges gilt für EUR und GBP. Das Ergebnis ist jeweils «wie viele CHF kostet 1 Einheit der Fremdwährung».

**AT-Sequenz:**
```
AT+HTTPTERM          → offene Session beenden
AT+HTTPINIT          → HTTP-Stack starten
AT+HTTPPARA="URL",…  → URL setzen (mit RX-ISR-Pause beim TX)
AT+HTTPACTION=0      → GET-Request senden
  warten auf: +HTTPACTION: 0,200,<len>
AT+HTTPREAD=0,<len>  → Body lesen (in 1024-Byte-Chunks)
AT+HTTPTERM          → Session beenden
```

### Zeitabfrage (kostenfrei)

```
AT+CTZU=1    → NITZ aktivieren (Modul übernimmt Netzzeit automatisch)
AT+CCLK?     → Lokalzeit + Zeitzone vom Operator lesen
```

Format: `+CCLK: "YY/MM/DD,HH:MM:SS±ZZ"` (ZZ = Viertelstunden).
Die Zeit wird von Operator-Lokalzeit → UTC → Zürich-Lokalzeit umgerechnet.

---

## Datenvolumen

| Vorgang | Methode | Volumen |
|---------|---------|:-------:|
| Zeitabfrage | AT+CCLK? (NITZ/GSM) | **0 Byte** |
| Wechselkurse | HTTP GET exchangerate-api.com | ~2 500 Byte |
| **Gesamt pro Tag** | | **~2 500 Byte** |

Ideal für PrePaid-SIM mit minimem Datenvolumen.

---

## Display-Layout

```
┌─────────────────────────────────┐  160 × 128 px
│  HH:MM   DD MMM                 │  Lokalzeit ZRH (gross, fett, weiss)
│                                 │
│  DXB       SIN       IAD        │  Weltuhren Zeile 1 (gelb)
│  HH:MM    HH:MM    HH:MM        │
│                                 │
│  SYD       BLR       SFO        │  Weltuhren Zeile 2 (gelb)
│  HH:MM    HH:MM    HH:MM        │
│                                 │
│  USD       EUR       GBP        │  Wechselkurse (grün)
│  x.xxxx   x.xxxx   x.xxxx      │  CHF pro 1 Einheit
└─────────────────────────────────┘
```

---

## Benötigte Arduino-Bibliotheken

| Bibliothek | Zweck |
|------------|-------|
| `TimeLib` | Zeitverwaltung (`setTime`, `hour()`, `minute()` …) |
| `SoftwareSerial` | UART zum LTE-Modul |
| `Adafruit_GFX` | Grafik-Grundlage |
| `Adafruit_ST7735` | TFT-Treiber |
| `Streaming` | Komfortabler Serial-Output |
| `Ticker` | ISR-basierter Watchdog-Timer |

Board: **ESP8266** (Arduino Board Manager → `esp8266 by ESP8266 Community`)

---

## Debug-Modus

```cpp
#define DEBUG true   // → false für Produktionsbetrieb
```

Mit `DEBUG true` gibt der Serial Monitor (9600 Baud) alle AT-Kommandos, Antworten, GSM-Zeit und berechnete Kurse aus. Im Produktionsbetrieb (`false`) wird ausserdem die Helligkeit über A0 geregelt.
