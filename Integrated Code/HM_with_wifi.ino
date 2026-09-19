// ================= LIBRARIES =================
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>

const byte DNS_PORT = 53;

// SECURED ACCESS POINT CREDENTIALS
const char* AP_SSID = "ESP32_Health_Monitor";
const char* AP_PASS = "123456789"; // Minimum 8 characters required for WPA2

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// --- STORED CONFIG VARIABLES ---
String wifi_ssid = "";
String wifi_pass = "";
String server_url = "";  // Base URL e.g. "http://192.168.1.8:5000"
String room_no = "";     // e.g. "ICU-101"

unsigned long lastPostTime = 0;
const unsigned long POST_INTERVAL_MS = 1000;

float mock_heart_rate = 85;
float mock_temp = 36.8;
float mock_resp_rate = 16.5;
String mock_abp = "120/80";
int mock_spO2 = 98;

// --- HTML PORTAL GUI WITH RESET OPTION ---
const char HTML_PORTAL[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Health Monitor Setup</title>
    <style>
        body { font-family: Arial, sans-serif; background: #0f172a; color: #f8fafc; margin: 0; padding: 20px; display: flex; justify-content: center; }
        .card { background: #1e293b; border-radius: 12px; padding: 24px; max-width: 400px; width: 100%; box-shadow: 0 10px 25px rgba(0,0,0,0.5); }
        h2 { color: #38bdf8; margin-top: 0; text-align: center; }
        label { display: block; margin-top: 14px; font-weight: bold; font-size: 0.9em; color: #94a3b8; }
        input { width: 100%; padding: 10px; margin-top: 6px; border-radius: 6px; border: 1px solid #334155; background: #0f172a; color: #fff; box-sizing: border-box; }
        input:focus { border-color: #38bdf8; outline: none; }
        button { width: 100%; margin-top: 20px; padding: 12px; background: #0284c7; border: none; border-radius: 6px; color: white; font-weight: bold; font-size: 1em; cursor: pointer; }
        button:hover { background: #0369a1; }
        .reset-btn { background: #e11d48; margin-top: 10px; }
        .reset-btn:hover { background: #be123c; }
        .footer { font-size: 0.75em; text-align: center; margin-top: 15px; color: #64748b; }
    </style>
</head>
<body>
    <div class="card">
        <h2>Health Monitor Setup</h2>
        <form action="/save" method="POST">
            <label for="ssid">Wi-Fi Name (SSID)</label>
            <input type="text" id="ssid" name="ssid" placeholder="Enter Wi-Fi SSID" required>

            <label for="pass">Wi-Fi Password</label>
            <input type="password" id="pass" name="pass" placeholder="Enter Wi-Fi Password">

            <label for="server">Server Base URL</label>
            <input type="text" id="server" name="server" placeholder="http://192.168.1.8:5000" required>

            <label for="room">Room Number</label>
            <input type="text" id="room" name="room" placeholder="ICU-101" required>

            <button type="submit">Save & Connect</button>
        </form>
        
        <form action="/reset" method="POST">
            <button type="submit" class="reset-btn">Wipe Saved Config</button>
        </form>

        <div class="footer">ESP32 Health Monitor System</div>
    </div>
</body>
</html>
)rawliteral";

// --- FUNCTION DECLARATIONS ---
void loadSavedConfig();
void setupAPAndWebServer();
void connectSTAMode();
void handleRoot();
void handleSave();
void handleReset();
void sendSensorDataHTTP();

// ================= PIN DEFINITIONS =================
#define SDA_PIN 21
#define SCL_PIN 22
#define MIC_PIN 12
#define DS18B20_PIN 23

// ================= HEART RATE (MAX30102) =================
MAX30105 particleSensor;

int beatCount = 0;
float timee = 0;
unsigned long lastPrintTime = 0;
unsigned long premillis = 0;

// ================= RESPIRATION (MIC) =================
const int sampleWindow = 50; 
float dcOffset = 2048.0;       
float smoothedWaveform = 0.0;  
const float DC_ALPHA = 0.01;   
const float SMOOTH_ALPHA = 0.15; 

const int BASE_WINDOW_SIZE = 20;   
float baseValuesHistory[BASE_WINDOW_SIZE];
int baseIndex = 0;
float base_value = 0.0;

const int BREATH_DEBOUNCE = 1500;  
unsigned long lastBreathTime = 0;
int breathCount = 0;
const int rel_threshold = 20;

// Non-blocking sampling variables
unsigned long startMillis = 0;
unsigned long sumAbsoluteDeviation = 0;
unsigned int sampleCount = 0;

// ================= TEMPERATURE (DS18B20) =================
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);

float temperatureC = 0.0;
unsigned long lastTempRequest = 0;

// ================= SENSOR FUNCTION DECLARATIONS =================
void heart_rate();
void breath_count();
void temperature_sensor();


// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  // 1. Enable dual Wi-Fi mode (Access Point + Client Station)
  WiFi.mode(WIFI_AP_STA);

  // 2. Start Secured Access Point & Web Server
  setupAPAndWebServer();

  // 3. Load stored settings from persistent flash memory
  loadSavedConfig();

  // 4. Attempt connection if credentials exist
  if (wifi_ssid.length() > 0) {
    connectSTAMode();
  } else {
    Serial.println("No saved Wi-Fi found. Awaiting input via Config AP Portal...");
  }

  // I2C Setup for MAX30102
  Wire.begin(SDA_PIN, SCL_PIN, 400000);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 was not found. Check wiring.");
    while (1);
  }

  // Sensor configuration
  byte ledBrightness = 0x24; 
  byte sampleAverage = 1;    
  byte ledMode = 2;          // Red + IR
  int sampleRate = 100;      // 100 Hz
  int pulseWidth = 411;      
  int adcRange = 4096;    

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeGreen(0);

  // Microphone Setup
  pinMode(MIC_PIN, INPUT);
  for (int i = 0; i < BASE_WINDOW_SIZE; i++) {
    baseValuesHistory[i] = 0.0;
  }

  // Temperature Sensor Setup
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();
  lastTempRequest = millis();

  Serial.println("--- Integrated Health Monitor Started ---");
  startMillis = millis();
}


// ================= MAIN LOOP =================
void loop() {
  // Always handle sensor measurement routines
  heart_rate();
  breath_count();
  temperature_sensor();

  // Always handle AP DNS and Web Server requests (allows re-configuration at any time)
  dnsServer.processNextRequest();
  server.handleClient();
  

  // Send periodic HTTP posts if connected to router Wi-Fi
  if (WiFi.status() == WL_CONNECTED) {
    if (millis() - lastPostTime >= POST_INTERVAL_MS) {
      lastPostTime = millis();
      sendSensorDataHTTP();
    }
  }
}


// ================= FUNCTIONS =================

void heart_rate() {
  long irValue = particleSensor.getIR();

  // 1. Finger Detection
  if (irValue < 10000) {
    if (millis() - lastPrintTime > 500) {
      Serial.println("Place finger on sensor...");
      lastPrintTime = millis();
    }
    return;
  }

  // 2. Check for Beat
  if (checkForBeat(irValue)) {
    beatCount++;
    timee = millis() / 60000.00;
    float bpm = beatCount / timee;
    int gap = millis() - premillis;
    premillis = millis();

    double gapmin = gap / 60000.0;
    float bpm2 = (1 / gapmin);

    Serial.print("Inst BPM: ");
    Serial.print(bpm2);
    Serial.print(" | Avg BPM: ");
    Serial.println(bpm);
    mock_heart_rate = bpm2;
  }
  
 

  // 3. Status output every 1 second
  if (millis() - lastPrintTime > 1000) {
    Serial.print("Sensor active | Raw IR: ");
    Serial.println(irValue);
    lastPrintTime = millis();
  }
}


void breath_count() {
  // Non-blocking sampling: sample continuously until 50ms window elapses
  int rawSample = analogRead(MIC_PIN);

  if (rawSample < 4095) { 
    dcOffset = (DC_ALPHA * rawSample) + ((1.0 - DC_ALPHA) * dcOffset);
    sumAbsoluteDeviation += abs(rawSample - dcOffset);
    sampleCount++;
  }

  // When 50ms has elapsed, process the frame
  if (millis() - startMillis >= sampleWindow) {
    float currentMagnitude = (sampleCount > 0) ? (sumAbsoluteDeviation / sampleCount) : 0;
    if (currentMagnitude < 30) currentMagnitude = 0;

    smoothedWaveform = (SMOOTH_ALPHA * currentMagnitude) + ((1.0 - SMOOTH_ALPHA) * smoothedWaveform);

    baseValuesHistory[baseIndex] = smoothedWaveform;
    baseIndex = (baseIndex + 1) % BASE_WINDOW_SIZE;

    float totalHistorySum = 0;
    for (int i = 0; i < BASE_WINDOW_SIZE; i++) {
      totalHistorySum += baseValuesHistory[i];
    }
    base_value = totalHistorySum / BASE_WINDOW_SIZE;

    float BREATH_THRESHOLD = base_value + rel_threshold;

    if (smoothedWaveform > BREATH_THRESHOLD && (millis() - lastBreathTime > BREATH_DEBOUNCE)) {
      breathCount++;
      lastBreathTime = millis();
      Serial.print("BREATH DETECTED! Total: ");
      Serial.println(breathCount);
    }

    // Reset window counters for the next frame
    sumAbsoluteDeviation = 0;
    sampleCount = 0;
    startMillis = millis();
  }
  mock_resp_rate = breathCount;
}


void temperature_sensor() {
  if (millis() - lastTempRequest >= 750) {
    temperatureC = sensors.getTempCByIndex(0);

    if (temperatureC == DEVICE_DISCONNECTED_C) {
      Serial.println("Error: DS18B20 disconnected!");
    } else {
      Serial.print("Temperature: ");
      Serial.print(temperatureC);
      Serial.println(" °C");
      mock_temp = temperatureC;
    }

    sensors.requestTemperatures();
    lastTempRequest = millis();
  }
}

void loadSavedConfig() {
  preferences.begin("health-cfg", true); // Read-only mode
  wifi_ssid  = preferences.getString("ssid", "");
  wifi_pass  = preferences.getString("pass", "");
  server_url = preferences.getString("server", "");
  room_no    = preferences.getString("room", "");
  preferences.end();

  Serial.println("\n--- Loaded Flash Config ---");
  Serial.println("SSID: " + wifi_ssid);
  Serial.println("Server URL: " + server_url);
  Serial.println("Room: " + room_no);
  Serial.println("---------------------------\n");
}

// ================= AP & CAPTIVE PORTAL SETUP =================
void setupAPAndWebServer() {
  // Start Password-Protected Access Point
  WiFi.softAP(AP_SSID, AP_PASS);

  // Captive Portal DNS setup
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  // Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_POST, handleReset);
  server.onNotFound(handleRoot); // Redirect all Captive Portal probes
  server.begin();

  Serial.println("\n--- Secured AP Started ---");
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("AP Password: ");
  Serial.println(AP_PASS);
  Serial.print("Portal IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("---------------------------\n");
}

void handleRoot() {
  server.send(200, "text/html", HTML_PORTAL);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("server") && server.hasArg("room")) {
    String new_ssid   = server.arg("ssid");
    String new_pass   = server.arg("pass");
    String new_server = server.arg("server");
    String new_room   = server.arg("room");

    // Clean up base URL trailing slash if present
    if (new_server.endsWith("/")) {
      new_server.remove(new_server.length() - 1);
    }

    // Write to persistent flash memory
    preferences.begin("health-cfg", false);
    preferences.putString("ssid", new_ssid);
    preferences.putString("pass", new_pass);
    preferences.putString("server", new_server);
    preferences.putString("room", new_room);
    preferences.end();

    String responseHTML = "<html><body style='font-family:sans-serif; background:#0f172a; color:#fff; text-align:center; padding-top:50px;'>"
                          "<h2>Configuration Saved!</h2>"
                          "<p>Rebooting ESP32 to connect to <b>" + new_ssid + "</b>...</p>"
                          "</body></html>";
    server.send(200, "text/html", responseHTML);

    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Bad Request: Missing required parameters");
  }
}

void handleReset() {
  preferences.begin("health-cfg", false);
  preferences.clear(); // Clear all keys stored under "health-cfg"
  preferences.end();

  String responseHTML = "<html><body style='font-family:sans-serif; background:#0f172a; color:#fff; text-align:center; padding-top:50px;'>"
                        "<h2>Memory Cleared!</h2>"
                        "<p>Erased saved Wi-Fi and Server settings. Rebooting...</p>"
                        "</body></html>";
  server.send(200, "text/html", responseHTML);

  delay(2000);
  ESP.restart();
}

// ================= STA MODE & HTTP CLIENT =================
void connectSTAMode() {
  WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(wifi_ssid);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 12000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected! IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Connection Failed! AP mode remains active for re-configuration.");
  }
}

// ================= HTTP POST SENSOR DATA =================
void sendSensorDataHTTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("HTTP Post skipped: Wi-Fi Disconnected");
    return;
  }

  HTTPClient http;

  // 1. Construct Target URL with Query Parameters
  String targetFullUrl = server_url + "/sensor?room=" + room_no;

  http.begin(targetFullUrl);
  http.addHeader("Content-Type", "application/json");

  // 2. Build JSON Body Payload
  String jsonPayload = "{";
  jsonPayload += "\"heart_rate\":" + String(mock_heart_rate) + ",";
  jsonPayload += "\"spO2\":" + String(mock_spO2) + ",";
  jsonPayload += "\"temp\":" + String(mock_temp, 2) + ",";
  jsonPayload += "\"resp_rate\":" + String(mock_resp_rate, 2) + ",";
  jsonPayload += "\"ABP\":\"" + mock_abp + "\"";
  jsonPayload += "}";

  Serial.println("\n[HTTP POST] Sending payload to: " + targetFullUrl);
  Serial.println("[HTTP POST] Body: " + jsonPayload);

  // 3. Send POST Request
  int httpResponseCode = http.POST(jsonPayload);

  if (httpResponseCode > 0) {
    String responseText = http.getString();
    Serial.print("Status Code: ");
    Serial.println(httpResponseCode);
    Serial.print("Response: ");
    Serial.println(responseText);
  } else {
    Serial.print("Error on sending POST request: ");
    Serial.println(http.errorToString(httpResponseCode).c_str());
  }

  http.end(); // Release resources
}
