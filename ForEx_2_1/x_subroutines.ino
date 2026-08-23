// x_subroutines.ino – ForEx_2_1
// LTE-AT-Kommunikation, GSM-Zeit und Timezone/DST-Berechnungen.
// NTP und BMP180 wurden durch GSM-Zeitabfrage (NITZ/AT+CCLK?) ersetzt.

// ============================================================
// AT-BEFEHL SENDEN UND ANTWORT LESEN
// ============================================================

// Sendet einen AT-Befehl und liest die Antwort innerhalb von
// `timeout` Millisekunden. Gibt den kompletten Response-String zurück.
String sendAT(const String& cmd, int timeout = 1000) {
  delay(30);                                           // Warte auf ausstehende Antwort-Bytes
  while (lteSerial.available()) lteSerial.read();     // Puffer leeren
  lteSerial.println(cmd);

  String response = "";
  unsigned long tStart = millis();
  while (millis() - tStart < timeout) {
    while (lteSerial.available()) {
      response += (char)lteSerial.read();
    }
    // Früh beenden wenn OK oder ERROR empfangen
    if (response.indexOf("OK\r") != -1 || response.indexOf("ERROR") != -1) break;
    yield();   // ESP8266 internen Software-Watchdog füttern
  }

  if (DEBUG) {
    Serial.print(">> "); Serial.println(cmd);
    Serial.print("<< "); Serial.println(response);
  }
  return response;
}

// Sendet AT-Befehl und wartet auf ein bestimmtes Schlüsselwort.
String sendATwait(const String& cmd, const String& waitFor, int timeout = 5000) {
  delay(30);
  while (lteSerial.available()) lteSerial.read();
  lteSerial.println(cmd);

  String response = "";
  unsigned long tStart = millis();
  while (millis() - tStart < timeout) {
    while (lteSerial.available()) {
      response += (char)lteSerial.read();
    }
    if (response.indexOf(waitFor) != -1) break;
    yield();   // ESP8266 internen Software-Watchdog füttern
  }

  if (DEBUG) {
    Serial.print(">> "); Serial.println(cmd);
    Serial.print("<< "); Serial.println(response);
  }
  return response;
}

// ============================================================
// Dauerhaft anhalten und Grund anzeigen.
//
// Wird nur bei SIM-PIN-Problemen verwendet. Ein Neustart wäre hier
// gefährlich: bei jedem Boot würde erneut eine PIN gesendet, und nach
// drei Fehlversuchen ist die SIM gesperrt und nur noch mit der PUK zu
// entsperren. Lieber stehenbleiben und den Grund anzeigen.
// ============================================================
void haltWithMessage(const __FlashStringHelper* line1,
                     const __FlashStringHelper* line2) {
  Serial.print(F("[HALT] "));
  Serial.println(line1);
  Serial.println(F("Geraet angehalten - kein automatischer Neustart."));
  Serial.flush();
  clearTFTScreen();
  tft.setTextColor(ST7735_RED);
  tft.setTextSize(1);
  tft.println(line1);
  tft.println(line2);
  tft.setTextColor(ST7735_WHITE);
  tft.println();
  tft.println("Geraet angehalten.");
  tft.println("Kein Auto-Neustart -");
  tft.println("sonst wird ein weiterer");
  tft.println("PIN-Versuch verbraucht.");
  while (true) {
    ESP.wdtFeed();
    delay(1000);
    yield();
  }
}

