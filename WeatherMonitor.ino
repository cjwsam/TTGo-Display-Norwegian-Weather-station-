/*
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  Author: cjwsam
 */

/***************************************************
   TTGO T-Display Weather Station + Captive Portal
   with Lat/Lon customization, WiFi fallback, and OTA
****************************************************/

#include <FS.h>             // Filesystem support
using fs::FS;

#include <TFT_eSPI.h>       // Bodmer's TFT library (configured for TTGO T-Display in User_Setup.h)
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <EEPROM.h>
#include <WebServer.h>      // Provides the web server for the captive portal
#include <DNSServer.h>      // For DNS hijacking when running in AP mode

/********************************************************
 *               EEPROM Storage Settings
 *
 *  We store:
 *    SSID (max 32 chars)
 *    Password (max 32 chars)
 *    Lat (max 8 chars)
 *    Lon (max 8 chars)
 *
 *  Total needed is well under 128 bytes,
 *  so we set EEPROM_SIZE to 128 to be safe.
 ********************************************************/
#define EEPROM_SIZE   128

#define SSID_ADDR     0
#define SSID_MAX_LEN  32

#define PASS_ADDR     (SSID_ADDR + SSID_MAX_LEN) // 32
#define PASS_MAX_LEN  32

#define LAT_ADDR      (PASS_ADDR + PASS_MAX_LEN) // 64
#define LAT_MAX_LEN   8

#define LON_ADDR      (LAT_ADDR + LAT_MAX_LEN)   // 72
#define LON_MAX_LEN   8

String storedSsid;
String storedPass;
String storedLat; // e.g. "59.91"
String storedLon; // e.g. "10.75"

/********************************************************
 *                 WiFi & API Settings
 *  If no credentials are found, these defaults are used
 *  to set up a fallback AP. Also see the new lat/lon
 *  fields stored in EEPROM for your location.
 ********************************************************/
#define DEFAULT_AP_SSID      "ESP32_AP"
#define DEFAULT_AP_PASSWORD  "password123"

// Default location if lat/lon are not stored or empty
#define DEFAULT_LAT  "59.91"
#define DEFAULT_LON  "10.75"

/********************************************************
 *                   Button Pins
 ********************************************************/
const int BUTTON_PIN      = 0;
const int BUTTON_PIN_NEXT = 35;
#define NUM_PAGES 5

/********************************************************
 *        Forward Declarations for Animations & Icons
 ********************************************************/
void renderSunnyFrame();
void renderCloudyFrame();
void renderRainFrame();
void renderSnowFrame();
void renderFogFrame();
void renderThunderFrame();
void drawSmallForecastIcon(int cond, int x, int y);

/********************************************************
 *                   Global State
 ********************************************************/
volatile bool displayOn   = true;
int forecastPage          = 0;   // 0..4
TFT_eSPI tft              = TFT_eSPI();

// On TTGO T-Display, rotation might produce 135×240 or 240×135.
// We'll use a Sprite that matches tft.width()/height().
TFT_eSprite spr           = TFT_eSprite(&tft);

const int forecastHeight  = 140; // Bottom area used for "today" data

// Forecast data structures
struct DailyForecast {
  String dayOfWeek;   // e.g., "Wed"
  String dateLabel;   // e.g., "14 Feb"
  float highTemp;
  float lowTemp;
  int   weatherCondition; // 0..5
};

DailyForecast forecastDays[4]; // [0]=today, [1..3]=next 3 days

// Each day can have up to 24 symbol entries from the API
static const int MAX_SYMBOLS_PER_DAY = 24;
String daySymbols[4][MAX_SYMBOLS_PER_DAY];
int    daySymbolsCount[4] = {0, 0, 0, 0};

float currentTemperature  = 0.0;
String weatherSymbol      = "";
int weatherCondition      = 1;  // default to Cloudy

unsigned long lastWeatherUpdate           = 0;
const unsigned long weatherUpdateInterval = 10UL * 60UL * 1000UL; // 10 minutes

/********************************************************
 *                Captive Portal Globals
 ********************************************************/
WebServer webServer(80);
DNSServer dnsServer;
bool inFallbackAP = false;
const byte DNS_PORT = 53;

// We'll allocate a global JSON buffer if needed
DynamicJsonDocument jsonAppBuffer(4096);

/********************************************************
 *            Function to Build Weather API URL
 *  Uses the stored latitude & longitude from EEPROM.
 *  Falls back to defaults if they are missing/empty.
 ********************************************************/
String buildWeatherURL() {
  if (storedLat.length() == 0) storedLat = DEFAULT_LAT;
  if (storedLon.length() == 0) storedLon = DEFAULT_LON;
  // MET API for locationforecast/2.0
  // Example: "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=59.91&lon=10.75"
  String url = "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=" + storedLat + "&lon=" + storedLon;
  return url;
}

/********************************************************
 *                EEPROM Functions
 ********************************************************/
void loadCredentials() {
  EEPROM.begin(EEPROM_SIZE);

  char ssid[SSID_MAX_LEN + 1];
  char pass[PASS_MAX_LEN + 1];
  char latBuff[LAT_MAX_LEN + 1];
  char lonBuff[LON_MAX_LEN + 1];

  for (int i = 0; i < SSID_MAX_LEN; i++) {
    ssid[i] = char(EEPROM.read(SSID_ADDR + i));
  }
  ssid[SSID_MAX_LEN] = '\0';

  for (int i = 0; i < PASS_MAX_LEN; i++) {
    pass[i] = char(EEPROM.read(PASS_ADDR + i));
  }
  pass[PASS_MAX_LEN] = '\0';

  for (int i = 0; i < LAT_MAX_LEN; i++) {
    latBuff[i] = char(EEPROM.read(LAT_ADDR + i));
  }
  latBuff[LAT_MAX_LEN] = '\0';

  for (int i = 0; i < LON_MAX_LEN; i++) {
    lonBuff[i] = char(EEPROM.read(LON_ADDR + i));
  }
  lonBuff[LON_MAX_LEN] = '\0';

  storedSsid = String(ssid);
  storedPass = String(pass);
  storedLat  = String(latBuff);
  storedLon  = String(lonBuff);

  EEPROM.end();
  Serial.printf("Loaded credentials from EEPROM:\n");
  Serial.printf("  SSID='%s'\n", storedSsid.c_str());
  Serial.printf("  PASS='%s' (hidden)\n", storedPass.c_str());
  Serial.printf("  LAT='%s'\n", storedLat.c_str());
  Serial.printf("  LON='%s'\n", storedLon.c_str());
}

