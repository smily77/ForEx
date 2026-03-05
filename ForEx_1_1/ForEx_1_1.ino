// Optimierte Version - Vereinfachte Zeitberechnung mit lokaler DST-Berechnung
// Keine Wetter-API für Zeitzonen mehr nötig
// Kein Web-Interface
// Aktualisierung nur beim Start und einmal täglich
// Airport-Code basierte Konfiguration mit automatischem Timezone-Lookup
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SFE_BMP180.h>
#include <Wire.h>
#include <Streaming.h>
#include <Ticker.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include "AirportDatabase.h"
extern "C" {
  #include "user_interface.h"
}

#define DEBUG true

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

SFE_BMP180 pressure;
char status;
double T,P;

WiFiClientSecure clientSec;
#include <Credentials.h>
#define maxWlanTrys 100

const char* timerServerDNSName = "0.ch.pool.ntp.org";
IPAddress timeServer;

WiFiUDP Udp;
const unsigned int localPort = 8888;
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

#define TFT_PIN_CS   15
#define TFT_PIN_DC   2
#define TFT_PIN_RST  12

#define ledPin 0

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);

time_t currentTime = 0;
char anzeige[24];
int secondLast, minuteLast;
int helligkeit;
boolean firstRun = true;
int wPeriode;

// Timezone-Strukturen (werden beim Start aus Airport-Codes gefüllt)
struct TimezoneInfo {
  String airCode;
  int stdOffset;      // Standard UTC offset in Sekunden
  int dstOffset;      // DST UTC offset in Sekunden
  byte dstType;       // 0=kein DST, 1=EU, 2=US, 3=AU, 4=NZ
};

TimezoneInfo timezones[7];

// Wechselkurs-Daten
String fxSym[4] = {"CHF", "USD", "EUR", "GBP"};
float fxValue[4];

Ticker secondTick;
volatile int watchDogCount = 0;

void ISRwatchDog() {
  watchDogCount++;
  if (watchDogCount >= 60) {
    watchDogAction();
  }
}

void watchDogFeed() {
  watchDogCount = 0;
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

void setup() {
  if (DEBUG) Serial.begin(9600);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  tft.initR(INITR_BLACKTAB);
  tft.setTextWrap(false);
  tft.setTextColor(ST7735_WHITE);
  tft.setRotation(1);
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE, ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor(0, 0);

  tft.print("Searching for: ");
  if (DEBUG) tft.print(ssid1);
    else tft.print(ssid2);
  tft.setCursor(0, 16);

  // WiFi-Verbindung
  if (DEBUG) WiFi.begin(ssid1, password1);
    else WiFi.begin(ssid2, password2);
  wifi_station_set_auto_connect(true);

wlanInitial:
  wPeriode = 1;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
    wPeriode++;
    if (wPeriode > maxWlanTrys) {
      tft.println();
      tft.println("Break due to missing WLAN");
      if (String(ssid1) == WiFi.SSID()) {
        tft.print("Switch to: ");
        tft.println(ssid2);
        WiFi.begin(ssid2, password2);
        wifi_station_set_auto_connect(true);
      }
      else {
        tft.print("Switch to: ");
        tft.println(ssid1);
        WiFi.begin(ssid1, password1);
        wifi_station_set_auto_connect(true);
      }
      goto wlanInitial;
    }
  }

  tft.setCursor(0, 32);
  tft.println("WiFi connected");
  tft.println("IP address: ");

  clearTFTScreen();

  tft.println(WiFi.localIP());
  tft.println();
  tft.print("MAC: ");
  tft.println(WiFi.macAddress());
  if (DEBUG) {
    Serial.print("MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.println(WiFi.localIP());
  }

  // Initialisiere Timezones aus Airport-Codes
  clearTFTScreen();
  tft.println("Loading timezones...");
  initializeTimezones();

  // NTP Zeit holen
  clearTFTScreen();
  while (currentTime == 0) {
    tft.print("Resolving NTP Server IP ");
    WiFi.hostByName(timerServerDNSName, timeServer);
    tft.println(timeServer.toString());

    tft.print("Starting UDP... ");
    Udp.begin(localPort);
    tft.print("local port: ");
    tft.println(Udp.localPort());

    tft.println("Waiting for NTP sync");
    currentTime = getNtpTime();
    tft.println(currentTime);
    setTime(currentTime);
  }

  // Wechselkurse holen
  clearTFTScreen();
  tft.println("Fetching currencies...");
  catchCurrencies();

  firstRun = false;
  secondLast = second();
  minuteLast = minute();
  displayMainScreen();

  Wire.begin(4, 5);
  pressure.begin();

  secondTick.attach(1, ISRwatchDog);
}

void loop() {
  if (minuteLast != minute()) {
    minuteLast = minute();
    if (DEBUG) Serial.print(hour());
    if (DEBUG) Serial.print(":");
    if (DEBUG) Serial.println(minute());

    // Einmal täglich um Mitternacht aktualisieren
    if ((hour() == 0) && (minuteLast == 0)) {
      if (DEBUG) Serial.println("Daily update...");
      do {
        currentTime = getNtpTime();
      } while (currentTime == 0);
      if (DEBUG) Serial.println("NTP synced");
      catchCurrencies();
      if (DEBUG) Serial.println("Currencies updated");
    }
    displayMainScreen();
  }

  helligkeit = analogRead(A0);
  delay(100);
  if (helligkeit > 1010) helligkeit = 1010;
  analogWrite(ledPin, helligkeit);
  watchDogFeed();
}