// ============================================================
// BK-7670 MODUL INITIALISIEREN
// ============================================================
bool initLTE() {
  if (DEBUG) Serial.println("=== LTE Init ===");

  // Warten bis Modul hochgefahren ist
  delay(3000);

  // Basis-Kommunikationstest – bis zu ~30 s geduldig.
  // Ein Mobilfunkmodul braucht nach dem Einschalten deutlich länger als der
  // ESP8266, bis es auf AT antwortet. Früher gab es nur zwei Versuche über
  // ~6 s. Beim KALTSTART (Netzteil einstecken) reicht das nicht; beim
  // Warmstart (nur ESP-Reset über DTR) fällt es nicht auf, weil das Modul
  // durchläuft und längst bereit ist.
  String resp;
  bool modemUp = false;
  for (int i = 0; i < 15; i++) {
    resp = sendAT("AT", 1000);
    if (resp.indexOf("OK") != -1) { modemUp = true; break; }
    ESP.wdtFeed();
    delay(1000);
    if ((i % 3) == 0) tft.print(".");
  }
  if (!modemUp) {
    Serial.println(F("[FEHLER] Kein Kontakt zum LTE-Modul (AT ohne Antwort)"));
    tft.setTextColor(ST7735_RED);
    tft.println("Kein Modulkontakt!");
    tft.setTextColor(ST7735_WHITE);
    return false;
  }
  Serial.println(F("[OK] LTE-Modul antwortet"));

  sendAT("ATE0", 500);          // Echo ausschalten
  sendAT("AT+CMEE=2", 500);     // Detaillierte Fehlermeldungen

  // Auf SIM-Bereitschaft warten (bis ~25 s) statt einmalig abzufragen.
  // Direkt nach dem Einschalten meldet das Modul je nach Zustand
  // "+CPIN: NOT READY" oder "+CME ERROR: SIM not inserted", obwohl die
  // Karte wenige Sekunden später problemlos lesbar ist.
  tft.print("SIM");
  bool simReady   = false;
  bool needPin    = false;
  bool simBlocked = false;
  for (int i = 0; i < 20; i++) {
    resp = sendAT("AT+CPIN?", 2000);
    if (resp.indexOf("READY")   != -1) { simReady   = true; break; }
    if (resp.indexOf("SIM PUK") != -1) { simBlocked = true; break; }
    if (resp.indexOf("SIM PIN") != -1) { needPin    = true; break; }
    ESP.wdtFeed();
    delay(1000);
    if ((i % 4) == 0) tft.print(".");
  }
  tft.println();

  // SIM bereits gesperrt – hier hilft nur noch die PUK, von Hand.
  if (simBlocked) {
    haltWithMessage(F("SIM GESPERRT (PUK)"),
                    F("Karte im Handy mit PUK entsperren."));
  }

  // PIN eingeben, falls die SIM danach verlangt.
  // Nicht mehr an ACTIVE_PROVIDER gekoppelt – die PIN gehört zur SIM,
  // nicht zum Mobilfunkanbieter.
  if (needPin) {
    if (strlen(GPSII_PIN) == 0) {
      haltWithMessage(F("SIM braucht PIN!"),
                      F("GPSII_PIN in Credentials.h setzen."));
    }
    tft.println("SIM PIN...");
    Serial.println(F("SIM verlangt PIN - wird gesendet"));
    // PIN NICHT über sendAT() senden, sonst steht sie im Klartext im Log
    delay(30);
    while (lteSerial.available()) lteSerial.read();
    lteSerial.print(F("AT+CPIN=\""));
    lteSerial.print(GPSII_PIN);
    lteSerial.println("\"");
    String pinResp;
    pinResp.reserve(64);
    unsigned long tPin = millis();
    while (millis() - tPin < 5000) {
      while (lteSerial.available()) pinResp += (char)lteSerial.read();
      if (pinResp.indexOf("OK") != -1 || pinResp.indexOf("ERROR") != -1) break;
      ESP.wdtFeed();
      yield();
    }
    if (pinResp.indexOf("OK") == -1) {
      // NICHT neu starten! Jeder Neustart würde einen weiteren der drei
      // Versuche verbrauchen und die SIM danach dauerhaft sperren.
      haltWithMessage(F("PIN ABGELEHNT!"),
                      F("PIN pruefen - nur 3 Versuche!"));
    }
    // Nach dem Entsperren erneut auf READY warten
    for (int i = 0; i < 15; i++) {
      delay(1000);
      resp = sendAT("AT+CPIN?", 2000);
      if (resp.indexOf("READY") != -1) { simReady = true; break; }
      ESP.wdtFeed();
    }
    if (simReady) Serial.println(F("[OK] SIM mit PIN entsperrt"));
  }

  if (!simReady) {
    // Grund auch aufs Display bringen – der serielle Port hängt im
    // Normalbetrieb nicht am Rechner.
    resp.replace("\r", " ");
    resp.replace("\n", " ");
    resp.trim();
    Serial.print(F("[FEHLER] SIM nicht bereit. Letzte CPIN-Antwort: "));
    Serial.println(resp);
    tft.setTextColor(ST7735_RED);
    tft.println("SIM nicht bereit!");
    tft.setTextColor(ST7735_WHITE);
    tft.println(resp.substring(0, 60));
    delay(5000);
    return false;
  }
  Serial.println(F("[OK] SIM bereit"));

  // Auf Netzwerk-Registrierung warten (max. 60 Sekunden)
  if (DEBUG) Serial.println("Warte auf Netz-Registrierung...");
  tft.println("Warte auf Netz...");
  bool registered = false;
  for (int i = 0; i < 60; i++) {
    resp = sendAT("AT+CREG?", 1000);
    // +CREG: 0,1 = im Heimnetz registriert
    // +CREG: 0,5 = im Roaming registriert
    if (resp.indexOf(",1") != -1 || resp.indexOf(",5") != -1) {
      registered = true;
      tft.println("Netz OK.");
      Serial.printf("[OK] Netz-Registrierung nach %d s\n", i);
      break;
    }
    delay(1000);
    if ((i % 10) == 0) tft.print(".");
  }
  if (!registered) {
    Serial.println(F("[FEHLER] Keine Netz-Registrierung nach 60 s"));
    tft.setTextColor(ST7735_RED);
    tft.println("KEIN NETZ!");
    tft.setTextColor(ST7735_WHITE);
    return false;
  }

  // NITZ aktivieren: Modul übernimmt Zeit automatisch vom Operator
  // AT+CTZU=1 → automatische Zeitzone aus dem Netz
  // Kostet kein Datenvolumen (Teil des GSM-Signalisierung)
  sendAT("AT+CTZU=1", 1000);

  // Kurz warten damit NITZ-Zeit übernommen wird
  delay(2000);

  // APN konfigurieren (für Datenverbindung / Wechselkurse)
  String apnCmd = String("AT+CGDCONT=1,\"IP\",\"") + LTE_APN + "\"";
  resp = sendAT(apnCmd, 2000);
  if (resp.indexOf("OK") == -1) {
    if (DEBUG) Serial.println("Warnung: CGDCONT nicht OK");
  }

  // PDP-Kontext aktivieren
  resp = sendAT("AT+CGACT=1,1", 10000);
  if (resp.indexOf("OK") == -1) {
    Serial.print(F("[WARN] PDP-Aktivierung nicht OK: ")); Serial.println(resp);
  }

  // Operator-Info anzeigen
  resp = sendAT("AT+COPS?", 2000);
  resp.replace("\r", " "); resp.replace("\n", " ");
  Serial.print(F("Operator: ")); Serial.println(resp);

  if (DEBUG) Serial.println("=== LTE Init OK ===");
  return true;
}