void saveCredentials(const String &ssid, const String &pass,
                     const String &lat,  const String &lon) {
  EEPROM.begin(EEPROM_SIZE);

  // First clear the EEPROM region
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }

  // Store SSID
  for (int i = 0; i < ssid.length() && i < SSID_MAX_LEN; i++) {
    EEPROM.write(SSID_ADDR + i, ssid[i]);
  }

  // Store Password
  for (int i = 0; i < pass.length() && i < PASS_MAX_LEN; i++) {
    EEPROM.write(PASS_ADDR + i, pass[i]);
  }

  // Store Lat
  for (int i = 0; i < lat.length() && i < LAT_MAX_LEN; i++) {
    EEPROM.write(LAT_ADDR + i, lat[i]);
  }

  // Store Lon
  for (int i = 0; i < lon.length() && i < LON_MAX_LEN; i++) {
    EEPROM.write(LON_ADDR + i, lon[i]);
  }

  EEPROM.commit();
  EEPROM.end();
  Serial.println("Credentials (incl. lat/lon) saved to EEPROM.");
}

/********************************************************
 *                Utility Functions
 ********************************************************/
bool isNightMode() {
  // Checks the current time to decide if it's "night" (<6 or >=18)
  time_t now = time(nullptr);
  struct tm *tm_info = localtime(&now);
  int hour = tm_info->tm_hour;
  return (hour < 6 || hour >= 18);
}

void displaySleep(bool on) {
  // Puts the display driver in or out of sleep mode
  if (on) {
    tft.writecommand(0x10); // Enter sleep
  } else {
    tft.writecommand(0x11); // Exit sleep
    delay(120);
  }
}

// Checks if the first button has been pressed to toggle the display
bool checkForButtonToggle() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW) {
      displayOn = !displayOn;
      if (!displayOn) displaySleep(true);
      else            displaySleep(false);

      // Wait until the button is released
      while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
      Serial.printf("Display toggled: now %s\n", displayOn ? "ON" : "OFF");
      return true;
    }
  }
  return false;
}

// Checks if the second button was pressed to switch forecast pages
bool checkForNextButton() {
  if (digitalRead(BUTTON_PIN_NEXT) == LOW) {
    delay(50);
    if (digitalRead(BUTTON_PIN_NEXT) == LOW) {
      forecastPage = (forecastPage + 1) % NUM_PAGES;
      while (digitalRead(BUTTON_PIN_NEXT) == LOW) { delay(10); }
      Serial.printf("Forecast page changed to: %d\n", forecastPage);
      return true;
    }
  }
  return false;
}

// If the display is off, wait until it's turned on
void waitForDisplayOn() {
  while (!displayOn) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(BUTTON_PIN) == LOW) {
        displayOn = true;
        displaySleep(false);
        while (digitalRead(BUTTON_PIN) == LOW) { delay(10); }
      }
    }
    delay(100);
  }
}

/********************************************************
 *             Helper Drawing Functions
 ********************************************************/
void drawArc(TFT_eSprite &sprite, int x, int y, int radius, int dummy, int start_angle, int end_angle, uint16_t color, int thickness) {
  // Draws an arc by segmenting it into lines
  const int segments = 30;
  float startRad = start_angle * DEG_TO_RAD;
  float endRad = end_angle * DEG_TO_RAD;
  float angleStep = (endRad - startRad) / segments;

  for (int i = 0; i < segments; i++) {
    float a0 = startRad + i * angleStep;
    float a1 = a0 + angleStep;
    int x0 = x + radius * cos(a0);
    int y0 = y + radius * sin(a0);
    int x1 = x + radius * cos(a1);
    int y1 = y + radius * sin(a1);
    sprite.drawLine(x0, y0, x1, y1, color);
  }
}

/********************************************************
 *   parseIsoTimeToEpoch() => local time_t
 *  Converts an ISO8601 string into a time_t (epoch).
 ********************************************************/
time_t parseIsoTimeToEpoch(const char* isoTime) {
  int year, month, day, hour, minute, second, ms;
  int ret = sscanf(isoTime, "%d-%d-%dT%d:%d:%d.%dZ",
                   &year, &month, &day, &hour, &minute, &second, &ms);

  if (ret < 6) {
    // If there's no fractional part, parse again without ms
    ret = sscanf(isoTime, "%d-%d-%dT%d:%d:%dZ",
                 &year, &month, &day, &hour, &minute, &second);
    Serial.printf("Parsed time without ms: ret=%d\n", ret);
  } else {
    Serial.printf("Parsed time with ms: ret=%d, ms=%d\n", ret, ms);
  }

  struct tm t = {0};
  t.tm_year  = year - 1900;
  t.tm_mon   = month - 1;
  t.tm_mday  = day;
  t.tm_hour  = hour;
  t.tm_min   = minute;
  t.tm_sec   = second;
  t.tm_isdst = -1;

  time_t epoch = mktime(&t);
  Serial.printf("Converted ISO time '%s' to epoch: %ld\n", isoTime, epoch);
  return epoch;
}

/********************************************************
 *   pickRepresentativeSymbol(dayIndex)
 *  Determines an overall condition for a day by scanning
 *  all symbol codes for that day.
 ********************************************************/
int pickRepresentativeSymbol(int dayIndex) {
  auto mapSymbolToCondition = [&](const String &sym) -> int {
    String s = sym;
    s.toLowerCase();
    if (s.indexOf("thunder") >= 0) return 5;
    if (s.indexOf("fog") >= 0 || s.indexOf("mist") >= 0) return 4;
    if (s.indexOf("snow") >= 0) return 3;
    if (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0) return 2;
    if (s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0 ||
        s.indexOf("partlycloudy") >= 0) return 1;
    return 0; // default to Sunny
  };

  int worst = 0;
  for (int i = 0; i < daySymbolsCount[dayIndex]; i++) {
    int c = mapSymbolToCondition(daySymbols[dayIndex][i]);
    if (c > worst) worst = c;
  }
  return worst;
}

