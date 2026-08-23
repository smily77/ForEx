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

// ============================================================
// Blockmarker aus der Modul-Antwort entfernen.
//
// Der BK-7670 liefert den HTTP-Body nicht am Stück, sondern in
// 1024-Byte-Blöcken – und stellt jedem Block eine eigene Zeile
// "+HTTPREAD: <n>" voran. Diese Zeilen landen MITTEN in den Nutzdaten:
//
//   ..."UGX":4290.09,"USD":1
//   +HTTPREAD: 217
//   .17,"UYU":46.78,...
//
// Dadurch wurde "USD":1.17 in "USD":1 und ".17" zerrissen; atof() las
// 1.0 und der USD-Kurs war identisch mit dem EUR-Kurs. Vor dem Parsen
// müssen die Marker samt zugehörigem Zeilenumbruch heraus, damit das
// JSON wieder zusammenhängend ist.
// ============================================================
static void stripHttpReadMarkers(char* buf) {
  char* src = buf;
  char* dst = buf;
  while (*src) {
    if (strncmp(src, "+HTTPREAD:", 10) == 0) {
      // Markerzeile bis einschliesslich Zeilenende überspringen
      while (*src && *src != '\n') src++;
      if (*src == '\n') src++;
      // das direkt davor kopierte CR/LF wieder zurücknehmen,
      // damit die beiden JSON-Hälften nahtlos aneinanderstossen
      while (dst > buf && (dst[-1] == '\r' || dst[-1] == '\n')) dst--;
      continue;
    }
    *dst++ = *src++;
  }
  *dst = '\0';
}

// Liefert true, wenn gültige Kurse gelesen und gesetzt wurden.
bool catchCurrencies() {
  const char* httpHost = "api.exchangerate-api.com";
  const char* httpPath = "/v4/latest/EUR";

  unsigned long fxStart = millis();
  Serial.print(F("--- Kursabruf: https://"));
  Serial.print(httpHost);
  Serial.println(httpPath);

  // Watchdog läuft weiter (180s Timeout reicht für HTTP-Sequenz)
  watchDogCount = 0;

  String resp;

  // Statischer Puffer statt String – verhindert Heap-Fragmentierungskrash
  // nach langem Betrieb. 'static' → liegt im BSS-Segment, nicht auf Stack/Heap.
  // 3584 statt 2600 Bytes: die Antwort enthält ~160 Währungen (~2.5 KB) und
  // "USD" steht weit hinten. Mit 2600 Bytes wurde die Antwort mitten in der
  // USD-Zahl abgeschnitten – atof() lieferte dann 1.0 statt des echten Kurses.
  static char body[3584];
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
    Serial.print(F("[FEHLER] HTTPINIT: ")); Serial.println(resp);
    tft.setTextColor(ST7735_YELLOW);
    tft.println("HTTP Init fehl");
    tft.setTextColor(ST7735_WHITE);
    sendAT("AT+HTTPTERM", 1000);
    return false;
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
    unsigned long t0 = millis();
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

    unsigned long tWait = millis();
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
    // URC immer protokollieren (enthält HTTP-Status und Länge)
    Serial.print(F("HTTPACTION -> "));
    for (int k = 0; k < urcLen; k++) {
      char c = urcBuf[k];
      Serial.print((c == '\r' || c == '\n') ? ' ' : c);
    }
    Serial.println();

    // HTTP 200 prüfen
    if (!strstr(urcBuf, ",200,")) {
      Serial.println(F("[FEHLER] HTTP-Status != 200 oder Timeout"));
      tft.setTextColor(ST7735_YELLOW);
      tft.println("HTTP Fehler");
      tft.setTextColor(ST7735_WHITE);
      sendAT("AT+HTTPTERM", 1000);
      return false;
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

    unsigned long t0        = millis();
    unsigned long tLastByte = millis();
    bool httpReadSeen = false;
    int  httpReadPos  = 0;

    while (millis() - t0 < 30000) {
      bool gotData = false;
      while (lteSerial.available() && bodyLen < (int)(sizeof(body) - 1)) {
        body[bodyLen++] = (char)lteSerial.read();
        body[bodyLen]   = '\0';
        gotData = true;
      }
      if (gotData) tLastByte = millis();

      if (!httpReadSeen) {
        char* p = strstr(body, "+HTTPREAD:");
        if (p) { httpReadSeen = true; httpReadPos = (int)(p - body); }
      }
      // Reguläres Ende: "+HTTPREAD: 0" ist der Abschlussmarker des Moduls
      // (nach dem letzten Datenblock). Das ist die schnellste und
      // eindeutigste Abbruchbedingung.
      if (strstr(body, "+HTTPREAD: 0\r")) break;
      if (httpReadSeen && strstr(body + httpReadPos, "OK\r")) break;
      if (strstr(body, "ERROR")) break;

      // Notausstieg bei Chunked-Antwort: api.exchangerate-api.com antwortet
      // mit "Transfer-Encoding: chunked". AT+HTTPACTION meldet dann
      // "+HTTPACTION: 0,200,chunk" – also KEINE Byte-Länge. Wir müssen
      // AT+HTTPREAD deshalb mit der Puffergrösse aufrufen, worauf das Modul
      // auf Daten wartet, die nie kommen, und das abschliessende OK ausbleibt.
      // Ohne diesen Abbruch lief jeder Kursabruf volle 30 s ins Timeout.
      if (bodyLen > 64 && millis() - tLastByte > 2000) break;

      // Hardware-Watchdog (8 s) füttern. Fehlte hier bisher komplett –
      // deshalb gab es mitten im Lesen einen "rst cause:4 / wdt reset".
      ESP.wdtFeed();
      delay(1);   // echte Rückgabe an den SDK-Task
    }
    if (DEBUG) { Serial.println(F("HTTP Body:")); Serial.println(body); }
    Serial.print(F("Body: ")); Serial.print(bodyLen); Serial.println(F(" Bytes"));
  }

  // HTTP-Stack beenden
  sendAT("AT+HTTPTERM", 1000);

  if (bodyLen < 10) {
    Serial.println(F("[FEHLER] Leere Antwort – Abbruch"));
    return false;
  }

  // Blockmarker entfernen – sonst zerreisst "+HTTPREAD: <n>" Zahlenwerte
  stripHttpReadMarkers(body);

  // Ab der ersten geschweiften Klammer parsen (davor steht noch das
  // "OK" der AT-Quittung)
  char* json = strchr(body, '{');
  if (json == NULL) {
    Serial.println(F("[FEHLER] Kein JSON in der Antwort"));
    return false;
  }
  if (DEBUG) { Serial.println(F("JSON bereinigt:")); Serial.println(json); }

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
      char* p = strstr(json, key);
      if (p) {
        p += strlen(key);
        fxValue[i] = atof(p);
      } else {
        fxValue[i] = 0.0;   // Kein Wert → 0 anzeigen statt alten (falschen) Wert
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

  // Plausibilitätsprüfung: alle drei Kurse müssen > 0 sein. Sonst hat das
  // Parsen etwas nicht gefunden und der Aufrufer soll erneut versuchen.
  bool ok = (fxValue[1] > 0.0 && fxValue[2] > 0.0 && fxValue[3] > 0.0);

  // Ergebnis immer protokollieren – macht spätere Fehlersuche möglich
  Serial.printf("[%s] Kurse nach %lu ms: CHF/USD %.4f  CHF/EUR %.4f  CHF/GBP %.4f\n",
                ok ? "OK" : "FEHLER", millis() - fxStart,
                fxValue[1], fxValue[2], fxValue[3]);
  return ok;
}
