# ForEx_2_1 – Schaltungsschema

Hardware-Version: **ForEx_1_1** mit BK-7670 LTE-Modul (AND Global) anstelle des BMP180.
GPIO 4 und GPIO 5 sind nun für die UART-Verbindung zum LTE-Modul belegt.

---

## 1. Übersichtsplan (alle Baugruppen)

```mermaid
graph TD
    subgraph ESP["ESP8266 (NodeMCU / WeMos D1 Mini)"]
        G0["GPIO 0  – LED PWM"]
        G2["GPIO 2  – TFT_DC"]
        G4["GPIO 4  – LTE RX  ← Modul TX"]
        G5["GPIO 5  – LTE TX  → Modul RX"]
        G12["GPIO 12 – TFT_RST"]
        G13["GPIO 13 – SPI MOSI"]
        G14["GPIO 14 – SPI CLK"]
        G15["GPIO 15 – TFT_CS"]
        A0["A0      – Helligkeitssensor"]
    end

    subgraph TFT["ST7735 TFT-Display (160×128 px)"]
        TCS["CS"]
        TDC["DC"]
        TRST["RST"]
        TSPI["SPI Bus"]
    end

    subgraph LTE["BK-7670 LTE-Modul (AND Global)"]
        LTX["TX"]
        LRX["RX"]
        LPWR["VCC / GND"]
    end

    subgraph LED["TFT-Hintergrundbeleuchtung"]
        LEDIN["LED+ (PWM)"]
    end

    subgraph POT["Potentiometer / LDR"]
        POTOUT["Analogausgang"]
    end

    G15  --> TCS
    G2   --> TDC
    G12  --> TRST
    G13  --> TSPI
    G14  --> TSPI

    G4   -->|RX ← TX| LTX
    G5   -->|TX → RX| LRX

    G0   --> LEDIN
    A0   --> POTOUT

    LTE  -->|LTE / Mobilfunk| INET["Internet\nfrankfurter.app API"]
    LTE  -->|GSM-Signalisierung\nNITZ – kein Datenvolumen| TIME["Netzzeit\nvom Operator"]
```

---

## 2. UART-Detail – ESP8266 ↔ BK-7670

```mermaid
graph LR
    ESP["ESP8266"] -->|GPIO 5  TX  →  RX| MOD["BK-7670"]
    MOD            -->|TX  →  RX  GPIO 4| ESP
    ESP            -->|3.3 V + GND      | MOD

    note1["Baudrate: 19200 (fix im Modul)"]
    note2["SoftwareSerial(4, 5)\nRX=GPIO4, TX=GPIO5"]
```

> **Achtung – Spannungsebenen:**
> Der ESP8266 arbeitet mit 3.3 V. Prüfen Sie, ob der BK-7670 ebenfalls
> 3.3 V-Logik auf den UART-Pins verwendet. Falls das Modul 1.8 V-Logik
> hat, ist ein Level-Shifter erforderlich.

---

## 3. SPI-Detail – ESP8266 ↔ ST7735 TFT

```mermaid
graph LR
    ESP2["ESP8266"] -->|MOSI – GPIO 13| TFT["ST7735"]
    ESP2            -->|CLK  – GPIO 14| TFT
    ESP2            -->|CS   – GPIO 15| TFT
    ESP2            -->|DC   – GPIO 2 | TFT
    ESP2            -->|RST  – GPIO 12| TFT
    ESP2            -->|3.3 V + GND  | TFT
```

---

## 4. Software-Ablauf (Flussdiagramm)