/********************************************************
 *               updateWeather()
 *  Fetches and parses weather data from the MET API.
 *  Uses a filtering approach with ArduinoJson to keep 
 *  memory usage manageable.
 ********************************************************/
void updateWeather() {
  Serial.println("Starting weather update...");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, skipping weather update.");
    return;
  }

  HTTPClient http;
  String url = buildWeatherURL();
  http.begin(url);
  http.addHeader("User-Agent", "ESP32-Weather (example@example.com)");
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP GET failed: %d\n", httpCode);
    http.end();
    return;
  }

  int contentLength = http.getSize();
  WiFiClient* stream = http.getStreamPtr();

  if (contentLength <= 0) {
    Serial.println("Invalid content length!");
    http.end();
    return;
  }

  Serial.printf("Heap before parsing: %u bytes\n", ESP.getFreeHeap());

  // Read the entire HTTP response into a String
  String payload;
  payload.reserve(contentLength + 1);

  unsigned long timeout = millis();
  while (stream->connected() && payload.length() < contentLength) {
    while (stream->available()) {
      char c = (char)stream->read();
      payload += c;
      timeout = millis(); // reset timeout if data is arriving
    }
    if (millis() - timeout > 5000) {
      Serial.println("Timeout reading HTTP response.");
      http.end();
      return;
    }
  }

  // Debug: show a snippet of the payload
  Serial.println("Payload snippet:");
  Serial.println(payload.substring(0, 200));

  // Create an explicit filter for ArduinoJson
  StaticJsonDocument<256> filter;
  JsonObject prop = filter.createNestedObject("properties");
  JsonArray tsArray = prop.createNestedArray("timeseries");
  JsonObject tsElem = tsArray.createNestedObject();
  tsElem["time"] = true;
  JsonObject data = tsElem.createNestedObject("data");
  JsonObject instant = data.createNestedObject("instant");
  JsonObject details = instant.createNestedObject("details");
  details["air_temperature"] = true;
  JsonObject next1 = data.createNestedObject("next_1_hours");
  JsonObject summary = next1.createNestedObject("summary");
  summary["symbol_code"] = true;

  DynamicJsonDocument doc(12288);
  DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));

  if (error) {
    Serial.printf("JSON deserialization failed: %s\n", error.c_str());
    http.end();
    return;
  }

  Serial.printf("Heap after parsing: %u bytes\n", ESP.getFreeHeap());

  // Retrieve the timeseries array
  JsonArray timeseries = doc["properties"]["timeseries"].as<JsonArray>();
  if (!timeseries) {
    Serial.println("No timeseries array found in JSON!");
    http.end();
    return;
  } else {
    Serial.printf("Found %d timeseries entries.\n", timeseries.size());
  }

  // Clear old forecast data
  for (int i = 0; i < 4; i++) {
    forecastDays[i] = {"", "", -999.0, 999.0, 0};
    daySymbolsCount[i] = 0;
  }

  time_t nowT = time(nullptr);
  struct tm* nowLocal = localtime(&nowT);
  char dateBuf[9];
  strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", nowLocal);
  uint32_t todayDate = atoi(dateBuf);

  int dayIndex = -1;
  uint32_t currentDate = 0;
  bool firstEntry = true;

  // Process each entry in the timeseries
  for (JsonObject entry : timeseries) {
    const char* isoTime = entry["time"];
    if (!isoTime) continue;

    int year, month, day;
    sscanf(isoTime, "%d-%d-%dT", &year, &month, &day);
    uint32_t entryDate = year * 10000UL + month * 100UL + day;

    // Skip data that's before the current day
    if (entryDate < todayDate) continue;

    // If this entry belongs to a new day, move to the next day index
    if (entryDate != currentDate) {
      currentDate = entryDate;
      dayIndex++;
      if (dayIndex >= 4) break;

      time_t epoch = parseIsoTimeToEpoch(isoTime);
      struct tm* fc = localtime(&epoch);
      char dow[4], dat[12];
      strftime(dow, sizeof(dow), "%a", fc);
      strftime(dat, sizeof(dat), "%d %b", fc);

      forecastDays[dayIndex].dayOfWeek = String(dow);
      forecastDays[dayIndex].dateLabel = String(dat);
      Serial.printf("New day: %s %s (entryDate: %lu)\n", dow, dat, entryDate);
    }

    float temp = entry["data"]["instant"]["details"]["air_temperature"] | 0.0;
    forecastDays[dayIndex].lowTemp = min(forecastDays[dayIndex].lowTemp, temp);
    forecastDays[dayIndex].highTemp = max(forecastDays[dayIndex].highTemp, temp);

    const char* symbol = entry["data"]["next_1_hours"]["summary"]["symbol_code"];
    if (symbol && daySymbolsCount[dayIndex] < MAX_SYMBOLS_PER_DAY) {
      daySymbols[dayIndex][daySymbolsCount[dayIndex]++] = String(symbol);
    }

    // Capture the first entry to represent "current" conditions
    if (firstEntry) {
      currentTemperature = temp;
      weatherSymbol = symbol ? String(symbol) : "cloudy";
      firstEntry = false;
      Serial.printf("Current temp: %.1f, symbol: %s\n", currentTemperature, weatherSymbol.c_str());
    }
  }

  // Determine representative weather condition for each day
  for (int i = 0; i < 4; i++) {
    forecastDays[i].weatherCondition = pickRepresentativeSymbol(i);
    Serial.printf("Day %d condition: %d, high: %.1f, low: %.1f\n",
                  i, forecastDays[i].weatherCondition,
                  forecastDays[i].highTemp, forecastDays[i].lowTemp);
  }

  // Derive current weatherCondition from current symbol
  String s = weatherSymbol;
  s.toLowerCase();
  if (s.indexOf("clearsky") >= 0 || s.indexOf("fair") >= 0) weatherCondition = 0;
  else if (s.indexOf("cloud") >= 0 || s.indexOf("overcast") >= 0 || s.indexOf("partlycloudy") >= 0) weatherCondition = 1;
  else if (s.indexOf("rain") >= 0 || s.indexOf("drizzle") >= 0 || s.indexOf("rainshowers") >= 0) weatherCondition = 2;
  else if (s.indexOf("snow") >= 0) weatherCondition = 3;
  else if (s.indexOf("fog") >= 0 || s.indexOf("mist") >= 0) weatherCondition = 4;
  else if (s.indexOf("thunder") >= 0) weatherCondition = 5;
  else weatherCondition = 1;

  Serial.printf("Current weather condition: %d (%s)\n", weatherCondition, weatherSymbol.c_str());
  http.end();
}

