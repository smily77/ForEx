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

// API: https://api.frankfurter.app/latest?base=EUR&symbols=CHF,USD,GBP
// Antwort-Beispiel:
//   {"amount":1.0,"base":"EUR","date":"2025-03-05",
//    "rates":{"CHF":0.9560,"GBP":0.8575,"USD":1.0853}}

void catchCurrencies() {
  const char* httpHost = "api.frankfurter.app";
  const char* httpPath = "/latest?base=EUR&symbols=CHF,USD,GBP";

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

  // URL setzen – direkt in Teilen senden, kein String-Objekt (vermeidet Heap-Fragmentierung).
  // Moderne LTE-Module erkennen https:// automatisch; AT+HTTPSSL=1 wird nicht benötigt.
  {
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.print(F("AT+HTTPPARA=\"URL\",\"https://"));
    lteSerial.print(httpHost);
    lteSerial.print(httpPath);
    lteSerial.println(F("\""));

    if (DEBUG) {
      Serial.print(F(">> AT+HTTPPARA=\"URL\",\"https://"));
      Serial.print(httpHost);
      Serial.print(httpPath);
      Serial.println(F("\""));
    }

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

  // User-Agent setzen – viele Server (inkl. Cloudflare) lehnen Anfragen
  // ohne User-Agent mit 403 ab. Direkter Print vermeidet String-Heap.
  {
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.println(F("AT+HTTPPARA=\"USERDATA\",\"User-Agent: ForEx/2.1\\r\\n\""));
    if (DEBUG) Serial.println(F(">> AT+HTTPPARA=\"USERDATA\",\"User-Agent: ForEx/2.1\\r\\n\""));
    String uaResp = "";
    long t0 = millis();
    while (millis() - t0 < 1000) {
      while (lteSerial.available()) uaResp += (char)lteSerial.read();
      if (uaResp.indexOf("OK") != -1 || uaResp.indexOf("ERROR") != -1) break;
      yield();
    }
    if (DEBUG) { Serial.print(F("<< ")); Serial.println(uaResp); }
  }

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
      String errBody = "";
      long t0 = millis();
      while (millis() - t0 < 5000) {
        while (lteSerial.available()) errBody += (char)lteSerial.read();
        if (errBody.indexOf("OK\r") != -1 || errBody.indexOf("ERROR") != -1) break;
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
  if (httpLen <= 0 || httpLen > 1024) httpLen = 512; // Sicherheits-Fallback

  // Antwort-Body lesen – direkter Print, kein String-Objekt (Heap-Fragmentierung)
  {
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.print(F("AT+HTTPREAD=0,"));
    lteSerial.println(httpLen);
    if (DEBUG) { Serial.print(F(">> AT+HTTPREAD=0,")); Serial.println(httpLen); }
    body = "";
    long t0 = millis();
    while (millis() - t0 < 8000) {
      while (lteSerial.available()) body += (char)lteSerial.read();
      if (body.indexOf("OK\r") != -1 || body.indexOf("ERROR") != -1) break;
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
