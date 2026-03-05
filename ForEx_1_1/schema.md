# Desk_Top_Widget_II_opt – Schaltungsschema

## 1. Schaltplan (Hardwareverbindungen)

```mermaid
graph TD
    subgraph ESP8266["ESP8266 (NodeMCU / WeMos D1 Mini)"]
        GPIO0["GPIO 0 – LED PWM"]
        GPIO2["GPIO 2 – TFT_DC"]
        GPIO4["GPIO 4 – LED PWM / I2C SCL"]
        GPIO5["GPIO 5 – I2C SCL"]
        GPIO12["GPIO 12 – TFT_RST"]
        GPIO15["GPIO 15 – TFT_CS"]
        GPIO1["GPIO 1 – I2C SDA"]
        GPIO3["GPIO 3 – I2C SDA"]
        A0["A0 – Helligkeitssensor"]
    end

    subgraph TFT["ST7735 TFT Display (160×128 px)"]
        CS["CS"]
        DC["DC"]
        RST["RST"]
        SPI["SPI Bus (MOSI/CLK)"]
    end

    subgraph BMP["BMP180 (optional)\nDruck- / Temperatursensor"]
        SDA["SDA"]
        SCL["SCL"]
    end

    subgraph LED["Hintergrundbeleuchtung"]
        LED_IN["LED+ (PWM-gesteuert)"]
    end

    subgraph POT["Potentiometer / LDR"]
        POT_OUT["Analogausgang"]
    end

    GPIO15 --> CS
    GPIO2  --> DC
    GPIO12 --> RST
    ESP8266 -->|SPI| SPI

    GPIO1  --> SDA
    GPIO4  --> SCL

    GPIO0  --> LED_IN
    A0     --> POT_OUT

    ESP8266 -->|WiFi 2,4 GHz| INTERNET["Internet\n(NTP / forex API)"]
```

---

## 2. SPI-Bus-Detail (TFT-Anschluss)

```mermaid
graph LR
    ESP["ESP8266"] -->|MOSI – GPIO13| TFT_D["ST7735"]
    ESP           -->|CLK  – GPIO14| TFT_D
    ESP           -->|CS   – GPIO15| TFT_D
    ESP           -->|DC   – GPIO2 | TFT_D
    ESP           -->|RST  – GPIO12| TFT_D
    ESP           -->|3.3 V + GND  | TFT_D
```

---

## 3. I²C-Bus-Detail (BMP180-Anschluss)

```mermaid
graph LR
    ESP2["ESP8266"] -->|SDA – GPIO1 oder GPIO3| BMP2["BMP180"]
    ESP2            -->|SCL – GPIO4 oder GPIO5| BMP2
    ESP2            -->|3.3 V + GND           | BMP2
```

---

## 4. Systemarchitektur (Software-Ablauf)

```mermaid
flowchart TD
    START([Start / Reset]) --> INIT_DISP[TFT-Display initialisieren]
    INIT_DISP --> WIFI[Mit WLAN verbinden\nSSID1 → Fallback SSID2]
    WIFI --> TZ[Zeitzonen laden\nAirportDatabase.h]
    TZ --> NTP[Zeit per NTP synchronisieren]
    NTP --> FX[Wechselkurse abrufen\nfrankfurter.app API]
    FX --> BMP_INIT[BMP180 initialisieren\noptional]
    BMP_INIT --> DISP_RENDER[Display rendern]
    DISP_RENDER --> WD_START[Watchdog-Timer starten\n60 s Timeout]

    WD_START --> LOOP([Hauptschleife])

    LOOP --> BRIGHTNESS[Helligkeit lesen A0\nPWM anpassen]
    BRIGHTNESS --> CHECK_MIN{Neue Minute?}
    CHECK_MIN -- Nein --> LOOP
    CHECK_MIN -- Ja --> DST[DST berechnen\nEU / US / AU / NZ]
    DST --> SHOW[Display aktualisieren]
    SHOW --> CHECK_MID{Mitternacht?}
    CHECK_MID -- Nein --> WD_FEED[Watchdog zurücksetzen]
    CHECK_MID -- Ja --> NTP2[NTP re-sync]
    NTP2 --> FX2[Wechselkurse neu laden]
    FX2 --> WD_FEED
    WD_FEED --> LOOP

    WD_FEED -. Watchdog-Timeout .-> RESTART([ESP8266 Neustart])
```

---

## 5. Display-Layout (Bildschirmaufteilung)

```mermaid
block-beta
    columns 1
    block:ROW0["Zeile 0 – Lokale Zeit & Datum (groß, fett)"]
        LOCAL["ZRH  HH:MM  DD.MM.YYYY"]
    end
    block:ROW1["Zeile 1 – Weltuhren 1"]
        WC1["DXB  HH:MM"] WC2["SIN  HH:MM"] WC3["IAD  HH:MM"]
    end
    block:ROW2["Zeile 2 – Weltuhren 2"]
        WC4["SYD  HH:MM"] WC5["BLR  HH:MM"] WC6["SFO  HH:MM"]
    end
    block:ROW3["Zeile 3 – Wechselkurse"]
        FX1["USD x.xxxx"] FX2["EUR x.xxxx"] FX3["GBP x.xxxx"]
    end
```

---

## 6. Pin-Übersicht (Tabelle)

| GPIO | Funktion            | Angeschlossenes Bauteil |
|------|---------------------|-------------------------|
| 0    | PWM – LED-Helligkeit | Hintergrundbeleuchtung  |
| 2    | TFT_DC (Data/Cmd)   | ST7735                  |
| 12   | TFT_RST (Reset)     | ST7735                  |
| 13   | SPI MOSI            | ST7735                  |
| 14   | SPI CLK             | ST7735                  |
| 15   | TFT_CS (Chip Select)| ST7735                  |
| 1/3  | I²C SDA             | BMP180 (optional)       |
| 4/5  | I²C SCL             | BMP180 (optional)       |
| A0   | Analogeingang       | Potentiometer / LDR     |