/********************************************************
 *   getConditionText() & getConditionColor()
 ********************************************************/
String getConditionText(int wc) {
  switch (wc) {
    case 0: return "Sunny";
    case 1: return "Cloudy";
    case 2: return "Rain";
    case 3: return "Snow";
    case 4: return "Fog";
    case 5: return "Thunder";
  }
  return "N/A";
}

uint16_t getConditionColor(int wc) {
  switch (wc) {
    case 0: return TFT_YELLOW;    // Sunny
    case 1: return 0xC618;        // Light gray
    case 2: return TFT_BLUE;      // Rain
    case 3: return TFT_WHITE;     // Snow
    case 4: return 0xCE79;        // Fog
    case 5: return TFT_YELLOW;    // Thunder
  }
  return TFT_WHITE;
}

/********************************************************
 *   drawForecastIcon() => large icons for "today"
 ********************************************************/
void drawForecastIcon(int cond, bool night, int x, int y, int size) {
  switch (cond) {
    case 0: // Sunny
      if (!night) {
        spr.fillCircle(x, y, size + 10, TFT_ORANGE);
        spr.fillCircle(x, y, size, TFT_YELLOW);
      } else {
        spr.fillCircle(x, y, size + 10, TFT_LIGHTGREY);
        spr.fillCircle(x + size / 3, y - size / 3, size, TFT_BLACK);
      }
      break;

    case 1: { // Cloudy
      uint16_t col = (night ? TFT_DARKGREY : TFT_LIGHTGREY);
      spr.fillCircle(x - size / 3, y, size * 0.6, col);
      spr.fillCircle(x + size / 3, y, size * 0.6, col);
      spr.fillCircle(x - size / 3, y - 3, size / 6, TFT_WHITE);
      break;
    }

    case 2: { // Rain
      uint16_t cloudCol = night ? TFT_DARKGREY : TFT_LIGHTGREY;
      uint16_t dropCol  = night ? TFT_DARKCYAN : TFT_BLUE;
      spr.fillCircle(x, y - size * 0.3, size * 0.8, cloudCol);
      int dropLen = size / 2;
      spr.drawLine(x - size / 4, y, x - size / 4, y + dropLen, dropCol);
      spr.drawLine(x + size / 4, y, x + size / 4, y + dropLen, dropCol);
      spr.fillCircle(x - size / 4, y + dropLen + 1, 2, dropCol);
      spr.fillCircle(x + size / 4, y + dropLen + 1, 2, dropCol);
      break;
    }

    case 3: { // Snow
      uint16_t cloudCol = night ? TFT_DARKGREY : TFT_LIGHTGREY;
      uint16_t flakeCol = TFT_WHITE;
      spr.fillCircle(x, y - size * 0.3, size * 0.8, cloudCol);
      int flakeY = y + size / 4;
      spr.drawLine(x, flakeY, x, flakeY + size / 3, flakeCol);
      spr.drawLine(x - size / 6, flakeY + size / 6, x + size / 6, flakeY + size / 6, flakeCol);
      spr.drawLine(x - size / 8, flakeY + size / 8, x + size / 8, flakeY + size / 6 + size / 8, flakeCol);
      break;
    }

    case 4: { // Fog
      uint16_t lineCol = night ? TFT_DARKGREY : 0xCE79;
      for (int i = 0; i < 3; i++) {
        int yy = y - size / 3 + i * (size / 3);
        spr.drawFastHLine(x - size, yy, size * 2, lineCol);
      }
      break;
    }

    case 5: { // Thunder
      uint16_t cloudCol = TFT_DARKGREY;
      spr.fillCircle(x, y - size * 0.3, size * 0.8, cloudCol);
      spr.drawLine(x, y, x - size / 4, y + size / 2, TFT_YELLOW);
      spr.drawLine(x - size / 4, y + size / 2, x, y + size * 0.5, TFT_YELLOW);
      break;
    }
  }
}

/********************************************************
 *   drawSmallForecastIcon() => simpler icons
 ********************************************************/
void drawSmallForecastIcon(int cond, int x, int y) {
  int size = 10;
  uint16_t colCloud   = TFT_LIGHTGREY;
  uint16_t colNight   = TFT_DARKGREY;
  uint16_t colRain    = TFT_BLUE;
  uint16_t colSnow    = TFT_WHITE;
  uint16_t colFog     = 0xCE79;
  uint16_t colThunder = TFT_YELLOW;

  switch (cond) {
    case 0: // Sunny
      spr.fillCircle(x, y, size, TFT_YELLOW);
      break;
    case 1: // Cloudy
      spr.fillCircle(x - size / 3, y, size * 0.6, colCloud);
      spr.fillCircle(x + size / 3, y, size * 0.6, colCloud);
      break;
    case 2: // Rain
      spr.fillCircle(x, y - size * 0.3, size * 0.7, colCloud);
      spr.drawLine(x, y, x, y + size, colRain);
      break;
    case 3: // Snow
      spr.fillCircle(x, y - size * 0.3, size * 0.7, colCloud);
      spr.drawLine(x, y + 1, x, y + size - 1, colSnow);
      break;
    case 4: // Fog
      spr.drawFastHLine(x - size, y - 2, size * 2, colFog);
      spr.drawFastHLine(x - size, y + 2, size * 2, colFog);
      break;
    case 5: // Thunder
      spr.fillCircle(x, y - size * 0.3, size * 0.7, colNight);
      spr.drawLine(x, y, x - size / 2, y + size * 0.5, colThunder);
      spr.drawLine(x - size / 2, y + size * 0.5, x, y + size * 0.5, colThunder);
      break;
  }
}

