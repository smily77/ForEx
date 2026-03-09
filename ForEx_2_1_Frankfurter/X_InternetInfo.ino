// X_InternetInfo.ino – ForEx_2_1_Frankfurter
// Wechselkurse per LTE-Modul BK-7670 abrufen.
//
// ARCHITEKTUR (v2.1-FR-SSL):
//   Statt den eingebauten HTTP/SSL-Stack des BK-7670 zu nutzen (dessen
//   TLS-Fingerprint von Cloudflare blockiert wird), öffnet das Modul nur
//   eine rohe TCP-Verbindung.  BearSSL auf dem ESP8266 übernimmt den
//   TLS-Handshake – mit demselben JA3-Fingerprint wie beim 4G-Modem/WiFi.
//
//   LTEClient  (AT+CIPOPEN, TCP-Transport)
//       ↓
//   ESP_SSLClient  (BearSSL TLS, läuft auf ESP8266)
//       ↓
//   api.frankfurter.app:443  (Cloudflare akzeptiert BearSSL-JA3)
//
// ABHÄNGIGKEIT:
//   Bibliothek "ESP_SSLClient" von mobizt installieren:
//     Arduino IDE: Sketch → Bibliothek einbinden → Bibliotheken verwalten
//                  → "ESP_SSLClient" suchen und installieren
//     PlatformIO:  lib_deps = mobizt/ESP_SSLClient
//
// Forward-Deklarationen nötig, da Arduino .ino-Dateien alphabetisch
// zusammengeführt werden und X_ vor x_subroutines kommt.
String sendAT(const String& cmd, int timeout);
String sendATwait(const String& cmd, const String& waitFor, int timeout);

// ============================================================
// LTEClient – Arduino-Client-Adapter für BK-7670 (SIMCom A7670E)
//
// Stellt eine rohe TCP-Verbindung über die AT-Befehle des Moduls her.
// Implementiert das Arduino-Client-Interface, damit ESP_SSLClient
// (BearSSL) darauf aufsetzen kann.
//
// WICHTIG: Alle Methoden die von BearSSL im TLS-Handshake aufgerufen
// werden (write/available/read) verwenden statische char-Puffer statt
// String-Objekte, um Heap-Fragmentierung zu vermeiden.
// BearSSL ruft available() hunderte Male auf – jede String-Allokation
// dort würde den Heap nach wenigen Handshakes zerstören.
//
// AT-Befehlssatz: NETOPEN / CIPOPEN / CIPSEND / CIPRXGET / CIPCLOSE
// Referenz: A76XX Series TCPIP Application Note (SIMCom)
// ============================================================
class LTEClient : public Client {
public:

  // --- Verbindung aufbauen ---
  // connect() wird nur 1x pro Kursabruf aufgerufen → sendAT mit String OK
  int connect(const char *host, uint16_t port) override {
    _connected = false;
    _rxLen = 0;
    _rxPos = 0;
    _lastPoll = 0;
    if (DEBUG) { Serial.print(F("Heap vor connect: ")); Serial.println(ESP.getFreeHeap()); }

    // Buffered-Receive: Modul puffert eingehende TCP-Daten,
    // kein unerwartetes Daten-Dump auf UART.
    sendAT("AT+CIPRXGET=1", 2000);

    // Alte Verbindungen aufräumen
    sendAT("AT+CIPCLOSE=0", 2000);
    sendAT("AT+NETCLOSE", 5000);
    delay(500);

    // PDP-Kontext 1 verwenden (gleicher wie in initLTE/CGACT)
    sendAT("AT+CSOCKSETPN=1", 1000);
    sendAT("AT+CIPMODE=0", 1000);   // Non-Transparent-Modus

    // Netzwerk-Socket-Dienst starten
    String resp = sendATwait("AT+NETOPEN", "OK", 15000);
    delay(1000);  // Modul braucht Zeit zum Öffnen des Netzwerk-Sockets

    // TCP-Verbindung öffnen
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "AT+CIPOPEN=0,\"TCP\",\"%s\",%d", host, port);
    resp = sendATwait(String(cmd), "+CIPOPEN: 0,0", 20000);

