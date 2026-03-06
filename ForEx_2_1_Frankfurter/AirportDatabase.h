// Airport Database mit Timezone-Informationen
// Gespeichert im PROGMEM um RAM zu sparen

#ifndef AIRPORT_DATABASE_H
#define AIRPORT_DATABASE_H

// DST Typen:
// 0 = Kein DST
// 1 = EU DST (letzter Sonntag März bis letzter Sonntag Oktober)
// 2 = US DST (zweiter Sonntag März bis erster Sonntag November)
// 3 = AU DST (erster Sonntag Oktober bis erster Sonntag April)
// 4 = NZ DST (letzter Sonntag September bis erster Sonntag April)

struct AirportTimezone {
  char code[4];            // 3-Buchstaben IATA Code
  int stdOffset;           // Standard UTC offset in Sekunden
  int dstOffset;           // DST UTC offset in Sekunden
  byte dstType;            // DST Typ (siehe oben)
};

// Große Flughäfen weltweit mit Timezone-Daten
const AirportTimezone AIRPORT_DATABASE[] PROGMEM = {
  // Europa
  {"ZRH", 3600, 7200, 1},      // Zürich, Schweiz
  {"GVA", 3600, 7200, 1},      // Genf, Schweiz
  {"LHR", 0, 3600, 1},         // London Heathrow, UK
  {"CDG", 3600, 7200, 1},      // Paris Charles de Gaulle, Frankreich
  {"FRA", 3600, 7200, 1},      // Frankfurt, Deutschland
  {"AMS", 3600, 7200, 1},      // Amsterdam, Niederlande
  {"MAD", 3600, 7200, 1},      // Madrid, Spanien
  {"BCN", 3600, 7200, 1},      // Barcelona, Spanien
  {"FCO", 3600, 7200, 1},      // Rom, Italien
  {"MXP", 3600, 7200, 1},      // Mailand, Italien
  {"VIE", 3600, 7200, 1},      // Wien, Österreich
  {"ZRH", 3600, 7200, 1},      // Zürich, Schweiz
  {"CPH", 3600, 7200, 1},      // Kopenhagen, Dänemark
  {"OSL", 3600, 7200, 1},      // Oslo, Norwegen
  {"ARN", 3600, 7200, 1},      // Stockholm, Schweden
  {"HEL", 7200, 10800, 1},     // Helsinki, Finnland
  {"IST", 10800, 10800, 0},    // Istanbul, Türkei (kein DST mehr seit 2016)
  {"ATH", 7200, 10800, 1},     // Athen, Griechenland
  {"PRG", 3600, 7200, 1},      // Prag, Tschechien
  {"WAW", 3600, 7200, 1},      // Warschau, Polen

  // Naher Osten
  {"DXB", 14400, 14400, 0},    // Dubai, VAE
  {"DBX", 14400, 14400, 0},    // Dubai, VAE (alternativer Code)
  {"DOH", 10800, 10800, 0},    // Doha, Qatar
  {"AUH", 14400, 14400, 0},    // Abu Dhabi, VAE
  {"CAI", 7200, 7200, 0},      // Kairo, Ägypten (DST ausgesetzt)
  {"TLV", 7200, 10800, 1},     // Tel Aviv, Israel (eigene DST-Regeln, hier EU angenähert)
  {"RUH", 10800, 10800, 0},    // Riad, Saudi-Arabien
  {"JED", 10800, 10800, 0},    // Jeddah, Saudi-Arabien

  // Asien-Pazifik
  {"SIN", 28800, 28800, 0},    // Singapore
  {"HKG", 28800, 28800, 0},    // Hong Kong
  {"BKK", 25200, 25200, 0},    // Bangkok, Thailand
  {"ICN", 32400, 32400, 0},    // Seoul, Südkorea
  {"NRT", 32400, 32400, 0},    // Tokyo Narita, Japan
  {"HND", 32400, 32400, 0},    // Tokyo Haneda, Japan
  {"PEK", 28800, 28800, 0},    // Beijing, China
  {"PVG", 28800, 28800, 0},    // Shanghai, China
  {"DEL", 19800, 19800, 0},    // Delhi, Indien
  {"BOM", 19800, 19800, 0},    // Mumbai, Indien
  {"BLR", 19800, 19800, 0},    // Bangalore, Indien
  {"KUL", 28800, 28800, 0},    // Kuala Lumpur, Malaysia
  {"CGK", 25200, 25200, 0},    // Jakarta, Indonesien
  {"MNL", 28800, 28800, 0},    // Manila, Philippinen
  {"TPE", 28800, 28800, 0},    // Taipei, Taiwan

  // Australien & Neuseeland
  {"SYD", 36000, 39600, 3},    // Sydney, Australien
  {"MEL", 36000, 39600, 3},    // Melbourne, Australien
  {"BNE", 36000, 36000, 0},    // Brisbane, Australien (Queensland hat kein DST)
  {"PER", 28800, 28800, 0},    // Perth, Australien (Western Australia hat kein DST)
  {"AKL", 43200, 46800, 4},    // Auckland, Neuseeland
  {"CHC", 43200, 46800, 4},    // Christchurch, Neuseeland
  {"ADL", 34200, 37800, 3},    // Adelaide, Australien (UTC+9:30/+10:30)

  // Nord- & Mittelamerika
  {"JFK", -18000, -14400, 2},  // New York JFK, USA
  {"EWR", -18000, -14400, 2},  // Newark, USA
  {"LGA", -18000, -14400, 2},  // New York LaGuardia, USA
  {"IAD", -18000, -14400, 2},  // Washington Dulles, USA
  {"DCA", -18000, -14400, 2},  // Washington National, USA
  {"BOS", -18000, -14400, 2},  // Boston, USA
  {"ORD", -21600, -18000, 2},  // Chicago O'Hare, USA
  {"DFW", -21600, -18000, 2},  // Dallas/Fort Worth, USA
  {"IAH", -21600, -18000, 2},  // Houston, USA
  {"DEN", -25200, -21600, 2},  // Denver, USA
  {"LAX", -28800, -25200, 2},  // Los Angeles, USA
  {"SFO", -28800, -25200, 2},  // San Francisco, USA
  {"SEA", -28800, -25200, 2},  // Seattle, USA
  {"PDX", -28800, -25200, 2},  // Portland, USA
  {"LAS", -28800, -25200, 2},  // Las Vegas, USA
  {"PHX", -25200, -25200, 0},  // Phoenix, USA (Arizona hat kein DST)
  {"MIA", -18000, -14400, 2},  // Miami, USA
  {"ATL", -18000, -14400, 2},  // Atlanta, USA
  {"YYZ", -18000, -14400, 2},  // Toronto, Kanada
  {"YVR", -28800, -25200, 2},  // Vancouver, Kanada
  {"YUL", -18000, -14400, 2},  // Montreal, Kanada
  {"MEX", -21600, -18000, 2},  // Mexico City, Mexiko

  // Südamerika
  {"GRU", -10800, -10800, 0},  // São Paulo, Brasilien (kein DST mehr seit 2019)
  {"GIG", -10800, -10800, 0},  // Rio de Janeiro, Brasilien
  {"EZE", -10800, -10800, 0},  // Buenos Aires, Argentinien (kein DST mehr seit 2009)
  {"SCL", -10800, -14400, 0},  // Santiago, Chile (DST-Regeln komplex, hier vereinfacht)
  {"LIM", -18000, -18000, 0},  // Lima, Peru
  {"BOG", -18000, -18000, 0},  // Bogotá, Kolumbien

  // Afrika
  {"JNB", 7200, 7200, 0},      // Johannesburg, Südafrika
  {"CPT", 7200, 7200, 0},      // Kapstadt, Südafrika
  {"NBO", 10800, 10800, 0},    // Nairobi, Kenia
  {"ADD", 10800, 10800, 0},    // Addis Abeba, Äthiopien
  {"LOS", 3600, 3600, 0},      // Lagos, Nigeria
  {"ALG", 3600, 3600, 0},      // Algier, Algerien
  {"TUN", 3600, 3600, 0},      // Tunis, Tunesien
  {"CMN", 3600, 3600, 0},      // Casablanca, Marokko (DST-Regeln komplex)
};

const int AIRPORT_DATABASE_SIZE = sizeof(AIRPORT_DATABASE) / sizeof(AirportTimezone);

#endif
