// ForEx_2_1_Frankfurter – GSM/LTE Version mit frankfurter.app API
// Hardware:  ESP8266 + ST7735 TFT + BK-7670 LTE-Modul (AND Global)
// Änderungen gegenüber v1.1:
//   - WiFi entfernt; Datenverbindung über BK-7670 LTE-Modul (UART)
//   - Zeit vom GSM-Netz (NITZ / AT+CCLK?) – kein NTP mehr nötig
//   - BMP180 entfernt (GPIO 4/5 jetzt für LTE-UART belegt)
//   - Wechselkurse täglich um 17:00 Uhr (statt Mitternacht)
//   - Provider-APN als einstell­bare Konstante

#include <TimeLib.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Streaming.h>
#include <Ticker.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "AirportDatabase.h"

#define DEBUG true

// ============================================================
// KONFIGURATION: Airport-Codes für Weltuhren
// Index 0 = Lokale Zeit (Heimatzone)
// ============================================================
const char* AIRPORT_CODES[7] = {
  "ZRH",  // 0: Lokale Zeit (Zürich, Schweiz)
  "DXB",  // 1: Dubai, VAE
  "SIN",  // 2: Singapore
  "IAD",  // 3: Washington DC, USA
  "SYD",  // 4: Sydney, Australien
  "BLR",  // 5: Bangalore, Indien
  "SFO"   // 6: San Francisco, USA
};

// ============================================================
// PROVIDER-KONFIGURATION
// Wähle deinen Schweizer Mobilfunk-Provider:
//   0 = M-Budget Mobile  (MVNO auf Swisscom-Netz)
//   1 = Digital Republic (MVNO auf Salt-Netz)
// ============================================================
#define ACTIVE_PROVIDER  0          // <-- HIER PROVIDER EINSTELLEN

#if ACTIVE_PROVIDER == 0
  // M-Budget Mobile – läuft auf dem Swisscom-Netz
  const char* LTE_APN       = "gprs.swisscom.ch";
  const char* PROVIDER_NAME = "M-Budget Mobile";
#elif ACTIVE_PROVIDER == 1
  // Digital Republic – läuft auf dem Salt-Netz
  const char* LTE_APN       = "internet";
  const char* PROVIDER_NAME = "Digital Republic";
#else
  #error "Unbekannter Provider! ACTIVE_PROVIDER muss 0 oder 1 sein."
#endif

// ============================================================
// LTE-MODUL: BK-7670 (AND Global) – UART-Verbindung
//   GPIO 4 (ESP8266) → TX-Pin des Moduls   (ESP8266 liest)
//   GPIO 5 (ESP8266) → RX-Pin des Moduls   (ESP8266 schreibt)
//   Baudrate fix: 19200
// ============================================================
#define LTE_RX_PIN   4      // ESP8266 RX ← Modul TX
#define LTE_TX_PIN   5      // ESP8266 TX → Modul RX
#define LTE_BAUD  19200

SoftwareSerial lteSerial(LTE_RX_PIN, LTE_TX_PIN);

// ============================================================
// TFT-DISPLAY: ST7735 160×128 px
// ============================================================
#define TFT_PIN_CS   15
#define TFT_PIN_DC    2
#define TFT_PIN_RST  12
#define ledPin        0

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);

// ============================================================
// ZEITZONEN (aus Airport-Codes befüllt)
// ============================================================
struct TimezoneInfo {
  String airCode;
  int    stdOffset;   // Standard-UTC-Offset in Sekunden
  int    dstOffset;   // DST-UTC-Offset in Sekunden
  byte   dstType;     // 0=kein DST, 1=EU, 2=US, 3=AU, 4=NZ
};

TimezoneInfo timezones[7];

// ============================================================
// WECHSELKURS-DATEN
// ============================================================
String fxSym[4]   = {"CHF", "USD", "EUR", "GBP"};
float  fxValue[4];

// ============================================================
// ZUSTANDSVARIABLEN
// ============================================================
time_t currentTime = 0;
char   anzeige[24];
int    secondLast, minuteLast;
int    helligkeit;
boolean firstRun = true;

// ============================================================
// WATCHDOG (60-Sekunden-Timeout → ESP-Reset)
// WICHTIG: Ticker-ISR darf keine blockierenden Operationen
// ausführen (delay, TFT, Serial). Nur Flag setzen, Aktion
// im Haupt-Loop ausführen.
// ============================================================
Ticker secondTick;
volatile int  watchDogCount     = 0;
volatile bool watchDogTriggered = false;

