#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>

// APRS CONFIG
#define APRS_CALLSIGN "NOCALL"
#define APRS_SSID "13"
#define APRS_LAT 37.220400
#define APRS_LON 28.343400
#define APRS_SYMBOL_TABLE "/"
#define APRS_SYMBOL "_"
#define APRS_COMMENT "DLS WStation"
#define APRS_SERVER "euro.aprs2.net"
#define APRS_PORT 14580

void sendToWindy(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendToWunderground(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendToPWSWeather(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendToWeatherCloud(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendAprsWeather(float lat, float lon, float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempC, float hum, float pressurePa);


WiFiClient aprsClient;
WiFiClientSecure client;

static bool isWifi = false;
static String ssid = "";
static String password = "";

unsigned long lastSendMillis = 0;
const unsigned long sendInterval = 5; // saniye

// ----------------- Weather Data -----------------
struct WeatherData {
  int winddir;
  float windspeed;
  float windGust;
  int windGustDir;
  float rainMM;
  float temp;
  float humd;
  float pressure;
  float alt;
} weather;

// ----------------- Serial Buffer -----------------
String serialBuffer = "";

// ----------------- Parse Function -----------------
void parseWeather(String line) {
  if (!line.startsWith("$") || !line.endsWith("#")) return;
  line = line.substring(1, line.length() - 1); // $ ve # kaldır

  while (line.length() > 0) {
    int comma = line.indexOf(',');
    String token;
    if (comma == -1) {
      token = line;
      line = "";
    } else {
      token = line.substring(0, comma);
      line = line.substring(comma + 1);
    }

    int eq = token.indexOf('=');
    if (eq > 0) {
      String key = token.substring(0, eq);
      String val = token.substring(eq + 1);

      if (key == "winddir") weather.winddir = val.toInt();
      else if (key == "windspeedmph") weather.windspeed = val.toFloat() * 0.44704; // mph → m/s
      else if (key == "windgustmph") weather.windGust = val.toFloat() * 0.44704;
      else if (key == "windgustdir_10m") weather.windGustDir = val.toInt();
      else if (key == "rainin") weather.rainMM = val.toFloat();
      else if (key == "tempf") weather.temp = (val.toFloat() - 32.0) * 5.0 / 9.0;
      else if (key == "humidity") weather.humd = val.toFloat();
      else if (key == "pressure") weather.pressure = val.toFloat();
      else if (key == "alt") weather.alt = val.toFloat();
    }
  }
}

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);
  Serial1.begin(9600); // Arduino TX → ESP8266 RX

  if (isWifi) {
    WiFi.begin(ssid, password);
    Serial.print("Wi-Fi'ye bağlanıyor");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("\nWi-Fi bağlı!");
    Serial.print("IP Adresi: ");
    Serial.println(WiFi.localIP());
  }

  Serial.println("ESP8266 Weather Receiver hazır!");
}

// ----------------- Loop -----------------
void loop() {
  // Serial veri oku
  while (Serial1.available()) {
    char c = Serial1.read();
    serialBuffer += c;
    if (c == '#') { // paket sonu
      parseWeather(serialBuffer);
      serialBuffer = "";

      // Seri ekrana yazdır
      Serial.println("---- Hava Verileri ----");
      Serial.printf("Rüzgar: %.2f m/s, Gust: %.2f, Yön: %d°\n", weather.windspeed, weather.windGust, weather.windGustDir);
      Serial.printf("Yağmur: %.2f mm\n", weather.rainMM);
      Serial.printf("Sıcaklık: %.2f C, Nem: %.2f %%\n", weather.temp, weather.humd);
      Serial.printf("Basınç: %.2f Pa, İrtifa: %.2f m\n", weather.pressure, weather.alt);
      Serial.println("-----------------------\n");
    }
  }

  // HTTP / APRS gönderimi
  if (isWifi && WiFi.status() == WL_CONNECTED && millis() - lastSendMillis >= (sendInterval * 1000)) {
    lastSendMillis = millis();

    sendToWindy(weather.windspeed, weather.windGust, weather.winddir, weather.rainMM, weather.temp, weather.humd, weather.pressure);
    sendToWunderground(weather.windspeed, weather.windGust, weather.winddir, weather.rainMM, weather.temp, weather.humd, weather.pressure);
    sendToPWSWeather(weather.windspeed, weather.windGust, weather.winddir, weather.rainMM, weather.temp, weather.humd, weather.pressure);
    sendToWeatherCloud(weather.windspeed, weather.windGust, weather.winddir, weather.rainMM, weather.temp, weather.humd, weather.pressure);
    sendAprsWeather(APRS_LAT, APRS_LON, weather.windspeed, weather.windGust, weather.winddir, weather.rainMM, weather.temp, weather.humd, weather.pressure);
  }

  delay(100);
}

void sendToWindy(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  String url = "https://stations.windy.com/pws/update/eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJjaSI6MjM4ODI0MSwiaWF0IjoxNzU2OTE0NjQ2fQ.LEF1adpj0y66j6x8vtMw63ilU7uoyR-OtWqawNhg6ao";
  url += "?winddir=" + String(windDir) + "&wind=" + String(windSpeed) + "&gust=" + String(windGust);
  url += "&temp=" + String(tempC) + "&rainin=" + String(rainMM) + "&mbar=" + String(pressurePa / 100.0) + "&humidity=" + String(hum);

  HTTPClient http;
  client.setInsecure();
  http.begin(client, url);
  int httpCode = http.GET();
  Serial.println("Windy: " + String(httpCode));
  http.end();
}

void sendToWunderground(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  HTTPClient http;
  client.setInsecure();
  http.begin(client, "https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "ID=IMULA18&PASSWORD=7XiGDJZN&dateutc=now";
  postData += "&winddir=" + String(windDir);
  postData += "&windspeedmph=" + String(windSpeed); // mph
  postData += "&windgustmph=" + String(windGust);   // mph
  postData += "&tempf=" + String(tempC * 9.0 / 5.0 + 32.0);
  postData += "&rainin=" + String(rainMM);
  postData += "&baromin=" + String(pressurePa * 0.0002953); // Pa → inHg
  postData += "&humidity=" + String(hum);
  postData += "&action=updateraw";

  int httpCode = http.POST(postData);
  Serial.println("Wunderground: " + String(httpCode));
  http.end();
}

void sendToPWSWeather(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  HTTPClient http;
  client.setInsecure();
  http.begin(client, "https://pwsupdate.pwsweather.com/api/v1/submitwx");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "ID=IMULA18&PASSWORD=9d4012c91304914fbf332612b6b78a76&dateutc=now";
  postData += "&winddir=" + String(windDir);
  postData += "&windspeedmph=" + String(windSpeed); // mph
  postData += "&windgustmph=" + String(windGust);   // mph
  postData += "&tempf=" + String(tempC * 9.0 / 5.0 + 32.0);
  postData += "&rainin=" + String(rainMM);
  postData += "&baromin=" + String(pressurePa * 0.0002953);
  postData += "&humidity=" + String(hum);
  postData += "&action=updateraw";

  int httpCode = http.POST(postData);
  Serial.println("PWSWeather: " + String(httpCode));
  http.end();
}

void sendToWeatherCloud(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  String url = "http://api.weathercloud.net/v01/set/wid/ed7e49327a534bf7/key/48bec33f0eaf6d4a0a05281c412366b1";
  url += "/temp/" + String(int(tempC * 10)); // °C * 10
  url += "/hum/" + String(int(hum));
  url += "/bar/" + String(int(pressurePa)); // Pa
  url += "/wspd/" + String(int(windSpeed));
  url += "/wdir/" + String(windDir);
  url += "/rain/" + String(int(rainMM * 10)); // mm * 10
  url += "/time/1415/date/20211224/software/weathercloud_software_v2.4";

  HTTPClient http;
  client.setInsecure();
  http.begin(client, url);
  int httpCode = http.GET();
  Serial.println("WeatherCloud: " + String(httpCode));
  http.end();
}

uint16_t aprsPasscode(String callsign) {
    callsign.toUpperCase();
    if (callsign.indexOf('-') != -1) callsign = callsign.substring(0, callsign.indexOf('-'));
    uint16_t code = 0x73e2;
    for (int i = 0; i < callsign.length(); i++) {
        code ^= ((i % 2) ? 0 : (callsign[i] << 8));
    }
    return code & 0x7FFF;
}

String aprsFormatLat(float latitude) {
    int deg = int(abs(latitude));
    float min = (abs(latitude) - deg) * 60.0;
    char dir = (latitude >= 0) ? 'N' : 'S';
    char buf[16];
    sprintf(buf, "%02d%05.2f%c", deg, min, dir);
    return String(buf);
}

String aprsFormatLon(float longitude) {
    int deg = int(abs(longitude));
    float min = (abs(longitude) - deg) * 60.0;
    char dir = (longitude >= 0) ? 'E' : 'W';
    char buf[16];
    sprintf(buf, "%03d%05.2f%c", deg, min, dir);
    return String(buf);
}

void sendAprsWeather(float lat, float lon, float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempC, float hum, float pressurePa) {
    if (!aprsClient.connect(APRS_SERVER, APRS_PORT)) {
        Serial.println("APRS sunucusuna bağlanamadı!");
        return;
    }

    uint16_t passcode = aprsPasscode(APRS_CALLSIGN);

    // Login
    String loginCmd = "user " + String(APRS_CALLSIGN) + " pass " + String(passcode) + " vers ESPWeather 1.0\n";
    aprsClient.print(loginCmd);

    // APRS position format
    String latStr = aprsFormatLat(lat);
    String lonStr = aprsFormatLon(lon);

    // --- Sensörleri APRS WX formatına çevir ---
    // 1. Rüzgar: m/s → knots
    int windKnots = int(windSpeedMS * 1.94384 + 0.5);
    int gustKnots = int(windGustMS * 1.94384 + 0.5);

    // 2. Sıcaklık: C → F
    int tempF = int(tempC * 9.0 / 5.0 + 32.0 + 0.5);

    // 3. Yağmur: mm → hundredths of inch
    int rainHundredths = int(rainMM * 0.0393701 * 100 + 0.5); // 1 mm = 0.03937 inch

    // 4. Basınç: Pa → tenths of mb
    int baroTenths = int(pressurePa / 10.0 + 0.5); // 101325 Pa → 1013.2 mb → 10132 tenths

    // 5. Nem: integer percent
    int humInt = int(hum + 0.5);

    // --- APRS WX Packet ---
    // !lat/lon_symbol_table_symbol_WXDATA
    char wxBuf[256];
    sprintf(wxBuf, "!%s%s%s_", 
            latStr.c_str(), 
            APRS_SYMBOL_TABLE, 
            lonStr.c_str());

    // Wind: DDD/SSS gGGG
    char windBuf[32];
    sprintf(windBuf, "%03d/%03dg%03d", windDir, windKnots, gustKnots);

    // Temperature tTT
    char tempBuf[8];
    sprintf(tempBuf, "t%03d", tempF);

    // Rain rRR
    char rainBuf[8];
    sprintf(rainBuf, "r%03d", rainHundredths);

    // Humidity hHH
    char humBuf[8];
    sprintf(humBuf, "h%02d", humInt);

    // Pressure bBBBB
    char baroBuf[12];
    sprintf(baroBuf, "b%05d", baroTenths);

    // Paket tamam
    String wxPacket = String(APRS_CALLSIGN) + "-" + APRS_SSID + ">APWD01,TCPIP*:" +
                      String(wxBuf) + windBuf + tempBuf + rainBuf + humBuf + baroBuf + "\n";

    aprsClient.print(wxPacket);

    // Opsiyonel status mesaj
    String statusMsg = String(APRS_CALLSIGN) + "-" + APRS_SSID + ">APWD01,TCPIP*:>Powered by DLS WStation\n";
    aprsClient.print(statusMsg);

    // APRS response
    String response = aprsClient.readStringUntil('\n');
    Serial.println("APRS Response: " + response);

    aprsClient.stop();
}
