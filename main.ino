#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <LiquidCrystal_I2C.h>
#include <ThingSpeak.h>

// ─── Pin Definitions ────────────────────────────────────────────────────────
#define DHTPIN        4
#define DHTTYPE       DHT21
#define RELAY1_PIN    25   // Fan
#define RELAY3_PIN    26   // Pump
const int soilPin   = 34;

// ─── WiFi & ThingSpeak Credentials ──────────────────────────────────────────
const char* ssid          = "UGMURO-INET";
const char* password      = "Gepuk15000";
unsigned long tsChannelID = 3092425;       // e.g. 1234567
const char*  tsWriteKey   = "VW6L3QX7K4FX8A3U";

// ─── Objects ────────────────────────────────────────────────────────────────
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4);
AsyncWebServer server(80);
WiFiClient tsClient;

// ─── Relay State (true = ON) ─────────────────────────────────────────────────
bool relay1State = false;  // Fan
bool relay3State = false;  // Pump

// ─── Sensor Data ────────────────────────────────────────────────────────────
float temperature   = 0;
float humidity      = 0;
int   soilPercentage = 0;

// ─── Timing ──────────────────────────────────────────────────────────────────
unsigned long lastThingSpeakUpdate = 0;
const unsigned long tsInterval     = 15000;  // 15 seconds (ThingSpeak free tier min)

// ─── Relay Helper ────────────────────────────────────────────────────────────
// Active-LOW relay board: LOW = ON, HIGH = OFF
void setRelay(int pin, bool on) {
  digitalWrite(pin, on ? LOW : HIGH);
}

// ─── LCD Update ─────────────────────────────────────────────────────────────
void updateLCD() {
  lcd.clear();

  lcd.setCursor(5, 0);
  lcd.print("Monitoring");

  lcd.setCursor(0, 1);
  lcd.print("Suhu   : ");
  lcd.setCursor(9, 1);
  lcd.print(temperature, 1);
  lcd.setCursor(14, 1);
  lcd.print((char)223);  // degree symbol
  lcd.print("C");

  lcd.setCursor(0, 2);
  lcd.print("K.Udara: ");
  lcd.setCursor(9, 2);
  lcd.print(humidity, 1);
  lcd.setCursor(14, 2);
  lcd.print("%");

  lcd.setCursor(0, 3);
  lcd.print("K.Tanah: ");
  lcd.setCursor(9, 3);
  lcd.print(soilPercentage);
  lcd.setCursor(12, 3);
  lcd.print("%");
}

// ─── ThingSpeak Push ────────────────────────────────────────────────────────
void pushThingSpeak() {
  ThingSpeak.setField(1, soilPercentage);
  ThingSpeak.setField(2, temperature);
  ThingSpeak.setField(3, humidity);
  ThingSpeak.setField(4, relay1State ? 1 : 0);
  ThingSpeak.setField(5, relay3State ? 1 : 0);

  int code = ThingSpeak.writeFields(tsChannelID, tsWriteKey);
  if (code == 200) {
    Serial.println("[ThingSpeak] Update OK");
  } else {
    Serial.printf("[ThingSpeak] Error: %d\n", code);
  }
}

// ─── Sensor JSON for web ────────────────────────────────────────────────────
String getSensorJSON() {
  JsonDocument doc;
  doc["temp"]    = temperature;
  doc["hum"]     = humidity;
  doc["soil"]    = soilPercentage;
  doc["relay1"]  = relay1State;
  doc["relay3"]  = relay3State;
  String out;
  serializeJson(doc, out);
  return out;
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // --- CEGAH RELAY NYALA SAAT BOOTING ---
  // Paksa pin ke HIGH (MATI untuk Active-LOW) sebelum dijadikan OUTPUT
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY3_PIN, HIGH);
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  
  relay1State = false;
  relay3State = false;

  // LCD splash
  lcd.init(); // Jika di IDE kamu minta begin(), ganti jadi lcd.begin();
  lcd.backlight();
  lcd.setCursor(3, 0);
  lcd.print("Selamat Datang!");
  lcd.setCursor(0, 1);
  lcd.print("WS Agroteknologi IoT");
  lcd.setCursor(3, 3);
  lcd.print("-- UG MURO --");
  delay(3000);
  lcd.clear();

  // Sensors
  pinMode(soilPin, INPUT);
  dht.begin();

  // LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("[LittleFS] Mount failed!");
    return;
  }
  Serial.println("[LittleFS] Mounted");

  // WiFi
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi OK!");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP().toString());
  delay(2000);

  // ThingSpeak
  ThingSpeak.begin(tsClient);

  // ─── Web Server Routes ──────────────────────────────────────────────────

  // Serve static files from LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // GET /api/sensors — returns live sensor data as JSON
  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", getSensorJSON());
  });

  // POST /api/relay — body: {"relay":1,"state":true}
  // Menggunakan Raw Body Handler agar kompatibel 100% dengan ArduinoJson v7
  server.on("/api/relay", HTTP_POST, 
    [](AsyncWebServerRequest *request){}, 
    NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, data, len);

      if (!error) {
        int  relay = doc["relay"];
        bool state = doc["state"];

        if (relay == 1) {
          relay1State = state;
          setRelay(RELAY1_PIN, relay1State);
          Serial.printf("[Relay] Fan -> %s\n", relay1State ? "ON" : "OFF");
        } else if (relay == 3) {
          relay3State = state;
          setRelay(RELAY3_PIN, relay3State);
          Serial.printf("[Relay] Pump -> %s\n", relay3State ? "ON" : "OFF");
        }
      } else {
        Serial.println("[Server] Error parsing JSON body");
      }
      request->send(200, "application/json", getSensorJSON());
    }
  );

  server.begin();
  Serial.println("[Server] Started");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  // Read sensors
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity    = h;

  int rawSoil    = analogRead(soilPin);
  soilPercentage = map(rawSoil, 2048, 0, 0, 100);
  soilPercentage = constrain(soilPercentage, 0, 100);

  updateLCD();

  // Push to ThingSpeak every tsInterval ms
  unsigned long now = millis();
  if (now - lastThingSpeakUpdate >= tsInterval) {
    lastThingSpeakUpdate = now;
    pushThingSpeak();
  }

  delay(1000);
}