```mermaid
flowchart TD
    START([Start / Reset]) --> INIT_TFT[TFT-Display initialisieren]
    INIT_TFT --> INIT_LTE[LTE-Modul initialisieren\nAT-Kommunikation\nAPN konfigurieren]
    INIT_LTE --> NETZ{Netz\ngefunden?}
    NETZ -- Nein --> RESET1([Neustart nach 30 s])
    NETZ -- Ja  --> NITZ[NITZ aktivieren\nAT+CTZU=1\nkein Datenvolumen]
    NITZ --> TZ[Zeitzonen laden\nAirportDatabase.h]
    TZ --> GSMTIME[Zeit vom Operator lesen\nAT+CCLK?\nkein Datenvolumen]
    GSMTIME --> SETTIME[setTime – ZRH-Lokalzeit]
    SETTIME --> FX[Wechselkurse abrufen\nHTTP via LTE\nfrankfurter.app]
    FX --> DISP[Display rendern]
    DISP --> WD[Watchdog starten\n60 s Timeout]

    WD --> LOOP([Hauptschleife])
    LOOP --> HELL[Helligkeit lesen A0\nPWM anpassen]
    HELL --> MIN{Neue Minute?}
    MIN -- Nein --> LOOP
    MIN -- Ja  --> SHOW[Display aktualisieren]
    SHOW --> H17{17:00 Uhr?}
    H17 -- Nein --> WDFEED[Watchdog zurücksetzen]
    H17 -- Ja  --> RESYNC[Zeit re-sync\nAT+CCLK?\nkein Datenvolumen]
    RESYNC --> FX2[Wechselkurse neu laden\nHTTP via LTE]
    FX2 --> WDFEED
    WDFEED --> LOOP

    WDFEED -. Watchdog-Timeout .-> RESET2([ESP8266 Neustart])
```

---

## 5. Display-Layout (Bildschirmaufteilung)

```mermaid
block-beta
    columns 1
    block:ROW0["Zeile 0 – Lokale Zeit & Datum (groß, fett, weiß)"]
        LOCAL["ZRH  HH:MM  DD MMM"]
    end
    block:ROW1["Zeile 1 – Weltuhren 1 (gelb)"]
        WC1["DXB  HH:MM"] WC2["SIN  HH:MM"] WC3["IAD  HH:MM"]
    end
    block:ROW2["Zeile 2 – Weltuhren 2 (gelb)"]
        WC4["SYD  HH:MM"] WC5["BLR  HH:MM"] WC6["SFO  HH:MM"]
    end
    block:ROW3["Zeile 3 – Wechselkurse (grün)"]
        FX1["USD x.xxxx"] FX2["EUR x.xxxx"] FX3["GBP x.xxxx"]
    end
```

---

## 6. Pin-Übersicht (Tabelle)

| GPIO | Richtung | Funktion                     | Bauteil                       |
|------|----------|------------------------------|-------------------------------|
| 0    | Ausgang  | PWM – Display-Helligkeit     | TFT-Hintergrundbeleuchtung    |
| 2    | Ausgang  | TFT Data/Command             | ST7735                        |
| 4    | **Eingang** | UART RX (← Modul TX)     | BK-7670 TX                    |
| 5    | **Ausgang** | UART TX (→ Modul RX)     | BK-7670 RX                    |
| 12   | Ausgang  | TFT Reset                    | ST7735                        |
| 13   | Ausgang  | SPI MOSI                     | ST7735                        |
| 14   | Ausgang  | SPI CLK                      | ST7735                        |
| 15   | Ausgang  | TFT Chip-Select              | ST7735                        |
| A0   | Eingang  | Analogeingang Helligkeit     | Potentiometer / LDR           |

---

## 7. Provider-Konfiguration

Die APN-Einstellung wird als Konstante in `ForEx_2_1.ino` gewählt:

```cpp
#define ACTIVE_PROVIDER  0   // 0 = M-Budget Mobile, 1 = Digital Republic
```

| `ACTIVE_PROVIDER` | Provider          | Netz      | APN                  |
|:-----------------:|-------------------|-----------|----------------------|
| `0`               | M-Budget Mobile   | Swisscom  | `gprs.swisscom.ch`   |
| `1`               | Digital Republic  | Salt      | `internet`           |

> Falls die Verbindung mit dem voreingestellten APN nicht klappt, bitte
> beim Provider nachfragen oder die Konstante `LTE_APN` direkt in
> `ForEx_2_1.ino` anpassen.

---

## 8. Zeitabfrage – Datenvolumen

| Funktion                           | Methode                  | Datenvolumen |
|------------------------------------|--------------------------|:------------:|
| Zeit synchronisieren               | AT+CCLK? (NITZ/GSM)     | **0 Byte**   |
| Wechselkurse abrufen               | HTTP GET frankfurter.app | ~300 Byte    |
| **Gesamt pro Tag**                 |                          | **~300 Byte** |

Die Zeitabfrage über NITZ ist Teil der GSM-Signalisierung und verursacht
**keinen Datenverbrauch** – ideal für PrePaid-SIM mit begrenztem Volumen.
