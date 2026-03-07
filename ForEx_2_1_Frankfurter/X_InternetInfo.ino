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
// AT-Befehlssatz: NETOPEN / CIPOPEN / CIPSEND / CIPRXGET / CIPCLOSE
// Referenz: A76XX Series TCPIP Application Note (SIMCom)
// ============================================================
class LTEClient : public Client {
public:

  // --- Verbindung aufbauen ---
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
    // Auf OK warten (NETOPEN: 0 kommt asynchron, aber OK reicht als Bestätigung)
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

    // Binärdaten senden – RX-ISR deaktivieren (Bit-Korruptionsschutz,
    // SoftwareSerial-TX ist auf ESP8266 nicht interrupt-sicher ab ~76 Bytes)
    lteSerial.enableRx(false);
    for (int i = 0; i < toSend; i++) {
      lteSerial.write(buf[i]);
    }
    lteSerial.enableRx(true);

    // +CIPSEND Bestätigung abwarten
    String resp;
    resp.reserve(64);
    t0 = millis();
    while (millis() - t0 < 10000) {
      while (lteSerial.available()) resp += (char)lteSerial.read();
      if (resp.indexOf("+CIPSEND:") != -1) break;
      if (resp.indexOf("ERROR") != -1) { _connected = false; return 0; }
      ESP.wdtFeed();
      yield();
    }
    return toSend;
  }

  // --- Daten empfangen ---
  int available() override {
    if (_rxPos < _rxLen) return _rxLen - _rxPos;

    // Modul nicht öfter als alle 50 ms abfragen (BearSSL pollt intensiv)
    unsigned long now = millis();
    if (now - _lastPoll < 50) return 0;
    _lastPoll = now;

    // Modul nach gepufferten Bytes fragen
    String resp = sendAT("AT+CIPRXGET=4,0", 1000);
    int idx = resp.indexOf("+CIPRXGET: 4,0,");
    if (idx >= 0) {
      int avail = resp.substring(idx + 15).toInt();
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

  int read(uint8_t *buf, size_t size) override {
    // Aus lokalem Puffer bedienen
    if (_rxPos < _rxLen) {
      int avail = _rxLen - _rxPos;
      int n = ((int)size < avail) ? (int)size : avail;
      memcpy(buf, _rxBuf + _rxPos, n);
      _rxPos += n;
      return n;
    }

    // Puffer leer → vom Modul nachladen
    String resp = sendAT("AT+CIPRXGET=4,0", 1000);
    int idx = resp.indexOf("+CIPRXGET: 4,0,");
    if (idx < 0) return 0;
    int moduleAvail = resp.substring(idx + 15).toInt();
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

  // Daten vom Modul in den lokalen Puffer lesen.
  // AT+CIPRXGET=2,0,<len> → +CIPRXGET: 2,0,<actual>,<remaining>\r\n<Daten>\r\nOK
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
    String header;
    header.reserve(64);
    bool gotHeader = false;
    unsigned long t0 = millis();
    while (millis() - t0 < 5000) {
      while (lteSerial.available()) {
        char c = (char)lteSerial.read();
        header += c;
        if (header.endsWith("\n") && header.indexOf("+CIPRXGET: 2") >= 0) {
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
    int idx = header.indexOf("+CIPRXGET: 2,0,");
    if (idx < 0) return;
    int actual = header.substring(idx + 15).toInt();
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

// Statische Member-Definition
uint8_t LTEClient::_rxBuf[256];

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

  // Eigenen 60s-Watchdog während TLS pausieren
  secondTick.detach();
  watchDogCount     = 0;
  watchDogTriggered = false;

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
    secondTick.attach(1, ISRwatchDog);
    return;
  }
  if (DEBUG) Serial.println(F("TCP OK, starte TLS..."));

  // --- BearSSL-TLS über die TCP-Verbindung ---
  ESP_SSLClient sslClient;
  sslClient.setInsecure();             // Zertifikat nicht prüfen (spart RAM)
  sslClient.setBufferSizes(4096, 512); // RX 4 KB für Zertifikats-Chain
  sslClient.setClient(&lteClient);

  if (!sslClient.connect(httpHost, 443)) {
    if (DEBUG) Serial.println(F("TLS-Handshake fehlgeschlagen"));
    tft.setTextColor(ST7735_YELLOW);
    tft.println("TLS Fehler");
    tft.setTextColor(ST7735_WHITE);
    sslClient.stop();
    secondTick.attach(1, ISRwatchDog);
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

  // Watchdog wieder aktivieren
  secondTick.attach(1, ISRwatchDog);

  if (!success) {
    if (DEBUG) Serial.println(F("Keine gültige Antwort erhalten"));
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
