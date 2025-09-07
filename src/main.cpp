#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_Weather_Meter_Kit_Arduino_Library.h>
#include <HTU21D.h>
#include <Adafruit_MPL3115A2.h>

// I²C pinleri (ESP8266 - NodeMCU / Wemos D1 mini)
#define SDA_PIN 4   // GPIO4 → D2
#define SCL_PIN 5   // GPIO5 → D1

// Weather Meter Kit pinleri
#define WIND_SPEED_PIN 14   // GPIO14 → D5
#define RAIN_PIN       12   // GPIO12 → D6
#define WIND_DIR_PIN   A0   // ADC0 → A0

// Nesneler
SFEWeatherMeterKit weather(WIND_DIR_PIN, WIND_SPEED_PIN, RAIN_PIN);
HTU21D htu;
Adafruit_MPL3115A2 mpl = Adafruit_MPL3115A2();

unsigned long lastMillis = 0;
const unsigned long interval = 5000;

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  // Weather kit
  weather.begin();

  // HTU21D
  if (!htu.begin()) {
    Serial.println("HTU21D bulunamadı!");
  }

  // MPL3115A2
  if (!mpl.begin()) {
    Serial.println("MPL3115A2 bulunamadı!");
  } else {
    mpl.setSeaPressure(101325);
  }

  Serial.println("SparkFun Weather Kit + ESP8266 hazır!");
}

void loop() {
  if (millis() - lastMillis >= interval) {
    lastMillis = millis();

    float windSpeed = weather.getWindSpeed();
    int windDir = weather.getWindDirection();
    float rainMM = weather.getTotalRainfall();

    float humd = htu.readHumidity();
    float temp = htu.readTemperature();

    float pressure = mpl.getPressure();
    float alt = mpl.getAltitude();

    Serial.println("---- Hava Verileri ----");
    Serial.printf("Rüzgar: %.2f m/s, Yön: %d°\n", windSpeed, windDir);
    Serial.printf("Yağmur: %.2f mm\n", rainMM);
    Serial.printf("Sıcaklık: %.2f C, Nem: %.2f %%\n", temp, humd);
    Serial.printf("Basınç: %.2f Pa, İrtifa: %.2f m\n", pressure, alt);
    Serial.println("-----------------------\n");
  }
}
