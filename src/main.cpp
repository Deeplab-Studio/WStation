#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <SoftwareSerial.h>

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

static bool isWifi = _lock_try_acquire;
static String ssid = "A V C U   2.4G";
static String password = "Orhun5830_";

unsigned long lastSendMillis = 0;
const unsigned long sendInterval = 15; // saniye
SoftwareSerial weatherSerial(D5, D6); // RX, TX

// ----------------- Weather Data -----------------
struct WeatherData {
  int winddir = -1;
  float windspeed = 0;
  float windGust = 0;
  int windGustDir = 0;
  float rainMM = 0;
  float temp = 0;
  float humd = 0;
  float pressure = 0;
  float batt_lvl = 0;
  float light_lvl = 0;
} weather;

// ----------------- Serial Buffer -----------------
String serialBuffer = "";

// ----------------- Parse Function -----------------
void parseWeatherPacket(String rawPacket) {
  // rawPacket içinden '#' öncesini al (gelen paket muhtemelen "#\r\n" ile bitebilir)
  int hashPos = rawPacket.indexOf('#');
  if (hashPos == -1) return; // yoksa işleme

  String payload = rawPacket.substring(0, hashPos); // '#' hariç
  // Temizle: CR/LF ve boşluklar
  payload.replace("\r", "");
  payload.replace("\n", "");
  payload.trim();

  // Başında $ olabilir, kaldır
  if (payload.startsWith("$")) payload = payload.substring(1);

  // Başındaki fazla virgül veya boşlukları temizle
  while (payload.length() > 0 && (payload.charAt(0) == ',' || payload.charAt(0) == ' ' || payload.charAt(0) == '\t')) {
    payload = payload.substring(1);
  }
  payload.trim();

  if (payload.length() == 0) return;

  // Debug: gelen temiz payload'ı görmek istersen aç
  // Serial.println("PARSE_PAYLOAD: " + payload);

  int pos = 0;
  while (pos < payload.length()) {
    int comma = payload.indexOf(',', pos);
    String token;
    if (comma == -1) {
      token = payload.substring(pos);
      pos = payload.length();
    } else {
      token = payload.substring(pos, comma);
      pos = comma + 1;
    }
    token.trim();
    if (token.length() == 0) continue;

    int eq = token.indexOf('=');
    if (eq == -1) {
      // token içinde '=' yoksa atla (örn baştaki boş token)
      // Serial.println("No '=' in token: " + token);
      continue;
    }

    String key = token.substring(0, eq);
    String val = token.substring(eq + 1);
    key.trim(); val.trim();
    if (val.length() == 0) continue; // boşsa atla

    // Burada anahtarları eşleştiriyoruz (farklı isim varyasyonlarını da göz önüne al)
    if (key == "winddir") weather.winddir = val.toInt();
    else if (key == "windspeedmph" || key == "windspdmph_avg2m") weather.windspeed = val.toFloat(); // mph
    else if (key == "windgustmph") weather.windGust = val.toFloat(); // mph
    else if (key == "windgustdir" || key == "windgustdir_10m" || key == "winddir_avg2m") weather.windGustDir = val.toInt();
    else if (key == "rainin" || key == "rainMM") weather.rainMM = val.toFloat(); // inch
    else if (key == "dailyrainin") { /* opsiyonel */ }
    else if (key == "tempf") weather.temp = val.toFloat(); // Fahrenheit
    else if (key == "humidity") weather.humd = val.toFloat();
    else if (key == "pressure") weather.pressure = val.toFloat(); // sensör ne verirse direkt
    else if (key == "batt_lvl") weather.batt_lvl = val.toFloat();
    else if (key == "light_lvl") weather.light_lvl = val.toFloat();
    else {
      // Bilinmeyen anahtar: debug için açabilirsin
      // Serial.println("Unknown key: " + key + " val: " + val);
    }
  }
}

