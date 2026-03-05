void clearTFTScreen() {
  tft.fillScreen(ST7735_BLACK);
  tft.setTextColor(ST7735_WHITE);
  tft.setFont();
  tft.setCursor(0, 0);
}

void displayMainScreen() {
  int timeLine = 13;
  int minAbstand = 5;
  int headerToInfo = 22;

  int ersteInfo = 42;
  int zweiteInfo = ersteInfo + headerToInfo + minAbstand;
  int dritteInfo = zweiteInfo + headerToInfo + 2 * minAbstand;

  tft.fillScreen(ST7735_BLACK);

  // Lokale Zeit und Datum anzeigen
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(2, 30);
  sprintf(anzeige, "%02i:%02i ", hour(), minute());
  tft.setTextColor(ST7735_WHITE);
  tft.print(anzeige);

  tft.setFont(&FreeSans9pt7b);
  sprintf(anzeige, "%02i %s ", day(), monthShortStr(month()));
  tft.print(anzeige);

  // Erste Zeile: DBX, SIN, IAD
  tft.setFont();
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(2, ersteInfo);
  tft.println(timezones[1].airCode);
  tft.setCursor(58, ersteInfo);
  tft.println(timezones[2].airCode);
  tft.setCursor(114, ersteInfo);
  tft.println(timezones[3].airCode);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, ersteInfo + headerToInfo);

  // Berechne Zeiten für die ersten drei Städte
  time_t t1 = getTimeForTimezone(1, now());
  time_t t2 = getTimeForTimezone(2, now());
  time_t t3 = getTimeForTimezone(3, now());

  int h1 = hour(t1);
  int m1 = minute(t1);
  int h2 = hour(t2);
  int m2 = minute(t2);
  int h3 = hour(t3);
  int m3 = minute(t3);

  sprintf(anzeige, "%02i:%02i  %02i:%02i  %02i:%02i", h1, m1, h2, m2, h3, m3);
  tft.print(anzeige);

  // Zweite Zeile: SYD, BLR, SFO
  tft.setFont();
  tft.setTextColor(ST7735_YELLOW);
  tft.setCursor(2, zweiteInfo);
  tft.println(timezones[4].airCode);
  tft.setCursor(58, zweiteInfo);
  tft.println(timezones[5].airCode);
  tft.setCursor(114, zweiteInfo);
  tft.println(timezones[6].airCode);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, zweiteInfo + headerToInfo);

  // Berechne Zeiten für die zweiten drei Städte
  time_t t4 = getTimeForTimezone(4, now());
  time_t t5 = getTimeForTimezone(5, now());
  time_t t6 = getTimeForTimezone(6, now());

  h1 = hour(t4);
  m1 = minute(t4);
  h2 = hour(t5);
  m2 = minute(t5);
  h3 = hour(t6);
  m3 = minute(t6);

  sprintf(anzeige, "%02i:%02i  %02i:%02i  %02i:%02i", h1, m1, h2, m2, h3, m3);
  tft.print(anzeige);

  // Dritte Zeile: Wechselkurse
  tft.setFont();
  tft.setTextColor(ST7735_GREEN);
  tft.setCursor(2, dritteInfo);
  tft.println(fxSym[1]);
  tft.setCursor(58, dritteInfo);
  tft.println(fxSym[2]);
  tft.setCursor(114, dritteInfo);
  tft.println(fxSym[3]);

  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(2, dritteInfo + headerToInfo);
  tft.print("  ");
  tft.print(fxValue[1]);
  tft.print("    ");
  tft.print(fxValue[2]);
  tft.print("    ");
  tft.print(fxValue[3]);
}