// ============================================================
// ZEIT VOM GSM-NETZ (NITZ) LESEN
// Kostet kein Datenvolumen! Teil der GSM-Signalisierung.
//
// AT+CCLK? Antwort-Format (3GPP TS 27.007):
//   +CCLK: "YY/MM/DD,HH:MM:SS±ZZ"
//   ZZ = Zeitzone in Viertelstunden (z.B. +04 = UTC+1:00)
//
// Rückgabe: Lokalzeit Zürich (timezone[0]) als time_t
//           0 bei Fehler
// ============================================================
time_t getGsmTime() {
  delay(500);   // kurze Pause nach Netzregistrierung

  String resp = sendAT("AT+CCLK?", 3000);

  // Suche "+CCLK: \"" oder "+CCLK:\""
  int idx = resp.indexOf("+CCLK: \"");
  if (idx != -1) {
    idx += 8;
  } else {
    idx = resp.indexOf("+CCLK:\"");
    if (idx != -1) idx += 7;
    else {
      if (DEBUG) Serial.println("CCLK: kein Treffer in: " + resp);
      return 0;
    }
  }

  // Mindestlänge prüfen: "YY/MM/DD,HH:MM:SS" = 17 Zeichen
  if ((int)resp.length() < idx + 17) {
    if (DEBUG) Serial.println("CCLK: Antwort zu kurz");
    return 0;
  }

  // Datum und Zeit parsen
  int yy = resp.substring(idx,      idx + 2).toInt();
  int mo = resp.substring(idx +  3, idx + 5).toInt();
  int dd = resp.substring(idx +  6, idx + 8).toInt();
  int hh = resp.substring(idx +  9, idx + 11).toInt();
  int mi = resp.substring(idx + 12, idx + 14).toInt();
  int ss = resp.substring(idx + 15, idx + 17).toInt();

  // Zeitzone: ±ZZ (Viertelstunden)
  int  tzOffsetSec = 0;
  char tzSign = resp.charAt(idx + 17);
  if (tzSign == '+' || tzSign == '-') {
    int tzQH = resp.substring(idx + 18, idx + 20).toInt();
    tzOffsetSec = tzQH * 15 * 60;          // Viertelstunden → Sekunden
    if (tzSign == '-') tzOffsetSec = -tzOffsetSec;
  }

  int yyyy = 2000 + yy;

  if (DEBUG) {
    Serial.printf("GSM Zeit: %04d-%02d-%02d %02d:%02d:%02d  TZ: %+.2f h\n",
                  yyyy, mo, dd, hh, mi, ss, tzOffsetSec / 3600.0);
  }

  // Zeit-Struktur aufbauen (Lokalzeit des Operators)
  tmElements_t tm;
  tm.Year   = yyyy - 1970;
  tm.Month  = mo;
  tm.Day    = dd;
  tm.Hour   = hh;
  tm.Minute = mi;
  tm.Second = ss;

  // Operator-Lokalzeit → UTC → Zürich-Lokalzeit
  time_t gsmLocalTime = makeTime(tm);
  time_t utcTime      = gsmLocalTime - tzOffsetSec;

  // ZRH-Offset aus Datenbank (inkl. DST-Berechnung)
  int zrhOffset    = getTimezoneOffset(0, utcTime);
  time_t zrhTime   = utcTime + zrhOffset;

  if (DEBUG) {
    Serial.printf("UTC: %lu  ZRH-Offset: %+d s  ZRH-Zeit: %lu\n",
                  (unsigned long)utcTime, zrhOffset, (unsigned long)zrhTime);
  }

  return zrhTime;
}