    // +CIPOPEN: 0,0 = Link 0, Ergebnis 0 (Erfolg)
    if (resp.indexOf("+CIPOPEN: 0,0") != -1) {
      _connected = true;
      return 1;
    }
    return 0;
  }

  int connect(IPAddress ip, uint16_t port) override {
    char host[16];
    snprintf(host, sizeof(host), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return connect(host, port);
  }

  // --- Daten senden ---
  // HOT PATH: wird von BearSSL während TLS-Handshake aufgerufen.
  // Verwendet statische Puffer statt String.
  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t *buf, size_t size) override {
    if (!_connected || size == 0) return 0;

    // A7670 CIPSEND max 1460 Bytes pro Aufruf (ein TCP-Segment)
    int toSend = (size > 1024) ? 1024 : (int)size;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=0,%d", toSend);

    // AT-Befehl senden, auf ">" Prompt warten
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.println(cmd);

    bool gotPrompt = false;
    unsigned long t0 = millis();
    while (millis() - t0 < 5000) {
      while (lteSerial.available()) {
        if (lteSerial.read() == '>') { gotPrompt = true; break; }
      }
      if (gotPrompt) break;
      ESP.wdtFeed();
      yield();
    }
    if (!gotPrompt) { _connected = false; return 0; }

    // Restliche Zeichen nach ">" konsumieren
    delay(10);
    while (lteSerial.available()) lteSerial.read();

    // Binärdaten senden – RX-ISR deaktivieren (Bit-Korruptionsschutz).
    // Batch-Write statt Einzelbytes → kürzere RX-Pause, schneller.
    lteSerial.enableRx(false);
    lteSerial.write(buf, toSend);
    lteSerial.enableRx(true);

    // +CIPSEND Bestätigung abwarten – statischer Puffer statt String
    int rLen = 0;
    t0 = millis();
    while (millis() - t0 < 10000 && rLen < (int)sizeof(_atBuf) - 1) {
      while (lteSerial.available() && rLen < (int)sizeof(_atBuf) - 1) {
        _atBuf[rLen++] = (char)lteSerial.read();
      }
      _atBuf[rLen] = '\0';
      if (strstr(_atBuf, "+CIPSEND:") != NULL) break;
      if (strstr(_atBuf, "ERROR") != NULL) { _connected = false; return 0; }
      ESP.wdtFeed();
      yield();
    }
    return toSend;
  }

  // --- Daten empfangen ---
  // HOT PATH: BearSSL pollt available() intensiv während TLS.
  // Komplett ohne String/Heap-Allokation implementiert.
  int available() override {
    if (_rxPos < _rxLen) return _rxLen - _rxPos;

    // Modul nicht öfter als alle 50 ms abfragen (BearSSL pollt intensiv)
    unsigned long now = millis();
    if (now - _lastPoll < 50) return 0;
    _lastPoll = now;

    // Modul nach gepufferten Bytes fragen – statischer Puffer
    _sendATraw(F("AT+CIPRXGET=4,0"), 1000);

    char* idx = strstr(_atBuf, "+CIPRXGET: 4,0,");
    if (idx) {
      int avail = atoi(idx + 15);
      if (avail > 0) {
        _pullFromModule(avail);
        return _rxLen - _rxPos;
      }
    }
    return 0;
  }

  int read() override {
    uint8_t b;
    if (read(&b, 1) == 1) return b;
    return -1;
  }

  // HOT PATH: Verwendet statischen Puffer für AT-Abfrage
  int read(uint8_t *buf, size_t size) override {
    // Aus lokalem Puffer bedienen
    if (_rxPos < _rxLen) {
      int avail = _rxLen - _rxPos;
      int n = ((int)size < avail) ? (int)size : avail;
      memcpy(buf, _rxBuf + _rxPos, n);
      _rxPos += n;
      return n;
    }

    // Puffer leer → vom Modul nachladen (statischer Puffer)
    _sendATraw(F("AT+CIPRXGET=4,0"), 1000);

    char* idx = strstr(_atBuf, "+CIPRXGET: 4,0,");
    if (!idx) return 0;
    int moduleAvail = atoi(idx + 15);
    if (moduleAvail <= 0) return 0;

    _pullFromModule(moduleAvail);

    if (_rxLen <= 0) return 0;
    int n = ((int)size < _rxLen) ? (int)size : _rxLen;
    memcpy(buf, _rxBuf, n);
    _rxPos = n;
    return n;
  }

  int peek() override {
    if (_rxPos < _rxLen) return _rxBuf[_rxPos];
    return -1;
  }

  void flush() override { }

  void stop() override {
    sendAT("AT+CIPCLOSE=0", 5000);
    delay(500);
    sendAT("AT+NETCLOSE", 5000);
    _connected = false;
    _rxLen = 0;
    _rxPos = 0;
  }

  uint8_t connected() override { return _connected ? 1 : 0; }
  operator bool() override { return _connected; }

