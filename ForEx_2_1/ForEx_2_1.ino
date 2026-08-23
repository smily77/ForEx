// ForEx_2_1 – GSM/LTE Version
// Hardware:  ESP8266 + ST7735 TFT + BK-7670 LTE-Modul (AND Global)
// Änderungen gegenüber v1.1:
//   - WiFi entfernt; Datenverbindung über BK-7670 LTE-Modul (UART)
//   - Zeit vom GSM-Netz (NITZ / AT+CCLK?) – kein NTP mehr nötig
//   - BMP180 entfernt (GPIO 4/5 jetzt für LTE-UART belegt)
//   - Wechselkurse täglich um 17:00 Uhr (statt Mitternacht)
//   - Provider-APN als einstell­bare Konstante

#include <ESP8266WiFi.h>      // nur zum Stilllegen des WiFi-Stacks (siehe setup())
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
#include "Credentials.h"      // SIM-PIN (nicht auf GitHub)

// DEBUG       = ausführliches AT-Protokoll (jedes Kommando + jede Antwort)
// TIMINGDEBUG = true: Kursabruf alle 30 Min (Test); false: täglich 17:00
//
// Die Basis-Diagnose (Boot-Grund, Init-Schritte, Fehler, Heap, Minutentakt)
// wird IMMER ausgegeben – unabhängig von DEBUG. Nur so ist ein Fehlerfall
// später am seriellen Port nachvollziehbar.
#define DEBUG        false
#define TIMINGDEBUG  false
#define TIMINGDEBUG_INTERVAL_MIN  30   // nur wirksam wenn TIMINGDEBUG true
#define LOG_BAUD     115200

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
// ============================================
// HELLIGKEITSSTEUERUNG
// ============================================
#define LDR_HELL   400   // A0-Wert bei hellem Licht
#define LDR_DUNKEL 1023  // A0-Wert bei Dunkelheit
#define PWM_HELL   0     // PWM fuer hellstes Display
#define PWM_DUNKEL 253   // PWM fuer dunklestes Display (gerade noch sichtbar)
// ============================================
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
// WATCHDOG (180-Sekunden-Timeout → ESP-Reset)
// Wird nur bei Minutenwechsel gefüttert – erkennt so auch
// eingefrorene Zustände, nicht nur blockierte Loops.
// WICHTIG: Ticker-ISR darf keine blockierenden Operationen
// ausführen (delay, TFT, Serial). Nur Flag setzen, Aktion
// im Haupt-Loop ausführen.
// ============================================================
Ticker secondTick;
volatile int  watchDogCount     = 0;
volatile bool watchDogTriggered = false;

void ISRwatchDog() {
  watchDogCount++;
  if (watchDogCount >= 180) {
    watchDogTriggered = true;   // Nur Flag setzen – kein Blocking im ISR!
  }
}

void watchDogFeed() {
  watchDogCount     = 0;
  watchDogTriggered = false;
}

void watchDogAction() {
  secondTick.detach();
  Serial.println(F("[FEHLER] Watchdog: 180 s ohne Minutenwechsel -> Neustart"));
  Serial.flush();
  clearTFTScreen();
  tft.setTextColor(ST7735_RED);
  tft.setTextSize(2);
  tft.println("WATCHDOG");
  tft.println("RESET");
  delay(3000);
  ESP.restart();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  // ---- 1. WiFi-Stack stilllegen -------------------------------------
  // WICHTIG: Auf diesem Board stehen aus der WiFi-Zeit (ForEx_1_1) noch
  // Zugangsdaten im SDK-Konfigbereich des Flash. Ohne die folgenden Zeilen
  // versucht der SDK bei jedem Boot endlos, sich mit diesem (nicht mehr
  // vorhandenen) AP zu verbinden. Die dabei laufenden WiFi-Interrupts
  // zerstören das Bit-Timing des bit-gebangten SoftwareSerial
  // (19200 Baud auf GPIO4/5) – AT-Kommandos kommen dann nur sporadisch
  // durch. Das war die Ursache für "läuft mal, läuft mal nicht".
  WiFi.persistent(false);      // keine WiFi-Änderungen mehr ins Flash schreiben
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true);       // laufenden Verbindungsversuch abbrechen
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  delay(1);                    // forceSleep braucht einen Zyklus

  // ---- 2. Diagnose-Port (immer aktiv) -------------------------------
  Serial.begin(LOG_BAUD);
  Serial.println(F("\n\n===== ForEx v2.1 ====="));
  Serial.print(F("Boot-Grund : ")); Serial.println(ESP.getResetReason());
  Serial.print(F("Freier Heap: ")); Serial.println(ESP.getFreeHeap());
  Serial.print(F("Provider   : ")); Serial.println(PROVIDER_NAME);
  Serial.print(F("APN        : ")); Serial.println(LTE_APN);
  Serial.print(F("WiFi-Modus : ")); Serial.println(WiFi.getMode());  // 0 = OFF

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
  lteSerial.begin(LTE_BAUD, SWSERIAL_8N1, LTE_RX_PIN, LTE_TX_PIN, false, 256); // 256-Byte ISR-Puffer
  tft.println("ForEx v2.1");
  tft.println("LTE Init...");
  tft.print("Provider: ");
  tft.println(PROVIDER_NAME);
  tft.print("APN: ");
  tft.println(LTE_APN);

  if (!initLTE()) {
    Serial.println(F("[FEHLER] LTE-Init fehlgeschlagen -> Neustart in 15 s"));
    Serial.flush();
    tft.setTextColor(ST7735_RED);
    tft.println("LTE Init fehl!");
    tft.println("Reset in 15s...");
    delay(15000);
    ESP.restart();
  }
  Serial.println(F("[OK] LTE-Init abgeschlossen"));

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
    Serial.printf("[OK] Zeit gesetzt: %02d:%02d:%02d  %02d.%02d.%04d\n",
                  hour(), minute(), second(), day(), month(), year());
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
  if (!catchCurrencies()) {
    // Ein Fehlversuch ist beim Kaltstart normal (Modul noch nicht ganz
    // eingebucht) – einmal kurz warten und erneut versuchen.
    Serial.println(F("[WARN] Erster Kursabruf fehlgeschlagen, Retry in 10 s"));
    delay(10000);
    catchCurrencies();
  }

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

    // Lebenszeichen je Minute: zeigt im Log, dass Uhr und Loop laufen
    Serial.printf("[%02d:%02d] Heap %u  A0 %d\n",
                  hour(), minute(), ESP.getFreeHeap(), analogRead(A0));

    // Zeit-Resync alle 6 Stunden (kostet kein Datenvolumen)
    if (minuteLast == 0 && (hour() % 6) == 0) {
      time_t newTime = getGsmTime();
      if (newTime != 0) {
        setTime(newTime);
        Serial.println(F("[OK] Zeit re-sync"));
      } else {
        Serial.println(F("[WARN] Zeit re-sync fehlgeschlagen"));
      }
    }

    // Kursabruf: im TIMINGDEBUG-Modus alle TIMINGDEBUG_INTERVAL_MIN Minuten,
    // sonst täglich um 17:00