// ============================================================
// AIRPORT-DATENBANK
// ============================================================

boolean lookupAirportTimezone(const char* code, TimezoneInfo &tz) {
  for (int i = 0; i < AIRPORT_DATABASE_SIZE; i++) {
    AirportTimezone apt;
    memcpy_P(&apt, &AIRPORT_DATABASE[i], sizeof(AirportTimezone));
    if (strcmp(code, apt.code) == 0) {
      tz.airCode    = String(code);
      tz.stdOffset  = apt.stdOffset;
      tz.dstOffset  = apt.dstOffset;
      tz.dstType    = apt.dstType;
      return true;
    }
  }
  return false;
}

void initializeTimezones() {
  if (DEBUG) Serial.println("Initialisiere Zeitzonen...");

  for (int i = 0; i < 7; i++) {
    if (lookupAirportTimezone(AIRPORT_CODES[i], timezones[i])) {
      if (DEBUG) {
        Serial.printf("  %s  UTC%+.1f / UTC%+.1f  (DST Typ %d)\n",
                      AIRPORT_CODES[i],
                      timezones[i].stdOffset / 3600.0,
                      timezones[i].dstOffset / 3600.0,
                      timezones[i].dstType);
      }
      if (firstRun) {
        tft.print(AIRPORT_CODES[i]);
        tft.print(": UTC");
        if (timezones[i].stdOffset >= 0) tft.print("+");
        tft.println(timezones[i].stdOffset / 3600.0);
      }
    } else {
      if (DEBUG) {
        Serial.print("FEHLER: Airport-Code nicht gefunden: ");
        Serial.println(AIRPORT_CODES[i]);
      }
      if (firstRun) {
        tft.setTextColor(ST7735_RED);
        tft.print("ERR: ");
        tft.print(AIRPORT_CODES[i]);
        tft.println(" unbekannt");
        tft.setTextColor(ST7735_WHITE);
      }
      // Fallback auf UTC
      timezones[i].airCode   = String(AIRPORT_CODES[i]);
      timezones[i].stdOffset = 0;
      timezones[i].dstOffset = 0;
      timezones[i].dstType   = 0;
    }
  }

  if (DEBUG) Serial.println("Zeitzonen initialisiert.");
}

