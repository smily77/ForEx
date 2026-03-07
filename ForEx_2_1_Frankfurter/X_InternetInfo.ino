// X_InternetInfo.ino – ForEx_2_1_Frankfurter
// Wechselkurse per LTE-Modul BK-7670 abrufen.
// Forward-Deklarationen nötig, da Arduino .ino-Dateien alphabetisch
// zusammengeführt werden und X_ vor x_subroutines kommt.
String sendAT(const String& cmd, int timeout);
String sendATwait(const String& cmd, const String& waitFor, int timeout);

// Verwendet den eingebauten HTTP-Stack des Moduls (AT+HTTPINIT / HTTPACTION).
// Die AT-Befehlssyntax folgt dem 3GPP-Standard / SIMCom-kompatiblem Befehlssatz.
// Falls der BK-7670 einen anderen HTTP-Befehlssatz verwendet, bitte Datenblatt
// konsultieren und die Befehle unten entsprechend anpassen.

// Primäre API: https://api.frankfurter.app/latest?from=EUR&to=CHF,USD,GBP
//   Gehostet von Frankfurter (Open Source, Cloudflare CDN).
//   Freier Endpunkt, kein API-Key nötig.
//   Liefert NUR die angefragten Währungen → sehr kompakte Antwort (~120 Bytes).
//   PROBLEM: Cloudflare blockiert den BK-7670 per TLS-Fingerprinting (JA3).
//   HTTP-Header allein reichen nicht – Cloudflare erkennt den TLS-Handshake
//   des Modul-Chipsatzes als Nicht-Browser und antwortet mit 403.
//
// Fallback-API: https://api.exchangerate-api.com/v4/latest/EUR
//   Fastly CDN (kein Cloudflare) → breite TLS-Kompatibilität, kein Bot-Blocking.
//   Freier Endpunkt, kein API-Key nötig.
//   Antwort enthält alle ~160 Währungen (~2500 Bytes) – grösser, aber zuverlässig.
//   JSON-Format identisch ({"rates":{"CHF":...}}) → gleicher Parser funktioniert.
//
// Strategie: frankfurter.app zuerst versuchen (kompakter). Bei 403 oder anderem
// Fehler automatisch auf exchangerate-api.com umschalten.