private:
  bool _connected = false;
  static uint8_t _rxBuf[256];   // Statischer Empfangspuffer (spart Stack)
  int _rxLen = 0;               // Gültige Bytes im Puffer
  int _rxPos = 0;               // Aktuelle Leseposition
  unsigned long _lastPoll = 0;  // Letzte Modul-Abfrage (Throttle)

  // Statischer AT-Antwortpuffer – wird von allen Hot-Path-Methoden geteilt.
  // Kein Reentrancy-Problem da ESP8266 single-threaded ist.
  static char _atBuf[96];

  // AT-Befehl senden und Antwort in _atBuf lesen – KEINE Heap-Allokation.
  // Für die Hot-Path-Methoden (available/read/write) während BearSSL.
  void _sendATraw(const __FlashStringHelper* cmd, unsigned long timeout) {
    int len = 0;
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.println(cmd);

    unsigned long t0 = millis();
    while (millis() - t0 < timeout && len < (int)sizeof(_atBuf) - 1) {
      while (lteSerial.available() && len < (int)sizeof(_atBuf) - 1) {
        _atBuf[len++] = (char)lteSerial.read();
      }
      _atBuf[len] = '\0';
      if (strstr(_atBuf, "OK\r") || strstr(_atBuf, "ERROR")) break;
      ESP.wdtFeed();
      yield();
    }
    _atBuf[len] = '\0';
  }

  // Daten vom Modul in den lokalen Puffer lesen.
  // AT+CIPRXGET=2,0,<len> → +CIPRXGET: 2,0,<actual>,<remaining>\r\n<Daten>\r\nOK
  // Verwendet statischen Header-Puffer statt String.
  void _pullFromModule(int moduleAvail) {
    _rxLen = 0;
    _rxPos = 0;

    int toRead = moduleAvail;
    if (toRead > (int)sizeof(_rxBuf)) toRead = (int)sizeof(_rxBuf);

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+CIPRXGET=2,0,%d", toRead);

    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.println(cmd);

    // Header lesen: +CIPRXGET: 2,0,<actual>,<remaining>\r\n
    // Statischer Puffer statt String – kein Heap-Allokation
    static char hdr[80];
    int hdrLen = 0;
    bool gotHeader = false;
    unsigned long t0 = millis();
    while (millis() - t0 < 5000 && hdrLen < (int)sizeof(hdr) - 1) {
      while (lteSerial.available() && hdrLen < (int)sizeof(hdr) - 1) {
        char c = (char)lteSerial.read();
        hdr[hdrLen++] = c;
        hdr[hdrLen] = '\0';
        if (c == '\n' && strstr(hdr, "+CIPRXGET: 2") != NULL) {
          gotHeader = true;
          break;
        }
      }
      if (gotHeader) break;
      ESP.wdtFeed();
      yield();
    }
    if (!gotHeader) return;

    // Tatsächliche Datenlänge parsen
    char* idx = strstr(hdr, "+CIPRXGET: 2,0,");
    if (!idx) return;
    int actual = atoi(idx + 15);
    if (actual <= 0) return;
    if (actual > (int)sizeof(_rxBuf)) actual = sizeof(_rxBuf);

    // Exakt 'actual' Bytes Binärdaten lesen
    t0 = millis();
    while (_rxLen < actual && millis() - t0 < 5000) {
      while (lteSerial.available() && _rxLen < actual) {
        _rxBuf[_rxLen++] = lteSerial.read();
      }
      if (_rxLen >= actual) break;
      ESP.wdtFeed();
      yield();
    }

    // Trailing \r\nOK\r\n konsumieren
    delay(50);
    while (lteSerial.available()) lteSerial.read();
  }
};

