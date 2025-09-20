#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <SoftwareSerial.h>

// APRS CONFIG
#define APRS_CALLSIGN "TA4VCU"
#define APRS_SSID "13"
#define APRS_LAT 37.220400
#define APRS_LON 28.343400
#define APRS_SYMBOL_TABLE "/"
#define APRS_SYMBOL "_"
#define APRS_COMMENT "DLS WStation"
#define APRS_SERVER "rotate.aprs.net"
#define APRS_PORT 14580

void sendToWindy(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendToWunderground(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendToPWSWeather(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendToWeatherCloud(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa);
void sendAprsWeather(float lat, float lon, float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempC, float hum, float pressurePa);


WiFiClient aprsClient;
WiFiClientSecure client;

static bool isWifi = true;
static String ssid = "A V C U   2.4G";
static String password = "Orhun5830_";

unsigned long lastSendMillis = 0;
const unsigned long sendInterval = 180; // saniye
SoftwareSerial weatherSerial(D5, D6); // RX, TX
int winddir_offset = 180; // kalibrasyon değeri (derece)

// ----------------- Weather Data -----------------
struct WeatherData {
  int winddir      = -1;      // sensör yoksa -1
  float windspeedmph  = -1.0;    // sensör yoksa -1
  float windspdmph_avg2m = -1.0; // sensör yoksa -1
  float windGust   = -1.0;    // sensör yoksa -1
  int windGustDir  = -1;      // sensör yoksa -1
  int windgustdir_10m = -1;   // sensör yoksa -1
  int winddir_avg2m = -1;     // sensör yoksa -1
  float rainin     = -1.0;    // sensör yoksa -1
  float temp       = -1.0;    // sensör yoksa -1
  float humd       = -1.0;    // sensör yoksa -1
  float pressure   = -1.0;    // sensör yoksa -1
  float batt_lvl   = -1.0;
  float light_lvl  = -1.0;
} weather;

// ----------------- Serial Buffer -----------------
String serialBuffer = "";

// ----------------- Parse Function -----------------
void parseWeatherPacket(String rawPacket) {
  Serial.println(rawPacket);
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
    else if (key == "windspeedmph") weather.windspeedmph = val.toFloat(); // mph
    else if (key == "windspdmph_avg2m") weather.windspdmph_avg2m = val.toFloat(); // mph
    else if (key == "windgustmph") weather.windGust = val.toFloat(); // mph
    else if (key == "windgustdir") weather.windGustDir = val.toInt();
    else if (key == "windgustdir_10m") weather.windgustdir_10m = val.toInt();
    else if (key == "winddir_avg2m") {
      weather.winddir_avg2m = val.toInt();

      // offset uygula
      weather.winddir_avg2m += winddir_offset;

      // normalize et (0–359 arası olsun)
      if (weather.winddir_avg2m >= 360) weather.winddir_avg2m -= 360;
      if (weather.winddir_avg2m < 0)    weather.winddir_avg2m += 360;
    }
    else if (key == "rainin") weather.rainin = val.toFloat(); // inch
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
      Serial.printf("Rüzgar: %.2f mph, Gust: %.2f mph\n", weather.windspeedmph, weather.windGust);
      Serial.printf("Yön1: %d°, Yö2: %d°, Yön3: %d°\n", weather.windGustDir, weather.windgustdir_10m, weather.winddir_avg2m);
      Serial.printf("Yağmur: %.2f in\n", weather.rainin);
      Serial.printf("Sıcaklık: %.2f F, Nem: %.2f %%\n", weather.temp, weather.humd);
      Serial.printf("Basınç: %.2f Pa\n", weather.pressure);
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

    sendToWindy(weather.windspeedmph, weather.windGust, weather.winddir_avg2m, weather.rainin, weather.temp, weather.humd, weather.pressure);
    sendToWunderground(weather.windspeedmph, weather.windGust, weather.winddir_avg2m, weather.rainin, weather.temp, weather.humd, weather.pressure);
    sendToPWSWeather(weather.windspeedmph, weather.windGust, weather.winddir_avg2m, weather.rainin, weather.temp, weather.humd, weather.pressure);
    sendToWeatherCloud(weather.windspeedmph, weather.windGust, weather.winddir_avg2m, weather.rainin, weather.temp, weather.humd, weather.pressure);
    
    sendAprsWeather(APRS_LAT, APRS_LON, weather.windspeedmph, weather.windGust, weather.winddir_avg2m, weather.rainin, weather.temp, weather.humd, weather.pressure);
    /*sendAprsWeather(
      APRS_LAT, APRS_LON,
      0.3, 8.0, 36,       // windSpeedMS, windGustMS, windDir
      2.4,               // rainMM
      30.2,             // tempC
      28.0,             // hum
      101300             // pressurePa
    );*/
  }

  /*if(WiFi.status() == WL_CONNECTED) {
    // 📶 RSSI değeri (dBm)
    long rssi = WiFi.RSSI();
    Serial.print("RSSI (dBm): ");
    Serial.println(rssi);
  }*/

  delay(1000);
}

void sendToWindy(float windSpeedMph, float windGustMph, int windDir, float rainVal, float tempF, float hum, float pressureVal)
{
  float rainIn = (rainVal > 10.0f) ? (rainVal / 25.4f) : rainVal;

  String url = "https://stations.windy.com/pws/update/eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJjaSI6MjM4ODI0MSwiaWF0IjoxNzU2OTE0NjQ2fQ.LEF1adpj0y66j6x8vtMw63ilU7uoyR-OtWqawNhg6ao";
  url += "?winddir=" + String(windDir);
  url += "&windspeedmph=" + String(windSpeedMph);
  url += "&windgustmph=" + String(windGustMph);
  url += "&tempf=" + String(tempF);
  url += "&rainin=" + String(rainIn);
  url += "&pressure=" + String(pressureVal);
  url += "&humidity=" + String(hum);

  HTTPClient http;
  client.setInsecure();
  http.begin(client, url);

  int httpCode = http.GET();
  Serial.println("Windy HTTP Code: " + String(httpCode));
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Windy Response: " + payload);
  } else {
    Serial.println("Windy HTTP request failed");
  }
  http.end();
}

void sendToWunderground(float windSpeedMph, float windGustMph, int windDir, float rainVal, float tempF, float hum, float pressureVal)
{
  float rainIn = (rainVal > 10.0f) ? (rainVal / 25.4f) : rainVal;
  float pressureHpa = (pressureVal > 2000.0f) ? (pressureVal / 100.0f) : pressureVal;
  float pressureInHg = pressureHpa * 0.02953;

  HTTPClient http;
  client.setInsecure();
  http.begin(client, "https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "ID=IMULA18&PASSWORD=7XiGDJZN&dateutc=now";
  postData += "&winddir=" + String(windDir);
  postData += "&windspeedmph=" + String(windSpeedMph);
  postData += "&windgustmph=" + String(windGustMph);
  postData += "&tempf=" + String(tempF);
  postData += "&rainin=" + String(rainIn);
  postData += "&baromin=" + String(pressureInHg);
  postData += "&humidity=" + String(hum);
  postData += "&action=updateraw";

  int httpCode = http.POST(postData);
  Serial.println("Wunderground HTTP Code: " + String(httpCode));
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Wunderground Response: " + payload);
  } else {
    Serial.println("Wunderground HTTP request failed");
  }
  http.end();
}

void sendToPWSWeather(float windSpeedMph, float windGustMph, int windDir, float rainVal, float tempF, float hum, float pressureVal)
{
  float rainIn = (rainVal > 10.0f) ? (rainVal / 25.4f) : rainVal;
  float pressureHpa = (pressureVal > 2000.0f) ? (pressureVal / 100.0f) : pressureVal;
  float pressureInHg = pressureHpa * 0.02953;

  HTTPClient http;
  client.setInsecure();
  http.begin(client, "https://pwsupdate.pwsweather.com/api/v1/submitwx");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String postData = "ID=IMULA18&PASSWORD=9d4012c91304914fbf332612b6b78a76&dateutc=now";
  postData += "&winddir=" + String(windDir);
  postData += "&windspeedmph=" + String(windSpeedMph);
  postData += "&windgustmph=" + String(windGustMph);
  postData += "&tempf=" + String(tempF);
  postData += "&rainin=" + String(rainIn);
  postData += "&baromin=" + String(pressureInHg);
  postData += "&humidity=" + String(hum);
  postData += "&action=updateraw";

  int httpCode = http.POST(postData);
  Serial.println("PWSWeather HTTP Code: " + String(httpCode));
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("PWSWeather Response: " + payload);
  } else {
    Serial.println("PWSWeather HTTP request failed");
  }
  http.end();
}