#if TIMINGDEBUG
    if (minuteLast % TIMINGDEBUG_INTERVAL_MIN == 0) {
      if (DEBUG) Serial.println(F("TIMINGDEBUG – Aktualisierung..."));
#else
    if ((hour() == 17) && (minuteLast == 0)) {
      if (DEBUG) Serial.println(F("17:00 – Tägliche Aktualisierung..."));
#endif
      // Ticker anhalten: der Kursabruf kann inkl. Retry deutlich länger
      // dauern als das 180-s-Watchdog-Fenster (HTTPACTION 35 s + HTTPREAD
      // 30 s + 30 s Retry-Pause + zweiter Versuch). Ohne dieses detach()
      // löst der Software-Watchdog mitten im Abruf einen Reset aus.
      secondTick.detach();

      // Erfolg am Rückgabewert festmachen, NICHT an fxValue[0]: das behält
      // nach dem ersten erfolgreichen Abruf dauerhaft einen gültigen Wert,
      // wodurch der Retry im laufenden Betrieb nie ausgelöst wurde.
      bool fxOk = catchCurrencies();
      if (!fxOk) {
        Serial.println(F("[WARN] Kursabruf fehlgeschlagen, Retry in 30 s..."));
        delay(30000);
        fxOk = catchCurrencies();
      }
      if (!fxOk) {
        // Auch der zweite Versuch schlug fehl -> LTE-Verbindung neu aufbauen
        // und danach ein letztes Mal versuchen.
        Serial.println(F("[WARN] Kursabruf 2x fehlgeschlagen -> LTE-Reinit"));
        if (initLTE()) {
          fxOk = catchCurrencies();
        }
      }

      secondTick.attach(1, ISRwatchDog);
      watchDogFeed();
      Serial.println(fxOk ? F("[OK] Kursabruf beendet")
                          : F("[FEHLER] Kursabruf endgueltig fehlgeschlagen"));
    }

    displayMainScreen();
    watchDogFeed();  // Nur bei Minutenwechsel – beweist, dass Uhr läuft
  }

  // Helligkeit über LDR an A0 anpassen.
  // Nur alle 200 ms statt in jedem Schleifendurchlauf: analogRead() blockiert
  // je Aufruf einige Dutzend Mikrosekunden und störte in der alten Fassung
  // (Aufruf tausendfach pro Sekunde) das SoftwareSerial-Timing.
  // analogWrite() nur bei tatsächlicher Änderung – spart PWM-Neuprogrammierung.
  // Nicht mehr an DEBUG gekoppelt: die Helligkeit soll auch mit
  // eingeschaltetem AT-Protokoll geregelt werden.
  static unsigned long lastLdrRead = 0;
  static int lastPwm = -1;
  if (millis() - lastLdrRead >= 200) {
    lastLdrRead = millis();
    int ldrVal = analogRead(A0);
    int pwm;
    if (ldrVal <= LDR_HELL) {
      pwm = PWM_HELL;
    } else if (ldrVal >= LDR_DUNKEL) {
      pwm = PWM_DUNKEL;
    } else {
      pwm = map(ldrVal, LDR_HELL, LDR_DUNKEL, PWM_HELL, PWM_DUNKEL);
    }
    if (pwm != lastPwm) {
      analogWrite(ledPin, pwm);
      lastPwm = pwm;
    }
  }

  // Unaufgeforderte Meldungen (URCs) des LTE-Moduls abholen und verwerfen.
  // Das BK-7670 schickt zwischen den Kursabrufen von sich aus Statusmeldungen.
  // Bisher las diese niemand: die Bytes blieben im ISR-Puffer des
  // SoftwareSerial stehen, der Puffer lief voll und der RX-Interrupt blieb
  // dauerhaft aktiv – wenige Sekunden nach jedem Kursabruf folgte ein
  // "rst cause:4 / wdt reset". Hier ist der Aufruf gefahrlos, weil
  // catchCurrencies() und getGsmTime() synchron aus dieser Schleife heraus
  // laufen und daher nie gleichzeitig auf dem Port lesen.
  while (lteSerial.available()) lteSerial.read();

  // Hardware-Watchdog (8 s) explizit füttern – unabhängig davon, ob
  // yield() im aktuellen SDK-Zustand ausreicht.
  ESP.wdtFeed();
  yield();
}