// Draw small WiFi icon using arcs
void drawSmallWiFiIcon(int x, int y, uint16_t color) {
  drawArc(spr, x, y, 6, 6, 210, 330, color, 2);
  drawArc(spr, x, y, 4, 4, 210, 330, color, 2);
  drawArc(spr, x, y, 2, 2, 210, 330, color, 2);
}

// Draw small memory chip icon
void drawSmallMemoryIcon(int x, int y, uint16_t color) {
  spr.drawRect(x - 6, y - 4, 12, 8, color);
  spr.drawPixel(x - 3, y - 5, color);
  spr.drawPixel(x - 1, y - 5, color);
  spr.drawPixel(x + 1, y - 5, color);
  spr.drawPixel(x + 3, y - 5, color);
  spr.drawPixel(x - 3, y + 4, color);
  spr.drawPixel(x - 1, y + 4, color);
  spr.drawPixel(x + 1, y + 4, color);
  spr.drawPixel(x + 3, y + 4, color);
}

// Draw small CPU icon
void drawSmallCPUIcon(int x, int y, uint16_t color) {
  spr.fillCircle(x, y, 5, TFT_BLACK);
  spr.drawCircle(x, y, 5, color);
  spr.drawLine(x, y - 5, x, y - 3, color);
  spr.drawLine(x, y + 5, x, y + 3, color);
  spr.drawLine(x - 5, y, x - 3, y, color);
  spr.drawLine(x + 5, y, x + 3, y, color);
}

// Draw a small IP icon
void drawSmallIPIcon(int x, int y, uint16_t color) {
  spr.drawCircle(x, y, 6, color);
  spr.drawFastHLine(x - 5, y, 10, color);
  spr.drawLine(x, y - 5, x, y + 5, color);
}

/********************************************************
 *   Stats Page Display
 ********************************************************/
void displayStatsPage() {
  spr.fillSprite(TFT_BLACK);
  long rssi = WiFi.RSSI();
  size_t freeHeap = ESP.getFreeHeap();
  uint32_t cpuFreqMHz = getCpuFrequencyMhz();
  IPAddress ip = WiFi.localIP();

  spr.setTextDatum(MC_DATUM);
  spr.setFreeFont(&FreeSansBold12pt7b);
  spr.setTextColor(TFT_MAGENTA, TFT_BLACK);
  spr.drawString("ESP Stats", spr.width() / 2, 25);

  int lineY = 60;
  drawSmallWiFiIcon(20, lineY, TFT_CYAN);
  spr.setTextFont(2);
  spr.setTextColor(TFT_CYAN, TFT_BLACK);
  spr.setTextDatum(ML_DATUM);
  String rssiStr = String(rssi) + " dBm";
  spr.drawString(rssiStr, 35, lineY);

  lineY += 20;
  drawSmallMemoryIcon(20, lineY, TFT_GREEN);
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  String memStr = String(freeHeap) + " B";
  spr.drawString(memStr, 35, lineY);

  lineY += 20;
  drawSmallCPUIcon(20, lineY, TFT_YELLOW);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  String cpuStr = String(cpuFreqMHz) + " MHz";
  spr.drawString(cpuStr, 35, lineY);

  lineY += 20;
  drawSmallIPIcon(20, lineY, TFT_WHITE);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(ip.toString(), 35, lineY);

  lineY += 20;
  spr.setTextColor(TFT_ORANGE, TFT_BLACK);
  unsigned long ms = millis();
  unsigned long sec = ms / 1000;
  unsigned long hrs = sec / 3600; sec %= 3600;
  unsigned long mins = sec / 60; sec %= 60;
  char upStr[24];
  snprintf(upStr, sizeof(upStr), "Uptime %lu:%02lu:%02lu", hrs, mins, sec);
  spr.drawString(String(upStr), 20, lineY);

  if (inFallbackAP) {
    lineY += 20;
    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString("AP Mode waiting...", 20, lineY);
  }
}

/********************************************************
 *   Display "Today" Forecast
 ********************************************************/
void displayTodayForecast() {
  // Show current time, date, temperature, and symbol
  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  char timeStr[16], dateStr[16];
  strftime(timeStr, sizeof(timeStr), "%I:%M %p", ti);
  strftime(dateStr, sizeof(dateStr), "%a %d %b", ti);

  int baseY = spr.height() - forecastHeight;
  spr.fillRect(0, baseY, spr.width(), forecastHeight, TFT_BLACK);

  int line1Y = baseY + 25;
  int line2Y = baseY + 50;
  int line3Y = baseY + 85;
  int line4Y = baseY + 115;

  spr.setFreeFont(&FreeSansBold12pt7b);
  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(String(timeStr), spr.width() / 2, line1Y);

  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString(String(dateStr), spr.width() / 2, line2Y);

  String tempS = String(currentTemperature, 1) + "°C";
  spr.setFreeFont(&FreeSansBold12pt7b);
  spr.drawString(tempS, spr.width() / 2, line3Y);

  bool night = isNightMode();
  int iconX = 15, iconY = line4Y - 10, iconSize = 12;
  drawForecastIcon(weatherCondition, night, iconX, iconY, iconSize);

  String symTxt = weatherSymbol;
  symTxt.replace("_day", "");
  symTxt.replace("_night", "");
  symTxt.replace("_", " ");

  spr.setTextDatum(TL_DATUM);
  spr.setFreeFont(&FreeSans9pt7b);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(symTxt, iconX + iconSize + 6, line4Y - 18);
}

/********************************************************
 *   Daily Summary Page (Pages 1..3)
 ********************************************************/