void sendToWeatherCloud(float windSpeedMph, float windGustMph, int windDir, float rainVal, float tempF, float hum, float pressureVal)
{
  float rainMM = (rainVal > 10.0f) ? rainVal : rainVal * 25.4f; // Rain: küçük değer inch ise mm'e çevir
  float tempC = (tempF - 32.0f) * 5.0f / 9.0f; // Temp: F → C
  float pressureHpa = pressureVal / 100.0f; // Pressure: Pa → hPa
  float windKmH = windSpeedMph * 1.60934f; // Wind: mph → km/h

  String url = "http://api.weathercloud.net/v01/set/wid/ed7e49327a534bf7/key/48bec33f0eaf6d4a0a05281c412366b1";
  url += "/temp/" + String(int(tempC * 10.0f));        // °C * 10
  url += "/hum/" + String(int(hum));                   // direkt %
  url += "/bar/" + String(int(pressureHpa * 10.0f));  // hPa * 10
  url += "/wspd/" + String(int(windKmH));             // km/h
  url += "/wdir/" + String(windDir);
  url += "/rain/" + String(int(rainMM * 10.0f));      // mm * 10

  HTTPClient http;
  WiFiClient client;
  http.begin(client, url);
  int httpCode = http.GET();
  Serial.println("WeatherCloud HTTP Code: " + String(httpCode));
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("WeatherCloud Response: " + payload);
  } else {
    Serial.println("WeatherCloud HTTP request failed");
  }
  http.end();
}