void ISRwatchDog() {
  watchDogCount++;
  if (watchDogCount >= 60) {
    watchDogTriggered = true;   // Nur Flag setzen – kein Blocking im ISR!
  }
}

void watchDogFeed() {
  watchDogCount     = 0;
  watchDogTriggered = false;
  delay(1);
}

void watchDogAction() {
  secondTick.detach();
  clearTFTScreen();
  tft.setTextColor(ST7735_RED);
  tft.setTextSize(2);
  tft.println("WATCHDOG");
  tft.println("ATTACK");
  delay(10000);
  ESP.reset();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  if (DEBUG) Serial.begin(9600);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  // TFT initialisieren
  tft.initR(INITR_BLACKTAB);
  tft.setTextWrap(false);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 0);

  // LTE-Modul starten
  lteSerial.begin(LTE_BAUD);
  tft.println("ForEx v2.1-FR");
  tft.println("LTE Init...");
  tft.print("Provider: ");
  tft.println(PROVIDER_NAME);
  tft.print("APN: ");
  tft.println(LTE_APN);

  if (!initLTE()) {
    tft.setTextColor(ST7735_RED);
    tft.println("LTE Init fehl!");
    tft.println("Reset in 30s...");
    delay(30000);
    ESP.reset();
  }

  // Zeitzonen aus Airport-Codes laden (kein Internet nötig)
  clearTFTScreen();
  tft.println("Zeitzonen laden...");
  initializeTimezones();

  // Zeit vom GSM-Netz (NITZ) holen – kostet kein Datenvolumen
  clearTFTScreen();
  tft.println("Zeit vom GSM-Netz...");
  currentTime = getGsmTime();
  if (currentTime == 0) {
    tft.setTextColor(ST7735_YELLOW);
    tft.println("Warnung: Zeitabfrage");
    tft.println("fehlgeschlagen.");
    tft.println("Warte 10s, retry...");
    tft.setTextColor(ST7735_WHITE);
    delay(10000);
    currentTime = getGsmTime();
  }
  if (currentTime != 0) {
    setTime(currentTime);
    if (DEBUG) {
      Serial.print("Zeit gesetzt: ");
      Serial.println(currentTime);
    }
  } else {
    tft.setTextColor(ST7735_RED);
    tft.println("FEHLER: Kein Zeit!");
    tft.println("Starte mit 00:00...");
    tft.setTextColor(ST7735_WHITE);
    setTime(0);
    delay(3000);
  }

  // Wechselkurse per LTE abrufen (Datenvolumen)
  clearTFTScreen();
  tft.println("Kurse abrufen...");
  catchCurrencies();

  firstRun = false;
  secondLast = second();
  minuteLast = minute();
  displayMainScreen();

  secondTick.attach(1, ISRwatchDog);
}

// ============================================================
// HAUPTSCHLEIFE
// ============================================================
void loop() {
  // Watchdog-Aktion im Haupt-Kontext ausführen (nie im ISR!)
  if (watchDogTriggered) {
    watchDogAction();
  }

  if (minuteLast != minute()) {
    minuteLast = minute();

    if (DEBUG) {
      Serial.print(hour());
      Serial.print(":");
      Serial.println(minute());
    }

    // Täglich um 17:00 Uhr: Zeit re-sync + neue Wechselkurse
    if ((hour() == 17) && (minuteLast == 0)) {
      if (DEBUG) Serial.println("17:00 – Tägliche Aktualisierung...");

      // Zeit vom Netz (kein Datenvolumen)
      time_t newTime = getGsmTime();
      if (newTime != 0) {
        setTime(newTime);
        if (DEBUG) Serial.println("Zeit re-sync OK");
      } else {
        if (DEBUG) Serial.println("Zeit re-sync fehlgeschlagen");
      }

      // Wechselkurse (Datenvolumen)
      catchCurrencies();
      if (DEBUG) Serial.println("Kurse aktualisiert");
    }

    displayMainScreen();
  }

  // Helligkeit über Potentiometer / LDR an A0 anpassen (im Debug-Modus deaktiviert)
  if (!DEBUG) {
    helligkeit = analogRead(A0);
    if (helligkeit > 1010) helligkeit = 1010;
    analogWrite(ledPin, helligkeit);
  }

  watchDogFeed();
}
