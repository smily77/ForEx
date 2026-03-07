// X_InternetInfo.ino – ForEx_2_1
// Wechselkurse per LTE-Modul BK-7670 abrufen.
// Forward-Deklarationen nötig, da Arduino .ino-Dateien alphabetisch
// zusammengeführt werden und X_ vor x_subroutines kommt.
String sendAT(const String& cmd, int timeout);
String sendATwait(const String& cmd, const String& waitFor, int timeout);

// Verwendet den eingebauten HTTP-Stack des Moduls (AT+HTTPINIT / HTTPACTION).
// Die AT-Befehlssyntax folgt dem 3GPP-Standard / SIMCom-kompatiblem Befehlssatz.
// Falls der BK-7670 einen anderen HTTP-Befehlssatz verwendet, bitte Datenblatt
// konsultieren und die Befehle unten entsprechend anpassen.

// API: https://api.exchangerate-api.com/v4/latest/EUR
// Fastly CDN (nicht Cloudflare) → breite TLS-Kompatibilität, kein Bot-Blocking.
// Freier Endpunkt, kein API-Key nötig.
// Response enthält alle ~160 Währungen alphabetisch. USD erscheint erst bei
// ~1700 Bytes → httpLen auf 2500 gesetzt.
// Antwort-Beispiel (gekürzt):
//   {"base":"EUR","date":"2025-03-05","time_last_updated":...,"rates":{
//    ...,"CHF":0.9560,...,"GBP":0.8575,...,"USD":1.0853,...}}

