void printHelligkeit() {
  sprintf(anzeige, "%04i",helligkeit);
  tft.println(anzeige);
}

void displayMainScreen() {
    int timeLine = 13; 
  int minAbstand = 5;
  int headerToInfo = 22;

//  int ersteInfo = timeLine + minAbstand;
  int ersteInfo = 42;
  int zweiteInfo = ersteInfo + headerToInfo + minAbstand;
  int dritteInfo = zweiteInfo + headerToInfo + 2*minAbstand;

  tft.fillScreen( ST7735_BLACK );    
  tft.setFont(&FreeSansBold18pt7b);
  tft.setCursor(2,30); // 0/13
//  sprintf(anzeige, "%02i:%02i  %02i %s ", hour(), minute(), day()  , monthShortStr(month()));
  sprintf(anzeige, "%02i:%02i ", hour(), minute());
  tft.setTextColor( ST7735_WHITE );
  tft.print(anzeige);
  tft.setFont(&FreeSans9pt7b);
  sprintf(anzeige, "%02i %s ", day()  , monthShortStr(month()));
  tft.print(anzeige);
  
  tft.setFont();
  tft.setTextColor( ST7735_YELLOW );
  tft.setCursor(2,ersteInfo);
  tft.println(airCode[1]);
  tft.setCursor(58,ersteInfo);
  tft.println(airCode[2]);
  tft.setCursor(114,ersteInfo);
  tft.println(airCode[3]);
  tft.setFont(&FreeSans9pt7b);  
  tft.setCursor(2,ersteInfo+headerToInfo);
//  tft.println("22:44  11:11  18:31");
  int h1 = hour(now()-utcOffset[0]+utcOffset[1]);
  int h2 = hour(now()-utcOffset[0]+utcOffset[2]);
  int h3 = hour(now()-utcOffset[0]+utcOffset[3]);
  int m1 = minute(now()-utcOffset[0]+utcOffset[1]);
  int m2 = minute(now()-utcOffset[0]+utcOffset[2]);
  int m3 = minute(now()-utcOffset[0]+utcOffset[3]);
  sprintf(anzeige,"%02i:%02i  %02i:%02i  %02i:%02i",h1,m1,h2,m2,h3,m3);
  tft.print(anzeige);

  tft.setFont();
  tft.setTextColor( ST7735_YELLOW );
  tft.setCursor(2,zweiteInfo);
  tft.println(airCode[4]);
  tft.setCursor(58,zweiteInfo);
  tft.println(airCode[5]);
  tft.setCursor(114,zweiteInfo);
  tft.println(airCode[6]);
  tft.setFont(&FreeSans9pt7b);  
  tft.setCursor(2,zweiteInfo+headerToInfo);
//  tft.println("11:11  22:44  01:10");  
  h1 = hour(now()-utcOffset[0]+utcOffset[4]);
  h2 = hour(now()-utcOffset[0]+utcOffset[5]);
  h3 = hour(now()-utcOffset[0]+utcOffset[6]);
  m1 = minute(now()-utcOffset[0]+utcOffset[4]);
  m2 = minute(now()-utcOffset[0]+utcOffset[5]);
  m3 = minute(now()-utcOffset[0]+utcOffset[6]);
  sprintf(anzeige,"%02i:%02i  %02i:%02i  %02i:%02i",h1,m1,h2,m2,h3,m3);
  tft.print(anzeige);
  
  tft.setFont();
  tft.setTextColor( ST7735_GREEN );
  tft.setCursor(2,dritteInfo);
  tft.println(fxSym[1]);
  tft.setCursor(58,dritteInfo);
  tft.println(fxSym[2]);
  tft.setCursor(114,dritteInfo);
  tft.println(fxSym[3]);
  tft.setFont(&FreeSans9pt7b);  
  tft.setCursor(2,dritteInfo+headerToInfo);  
  tft.print("  ");
  tft.print(fxValue[1]);
  tft.print("    ");
  tft.print(fxValue[2]);
  tft.print("    ");
  tft.print(fxValue[3]);
}

void printWeb() {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println();
    client.print("<html><head><title>");
    client.print("ForEx");
    client.print("</title><body>");
    client.print("<form>");
    client.print("City, Sign");
    client.print("<br>");
    client.print("<input type=text name=city value=");
    client.print(location[6]);
    client.print(">");
    client.print("   ");
    client.print("<input type=text name=airPort size=3 maxlength=3 value=");
    client.print(airCode[6]);
    client.print(">");
    client.print("<br>");
    client.print("<br>");
    client.print("Currency");
    client.print("<br>");   
    client.print("<input type=text name=curr size=3 maxlength=3 value=");
    client.print(fxSym[3]);
    client.print(">");    
    client.print("<br>");
    client.print("<br>");
    client.print("<input type=submit value=submit>");
    client.print("<br>");
    client.print("</form>");    
    client.println("</body></html>"); 
    client.println();
}