void displayDailySummaryPage(int index) {
  spr.fillSprite(TFT_BLACK);
  DailyForecast &df = forecastDays[index];

  if (df.dateLabel.length() == 0) {
    spr.setTextDatum(MC_DATUM);
    spr.setFreeFont(&FreeSansBold12pt7b);
    spr.setTextColor(TFT_RED, TFT_BLACK);
    spr.drawString("No Data for Day " + String(index),
                   spr.width() / 2, spr.height() / 2);
    return;
  }

  spr.setTextDatum(MC_DATUM);
  spr.setFreeFont(&FreeSansBold12pt7b);
  spr.setTextColor(TFT_CYAN, TFT_BLACK);
  String title = df.dayOfWeek + " " + df.dateLabel;
  spr.drawString(title, spr.width() / 2, 25);

  // Reduced icon size from 24 => 16 so it's not too large
  int iconX = spr.width() / 2;
  int iconY = 65;
  int iconSize = 16; 
  drawForecastIcon(df.weatherCondition, false, iconX, iconY, iconSize);

  spr.setFreeFont(&FreeSans9pt7b);
  spr.setTextColor(TFT_RED, TFT_BLACK);
  String hiStr = "High: " + String(df.highTemp, 1) + "°C";
  spr.drawString(hiStr, spr.width() / 2, 105);

  spr.setTextColor(TFT_BLUE, TFT_BLACK);
  String loStr = "Low:  " + String(df.lowTemp, 1) + "°C";
  spr.drawString(loStr, spr.width() / 2, 125);

  spr.setTextColor(getConditionColor(df.weatherCondition), TFT_BLACK);
  String condTxt = getConditionText(df.weatherCondition);
  spr.drawString(condTxt, spr.width() / 2, 145);
}

/********************************************************
 *   Captive Portal – HTML Form
 *   Now includes fields for lat/lon.
 ********************************************************/
const char* portalForm = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 WiFi Config</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    body { font-family: Arial, sans-serif; margin: 40px; }
    h2 { color: #333; }
    label { display:block; margin-top: 20px; }
    input[type=text], input[type=password] {
      width: 100%; padding: 8px; box-sizing: border-box; font-size:16px;
    }
    input[type=submit] {
      margin-top: 20px; padding:10px; background:#5c9; border:none;
      cursor:pointer; font-size:16px; border-radius:4px;
    }
    .container { max-width:400px; margin:auto; background:#eef; padding:20px; border-radius:8px; }
  </style>
</head>
<body>
  <div class="container">
    <h2>Configure WiFi & Location</h2>
    <form method="POST" action="/save">
      <label for="ssid">SSID:</label>
      <input type="text" id="ssid" name="ssid" required>
      <label for="pass">Password:</label>
      <input type="password" id="pass" name="pass" required>
      <label for="lat">Latitude:</label>
      <input type="text" id="lat" name="lat" placeholder="e.g. 59.91" required>
      <label for="lon">Longitude:</label>
      <input type="text" id="lon" name="lon" placeholder="e.g. 10.75" required>
      <input type="submit" value="Save & Connect">
    </form>
  </div>
</body>
</html>
)rawliteral";

void handleRoot() {
  webServer.send(200, "text/html", portalForm);
}

void handleSave() {
  if (webServer.hasArg("ssid") && webServer.hasArg("pass") &&
      webServer.hasArg("lat")  && webServer.hasArg("lon")) {

    String newSsid = webServer.arg("ssid");
    String newPass = webServer.arg("pass");
    String newLat  = webServer.arg("lat");
    String newLon  = webServer.arg("lon");

    saveCredentials(newSsid, newPass, newLat, newLon);

    webServer.send(200, "text/html", "<html><body><h2>Saved. Rebooting...</h2></body></html>");
    delay(1000);
    ESP.restart();
  } else {
    webServer.send(200, "text/html", "<html><body><h2>Missing field (SSID, Pass, Lat, Lon)</h2></body></html>");
  }
}

void handleCaptivePortal() {
  // Redirect to the device IP if the user tries to visit an arbitrary URL
  webServer.sendHeader("Location", String("http://") + webServer.client().localIP().toString(), true);
  webServer.send(302, "text/plain", "");
}

/********************************************************
 *   Start Captive Portal AP
 ********************************************************/
void startCaptivePortalAP() {
  inFallbackAP = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD);

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  webServer.onNotFound([]() {
    if (!webServer.hostHeader().equals(WiFi.softAPIP().toString())) {
      handleCaptivePortal();
      return;
    }
    webServer.send(404, "text/plain", "Not found");
  });
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/save", HTTP_POST, handleSave);
  webServer.begin();

  Serial.println("===== AP started! =====");
  Serial.print("SSID: ");
  Serial.println(DEFAULT_AP_SSID);
  Serial.print("Password: ");
  Serial.println(DEFAULT_AP_PASSWORD);
  Serial.println("Open browser => http://192.168.4.1");

  // Draw a quick message on screen so user knows AP is up
  tft.setRotation(1);
  spr.deleteSprite();
  spr.createSprite(tft.width(), tft.height());

  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(MC_DATUM);
  spr.setFreeFont(&FreeSansBold12pt7b);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.drawString("AP MODE:", spr.width() / 2, spr.height() / 2 - 20);

  spr.setFreeFont(&FreeSans9pt7b);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  String msg = String("SSID: ") + DEFAULT_AP_SSID;
  spr.drawString(msg, spr.width() / 2, spr.height() / 2 + 10);
  msg = String("Pass: ") + DEFAULT_AP_PASSWORD;
  spr.drawString(msg, spr.width() / 2, spr.height() / 2 + 30);
  msg = "URL: 192.168.4.1";
  spr.drawString(msg, spr.width() / 2, spr.height() / 2 + 50);

  spr.pushSprite(0, 0);
}

/********************************************************
 *   WiFi Connection (Using EEPROM)
 ********************************************************/
void connectWiFi() {
  loadCredentials();
  if (storedSsid == "") {
    Serial.println("No stored WiFi credentials. Starting AP...");
    startCaptivePortalAP();
    return;
  }

  Serial.printf("Connecting to WiFi SSID: %s\n", storedSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(storedSsid.c_str(), storedPass.c_str());

  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 10000; // 10 seconds

  tft.setRotation(1);
  spr.deleteSprite();
  spr.createSprite(tft.width(), tft.height());

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    delay(500);
    Serial.print(".");
    checkForButtonToggle();

    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(MC_DATUM);
    spr.setFreeFont(&FreeSansBold12pt7b);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("Connecting to:", spr.width() / 2, spr.height() / 2 - 20);
    spr.drawString(storedSsid,        spr.width() / 2, spr.height() / 2);
    spr.pushSprite(0, 0);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println(WiFi.localIP());
    tft.setRotation(0);
    spr.deleteSprite();
    spr.createSprite(tft.width(), tft.height());
  } else {
    Serial.println("\nFailed to connect to WiFi => AP mode...");
    startCaptivePortalAP();
  }
}

