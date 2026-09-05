#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

#define OLED_SDA 21
#define OLED_SCL 22
#define BOOT_BUTTON 0
#define OLED_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define FETCH_INTERVAL_MS 5000UL
#define HOLD_CLEAR_MS 3000UL
#define AP_SSID "PlaneRadar-Setup"
#define NVS_NAMESPACE "planeradar"

struct AircraftMark {
  float bearing;
  float distance;
  String label;
};

double haversineKm(double lat1, double lon1, double lat2, double lon2);
double initialBearing(double lat1, double lon1, double lat2, double lon2);
void showMessage(const char *line1, const char *line2);
void drawRadar(const AircraftMark *marks, int markCount, const char *status);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Preferences prefs;
WebServer configServer(80);

float radarRangeKm = 10.0f;
double radarLat = 0.0;
double radarLon = 0.0;
bool configMode = false;
unsigned long lastFetchMs = 0;
unsigned long bootPressedAt = 0;
bool bootWasLow = false;
static const double DEG2RAD = M_PI / 180.0;

const int RADAR_X = 38;
const int RADAR_Y = 34;
const int RADAR_R = 27;

void startConfigPortal();
void handleConfigRoot();
void handleConfigSave();
void fetchAircraft();

double haversineKm(double lat1, double lon1, double lat2, double lon2) {
  double dlat = (lat2 - lat1) * DEG2RAD;
  double dlon = (lon2 - lon1) * DEG2RAD;
  double a = sin(dlat / 2) * sin(dlat / 2) + cos(lat1 * DEG2RAD) * cos(lat2 * DEG2RAD) * sin(dlon / 2) * sin(dlon / 2);
  return 6371.0 * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

double initialBearing(double lat1, double lon1, double lat2, double lon2) {
  double dlon = (lon2 - lon1) * DEG2RAD;
  double y = sin(dlon) * cos(lat2 * DEG2RAD);
  double x = cos(lat1 * DEG2RAD) * sin(lat2 * DEG2RAD) - sin(lat1 * DEG2RAD) * cos(lat2 * DEG2RAD) * cos(dlon);
  return fmod(atan2(y, x) / DEG2RAD + 360.0, 360.0);
}

void showMessage(const char *line1, const char *line2 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  if (line2 != nullptr) display.println(line2);
  display.display();
}

void drawRadar(const AircraftMark *marks, int markCount, const char *status) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.drawCircle(RADAR_X, RADAR_Y, RADAR_R, SSD1306_WHITE);
  display.drawCircle(RADAR_X, RADAR_Y, 18, SSD1306_WHITE);
  display.drawCircle(RADAR_X, RADAR_Y, 9, SSD1306_WHITE);
  display.drawFastHLine(RADAR_X - RADAR_R, RADAR_Y, RADAR_R * 2, SSD1306_WHITE);
  display.drawFastVLine(RADAR_X, RADAR_Y - RADAR_R, RADAR_R * 2, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(RADAR_X - 3, 0); display.print('N');
  display.setCursor(82, 0); display.print((int)radarRangeKm); display.print("km");
  display.setCursor(82, 11); display.print(markCount); display.print(" ac");
  display.setCursor(82, 23); display.print("BOOT rng");
  display.setCursor(82, 34); display.print("hold reset");

  for (int i = 0; i < markCount; ++i) {
    float angle = (marks[i].bearing - 90.0f) * DEG_TO_RAD;
    float radius = (marks[i].distance / radarRangeKm) * RADAR_R;
    int px = RADAR_X + (int)(cosf(angle) * radius);
    int py = RADAR_Y + (int)(sinf(angle) * radius);
    display.fillTriangle(px, py - 3, px - 2, py + 2, px + 2, py + 2, SSD1306_WHITE);
    int tx = constrain(px + 3, 0, 103);
    int ty = constrain(py - 4, 8, 55);
    display.setCursor(tx, ty);
    display.print(marks[i].label);
  }
  display.setCursor(0, 58);
  display.print(status);
  display.display();
}

static const char CONFIG_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Plane Radar Setup</title><style>body{font-family:sans-serif;max-width:380px;margin:32px auto;padding:0 16px}input,button{box-sizing:border-box;width:100%;padding:10px;margin:5px 0 14px;font-size:16px}button{background:#1769aa;color:white;border:0}</style></head><body><h2>Plane Radar Setup</h2><p>Enter Wi-Fi details and the latitude/longitude at the centre of your radar.</p><form method="post" action="/save"><label>Wi-Fi name</label><input name="ssid" required><label>Wi-Fi password</label><input name="pass" type="password"><label>Latitude</label><input name="lat" placeholder="52.3676" required><label>Longitude</label><input name="lon" placeholder="4.9041" required><button>Save and connect</button></form></body></html>
)HTML";

void handleConfigRoot() { configServer.send(200, "text/html", CONFIG_HTML); }

void handleConfigSave() {
  prefs.begin(NVS_NAMESPACE, false);
  prefs.putString("ssid", configServer.arg("ssid"));
  prefs.putString("pass", configServer.arg("pass"));
  prefs.putDouble("lat", configServer.arg("lat").toDouble());
  prefs.putDouble("lon", configServer.arg("lon").toDouble());
  prefs.end();
  configServer.send(200, "text/html", "<h2>Saved.</h2><p>The radar is restarting and connecting.</p>");
  delay(1200);
  ESP.restart();
}

void startConfigPortal() {
  configMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  configServer.on("/", HTTP_GET, handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.begin();
  Serial.print("Setup portal: http://");
  Serial.println(WiFi.softAPIP());
  showMessage("Join Wi-Fi:", "PlaneRadar-Setup");
}

void fetchAircraft() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  float nauticalMiles = radarRangeKm / 1.852f;
  String url = "https://opendata.adsb.fi/api/v3/lat/" + String(radarLat, 5) + "/lon/" + String(radarLon, 5) + "/dist/" + String(nauticalMiles, 1);
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(5000);
  int responseCode = http.GET();
  if (responseCode != HTTP_CODE_OK) {
    Serial.printf("ADS-B request failed: %d\n", responseCode);
    drawRadar(nullptr, 0, "Data connection error");
    http.end();
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    drawRadar(nullptr, 0, "Data format error");
    return;
  }

  AircraftMark marks[12];
  int count = 0;
  for (JsonObject plane : doc["ac"].as<JsonArray>()) {
    if (count >= 12 || !plane["lat"].is<double>() || !plane["lon"].is<double>()) continue;
    double distance = haversineKm(radarLat, radarLon, plane["lat"].as<double>(), plane["lon"].as<double>());
    if (distance > radarRangeKm) continue;
    const char *rawLabel = plane["flight"] | plane["hex"] | "AC";
    String label(rawLabel);
    label.trim();
    if (label.length() > 5) label = label.substring(0, 5);
    marks[count++] = {(float)initialBearing(radarLat, radarLon, plane["lat"].as<double>(), plane["lon"].as<double>()), (float)distance, label};
  }
  char status[22];
  snprintf(status, sizeof(status), "Wi-Fi OK: %d aircraft", count);
  drawRadar(marks, count, status);
  Serial.printf("Plotted %d aircraft within %.0f km\n", count, radarRangeKm);
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 was not found at 0x3C");
    while (true) delay(100);
  }
  showMessage("Plane Radar", "Starting...");
  prefs.begin(NVS_NAMESPACE, true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  radarLat = prefs.getDouble("lat", 0.0);
  radarLon = prefs.getDouble("lon", 0.0);
  prefs.end();
  if (savedSsid.isEmpty()) { startConfigPortal(); return; }
  showMessage("Connecting Wi-Fi...", savedSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPass.c_str());
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000UL) delay(250);
  if (WiFi.status() != WL_CONNECTED) { startConfigPortal(); return; }
  drawRadar(nullptr, 0, "Wi-Fi connected");
  lastFetchMs = millis() - FETCH_INTERVAL_MS;
}

void loop() {
  if (configMode) { configServer.handleClient(); return; }
  bool bootLow = digitalRead(BOOT_BUTTON) == LOW;
  if (bootLow && !bootWasLow) { bootPressedAt = millis(); bootWasLow = true; }
  if (!bootLow && bootWasLow) {
    unsigned long held = millis() - bootPressedAt;
    bootWasLow = false;
    if (held >= HOLD_CLEAR_MS) {
      prefs.begin(NVS_NAMESPACE, false); prefs.clear(); prefs.end();
      showMessage("Setup cleared", "Restarting..."); delay(500); ESP.restart();
    }
    if (held < HOLD_CLEAR_MS) {
      if (radarRangeKm == 5) radarRangeKm = 10;
      else if (radarRangeKm == 10) radarRangeKm = 15;
      else if (radarRangeKm == 15) radarRangeKm = 25;
      else radarRangeKm = 5;
      lastFetchMs = millis() - FETCH_INTERVAL_MS;
    }
  }
  if (millis() - lastFetchMs >= FETCH_INTERVAL_MS) {
    lastFetchMs = millis();
    fetchAircraft();
  }
}
