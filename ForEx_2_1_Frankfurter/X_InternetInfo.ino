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

// API: https://api.frankfurter.app/latest?from=EUR&to=CHF,USD,GBP
// Gehostet von Frankfurter (Open Source, Cloudflare CDN).
// Freier Endpunkt, kein API-Key nötig.
// Liefert NUR die angefragten Währungen → sehr kompakte Antwort (~120 Bytes).
// httpLen auf 400 gesetzt (grosszügiger Puffer).
// AT+HTTPPARA="USERDATA" setzt User-Agent + Accept-Header – nötig damit
// Cloudflare den Request nicht als Bot-Traffic blockiert.
// Antwort-Beispiel:
//   {"amount":1.0,"base":"EUR","date":"2026-03-06",
//    "rates":{"CHF":0.956,"GBP":0.857,"USD":1.085}}

void catchCurrencies() {
  const char* httpHost = "api.frankfurter.app";
  const char* httpPath = "/latest?from=EUR&to=CHF,USD,GBP";

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
  String body = "";

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
    // Länge: "AT+HTTPPARA="URL","https://api.frankfurter.app/latest?from=EUR&to=CHF,USD,GBP""
    //        = 80 Zeichen + NUL → passt in 100 Bytes.
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

  // User-Agent + Accept-Header setzen – Cloudflare blockt Requests ohne plausiblen
  // User-Agent als Bot-Traffic. AT+HTTPPARA="USERDATA" hängt beliebige HTTP-Header
  // an (SIM7670-Befehlssatz, \\r\\n als Header-Trenner).
  resp = sendAT("AT+HTTPPARA=\"USERDATA\",\"User-Agent: Mozilla/5.0\\r\\nAccept: application/json\"", 2000);
  if (DEBUG) Serial.println("USERDATA: " + resp);

  // GET-Request senden (0 = GET).
  // Das Modul antwortet zuerst mit "OK" (Befehl akzeptiert), danach asynchron
  // mit "+HTTPACTION: 0,<status>,<len>\r\n". sendATwait bricht bei Teilempfang
  // von "+HTTPACTION:" ab – daher warten wir explizit auf die komplette Zeile.
  resp = sendAT("AT+HTTPACTION=0", 5000);

  // Warten auf vollständige URC-Zeile: +HTTPACTION: 0,200,123\r\n
  // Abbruch erst wenn BEIDE Kommas und ein Newline dahinter vorhanden sind.
  {
    long tWait = millis();
    while (millis() - tWait < 30000) {
      while (lteSerial.available()) resp += (char)lteSerial.read();
      int hIdx = resp.indexOf("+HTTPACTION:");
      if (hIdx != -1) {
        int c1 = resp.indexOf(',', hIdx);
        int c2 = (c1 != -1) ? resp.indexOf(',', c1 + 1) : -1;
        if (c2 != -1 && resp.indexOf('\n', c2) != -1) break; // vollständige Zeile
      }
      delay(200);
      yield();
    }
  }

  if (DEBUG) Serial.println("HTTPACTION Response: " + resp);

  // HTTP-Statuscode prüfen (200 = OK)
  if (resp.indexOf(",200,") == -1) {
    if (DEBUG) {
      Serial.println("HTTP-Fehler: " + resp);
      // Antwort-Body lesen und ausgeben – zeigt was der Server zurückgibt
      delay(30);
      while (lteSerial.available()) lteSerial.read();
      lteSerial.println(F("AT+HTTPREAD=0,200"));
      Serial.println(F(">> AT+HTTPREAD=0,200"));
      // Modul sendet zuerst "OK\r\n" (Befehlsbestätigung), danach erst
      // "+HTTPREAD: <len>\r\n<Daten>OK\r\n". Erst nach dem +HTTPREAD-Header
      // auf das finale OK warten, sonst wird die Bestätigung als Ende erkannt.
      String errBody = "";
      long t0 = millis();
      int errReadAt = -1;
      while (millis() - t0 < 5000) {
        while (lteSerial.available()) errBody += (char)lteSerial.read();
        if (errReadAt < 0) errReadAt = errBody.indexOf("+HTTPREAD:");
        if (errReadAt >= 0 && errBody.indexOf("OK\r", errReadAt) != -1) break;
        if (errBody.indexOf("ERROR") != -1) break;
        yield();
      }
      Serial.println("Fehler-Body: " + errBody);
    }
    tft.setTextColor(ST7735_YELLOW);
    tft.println("HTTP Fehler");
    tft.setTextColor(ST7735_WHITE);
    sendAT("AT+HTTPTERM", 1000);
    secondTick.attach(1, ISRwatchDog);
    return;
  }

  // Datenlänge aus +HTTPACTION: 0,200,<len> extrahieren
  int httpLen = 0;
  int lastComma = resp.lastIndexOf(",");
  if (lastComma != -1) {
    // Suche Ende der Zahl (CR, LF oder Anführungszeichen)
    String lenStr = resp.substring(lastComma + 1);
    lenStr.trim();
    httpLen = lenStr.toInt();
  }
  // frankfurter.app liefert nur die angefragten Währungen (~120 Bytes).
  // Fallback: wenn Server weniger meldet, das Gemeldete lesen; wenn mehr, auf 400 kappen.
  if (httpLen <= 0 || httpLen > 400) httpLen = 400;

  // Antwort-Body lesen – direkter Print, kein String-Objekt (Heap-Fragmentierung)
  {
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.print(F("AT+HTTPREAD=0,"));
    lteSerial.println(httpLen);
    if (DEBUG) { Serial.print(F(">> AT+HTTPREAD=0,")); Serial.println(httpLen); }
    body = "";
    body.reserve(httpLen + 50); // Heap vorab reservieren – verhindert viele Reallocations
    long t0 = millis();
    // Modul sendet zuerst "OK\r\n" (Befehlsbestätigung), dann erst:
    //   +HTTPREAD: <len>\r\n<JSON-Daten>OK\r\n
    // Erst warten bis "+HTTPREAD:" gesehen, dann auf das finale "OK\r" danach.
    int httpReadAt = -1;
    while (millis() - t0 < 12000) {
      while (lteSerial.available()) body += (char)lteSerial.read();
      if (httpReadAt < 0) httpReadAt = body.indexOf("+HTTPREAD:");
      if (httpReadAt >= 0 && body.indexOf("OK\r", httpReadAt) != -1) break;
      if (body.indexOf("ERROR") != -1) break;
      yield();
    }
    if (DEBUG) { Serial.println(F("HTTP Body:")); Serial.println(body); }
  }

  // HTTP-Stack beenden und eigenen Watchdog wieder aktivieren
  sendAT("AT+HTTPTERM", 1000);
  secondTick.attach(1, ISRwatchDog);

  if (body.length() < 10) {
    if (DEBUG) Serial.println("Leere Antwort – Abbruch");
    return;
  }

  // -------------------------------------------------------
  // JSON parsen: "CHF":x.xxxx  "USD":x.xxxx  "GBP":x.xxxx
  // fxSym[0]="CHF", fxSym[1]="USD", fxSym[2]="EUR", fxSym[3]="GBP"
  // API liefert CHF, USD, GBP relativ zu EUR (Basis).
  // EUR selbst ist nicht in der Antwort → wird auf 1.0 gesetzt.
  // -------------------------------------------------------
  for (int i = 0; i < 4; i++) {
    if (fxSym[i] != "EUR") {
      // Suche z.B. "CHF":0.9560 im Body
      int keyPos = body.indexOf("\"" + fxSym[i] + "\":");
      if (keyPos != -1) {
        int valStart = keyPos + fxSym[i].length() + 3; // nach ":"
        // Wert endet bei Komma, } oder "
        int valEnd = body.indexOf(",", valStart);
        int valEnd2 = body.indexOf("}", valStart);
        if (valEnd == -1 || (valEnd2 != -1 && valEnd2 < valEnd)) valEnd = valEnd2;
        if (valEnd == -1) valEnd = valStart + 10;
        String valStr = body.substring(valStart, valEnd);
        valStr.trim();
        fxValue[i] = valStr.toFloat();
      } else {
        fxValue[i] = 0.0;
      }
    } else {
      fxValue[i] = 1.0;   // EUR ist Basis → 1.0
    }

    if (DEBUG) {
      Serial.print(fxSym[i]); Serial.print("/EUR raw: ");
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
