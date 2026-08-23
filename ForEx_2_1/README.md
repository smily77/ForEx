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
#define ACTIVE_PROVIDER  0   // 0 = M-Budget Mobile (Swisscom)
                             // 1 = Digital Republic (Salt)
```

> Aktuell eingestellt: **0** (M-Budget Mobile, APN `gprs.swisscom.ch`).
> Im Betrieb bestätigt: `+COPS: 0,2,"22801",7` – MCC/MNC 228-01 (Swisscom), LTE.

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

- Jede Minute: Display neu rendern + Statuszeile auf den seriellen Port
- Alle 6 Stunden (zur vollen Stunde): Zeit re-synchronisieren (kostenlos)
- Täglich um **17:00 Uhr**: neue Kurse laden
- A0-Analogwert → PWM-Helligkeit, alle 200 ms, unabhängig von `DEBUG`
- Unaufgeforderte Meldungen (URCs) des LTE-Moduls abholen und verwerfen
- Watchdog: 180 s ohne Minutenwechsel → `ESP.restart()`
  (während des Kursabrufs pausiert, da dieser länger dauern kann)

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
  warten auf: +HTTPACTION: 0,200,chunk
AT+HTTPREAD=0,<max>  → Body lesen (in 1024-Byte-Blöcken)
AT+HTTPTERM          → Session beenden
```

> **Achtung – zwei Eigenheiten des Moduls:**
>
> 1. Die API antwortet mit `Transfer-Encoding: chunked`. `AT+HTTPACTION`
>    meldet dann `0,200,chunk` statt einer Byte-Länge. `AT+HTTPREAD` muss
>    deshalb mit der Puffergrösse aufgerufen werden; das Ende wird am
>    Abschlussmarker `+HTTPREAD: 0` erkannt.
> 2. Das Modul liefert den Body in 1024-Byte-Blöcken und stellt **jedem
>    Block eine eigene Zeile `+HTTPREAD: <n>` voran** – mitten in den
>    Nutzdaten. Diese Marker müssen vor dem Parsen entfernt werden
>    (`stripHttpReadMarkers()`), sonst zerreissen sie Zahlenwerte.

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
| `ESP8266WiFi` | nur um den WiFi-Stack abzuschalten (Core-Bestandteil) |

### Credentials.h

Der Sketch bindet `Credentials.h` ein, die `GPSII_PIN` (die PIN der
SIM-Karte) definiert. Diese Datei liegt bewusst **nicht** im Sketch-Ordner,
sondern im Arduino-Bibliotheksverzeichnis, damit sie ausserhalb des Repos
bleibt und von mehreren Sketches geteilt werden kann:

```
Dokumente\Arduino\libraries\MyLGFXConfigs\Credentials.h
```

```cpp
const char* GPSII_PIN = "1234";   // "" = SIM ohne PIN-Abfrage
```

> **Wichtig – keine `Credentials.h` im Sketch-Ordner anlegen!**
> `#include "Credentials.h"` durchsucht zuerst das Sketch-Verzeichnis.
> Eine Datei gleichen Namens dort verdeckt die echte lautlos, und der
> Sketch wird mit den falschen Werten gebaut. `.gitignore` enthält
> `Credentials.h` nur noch als Sicherheitsnetz.

Ob der Build die richtige Datei nimmt, zeigt `arduino-cli compile -v`:

```
Alternativen für Credentials.h: [MyLGFXConfigs@1.0.0]
```

Board: **ESP8266** (Arduino Board Manager → `esp8266 by ESP8266 Community`)

---

## Diagnose

Der serielle Port läuft **immer** mit **115200 Baud** – auch im
Produktionsbetrieb. Boot-Grund, Init-Schritte, Kursabruf und ein
Minutentakt werden grundsätzlich ausgegeben. Ohne diese Grundausgabe
liess sich ein Fehlerfall am Gerät nicht nachvollziehen.

Typischer Start (Sollzustand):

```
===== ForEx v2.1 =====
Boot-Grund : External System
Freier Heap: 46048
Provider   : M-Budget Mobile
APN        : gprs.swisscom.ch
WiFi-Modus : 0
[OK] Netz-Registrierung nach 0 s
Operator:   +COPS: 0,2,"22801",7    OK
[OK] LTE-Init abgeschlossen
[OK] Zeit gesetzt: 22:52:06  23.08.2026
[OK] Kurse nach 6321 ms: CHF/USD 0.8000  CHF/EUR 0.9360  CHF/GBP 1.0935
[22:53] Heap 35264  A0 1024
```

Danach je Minute eine Statuszeile. **`Heap` muss konstant bleiben** –
ein stetig fallender Wert wäre ein Speicherleck.

