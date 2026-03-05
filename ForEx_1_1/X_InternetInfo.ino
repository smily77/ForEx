// Vereinfachte Internet-Funktionen - nur noch für Wechselkurse
// Keine Wetter-API und Timezone-API mehr nötig

void catchCurrencies() {
  const char* host = "api.frankfurter.app";
  int httpsPort = 443;
  String result;

  if (DEBUG) Serial.println(fxSym[0]);
  if (DEBUG) Serial.println(fxSym[1]);
  if (DEBUG) Serial.println(fxSym[2]);
  if (DEBUG) Serial.println(fxSym[3]);

  if (DEBUG) Serial.print("connecting to ");
  if (DEBUG) Serial.println(host);

  clientSec.setInsecure();
  if (!clientSec.connect(host, httpsPort)) {
    if (DEBUG) Serial.println("connection failed");
    return;
  }

  String url = "/latest";
  if (DEBUG) Serial.print("requesting URL: ");
  if (DEBUG) Serial.println(url);

  clientSec.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: ESP8266Widget\r\n" +
               "Connection: close\r\n\r\n");

  if (DEBUG) {
    Serial.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "User-Agent: ESP8266Widget\r\n" +
                 "Connection: close\r\n\r\n");
  }

  if (DEBUG) Serial.println("request sent");

  // Warte auf Header
  while (clientSec.connected()) {
    String line = clientSec.readStringUntil('\n');
    if (line == "\r") {
      if (DEBUG) Serial.println("headers received");
      break;
    }
  }

  // Lese Response Body
  String line = clientSec.readString();
  if (DEBUG) Serial.println("reply was:");
  if (DEBUG) Serial.println("==========");
  if (DEBUG) Serial.println(line);
  if (DEBUG) Serial.println("==========");
  if (DEBUG) Serial.println("closing connection");

  // Parse Wechselkurse aus JSON
  for (int i = 0; i < 4; i++) {
    if (fxSym[i].compareTo("EUR")) {
      result = line.substring(line.indexOf(fxSym[i]) + 5,
                              line.indexOf(",", line.indexOf(fxSym[i])));
      fxValue[i] = result.toFloat();
    }
    else {
      fxValue[i] = 1.00;
    }
    if (DEBUG) Serial.print(fxSym[i]);
    if (DEBUG) Serial.print(":   ");
    if (DEBUG) Serial.println(fxValue[i]);
  }

  // Berechne CHF zu anderen Währungen
  for (int l = 1; l < 4; l++) {
    fxValue[l] = fxValue[0] / fxValue[l];
    if (firstRun) {
      tft.print(fxSym[l]);
      tft.print(": ");
      tft.println(fxValue[l]);
    }
  }
}