void catchCurrencies() {
  // Zwei API-Endpunkte: kompakt (Cloudflare) und zuverlässig (Fastly)
  const char* apiHost[] = {
    "api.frankfurter.app",
    "api.exchangerate-api.com"
  };
  const char* apiPath[] = {
    "/latest?from=EUR&to=CHF,USD,GBP",
    "/v4/latest/EUR"
  };
  const int apiCount = 2;

  if (DEBUG) Serial.println(F("=== catchCurrencies ==="));

  // Eigenen 60s-Watchdog während HTTP pausieren – die gesamte HTTP-Sequenz
  // (HTTPINIT + SSL + GET + HTTPREAD) kann 30-50 s dauern.
  secondTick.detach();
  watchDogCount     = 0;
  watchDogTriggered = false;

  String resp;

  // Statischer Puffer statt String – verhindert Heap-Fragmentierungskrash
  // nach langem Betrieb. 2600 Bytes nötig für die grosse Fallback-Antwort.
  static char body[2600];
  int bodyLen = 0;

  if (DEBUG) {
    Serial.print(F("Freier Heap: "));
    Serial.println(ESP.getFreeHeap());
  }

  bool gotData = false;

  // ---------------------------------------------------------------
  // Schleife über API-Endpunkte: frankfurter.app zuerst, dann Fallback
  // ---------------------------------------------------------------
  for (int api = 0; api < apiCount && !gotData; api++) {
    const char* httpHost = apiHost[api];
    const char* httpPath = apiPath[api];

    memset(body, 0, sizeof(body));
    bodyLen = 0;

    if (DEBUG) {
      if (api > 0) Serial.println(F("--- Fallback-API ---"));
      Serial.print(F("URL: https://"));
      Serial.print(httpHost);
      Serial.println(httpPath);
    }

    // --- HTTP-Stack initialisieren ---
    // BK-7670 verwendet den aktiven PDP-Kontext (AT+CGACT=1,1) automatisch.
    // SAPBR und CID werden von diesem Modul nicht unterstützt.
    sendAT("AT+HTTPTERM", 1000);   // evtl. offene Session beenden
    delay(200);

    resp = sendAT("AT+HTTPINIT", 3000);
    if (resp.indexOf("OK") == -1) {
      if (DEBUG) Serial.println("HTTPINIT fehlgeschlagen: " + resp);
      if (api == apiCount - 1) {
        tft.setTextColor(ST7735_YELLOW);
        tft.println("HTTP Init fehl");
        tft.setTextColor(ST7735_WHITE);
      }
      sendAT("AT+HTTPTERM", 1000);
      continue;
    }

    // URL setzen – vollständigen AT-Befehl in char[] bauen und als EINE println()-
    // Übertragung senden. Während TX den SoftwareSerial-RX-Interrupt deaktivieren:
    // Auf ESP8266 ist SoftwareSerial-TX nicht interrupt-sicher – der RX-ISR feuert
    // zwischen TX-Bits und zerstört das Byte-Timing (Korruption ab ~Byte 76).
    {
      // Buffer: "AT+HTTPPARA="URL","https://<host><path>"" + NUL
      // Längste URL: exchangerate-api.com/v4/latest/EUR → ~65 Zeichen
      // frankfurter.app/latest?from=EUR&to=CHF,USD,GBP   → ~80 Zeichen
      // Mit AT-Prefix: ~120 Zeichen max → passt in 128 Bytes.
      char urlCmd[128];
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

    // User-Agent + Host + Accept-Header setzen.
    // Vollständiger Browser-UA – minimiert Cloudflare-Bot-Erkennung.
    // Host-Header explizit, damit der Server den richtigen vHost auswählt.
    // AT+HTTPPARA="USERDATA" hängt beliebige HTTP-Header an
    // (SIM7670-Befehlssatz, \\r\\n als Header-Trenner im AT-Befehl).
    // Befehl ist >76 Bytes → RX-ISR während TX deaktivieren (Bit-Korruptionsschutz).
    {
      char hdCmd[256];
      snprintf(hdCmd, sizeof(hdCmd),
               "AT+HTTPPARA=\"USERDATA\",\"User-Agent: Mozilla/5.0 "
               "(Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
               "(KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"
               "\\r\\nHost: %s"
               "\\r\\nAccept: application/json\"",
               httpHost);

      delay(30);
      while (lteSerial.available()) lteSerial.read();

      if (DEBUG) { Serial.print(F(">> ")); Serial.println(hdCmd); }

      lteSerial.enableRx(false);
      lteSerial.println(hdCmd);
      lteSerial.enableRx(true);

      resp = "";
      long t0 = millis();
      while (millis() - t0 < 3000) {
        while (lteSerial.available()) resp += (char)lteSerial.read();
        if (resp.indexOf("OK") != -1 || resp.indexOf("ERROR") != -1) break;
        yield();
      }
      if (DEBUG) { Serial.print(F("USERDATA: ")); Serial.println(resp); }
    }

    // Redirect-Following aktivieren – manche CDNs leiten um (301/302).
    // Falls der Befehl nicht unterstützt wird → ERROR, schadet nicht.
    sendAT("AT+HTTPPARA=\"REDIR\",\"1\"", 2000);

    // GET-Request senden und +HTTPACTION-URC in statischen char-Puffer einlesen.
    // Kein String, kein realloc() – kein Heap-Stress in der kritischen Wartephase.
    int httpLen = 0;
    bool http200 = false;
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
      http200 = (strstr(urcBuf, ",200,") != nullptr);

      if (http200) {
        // Länge aus ",200,<len>" extrahieren
        char* lenPtr = strstr(urcBuf, ",200,");
        httpLen = lenPtr ? atoi(lenPtr + 5) : 0;
      } else {
        if (DEBUG) {
          Serial.print(F("HTTP-Fehler (API "));
          Serial.print(api);
          Serial.println(F(")"));
        }
        sendAT("AT+HTTPTERM", 1000);
        delay(500);
        continue;   // → nächster API-Endpunkt
      }
    }

    // Antwortlänge auf Puffergrösse begrenzen
    if (httpLen <= 0 || httpLen > (int)(sizeof(body) - 1)) httpLen = sizeof(body) - 1;

    // Antwort-Body in statischen char-Buffer lesen.
    // Modul sendet: OK\r\n  +HTTPREAD: <len>\r\n  <Daten>  OK\r\n
    {
      delay(30);
      while (lteSerial.available()) lteSerial.read();
      lteSerial.print(F("AT+HTTPREAD=0,"));
      lteSerial.println(httpLen);
      if (DEBUG) { Serial.print(F(">> AT+HTTPREAD=0,")); Serial.println(httpLen); }

      long t0 = millis();
      bool httpReadSeen = false;
      int  httpReadPos  = 0;

      while (millis() - t0 < 30000) {
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

    // HTTP-Stack beenden
    sendAT("AT+HTTPTERM", 1000);

    gotData = (bodyLen >= 10);

    if (gotData && DEBUG) {
      Serial.print(F("Daten empfangen von API "));
      Serial.println(api);
    }
  }
  // ---------------------------------------------------------------
  // Ende der API-Schleife
  // ---------------------------------------------------------------

  // Eigenen Watchdog wieder aktivieren
  secondTick.attach(1, ISRwatchDog);

  if (!gotData) {
    if (DEBUG) Serial.println(F("Alle APIs fehlgeschlagen – Abbruch"));
    tft.setTextColor(ST7735_YELLOW);
    tft.println("Kurs-Fehler");
    tft.setTextColor(ST7735_WHITE);
    return;
  }

  // -------------------------------------------------------
  // JSON parsen mit strstr/atof – kein String-Objekt nötig.
  // fxSym[0]="CHF", fxSym[1]="USD", fxSym[2]="EUR", fxSym[3]="GBP"
  // Beide APIs liefern CHF, USD, GBP relativ zu EUR im Format "KEY":value
  // innerhalb eines "rates":{...} Objekts.
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
