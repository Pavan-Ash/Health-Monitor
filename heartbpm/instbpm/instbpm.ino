#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 particleSensor;

#define SDA_PIN 21
#define SCL_PIN 22

int beatCount = 0;
float timee=0;
unsigned long lastPrintTime = 0;
unsigned long premillis = 0;
double timer=0;

void setup() {
  Serial.begin(115200);
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
}

void loop() {
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
    timee=millis()/60000.00;
    float bpm=beatCount/timee;
    int gap = millis()-premillis;
    premillis=millis();
    // Serial.print(">>> gap: ");
    // Serial.print(gap);
    // Serial.print("\tmins: ");
    double gapmin = gap/60000.0;
    // Serial.print(gapmin);
    // Serial.print("\tbpm: ");
    float bpm2 = (1/gapmin);
    Serial.print("Inst BPM: ");
    Serial.println(bpm2);
    // Serial.print(">>> Total Beats: ");
    // Serial.print(beatCount);
    // Serial.print("\ttime: ");
    // Serial.print(timee);
    // Serial.print("\tbpm: ");
    Serial.print("Avg BPM: ");
    Serial.println(bpm);
  }

  // 3. Status heartbeat every 1 second (so you know loop is running)
  if (millis() - lastPrintTime > 1000) {
    Serial.print("Sensor active | Raw IR: ");
    Serial.println(irValue);
    lastPrintTime = millis();
  }
}