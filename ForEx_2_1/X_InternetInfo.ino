// X_InternetInfo.ino – ForEx_2_1
// Wechselkurse per LTE-Modul BK-7670 abrufen.
// Forward-Deklarationen nötig, da Arduino .ino-Dateien alphabetisch
// zusammengeführt werden und X_ vor x_subroutines kommt.
String sendAT(const String& cmd, int timeout = 1000);
String sendATwait(const String& cmd, const String& waitFor, int timeout = 5000);

// Verwendet den eingebauten HTTP-Stack des Moduls (AT+HTTPINIT / HTTPACTION).
// Die AT-Befehlssyntax folgt dem 3GPP-Standard / SIMCom-kompatiblem Befehlssatz.
// Falls der BK-7670 einen anderen HTTP-Befehlssatz verwendet, bitte Datenblatt
// konsultieren und die Befehle unten entsprechend anpassen.

// API: http://api.frankfurter.app/latest?base=EUR&symbols=CHF,USD,GBP
// Antwort-Beispiel:
//   {"amount":1.0,"base":"EUR","date":"2025-03-05",
//    "rates":{"CHF":0.9560,"GBP":0.8575,"USD":1.0853}}

void catchCurrencies() {
  const char* httpHost = "api.frankfurter.app";
  const char* httpPath = "/latest?base=EUR&symbols=CHF,USD,GBP";

  if (DEBUG) {
    Serial.println("=== catchCurrencies ===");
    Serial.print("URL: http://");
    Serial.print(httpHost);
    Serial.println(httpPath);
  }

  String resp;
  String body = "";

  // --- HTTP-Stack initialisieren ---
  // Zuerst evtl. offene Session beenden
  sendAT("AT+HTTPTERM", 1000);
  delay(200);

  resp = sendAT("AT+HTTPINIT", 3000);
  if (resp.indexOf("OK") == -1) {
    if (DEBUG) Serial.println("HTTPINIT fehlgeschlagen: " + resp);
    tft.setTextColor(ST7735_YELLOW);
    tft.println("HTTP Init fehl");
    tft.setTextColor(ST7735_WHITE);
    sendAT("AT+HTTPTERM", 1000);
    return;
  }

  // PDP-Kontext dem HTTP-Stack zuweisen
  sendAT("AT+HTTPPARA=\"CID\",1", 1000);

  // URL setzen (HTTP, kein SSL nötig – öffentliche API)
  String urlCmd = String("AT+HTTPPARA=\"URL\",\"http://")
                  + httpHost + httpPath + "\"";
  resp = sendAT(urlCmd, 2000);
  if (resp.indexOf("OK") == -1) {
    if (DEBUG) Serial.println("URL-Param fehlgeschlagen");
  }

  // Optionaler User-Agent-Header
  sendAT("AT+HTTPPARA=\"USERDATA\",\"User-Agent: ESP8266-ForEx/2.1\\r\\n\"", 1000);

  // GET-Request senden (0 = GET)
  resp = sendATwait("AT+HTTPACTION=0", "+HTTPACTION:", 15000);

  // Auf +HTTPACTION: 0,200,<len> warten (Antwort kommt asynchron)
  long tWait = millis();
  while (millis() - tWait < 15000) {
    while (lteSerial.available()) {
      resp += (char)lteSerial.read();
    }
    if (resp.indexOf("+HTTPACTION:") != -1) break;
    delay(200);
  }

  if (DEBUG) Serial.println("HTTPACTION Response: " + resp);

  // HTTP-Statuscode prüfen (200 = OK)
  if (resp.indexOf(",200,") == -1) {
    if (DEBUG) Serial.println("HTTP-Fehler (kein 200)");
    tft.setTextColor(ST7735_YELLOW);
    tft.println("HTTP Fehler");
    tft.setTextColor(ST7735_WHITE);
    sendAT("AT+HTTPTERM", 1000);
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

  // Antwort-Body lesen: AT+HTTPREAD=<startpos>,<len>
  String readCmd = String("AT+HTTPREAD=0,") + String(httpLen);
  body = sendAT(readCmd, 5000);

  if (DEBUG) {
    Serial.println("HTTP Body:");
    Serial.println(body);
  }

  // HTTP-Stack beenden
  sendAT("AT+HTTPTERM", 1000);

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
