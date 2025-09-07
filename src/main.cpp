#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_Weather_Meter_Kit_Arduino_Library.h>
#include <HTU21D.h>
#include <Adafruit_MPL3115A2.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// APRS CONFİG
#define APRS_CALLSIGN "NOCALL"
#define APRS_SSID "13"
#define APRS_LAT 37.220400
#define APRS_LON 28.343400
#define APRS_SYMBOL_TABLE "/"
#define APRS_SYMBOL "_"
#define APRS_COMMENT "DLS WStation"
#define APRS_SERVER "euro.aprs2.net"
#define APRS_PORT 14580

// I²C pinleri (ESP8266 - NodeMCU / Wemos D1 mini)
#define SDA_PIN 4 // GPIO4 → D2
#define SCL_PIN 5 // GPIO5 → D1

// Weather Meter Kit pinleri
#define WIND_SPEED_PIN 14 // GPIO14 → D5
#define RAIN_PIN 12       // GPIO12 → D6
#define WIND_DIR_PIN A0   // ADC0 → A0

WiFiClient aprsClient;

static bool isWifi = false;
static String ssid = "";
static String password = "";

// Nesneler
SFEWeatherMeterKit weather(WIND_DIR_PIN, WIND_SPEED_PIN, RAIN_PIN);
HTU21D htu;
Adafruit_MPL3115A2 mpl = Adafruit_MPL3115A2();

unsigned long lastSendMillis = 0;
const unsigned long sendInterval = 900; // 5 saniyede bir HTTP gönderimi

const int gustArraySize = 10;
float gustArray[gustArraySize] = {0};
int gustDirArray[gustArraySize] = {0};
int gustIndex = 0;

void setup()
{
  Serial.begin(115200);

  if (isWifi)
  {
    WiFi.begin(ssid, password);
    Serial.print("Wi-Fi'ye bağlanıyor");

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
    }

    Serial.println("\nWi-Fi bağlı!");
    Serial.print("IP Adresi: ");
    Serial.println(WiFi.localIP());
  }

  Wire.begin(SDA_PIN, SCL_PIN);

  // Weather kit
  weather.begin();

  // HTU21D
  if (!htu.begin())
  {
    Serial.println("HTU21D bulunamadı!");
  }

  // MPL3115A2
  if (!mpl.begin())
  {
    Serial.println("MPL3115A2 bulunamadı!");
  }
  else
  {
    mpl.setMode(MPL3115A2_BAROMETER);
    mpl.setSeaPressure(101325);
  }

  Serial.println("SparkFun Weather Kit + ESP8266 hazır!");
}

void loop()
{
  // Her saniye sensörleri oku ve gust hesapla
  float windSpeed = weather.getWindSpeed();
  int windDir = weather.getWindDirection();
  float rainMM = weather.getTotalRainfall();
  float humd = htu.readHumidity();
  float temp = htu.readTemperature();
  float pressure = mpl.getPressure();
  float alt = mpl.getAltitude();

  // Gust array güncelle
  gustArray[gustIndex] = windSpeed;
  gustDirArray[gustIndex] = windDir;
  gustIndex++;
  if (gustIndex >= gustArraySize)
    gustIndex = 0;

  // Array’den maksimumu bul
  float windGust = 0;
  int windGustDir = 0;
  for (int i = 0; i < gustArraySize; i++)
  {
    if (gustArray[i] > windGust)
    {
      windGust = gustArray[i];
      windGustDir = gustDirArray[i];
    }
  }

  // Seri yazdır
  Serial.println("---- Hava Verileri ----");
  Serial.printf("Rüzgar: %.2f m/s, Gust: %.2f, Yön: %d°\n", windSpeed, windGust, windGustDir);
  Serial.printf("Yağmur: %.2f mm\n", rainMM);
  Serial.printf("Sıcaklık: %.2f C, Nem: %.2f %%\n", temp, humd);
  Serial.printf("Basınç: %.2f Pa, İrtifa: %.2f m\n", pressure, alt);
  Serial.println("-----------------------\n");

  // HTTP gönderimi interval kontrolü
  if (isWifi && WiFi.status() == WL_CONNECTED && millis() - lastSendMillis >= (sendInterval * 1000))
  {
    lastSendMillis = millis();
    sendToWindy(windSpeed, windGust, windGustDir, rainMM, temp, humd, pressure);
    sendToWunderground(windSpeed, windGust, windGustDir, rainMM, temp, humd, pressure);
    sendToPWSWeather(windSpeed, windGust, windGustDir, rainMM, temp, humd, pressure);
    sendToWeatherCloud(windSpeed, windGust, windGustDir, rainMM, temp, humd, pressure);

    sendAprsWeather(
        APRS_LAT,         // float: konum enlem
        APRS_LON,        // float: konum boylam
        windSpeed,        // float: rüzgar hızı m/s
        windGust,         // float: rüzgar rüzgar hızı (gust) m/s
        windGustDir,      // int: rüzgar yönü
        rainMM,           // float: yağmur mm
        temp,             // float: sıcaklık °C
        humd,             // float: nem %
        pressure          // float: basınç Pa
    );
  }

  delay(1000); // loop her saniye çalışsın
}

void sendToWindy(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  String url = "https://stations.windy.com/pws/update/eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJjaSI6MjM4ODI0MSwiaWF0IjoxNzU2OTE0NjQ2fQ.LEF1adpj0y66j6x8vtMw63ilU7uoyR-OtWqawNhg6ao";
  url += "?winddir=" + String(windDir) + "&wind=" + String(windSpeed) + "&gust=" + String(windGust);
  url += "&temp=" + String(tempC) + "&rainin=" + String(rainMM) + "&mbar=" + String(pressurePa / 100.0) + "&humidity=" + String(hum);

  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  Serial.println("Windy: " + String(httpCode));
  http.end();
}

void sendToWunderground(float windSpeed, float windGust, int windDir, float rainMM, float tempC, float hum, float pressurePa)
{
  HTTPClient http;
  http.begin("https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php");
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
  http.begin("https://pwsupdate.pwsweather.com/api/v1/submitwx");
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
  http.begin(url);
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