void handleSerialReading(Stream &weatherSerial) {
  while (weatherSerial.available()) {
    char c = weatherSerial.read();
    serialBuffer += c;                      // ÖNEMLİ: karakteri buffer'a önce ekle!
    // Eğer buffer içinde bir veya daha fazla '#' varsa hepsini sırayla işle
    int hashPos;
    while ((hashPos = serialBuffer.indexOf('#')) != -1) {
      String packet = serialBuffer.substring(0, hashPos + 1); // '#' dahil
      serialBuffer = serialBuffer.substring(hashPos + 1);     // kalan kısmı buffer'a geri koy
      // debug raw packet görmek istersen:
      // Serial.println("RAW PACKET: " + packet);

      // parse et
      parseWeatherPacket(packet);

      // parse sonrası çıktı (isteğe bağlı)
      Serial.println("---- Hava Verileri ----");
      Serial.printf("Rüzgar: %.2f mph, Gust: %.2f mph, Yön: %d°\n", weather.windspeed, weather.windGust, weather.windGustDir);
      Serial.printf("Yağmur: %.2f in\n", weather.rainMM);
      Serial.printf("Sıcaklık: %.2f F, Nem: %.2f %%\n", weather.temp, weather.humd);
      Serial.printf("Basınç: %.2f hPa\n", weather.pressure);
      Serial.println("-----------------------\n");
    }
  }
}

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);
  weatherSerial.begin(9600);  // Arduino TX

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

    // 📶 RSSI değeri (dBm)
    long rssi = WiFi.RSSI();
    Serial.print("RSSI (dBm): ");
    Serial.println(rssi);
    
    // 🌐 Bağlı olunan SSID
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    
    // 🔐 Güvenlik tipi (örnek: WPA2 vs.)
    Serial.print("BSSID: ");
    Serial.println(WiFi.BSSIDstr());
    
    // 📶 Kanal bilgisi
    Serial.print("Kanal: ");
    Serial.println(WiFi.channel());
  }

  Serial.println("ESP8266 Weather Receiver hazır!");
}

// ----------------- Loop -----------------
void loop() {
  handleSerialReading(weatherSerial);

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

void sendToWindy(float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  float windSpeedMPH = windSpeedMS * 2.23694;
  float windGustMPH  = windGustMS * 2.23694;
  float tempF        = tempC * 9.0 / 5.0 + 32.0;
  float rainIN       = rainMM / 25.4;
  float pressureHpa  = pressurePa / 100.0;

  String url = "https://stations.windy.com/pws/update/...";
  url += "?winddir=" + String(windDir);
  url += "&windspeedmph=" + String(windSpeedMPH);
  url += "&windgustmph=" + String(windGustMPH);
  url += "&tempf=" + String(tempF);
  url += "&rainin=" + String(rainIN);
  url += "&mbar=" + String(pressureHpa);
  url += "&humidity=" + String(hum);

  HTTPClient http;
  client.setInsecure();
  http.begin(client, url);
  int httpCode = http.GET();
  Serial.println("Windy: " + String(httpCode));
  http.end();
}

void sendToWunderground(float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  float windSpeedMPH = windSpeedMS * 2.23694;
  float windGustMPH  = windGustMS * 2.23694;
  float tempF        = tempC * 9.0 / 5.0 + 32.0;
  float rainIN       = rainMM / 25.4;
  float pressureInHg = pressurePa * 0.0002953;

  HTTPClient http;
  client.setInsecure();
  http.begin(client, "https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "ID=IMULA18&PASSWORD=7XiGDJZN&dateutc=now";
  postData += "&winddir=" + String(windDir);
  postData += "&windspeedmph=" + String(windSpeedMPH);
  postData += "&windgustmph=" + String(windGustMPH);
  postData += "&tempf=" + String(tempF);
  postData += "&rainin=" + String(rainIN);
  postData += "&baromin=" + String(pressureInHg);
  postData += "&humidity=" + String(hum);
  postData += "&action=updateraw";

  int httpCode = http.POST(postData);
  Serial.println("Wunderground: " + String(httpCode));
  http.end();
}

void sendToPWSWeather(float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  float windSpeedMPH = windSpeedMS * 2.23694;
  float windGustMPH  = windGustMS * 2.23694;
  float tempF        = tempC * 9.0 / 5.0 + 32.0;
  float rainIN       = rainMM / 25.4;
  float pressureInHg = pressurePa * 0.0002953;

  HTTPClient http;
  client.setInsecure();
  http.begin(client, "https://pwsupdate.pwsweather.com/api/v1/submitwx");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "ID=IMULA18&PASSWORD=9d4012c91304914fbf332612b6b78a76&dateutc=now";
  postData += "&winddir=" + String(windDir);
  postData += "&windspeedmph=" + String(windSpeedMPH);
  postData += "&windgustmph=" + String(windGustMPH);
  postData += "&tempf=" + String(tempF);
  postData += "&rainin=" + String(rainIN);
  postData += "&baromin=" + String(pressureInHg);
  postData += "&humidity=" + String(hum);
  postData += "&action=updateraw";

  int httpCode = http.POST(postData);
  Serial.println("PWSWeather: " + String(httpCode));
  http.end();
}

void sendToWeatherCloud(float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  float windSpeedKMH = windSpeedMS * 3.6;
  float pressureHpa  = pressurePa / 100.0;

  String url = "http://api.weathercloud.net/v01/set/wid/ed7e.../key/48be...";
  url += "/temp/" + String(int(tempC * 10));
  url += "/hum/" + String(int(hum));
  url += "/bar/" + String(int(pressureHpa * 10));
  url += "/wspd/" + String(int(windSpeedKMH));
  url += "/wdir/" + String(windDir);
  url += "/rain/" + String(int(rainMM * 10));
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
