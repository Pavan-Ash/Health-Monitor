// ================= LIBRARIES =================
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "MAX30105.h"
#include "heartRate.h"

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

// ================= FUNCTION DECLARATIONS =================
void heart_rate();
void breath_count();
void temperature_sensor();


// ================= SETUP =================
void setup() {
  Serial.begin(115200);

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
  heart_rate();
  breath_count();
  temperature_sensor();
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
    }

    sensors.requestTemperatures();
    lastTempRequest = millis();
  }
}