uint16_t aprsPasscode(String callsign) {
    callsign.toUpperCase();
    if (callsign.indexOf('-') != -1) callsign = callsign.substring(0, callsign.indexOf('-'));
    uint16_t code = 0x73e2;
    for (int i = 0; i < callsign.length(); i++) {
        if (i % 2 == 0) code ^= (callsign[i] << 8);  // çift indeks
        else          code ^= callsign[i];          // tek indeks, direkt XOR
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

void sendAprsWeather(float lat, float lon, float windSpeedMS, float windGustMS, int windDir, float rainMM, float tempF, float hum, float pressurePa)
{
    // Debug
    Serial.println("=== sendAprsWeather() input ===");
    Serial.println("windSpeedMS: " + String(windSpeedMS));
    Serial.println("windGustMS: " + String(windGustMS));
    Serial.println("windDir: " + String(windDir));
    Serial.println("rainMM: " + String(rainMM));
    Serial.println("tempF: " + String(tempF));
    Serial.println("hum: " + String(hum));
    Serial.println("pressurePa: " + String(pressurePa));
    Serial.println("==============================");

    if (!aprsClient.connect(APRS_SERVER, APRS_PORT)) {
        Serial.println("APRS sunucusuna bağlanamadı!");
        return;
    }

    // Passcode & login
    uint16_t passcode = aprsPasscode(APRS_CALLSIGN);
    String loginCmd = "user " + String(APRS_CALLSIGN) + " pass " + String(passcode) + " vers ESPWeather 1.0\n";
    aprsClient.print(loginCmd);

    // Pozisyon
    String latStr = aprsFormatLat(lat);
    String lonStr = aprsFormatLon(lon);

    // --- Dönüşümler ---
    // Rüzgar (m/s → knots)
    int windKnots = (windSpeedMS > 0) ? int(windSpeedMS * 1.94384f + 0.5f) : 0;
    int gustKnots = (windGustMS > 0) ? int(windGustMS * 1.94384f + 0.5f) : 0;

    // Sıcaklık (APRS °F ister, sensör zaten °F veriyor!)
    int tempFInt = int(tempF + 0.5f);
    if (tempFInt < -99) tempFInt = -99;
    if (tempFInt > 199) tempFInt = 199;

    // Yağmur (mm → 0.01 inch, APRS için)
    // 1 mm = 0.03937 inch → 100 * inch = 3.937 * mm
    int rainHundredthsInch = (rainMM > 0) ? int(rainMM * 3.937f + 0.5f) : 0;
    if (rainHundredthsInch > 999) rainHundredthsInch = 999;

    // Basınç (Pa → mb → APRS: bxxxxx, mb*10)
    // 101325 Pa = 1013.25 mb → b10132
    int baroVal = (pressurePa > 0) ? int((pressurePa / 100.0f) + 0.5f) : 0;
    if (baroVal < 800) baroVal = 800;
    if (baroVal > 1200) baroVal = 1200;
    baroVal *= 10; // APRS formatı ×10

    // Nem %
    int humInt = (hum >= 0) ? int(hum + 0.5f) : 0;
    if (humInt > 99) humInt = 99;

    // --- Paket oluşturma ---
    char wxBuf[256];
    sprintf(wxBuf, "!%s%s%s_", latStr.c_str(), APRS_SYMBOL_TABLE, lonStr.c_str());

    int sendWindDir = (windDir >= 0 && windDir <= 360) ? windDir : 0;

    char windBuf[32]; sprintf(windBuf, "%03d/%03dg%03d", sendWindDir, windKnots, gustKnots);
    char tempBuf[16]; sprintf(tempBuf, "t%03d", tempFInt);
    char rainBuf[16]; sprintf(rainBuf, "r%03d", rainHundredthsInch);
    char humBuf[8];   sprintf(humBuf, "h%02d", humInt);
    char baroBuf[16]; sprintf(baroBuf, "b%05d", baroVal);

    String wxPacket = String(APRS_CALLSIGN) + "-" + APRS_SSID + ">APWD01,TCPIP*:" +
                      String(wxBuf) + windBuf + tempBuf + rainBuf + humBuf + baroBuf + "\n";

    // Debug
    Serial.println("APRS Packet:");
    Serial.println(wxPacket);

    // Gönder
    aprsClient.print(wxPacket);

    // Status mesajı
    String statusMsg = String(APRS_CALLSIGN) + "-" + APRS_SSID + ">APWD01,TCPIP*:>Powered by DLS WStation\n";
    aprsClient.print(statusMsg);

    aprsClient.stop();
    Serial.println("APRS bağlantısı kapatıldı.");
}