/********************************************************
 *   OTA Setup
 ********************************************************/
void setupOTA() {
  ArduinoOTA.setHostname("ESP32-Weather");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else
        type = "filesystem";
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR)    Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR)   Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR)     Serial.println("End Failed");
    });
  ArduinoOTA.begin();
}

/********************************************************
 *                   setup()
 ********************************************************/
void setup() {
  Serial.begin(115200);
  Serial.println("Booting Forecast + Stats + TFT Display with Lat/Lon config...");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN_NEXT, INPUT_PULLUP);

  tft.init();
  tft.setRotation(0);

  spr.createSprite(tft.width(), tft.height());
  spr.fillSprite(TFT_BLACK);

  connectWiFi();
  setupOTA();

  configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
  time_t nt = time(nullptr);
  while (nt < 100000 && !inFallbackAP) {
    delay(500);
    nt = time(nullptr);
  }

  lastWeatherUpdate = millis();
  if (!inFallbackAP) {
    updateWeather();
  }

  // Brief test animation for each weather condition
  const int testDuration = 1000; // ms for each condition
  for (int cond = 0; cond < 6; cond++) {
    unsigned long start = millis();
    while (millis() - start < testDuration) {
      spr.fillSprite(TFT_BLACK);
      switch (cond) {
        case 0: renderSunnyFrame();   break;
        case 1: renderCloudyFrame();  break;
        case 2: renderRainFrame();    break;
        case 3: renderSnowFrame();    break;
        case 4: renderFogFrame();     break;
        case 5: renderThunderFrame(); break;
      }
      spr.setFreeFont(&FreeSans9pt7b);
      spr.setTextDatum(TL_DATUM);
      String label;
      switch (cond) {
        case 0: label = "Sunny/Moon"; break;
        case 1: label = "Cloudy";     break;
        case 2: label = "Rain";       break;
        case 3: label = "Snow";       break;
        case 4: label = "Fog/Mist";   break;
        case 5: label = "Thunder";    break;
      }
      spr.drawString(label, 5, 5);

      int smallIconX = spr.width() / 2;
      int smallIconY = spr.height() - 30;
      drawSmallForecastIcon(cond, smallIconX, smallIconY);
      spr.pushSprite(0, 0);
      delay(40);
    }

    // Clear screen with a quick "wipe" transition
    for (int y = 0; y < spr.height(); y += 4) {
      spr.fillRect(0, y, spr.width(), 4, TFT_BLACK);
      spr.pushSprite(0, 0);
      delay(5);
    }
  }
}

/********************************************************
 *                   loop()
 ********************************************************/
void loop() {
  if (inFallbackAP) {
    // In fallback AP mode, handle DNS & web server
    dnsServer.processNextRequest();
    webServer.handleClient();
    checkForButtonToggle();

    // Optionally refresh the "AP mode" screen?
    // We'll keep it simple: no repeated screen draw here
    delay(50);
    return;
  }

  if (!displayOn) {
    waitForDisplayOn();
  }

  ArduinoOTA.handle();

  if (millis() - lastWeatherUpdate > weatherUpdateInterval) {
    updateWeather();
    lastWeatherUpdate = millis();
  }

  checkForButtonToggle();
  checkForNextButton();

  spr.fillSprite(TFT_BLACK);

  // Page 0 => "Today" forecast + animation
  if (forecastPage == 0) {
    switch (weatherCondition) {
      case 0: renderSunnyFrame();   break;
      case 1: renderCloudyFrame();  break;
      case 2: renderRainFrame();    break;
      case 3: renderSnowFrame();    break;
      case 4: renderFogFrame();     break;
      case 5: renderThunderFrame(); break;
      default: renderCloudyFrame(); break;
    }
    displayTodayForecast();
  }
  // Pages 1..3 => daily summaries
  else if (forecastPage == 1 || forecastPage == 2 || forecastPage == 3) {
    displayDailySummaryPage(forecastPage);
  }
  // Page 4 => Stats
  else {
    displayStatsPage();
  }

  spr.pushSprite(0, 0);
  delay(30);
}

/********************************************************
 *     Weather Animations for "Today" Page (Page 0)
 ********************************************************/
void renderSunnyFrame() {
  bool night = isNightMode();
  int animCenterY = (spr.height() - forecastHeight) / 2;
  int cx = spr.width() / 2;
  int cy = animCenterY;
  unsigned long nowMillis = millis();
  float t = (nowMillis % 6000L) / 1000.0;
  int baseRadius = 20;
  int radius = baseRadius + (int)(4 * sin(2 * M_PI * t));

  if (!night) {
    spr.fillCircle(cx, cy, radius + 10, TFT_ORANGE);
    spr.fillCircle(cx, cy, radius, TFT_YELLOW);

    int rayStart = radius + 4;
    int rayLength = 20;
    const int numRays = 16;
    static float rayRotation = 0.0;
    for (int i = 0; i < numRays; i++) {
      float angle = rayRotation + i * (360.0 / numRays);
      float rad = angle * M_PI / 180.0;
      int xS = cx + rayStart * cos(rad);
      int yS = cy + rayStart * sin(rad);
      int xE = cx + (rayStart + rayLength) * cos(rad);
      int yE = cy + (rayStart + rayLength) * sin(rad);
      spr.drawLine(xS, yS, xE, yE, TFT_YELLOW);
    }
    rayRotation += 1.0;
  } else {
    spr.fillCircle(cx, cy, radius + 10, TFT_LIGHTGREY);
    spr.fillCircle(cx + 6, cy - 6, radius + 8, TFT_BLACK);

    // Speckled stars
    for (int i = 0; i < 4; i++) {
      int sx = cx - 30 + random(60);
      int sy = cy - 30 + random(60);
      spr.drawPixel(sx, sy, TFT_WHITE);
    }
  }
}

