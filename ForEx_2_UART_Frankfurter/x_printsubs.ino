// x_printsubs.ino – ForEx_2_1
// Display-Funktionen

void clearTFTScreen() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE);
  tft.setFont();
  tft.setCursor(0, 0);
}

void displayMainScreen() {
  int headerToInfo  = 22;
  int minAbstand    = 5;

  int ersteInfo  = 42;
  int zweiteInfo = ersteInfo + headerToInfo + minAbstand;
  int dritteInfo = zweiteInfo + headerToInfo + 2 * minAbstand;

  // Nur dynamische Bereiche löschen statt ganzen Bildschirm (weniger Flackern)
  // Zeile 0: Zeit + Datum (y=0..38)
  tft.fillRect(0, 0, 160, 39, ST7735_BLACK);
  // Zeile 1: Zeitzonen-Zeiten (y=ersteInfo..zweiteInfo-1)
  tft.fillRect(0, ersteInfo, 160, headerToInfo + minAbstand, ST7735_BLACK);
  // Zeile 2: Zeitzonen-Zeiten (y=zweiteInfo..dritteInfo-1)
  tft.fillRect(0, zweiteInfo, 160, headerToInfo + 2 * minAbstand, ST7735_BLACK);
  // Zeile 3: Wechselkurse (y=dritteInfo..Ende)
  tft.fillRect(0, dritteInfo, 160, 128 - dritteInfo, ST7735_BLACK);

  // Lokale Zeit und Datum (groß, fett)
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(2, 30);
  snprintf(anzeige, sizeof(anzeige), "%02i:%02i ", hour(), minute());
  tft.setTextColor(ST7735_WHITE);
  tft.print(anzeige);

  tft.setFont(&FreeSans9pt7b);
  snprintf(anzeige, sizeof(anzeige), "%02i %s ", day(), monthShortStr(month()));
  tft.print(anzeige);

  // Zeile 1: DXB, SIN, IAD
  tft.setFont();
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(2,   ersteInfo); tft.println(timezones[1].airCode);
  tft.setCursor(58,  ersteInfo); tft.println(timezones[2].airCode);
  tft.setCursor(114, ersteInfo); tft.println(timezones[3].airCode);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, ersteInfo + headerToInfo);

  time_t t1 = getTimeForTimezone(1, now());
  time_t t2 = getTimeForTimezone(2, now());
  time_t t3 = getTimeForTimezone(3, now());
  snprintf(anzeige, sizeof(anzeige), "%02i:%02i  %02i:%02i  %02i:%02i",
          hour(t1), minute(t1), hour(t2), minute(t2), hour(t3), minute(t3));
  tft.print(anzeige);

  // Zeile 2: SYD, BLR, SFO
  tft.setFont();
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(2,   zweiteInfo); tft.println(timezones[4].airCode);
  tft.setCursor(58,  zweiteInfo); tft.println(timezones[5].airCode);
  tft.setCursor(114, zweiteInfo); tft.println(timezones[6].airCode);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, zweiteInfo + headerToInfo);

  time_t t4 = getTimeForTimezone(4, now());
  time_t t5 = getTimeForTimezone(5, now());
  time_t t6 = getTimeForTimezone(6, now());
  snprintf(anzeige, sizeof(anzeige), "%02i:%02i  %02i:%02i  %02i:%02i",
          hour(t4), minute(t4), hour(t5), minute(t5), hour(t6), minute(t6));
  tft.print(anzeige);

  // Zeile 3: Wechselkurse
  tft.setFont();
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(2,   dritteInfo); tft.println(fxSym[1]);
  tft.setCursor(58,  dritteInfo); tft.println(fxSym[2]);
  tft.setCursor(114, dritteInfo); tft.println(fxSym[3]);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, dritteInfo + headerToInfo);
  tft.print("  ");   tft.print(fxValue[1]);
  tft.print("    "); tft.print(fxValue[2]);
  tft.print("    "); tft.print(fxValue[3]);
}
