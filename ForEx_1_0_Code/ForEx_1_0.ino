#include <TimeLib.h> 
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h> 
#include <TextFinder.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
extern "C" {
  #include "user_interface.h"
}

#define LDR
#define screenTest false

WiFiClient client;
TextFinder finder(client);

const char* TZapi = " Your worldweatheronline API ";       // Timezone API
const char* OPWMapi = " Your OpenWeatherMap API ";  // OpenWeatherMap API

const char* ssid1     = "Your SSID 1";
const char* password1 = "Your Password 1";
const char* ssid2     = "Your SSID 2 (Optional)";
const char* password2 = "Your Password 2 (Optional)";
#define maxWlanTrys 30

const char* timerServerDNSName = "0.ch.pool.ntp.org";
IPAddress timeServer;

WiFiUDP Udp;
const unsigned int localPort = 8888;  // local port to listen for UDP packets
const int NTP_PACKET_SIZE = 48;       // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE];   // buffer to hold incoming & outgoing packets

#define TFT_PIN_CS   15
#define TFT_PIN_DC   2
#define TFT_PIN_RST  12
#define ledPin 4

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_PIN_CS, TFT_PIN_DC, TFT_PIN_RST);

WiFiServer server(80);
unsigned long ulReqcount;
unsigned long ulReconncount;


time_t currentTime =0;
char anzeige[24];
int secondLast, minuteLast;
byte icon, humidity, temp;
char iconSub;
boolean gotWeather, gotForecast;
String  strBuf, strBuf2;
float lat, lon, utc;
String location[7];
String airCode[7];
long utcOffset[7];
String fxSym[4];
float fxValue[4];
int helligkeit;
boolean firstRun = true;
int wPeriode;
char buf[40];
int h1,h2;
boolean webInp = false;

void setup() {
  location[0] = "Herisau,ch"; //Origin
  location[1] = "Dubai,ae";
  airCode[1] = "DBX";
  location[2] = "Singapore,sg";
  airCode[2] = "SIN";
  location[3] = "Mejicanos,sv";
  airCode[3] = "SAL";
  location[4] = "Sydney,au";
  airCode[4] = "SYD";
  location[5] = "Bangalore,in";
  airCode[5] = "BLR";
  location[6] = "Sausalito,us";
  airCode[6] = "SFO";
  
  fxSym[0] = "CHF";  //Origin
  fxSym[1] = "USD";
  fxSym[2] = "EUR";
  fxSym[3] = "GBP";
  
#if defined(LDR)
  pinMode(ledPin,OUTPUT);
  digitalWrite(ledPin,LOW);
#endif

  tft.initR(INITR_BLACKTAB);
  tft.setTextWrap( false );
  tft.setTextColor( ST7735_WHITE );
  tft.setRotation( 1 );
  tft.fillScreen( ST7735_BLACK );
  tft.setTextColor(ST7735_WHITE,ST7735_BLACK);
  tft.setTextSize(1);
  tft.setCursor( 0, 0 );

  tft.print( "Searching for: ");
  tft.print(ssid1);
  tft.setCursor( 0, 16 );

if (!screenTest) {      
    WiFi.begin(ssid1, password1);
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
  }
  tft.setCursor( 0, 32 );
  tft.println("WiFi connected");  
  tft.println("IP address: ");
  
  tft.fillScreen( ST7735_BLACK );
  tft.setCursor( 0, 0 );

  tft.println(WiFi.localIP());
  tft.println();
  if (!screenTest) catchTimes();
 
  tft.fillScreen( ST7735_BLACK );
  tft.setCursor( 0, 0 );
 
  if (!screenTest) {
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
  }
  tft.fillScreen( ST7735_BLACK );
  tft.setCursor( 0, 0 );
  if (!screenTest) catchCurrencies();
  
  firstRun = false;
  secondLast = second();
  minuteLast = minute();
  if (!screenTest) displayMainScreen();
    else {
      tft.drawRect(0,0,160,128,ST7735_WHITE);
      tft.drawRect(1,1,158,126,ST7735_BLUE);
      tft.drawRect(2,2,156,124,ST7735_RED);
      tft.drawRect(3,3,154,122,ST7735_YELLOW);
      tft.drawRect(4,4,152,120,ST7735_GREEN);
      tft.drawRect(5,5,150,118,ST7735_CYAN);
      tft.drawRect(6,6,148,116,ST7735_BLACK);
      tft.drawRect(7,7,146,114,ST7735_WHITE);
      tft.drawRect(8,8,144,112,ST7735_YELLOW);
      tft.drawRect(9,9,142,110,ST7735_RED);
  }
   server.begin();
}

void loop(){
  if (!screenTest) {
    if (minuteLast != minute()) {
      minuteLast = minute();
      displayMainScreen(); 
      if (minuteLast == 00) {
        catchTimes();
        do {
          currentTime = getNtpTime();
        } while (currentTime == 0);
        catchCurrencies();
      }
    } 
 
    client = server.available();
    if (client) {
      boolean currentLineIsBlank = true;
      if (client.connected()) {
        while(!client.available());
        if (finder.findUntil("city","HTTP")) {
          h1 = finder.getString("=","&",buf,40);
          location[6] = "";
          for (h2 = 0; h2 < h1; h2++) {
            if (buf[h2] != '%') location[6] += buf[h2];
              else {
                location[6] += ",";
                h2++;
                h2++;
              } // else
          } // for
          if (finder.findUntil("airPort","HTTP")) {
            webInp = true;
            h1 = finder.getString("=","&",buf,40);
            airCode[6]="";
            for (h2 = 0; h2 < h1; h2++) {
              airCode[6] += buf[h2];
            } // for
            finder.findUntil("curr","HTTP"); 
            h1 = finder.getString("="," ",buf,40);
            fxSym[3]="";
            for (h2 = 0; h2 < h1; h2++) {
              fxSym[3] += buf[h2];
            } // for
          } // airPort
        } //city
       } 
      while (client.connected()) {  
       if (client.available()) {        
          char c = client.read();
          if (c == '\n' && currentLineIsBlank) break;
          if (c == '\n') currentLineIsBlank = true; else if (c != '\r') currentLineIsBlank = false;
        } // if available
      } //while
      printWeb();
      if (webInp) {
        catchTimes();
        catchCurrencies();
        webInp = false;
        displayMainScreen(); 
      }
    } // webinterface
  } // (!screenTest}
  else {
    tft.setTextSize(2);
    tft.setCursor(50,50);   
#if defined(LDR)
    int helligkeit = analogRead(A0);
#else
   int helligkeit = 999;
#endif
    if (helligkeit < 1000) tft.print(" ");  
    if (helligkeit < 100) tft.print(" ");  
    if (helligkeit < 10) tft.print(" ");  
    tft.println(helligkeit);
  }
     
#if defined(LDR)  
  helligkeit = analogRead(A0);
  if (helligkeit > 1010) helligkeit = 1010;
  analogWrite(ledPin,helligkeit);
#endif

}