`Boot-Grund` ist der wichtigste Wert bei Problemen:

| Wert | Bedeutung |
|------|-----------|
| `External System` | normaler Reset (Reset-Taste, Upload, DTR) |
| `Power on` | Netzteil eingesteckt |
| `Hardware Watchdog` | Code blockierte > 8 s ohne den WDT zu füttern |
| `Software Watchdog` | Code blockierte > 3 s ohne `yield()` |
| `Exception` | Absturz – die folgende Zeile enthält `epc1`/`ctx` |

### Ausführliches AT-Protokoll

```cpp
#define DEBUG        true    // jedes AT-Kommando + jede Antwort
#define TIMINGDEBUG  true    // Kursabruf alle N Minuten statt täglich 17:00
#define TIMINGDEBUG_INTERVAL_MIN  3
```

`TIMINGDEBUG` ist zum Testen des Kursabrufs gedacht – damit muss man
nicht bis 17:00 Uhr warten. **Beide vor dem Produktivbetrieb wieder auf
`false` setzen** (`DEBUG true` erzeugt sehr viel Ausgabe).

---

## Behobene Fehler (Stand 2026-08-23)

Diese Punkte verursachten das Verhalten «läuft mal, läuft mal nicht,
stürzt manchmal ab»:

| Problem | Ursache | Behebung |
|---------|---------|----------|
| Dauernde Neustarts, sporadische AT-Fehler | WiFi war nie abgeschaltet. Im Flash standen noch Zugangsdaten aus ForEx_1_1 (`NECpresenter`); der SDK versuchte endlos zu verbinden. Die WiFi-Interrupts zerstörten das Bit-Timing des SoftwareSerial (19200 Baud). | `WiFi.persistent(false)` + `disconnect(true)` + `mode(WIFI_OFF)` + `forceSleepBegin()` am Anfang von `setup()`; Flash einmalig komplett gelöscht |
| `rst cause:4 / wdt reset` kurz nach jedem Kursabruf | Die URCs des LTE-Moduls wurden zwischen den Abrufen nie gelesen; der ISR-Puffer des SoftwareSerial lief voll und der RX-Interrupt blieb dauerhaft aktiv | `while (lteSerial.available()) lteSerial.read();` + `ESP.wdtFeed()` in `loop()` |
| `rst cause:4` mitten im Lesen des Bodys | In der `AT+HTTPREAD`-Schleife fehlte `ESP.wdtFeed()` | ergänzt, zusätzlich `delay(1)` |
| Kursabruf dauerte immer 35 s | Chunked-Antwort: das abschliessende `OK` kam nie, die Schleife lief stets ins 30-s-Timeout | Abbruch bei `+HTTPREAD: 0` bzw. nach 2 s ohne neue Bytes → jetzt ~6,5 s |
| CHF/USD war identisch mit CHF/EUR | Der Blockmarker `+HTTPREAD: 217` stand mitten im Wert: `"USD":1` ⼁ `.17` → `atof()` las 1.0 | `stripHttpReadMarkers()` vor dem Parsen |
| Retry nach Fehlversuch griff nie | Die Bedingung prüfte `fxValue[0]`, das nach dem ersten Erfolg dauerhaft gültig blieb | `catchCurrencies()` liefert jetzt `bool` |
| Watchdog-Reset während des Kursabrufs | Der 180-s-Ticker lief während eines Abrufs weiter, der inkl. Retry länger dauern kann | Ticker während des Abrufs pausiert |
| Helligkeit im Debug-Modus tot | Regelung war an `if (!DEBUG)` gekoppelt | entkoppelt, zusätzlich auf 200 ms gedrosselt |
| Projekt nicht baubar | Mehrere Bibliotheken waren nicht installiert (Adafruit GFX, ST7735, Time) | über `arduino-cli lib install` ergänzt |
| SIM-Init scheiterte beim Kaltstart | `AT` wurde nur 2x über ~6 s versucht, `AT+CPIN?` genau einmal abgefragt. Beim Warmstart (nur ESP-Reset) fiel das nie auf, weil das Modul durchlief | auf Modul (bis 30 s) und SIM (bis 25 s) wird jetzt gewartet |
| PIN-Eingabe nur bei Provider 0 | Eingabe war in `#if ACTIVE_PROVIDER == 0` eingeschlossen – die PIN gehört aber zur SIM, nicht zum Anbieter | Bedingung entfernt |
| Risiko SIM-Sperre | Bei abgelehnter PIN startete das Gerät neu und sendete die PIN erneut – nach drei Versuchen ist die SIM PUK-gesperrt | `haltWithMessage()`: bei abgelehnter PIN oder `SIM PUK` hält das Gerät dauerhaft an |
