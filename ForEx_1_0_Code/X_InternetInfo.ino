void catchTimes() {
  
  utcOffset[0] = getUTCbasedonLocation(location[0]);
  if (firstRun) {
    tft.print("To UTC @ Origin: ");
    tft.println(utcOffset[0]/3600);
  }
  for (int i=1; i <7 ; i++) {
    utcOffset[i] = getUTCbasedonLocation(location[i]);
    if (firstRun) {
      tft.print(airCode[i]);
      tft.print(": ");
      tft.println(utcOffset[i]/3600);
    }
  }
}

long getUTCbasedonLocation(String place) {

  while (!catchWeatherInfo(place, temp, humidity, icon, iconSub, lat, lon));
  while (!catchTZInfo(lon, lat, utc));

  return int(utc*3600);
}

boolean catchWeatherInfo(String city, byte &temp, byte &humidity, byte &icon, char &iconSub, float &lat, float &lon) {
  boolean ok = false;
  
  if (!client.connect("api.openweathermap.org", 80)) {
    return false;
  }

  String url ="/data/2.5/weather?q=" + city + "&units=metric&APPID=" + OPWMapi;

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + "api.openweathermap.org" + "\r\n" + 
               "Connection: close\r\n\r\n");
  
  if (client.connected()) {
    while(!client.available());
    ok = true;
    // lon
    finder.find("coord");
    finder.find("lon");
    byte l = client.read(); //"
    l = client.read(); //:
    lon = finder.getFloat();
    // lat
    finder.find("lat");
    l = client.read(); //"
    l = client.read(); //:
    lat = finder.getFloat();    
    // icon
    finder.find("icon");
    l = client.read(); //"
    l = client.read(); //:
    l = client.read(); //"
    icon = 0;
    icon = int(client.read())-48;
    icon = 10 * icon + (int(client.read())-48);
    iconSub = client.read();    
    // temperature
    finder.find("temp");
    l = client.read(); //"
    l = client.read(); //:
    float temp1 = finder.getFloat();
    temp = (int)temp1;
    if ((temp1 - temp) > 0.5) temp++;
    // humidity
    finder.find("humidity");
    l = client.read(); //"
    l = client.read(); //:
    humidity = finder.getValue();
    
    while(client.available()){
      int c = int(client.read());
    }
  }
  client.stop();
  client.flush();
  return ok;
}

boolean catchTZInfo(float lon, float lat, float &utc) {
  boolean ok = false;
  
  if (!client.connect("api.worldweatheronline.com", 80)) {
    return false;
  }
  
  String url = "/free/v2/tz.ashx?key="; 
  url += TZapi;
  url += "&q=";
  url += lat;
  url += ",";
  url += lon;
  url += "&format=xml";

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + "api.worldweatheronline.com" + "\r\n" + 
               "Connection: close\r\n\r\n");
  
   if (client.connected()) {
    ok = true;
    finder.find("<utcOffset>");
    utc = finder.getFloat();
    while(client.available()){
      int c = int(client.read());
    }
  }
  client.stop();
  client.flush();
  return ok;
}

float catchCurrency(String baseCurrency, String currency) {
 // http://api.fixer.io/latest?base=USD;symbols=CHF
  float wert = 0;
  if (!client.connect("api.fixer.io", 80)) {
    return wert;
  }
  
  String url = "/latest?base="; 
  url += currency;
  url += ";symbols=";
  url += baseCurrency;

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + "api.fixer.io" + "\r\n" + 
               "Connection: close\r\n\r\n");

  delay(100);
   if (client.connected()) {
    finder.find("CHF");
    wert = finder.getFloat();
    while(client.available()){
      int c = int(client.read());
    }
  }
   client.stop();
  client.flush();
  return wert;
}

void catchCurrencies() {
  
  for (int l = 1; l < 4 ; l++) {
    do {
      fxValue[l] = catchCurrency(fxSym[0], fxSym[l]);
    } while (fxValue[l] ==0);
    if (firstRun) {
      tft.print(fxSym[l]);
      tft.print(": ");
      tft.println(fxValue[l]);
    }
  }
}
    
  