// Statische Member-Definitionen
uint8_t LTEClient::_rxBuf[256];
char    LTEClient::_atBuf[96];

// ============================================================
// WECHSELKURSE ABRUFEN
// ============================================================

void catchCurrencies() {
  const char* httpHost = "api.frankfurter.app";
  const char* httpPath = "/latest?from=EUR&to=CHF,USD,GBP";

  if (DEBUG) {
    Serial.println(F("=== catchCurrencies (SSL-Passthrough) ==="));
    Serial.print(F("URL: https://"));
    Serial.print(httpHost);
    Serial.println(httpPath);
    Serial.print(F("Freier Heap: "));
    Serial.println(ESP.getFreeHeap());
  }

  // Watchdog-Zähler zurücksetzen (180s Timeout reicht für TLS)
  watchDogCount = 0;

  // Heap-Guard: BearSSL braucht ~15-20 KB für TLS-Handshake
  if (ESP.getFreeHeap() < 15000) {
    if (DEBUG) Serial.println(F("ABBRUCH: Heap zu niedrig fuer TLS!"));
    tft.setTextColor(ST7735_YELLOW);
    tft.println("Heap zu niedrig");
    tft.setTextColor(ST7735_WHITE);
    return;
  }

  // Statischer Puffer für HTTP-Antwort (frankfurter.app: ~120 Bytes)
  static char body[512];
  int bodyLen = 0;
  memset(body, 0, sizeof(body));
  bool success = false;

  // --- TCP-Verbindung über LTEClient öffnen ---
  static LTEClient lteClient;  // static: nicht auf dem Stack (spart ~50 Bytes)

  if (DEBUG) Serial.println(F("TCP-Verbindung aufbauen..."));

  if (!lteClient.connect(httpHost, 443)) {
    if (DEBUG) Serial.println(F("TCP-Verbindung fehlgeschlagen"));
    tft.setTextColor(ST7735_YELLOW);
    tft.println("TCP Fehler");
    tft.setTextColor(ST7735_WHITE);
    lteClient.stop();
    return;
  }
  if (DEBUG) Serial.println(F("TCP OK, starte TLS..."));

  // --- BearSSL-TLS über die TCP-Verbindung ---
  // Static: vermeidet wiederholte Konstruktion/Destruktion und Heap-Churn.
  // BearSSL-Kontextstrukturen bleiben allokiert statt bei jedem Abruf
  // neu angelegt und freigegeben zu werden (reduziert Fragmentierung).
  static ESP_SSLClient sslClient;
  sslClient.setInsecure();             // Zertifikat nicht prüfen (spart RAM)
  sslClient.setBufferSizes(2048, 512); // RX 2 KB (statt 4 KB – spart Heap)
  sslClient.setClient(&lteClient);

  if (DEBUG) {
    Serial.print(F("Freier Heap vor TLS: "));
    Serial.println(ESP.getFreeHeap());
  }

  if (!sslClient.connect(httpHost, 443)) {
    if (DEBUG) Serial.println(F("TLS-Handshake fehlgeschlagen"));
    tft.setTextColor(ST7735_YELLOW);
    tft.println("TLS Fehler");
    tft.setTextColor(ST7735_WHITE);
    sslClient.stop();
    return;
  }
  if (DEBUG) {
    Serial.println(F("TLS OK, sende HTTP GET..."));
    Serial.print(F("Freier Heap nach TLS: "));
    Serial.println(ESP.getFreeHeap());
  }

  // --- HTTP GET Request senden ---
  sslClient.print(F("GET "));
  sslClient.print(httpPath);
  sslClient.println(F(" HTTP/1.1"));
  sslClient.print(F("Host: "));
  sslClient.println(httpHost);
  sslClient.println(F("Accept: application/json"));
  sslClient.println(F("Connection: close"));
  sslClient.print("\r\n");

  // --- HTTP-Header überspringen (bis \r\n\r\n) ---
  if (DEBUG) Serial.println(F("Warte auf Antwort..."));
  int nlCount = 0;
  bool headersDone = false;
  unsigned long t0 = millis();

  while (sslClient.connected() && millis() - t0 < 30000) {
    if (sslClient.available()) {
      char c = sslClient.read();
      if (c == '\r') continue;
      if (c == '\n') {
        nlCount++;
        if (nlCount >= 2) { headersDone = true; break; }
      } else {
        nlCount = 0;
      }
    }
    ESP.wdtFeed();
    yield();
  }

  // --- HTTP-Body lesen ---
  if (headersDone) {
    t0 = millis();
    while (sslClient.connected() && millis() - t0 < 15000) {
      while (sslClient.available() && bodyLen < (int)(sizeof(body) - 1)) {
        body[bodyLen++] = sslClient.read();
      }
      if (!sslClient.available() && bodyLen > 0) {
        // Kurz warten ob noch Daten kommen
        delay(200);
        if (!sslClient.available()) break;
      }
      ESP.wdtFeed();
      yield();
    }
    body[bodyLen] = '\0';
    success = (bodyLen >= 10);
  }

  sslClient.stop();

  if (DEBUG) {
    Serial.print(F("Body ("));
    Serial.print(bodyLen);
    Serial.println(F(" Bytes):"));
    Serial.println(body);
  }

  if (!success) {
    if (DEBUG) Serial.println(F("Keine gueltige Antwort erhalten"));
    tft.setTextColor(ST7735_YELLOW);
    tft.println("Kurs-Fehler");
    tft.setTextColor(ST7735_WHITE);
    return;
  }

  // -------------------------------------------------------
  // JSON parsen mit strstr/atof – kein String-Objekt nötig.
  // fxSym[0]="CHF", fxSym[1]="USD", fxSym[2]="EUR", fxSym[3]="GBP"
  // API liefert CHF, USD, GBP relativ zu EUR.
  // EUR selbst nicht in Antwort → 1.0 gesetzt.
  // -------------------------------------------------------
  for (int i = 0; i < 4; i++) {
    if (fxSym[i] != "EUR") {
      char key[10];
      snprintf(key, sizeof(key), "\"%s\":", fxSym[i].c_str());
      char* p = strstr(body, key);
      if (p) {
        p += strlen(key);
        fxValue[i] = atof(p);
      } else {
        fxValue[i] = 0.0;
      }
    } else {
      fxValue[i] = 1.0;
    }

    if (DEBUG) {
      Serial.print(fxSym[i]); Serial.print(F("/EUR raw: "));
      Serial.println(fxValue[i]);
    }
  }

  // Umrechnung: CHF/Währung = CHF/EUR ÷ Währung/EUR
  for (int l = 1; l < 4; l++) {
    if (fxValue[l] > 0.0) {
      fxValue[l] = fxValue[0] / fxValue[l];
    }
    if (firstRun) {
      tft.print(fxSym[l]);
      tft.print(": ");
      tft.println(fxValue[l]);
    }
    if (DEBUG) {
      Serial.print("CHF/"); Serial.print(fxSym[l]);
      Serial.print(" = "); Serial.println(fxValue[l]);
    }
  }

  if (DEBUG) Serial.println(F("=== catchCurrencies Ende ==="));
}