void renderCloudyFrame() {
  bool night = isNightMode();
  int animCenterY = (spr.height() - forecastHeight) / 2;
  int drift = (int)(10 * sin(millis() * 0.0005));
  int cx = spr.width() / 2 + drift;
  int cy = animCenterY;
  uint16_t ccol = night ? TFT_DARKGREY : TFT_LIGHTGREY;

  spr.fillCircle(cx - 20, cy, 15, ccol);
  spr.fillCircle(cx,     cy - 10, 20, ccol);
  spr.fillCircle(cx + 20, cy, 15, ccol);
  spr.fillRoundRect(cx - 30, cy, 60, 20, 10, ccol);

  if (night) {
    // Moon overlay
    spr.fillCircle(cx + 25, cy - 20, 10, TFT_LIGHTGREY);
    spr.fillCircle(cx + 27, cy - 22, 10, TFT_BLACK);
  }
}

void renderRainFrame() {
  bool night = isNightMode();
  int animCenterY = (spr.height() - forecastHeight) / 2;
  int cx = spr.width() / 2;
  int cloudY = animCenterY - 20;

  static bool init = false;
  static float rainX[20], rainY[20], rSpeed[20];
  if (!init) {
    for (int i = 0; i < 20; i++) {
      rainX[i] = cx - 40 + random(81);
      rainY[i] = cloudY + 30 + random(20);
      rSpeed[i] = random(3, 6);
    }
    init = true;
  }
  uint16_t ccol = night ? TFT_DARKGREY : TFT_LIGHTGREY;
  uint16_t rcol = night ? TFT_DARKCYAN : TFT_BLUE;

  spr.fillCircle(cx - 20, cloudY, 15, ccol);
  spr.fillCircle(cx, cloudY - 10, 20, ccol);
  spr.fillCircle(cx + 20, cloudY, 15, ccol);
  spr.fillRoundRect(cx - 30, cloudY, 60, 20, 10, ccol);

  // Falling raindrops
  for (int i = 0; i < 20; i++) {
    int dropLen = 15;
    spr.drawLine(rainX[i], rainY[i], rainX[i], rainY[i] + dropLen, rcol);
    spr.drawPixel(rainX[i], rainY[i] + dropLen + 1, rcol);
    rainY[i] += rSpeed[i] * 0.3;
    if (rainY[i] > animCenterY + (spr.height() - forecastHeight) / 2) {
      rainY[i] = cloudY + 30;
      rainX[i] = cx - 40 + random(81);
    }
  }
}

void renderSnowFrame() {
  bool night = isNightMode();
  int animCenterY = (spr.height() - forecastHeight) / 2;
  int cx = spr.width() / 2;
  int cloudY = animCenterY - 20;

  static bool init = false;
  static float snowX[20], snowY[20], sSpeed[20];
  if (!init) {
    for (int i = 0; i < 20; i++) {
      snowX[i] = cx - 40 + random(81);
      snowY[i] = cloudY + 30 + random(20);
      sSpeed[i] = random(1, 3);
    }
    init = true;
  }
  uint16_t ccol = night ? TFT_DARKGREY : TFT_LIGHTGREY;
  uint16_t scol = night ? TFT_LIGHTGREY : TFT_WHITE;

  spr.fillCircle(cx - 20, cloudY, 15, ccol);
  spr.fillCircle(cx, cloudY - 10, 20, ccol);
  spr.fillCircle(cx + 20, cloudY, 15, ccol);
  spr.fillRoundRect(cx - 30, cloudY, 60, 20, 10, ccol);

  // Falling snow
  for (int i = 0; i < 20; i++) {
    spr.fillCircle(snowX[i], snowY[i], 2, scol);
    snowY[i] += sSpeed[i] * 0.2;
    if (snowY[i] > animCenterY + (spr.height() - forecastHeight) / 2) {
      snowY[i] = cloudY + 30;
      snowX[i] = cx - 40 + random(81);
    }
  }
}

void renderFogFrame() {
  // Basic "fog" lines that shift sinusoidally
  int animAreaH = spr.height() - forecastHeight;
  spr.fillRect(0, 0, spr.width(), animAreaH, TFT_BLACK);
  uint16_t fogCols[3] = {TFT_LIGHTGREY, TFT_SILVER, 0xCE79};

  for (int line = 0; line < 3; line++) {
    float amplitude = 6.0 + line * 2.0;
    float freq = 0.04 + line * 0.01;
    int baseY = 30 + line * 25;
    for (int x = 0; x < spr.width() - 1; x++) {
      int y1 = baseY + amplitude * sin(freq * (x + millis() * 0.02) + line);
      int y2 = baseY + amplitude * sin(freq * (x + 1 + millis() * 0.02) + line);
      spr.drawLine(x, y1, x + 1, y2, fogCols[line]);
    }
  }
}

void renderThunderFrame() {
  bool night = isNightMode();
  int animCenterY = (spr.height() - forecastHeight) / 2;
  int cx = spr.width() / 2;
  int cy = animCenterY;

  spr.fillRect(0, 0, spr.width(), animCenterY * 2, TFT_BLACK);

  uint16_t ccol = TFT_DARKGREY;
  spr.fillCircle(cx - 20, cy, 15, ccol);
  spr.fillCircle(cx, cy - 10, 20, ccol);
  spr.fillCircle(cx + 20, cy, 15, ccol);
  spr.fillRoundRect(cx - 30, cy, 60, 20, 10, ccol);

  // Random lightning bolts
  if (random(0, 100) < 15) {
    int bp = 6;
    int bx[bp], by[bp];
    bx[0] = cx;
    by[0] = cy + 10;
    for (int i = 1; i < bp; i++) {
      bx[i] = bx[i - 1] + random(-12, 13);
      by[i] = by[i - 1] + random(10, 20);
    }
    for (int i = 0; i < bp - 1; i++) {
      spr.drawLine(bx[i], by[i], bx[i + 1], by[i + 1], TFT_YELLOW);
    }
  }
}