void catchCurrencies() {
  const char* httpHost = "api.exchangerate-api.com";
  const char* httpPath = "/v4/latest/EUR";

  if (DEBUG) {
    Serial.println("=== catchCurrencies ===");
    Serial.print("URL: https://");
    Serial.print(httpHost);
    Serial.println(httpPath);
  }

  // Eigenen 60s-Watchdog während HTTP pausieren – die gesamte HTTP-Sequenz
  // (HTTPINIT + SSL + GET + HTTPREAD) kann 30-50 s dauern.
  secondTick.detach();
  watchDogCount     = 0;
  watchDogTriggered = false;

  String resp;

  // Statischer Puffer statt String – verhindert Heap-Fragmentierungskrash
  // nach langem Betrieb. 'static' → liegt im BSS-Segment, nicht auf Stack/Heap.
  static char body[2600];
  int bodyLen = 0;
  memset(body, 0, sizeof(body));

  if (DEBUG) {
    Serial.print(F("Freier Heap: "));
    Serial.println(ESP.getFreeHeap());
  }

  // --- HTTP-Stack initialisieren ---
  // BK-7670 verwendet den aktiven PDP-Kontext (AT+CGACT=1,1) automatisch.
  // SAPBR und CID werden von diesem Modul nicht unterstützt.
  sendAT("AT+HTTPTERM", 1000);   // evtl. offene Session beenden
  delay(200);

  resp = sendAT("AT+HTTPINIT", 3000);
  if (resp.indexOf("OK") == -1) {
    if (DEBUG) Serial.println("HTTPINIT fehlgeschlagen: " + resp);
    tft.setTextColor(ST7735_YELLOW);
    tft.println("HTTP Init fehl");
    tft.setTextColor(ST7735_WHITE);
    sendAT("AT+HTTPTERM", 1000);
    secondTick.attach(1, ISRwatchDog);
    return;
  }

  // URL setzen – vollständigen AT-Befehl in char[] bauen und als EINE println()-
  // Übertragung senden. Während TX den SoftwareSerial-RX-Interrupt deaktivieren:
  // Auf ESP8266 ist SoftwareSerial-TX nicht interrupt-sicher – der RX-ISR feuert
  // zwischen TX-Bits und zerstört das Byte-Timing (Korruption ab ~Byte 76).
  {
    // Buffer: "AT+HTTPPARA="URL","https://<host><path>"" + NUL ≤ 100 Bytes
    char urlCmd[100];
    snprintf(urlCmd, sizeof(urlCmd),
             "AT+HTTPPARA=\"URL\",\"https://%s%s\"", httpHost, httpPath);

    delay(30);
    while (lteSerial.available()) lteSerial.read();

    if (DEBUG) { Serial.print(F(">> ")); Serial.println(urlCmd); }

    lteSerial.enableRx(false);   // RX-ISR aus → kein Interrupt-Jitter beim TX
    lteSerial.println(urlCmd);
    lteSerial.enableRx(true);    // RX-ISR wieder an, bereit für Modul-Antwort

    resp = "";
    long t0 = millis();
    while (millis() - t0 < 3000) {
      while (lteSerial.available()) resp += (char)lteSerial.read();
      if (resp.indexOf("OK") != -1 || resp.indexOf("ERROR") != -1) break;
      yield();
    }
    if (DEBUG) { Serial.print(F("<< ")); Serial.println(resp); }
    if (resp.indexOf("OK") == -1) {
      if (DEBUG) Serial.println(F("URL-Param fehlgeschlagen"));
    }
  }

  // GET-Request senden und +HTTPACTION-URC in statischen char-Puffer einlesen.
  // Kein String, kein realloc() – kein Heap-Stress in der kritischen Wartephase.
  int httpLen = 0;
  {
    static char urcBuf[128];
    int urcLen = 0;
    memset(urcBuf, 0, sizeof(urcBuf));

    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.println(F("AT+HTTPACTION=0"));
    if (DEBUG) Serial.println(F(">> AT+HTTPACTION=0"));

    long tWait = millis();
    while (millis() - tWait < 35000) {
      while (lteSerial.available() && urcLen < (int)(sizeof(urcBuf) - 1)) {
        urcBuf[urcLen++] = (char)lteSerial.read();
        urcBuf[urcLen]   = '\0';
      }
      // Vollständige URC: "+HTTPACTION:" + 2 Kommas + Newline
      char* hPtr = strstr(urcBuf, "+HTTPACTION:");
      if (hPtr) {
        char* c1 = strchr(hPtr, ',');
        char* c2 = c1 ? strchr(c1 + 1, ',') : nullptr;
        if (c2 && strchr(c2 + 1, '\n')) break;
      }
      if (urcLen >= (int)(sizeof(urcBuf) - 10)) break; // Pufferschutz
      ESP.wdtFeed();
      delay(100);
      yield();
    }
    if (DEBUG) { Serial.print(F("HTTPACTION: ")); Serial.println(urcBuf); }

    // HTTP 200 prüfen
    if (!strstr(urcBuf, ",200,")) {
      if (DEBUG) Serial.println(F("HTTP-Fehler oder Timeout"));
      tft.setTextColor(ST7735_YELLOW);
      tft.println("HTTP Fehler");
      tft.setTextColor(ST7735_WHITE);
      sendAT("AT+HTTPTERM", 1000);
      secondTick.attach(1, ISRwatchDog);
      return;
    }

    // Länge aus ",200,<len>" extrahieren
    char* lenPtr = strstr(urcBuf, ",200,");
    httpLen = lenPtr ? atoi(lenPtr + 5) : 0;
  }

  if (httpLen <= 0 || httpLen > (int)(sizeof(body) - 1)) httpLen = sizeof(body) - 1;

  // Antwort-Body in statischen char-Buffer lesen.
  // Modul sendet: OK\r\n  +HTTPREAD: <len>\r\n  <Daten>  OK\r\n
  // Warten bis "+HTTPREAD:" gesehen UND danach "OK\r" (End-Markierung).
  {
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.print(F("AT+HTTPREAD=0,"));
    lteSerial.println(httpLen);
    if (DEBUG) { Serial.print(F(">> AT+HTTPREAD=0,")); Serial.println(httpLen); }

    long t0 = millis();
    bool httpReadSeen = false;
    int  httpReadPos  = 0;

    while (millis() - t0 < 20000) {
      while (lteSerial.available() && bodyLen < (int)(sizeof(body) - 1)) {
        body[bodyLen++] = (char)lteSerial.read();
        body[bodyLen]   = '\0';
      }
      if (!httpReadSeen) {
        char* p = strstr(body, "+HTTPREAD:");
        if (p) { httpReadSeen = true; httpReadPos = (int)(p - body); }
      }
      if (httpReadSeen && strstr(body + httpReadPos, "OK\r")) break;
      if (strstr(body, "ERROR")) break;
      yield();
    }
    if (DEBUG) { Serial.println(F("HTTP Body:")); Serial.println(body); }
  }

  // HTTP-Stack beenden und eigenen Watchdog wieder aktivieren
  sendAT("AT+HTTPTERM", 1000);
  secondTick.attach(1, ISRwatchDog);

  if (bodyLen < 10) {
    if (DEBUG) Serial.println(F("Leere Antwort – Abbruch"));
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
      // Suchschlüssel z.B. "\"CHF\":"
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
  // Ergebnis: wie viele CHF pro 1 USD/EUR/GBP
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

  if (DEBUG) Serial.println("=== catchCurrencies Ende ===");
}