// ============================================================
// DST-BERECHNUNGEN (unverändert aus v1.1)
// ============================================================

boolean isDstEU(time_t t) {
  int y = year(t), m = month(t), d = day(t), h = hour(t);
  int marchLastSun   = 31 - ((5 * y / 4 + 4) % 7);
  int octoberLastSun = 31 - ((5 * y / 4 + 1) % 7);

  if (m < 3 || m > 10) return false;
  if (m > 3 && m < 10) return true;
  if (m == 3) {
    if (d < marchLastSun)  return false;
    if (d > marchLastSun)  return true;
    return (h >= 2);
  }
  if (m == 10) {
    if (d < octoberLastSun) return true;
    if (d > octoberLastSun) return false;
    return (h < 3);
  }
  return false;
}

boolean isDstUS(time_t t) {
  int y = year(t), m = month(t), d = day(t);
  if (m < 3 || m > 11) return false;
  if (m > 3 && m < 11) return true;
  int marchSecondSun    = (14 - (5 * y / 4 + 1) % 7) + 7;
  int novemberFirstSun  = (7  - (5 * y / 4 + 1) % 7);
  if (novemberFirstSun == 0) novemberFirstSun = 7;
  if (m == 3)  return (d >= marchSecondSun);
  if (m == 11) return (d <  novemberFirstSun);
  return false;
}

boolean isDstAU(time_t t) {
  int y = year(t), m = month(t), d = day(t);
  if (m < 4 || m > 9)  return true;
  if (m > 4 && m < 10) return false;
  int octoberFirstSun = (7 - (5 * y / 4 + 1) % 7);
  if (octoberFirstSun == 0) octoberFirstSun = 7;
  int aprilFirstSun   = (7 - (5 * y / 4 + 4) % 7);
  if (aprilFirstSun == 0) aprilFirstSun = 7;
  if (m == 10) return (d >= octoberFirstSun);
  if (m ==  4) return (d <  aprilFirstSun);
  return false;
}

boolean isDstNZ(time_t t) {
  int y = year(t), m = month(t), d = day(t);
  if (m < 4 || m > 9)  return true;
  if (m > 4 && m < 9)  return false;
  int septLastSun   = 30 - ((5 * y / 4 + 2) % 7);
  int aprilFirstSun = (7  - (5 * y / 4 + 4) % 7);
  if (aprilFirstSun == 0) aprilFirstSun = 7;
  if (m == 9) return (d >= septLastSun);
  if (m == 4) return (d <  aprilFirstSun);
  return false;
}

int getTimezoneOffset(int tzIndex, time_t t) {
  TimezoneInfo tz = timezones[tzIndex];
  if (tz.dstType == 0) return tz.stdOffset;

  boolean isDst = false;
  switch (tz.dstType) {
    case 1: isDst = isDstEU(t); break;
    case 2: isDst = isDstUS(t); break;
    case 3: isDst = isDstAU(t); break;
    case 4: isDst = isDstNZ(t); break;
  }
  return isDst ? tz.dstOffset : tz.stdOffset;
}

time_t getTimeForTimezone(int tzIndex, time_t localTime) {
  int    localOffset = getTimezoneOffset(0, localTime);
  time_t utcTime     = localTime - localOffset;
  int    targetOffset = getTimezoneOffset(tzIndex, utcTime);
  return utcTime + targetOffset;
}
