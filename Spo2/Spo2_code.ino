#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

#define SDA_PIN 21
#define SCL_PIN 22

MAX30105 particleSensor;

uint32_t irBuffer[100];  
uint32_t redBuffer[100]; 

int32_t bufferLength = 100; 
int32_t spo2;              
int8_t validSPO2;          
int32_t unusedHeartRate;     
int8_t unusedValidHeartRate; 

#define FINGER_THRESHOLD 40000 

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("MAX30105 not found. Check wiring/power."));
    while (1);
  }

  byte ledBrightness = 60; // ~12mA LED current
  byte sampleAverage = 4;  
  byte ledMode = 2;        // Red + IR
  byte sampleRate = 100;   
  int pulseWidth = 411;    
  int adcRange = 4096;     

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
}

void processAndPrintDiagnostics() {
  uint32_t minIR = 0xFFFFFFFF, maxIR = 0;
  uint32_t minRed = 0xFFFFFFFF, maxRed = 0;
  uint64_t sumIR = 0, sumRed = 0;

  // Analyze buffer: calculate DC (averages) and AC (peak-to-peak)
  for (int i = 0; i < bufferLength; i++) {
    sumIR += irBuffer[i];
    sumRed += redBuffer[i];

    if (irBuffer[i] < minIR) minIR = irBuffer[i];
    if (irBuffer[i] > maxIR) maxIR = irBuffer[i];

    if (redBuffer[i] < minRed) minRed = redBuffer[i];
    if (redBuffer[i] > maxRed) maxRed = redBuffer[i];
  }

  float dcIR = (float)sumIR / bufferLength;
  float dcRed = (float)sumRed / bufferLength;

  float acIR = (float)(maxIR - minIR);
  float acRed = (float)(maxRed - minRed);

  // Compute Ratio of Ratios R = (AC_red/DC_red) / (AC_ir/DC_ir)
  float ratioR = 0;
  if (dcRed > 0 && dcIR > 0 && acIR > 0) {
    ratioR = (acRed / dcRed) / (acIR / dcIR);
  }

  // Detect trend from the newest sample vs the average
  uint32_t latestIR = irBuffer[bufferLength - 1];
  
  Serial.print(F("[PULSE STATE]: "));
  if (latestIR < (dcIR - (acIR * 0.15))) {
    Serial.println(F(">>> FRESH BLOOD SURGE DETECTED (Arterial Peak / Max Absorption) <<<"));
  } else if (latestIR > (dcIR + (acIR * 0.15))) {
    Serial.println(F("--- Blood Draining (Tissue Resting / Min Absorption) ---"));
  } else {
    Serial.println(F("    Mid-wave transition..."));
  }

  // Execute standard Maxim SpO2 algorithm
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &unusedHeartRate, &unusedValidHeartRate);

  Serial.print(F(" [RAW READINGS] AC Red: ")); Serial.print(acRed, 0);
  Serial.print(F(" | DC Red: ")); Serial.print(dcRed, 0);
  Serial.print(F(" | AC IR: ")); Serial.print(acIR, 0);
  Serial.print(F(" | DC IR: ")); Serial.println(dcIR, 0);

  Serial.print(F(" [CALCULATION ] Ratio R = "));
  Serial.print(ratioR, 4);

  if (validSPO2 && spo2 >= 70 && spo2 <= 100) {
    Serial.print(F(" ===> SpO2: "));
    Serial.print(spo2);
    Serial.println(F("%"));
  } else {
    Serial.println(F(" ===> SpO2: Unstable/Recalculating..."));
  }
  Serial.println(F("--------------------------------------------------------------------------------"));
}

void loop() {
  if (particleSensor.getIR() < FINGER_THRESHOLD) {
    Serial.println(F("No finger detected. Place finger on sensor..."));
    delay(500);
    return;
  }

  Serial.println(F("Finger detected. Collecting baseline (100 samples)..."));

  for (byte i = 0; i < bufferLength; i++) {
    while (!particleSensor.available()) particleSensor.check();
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  while (1) {
    if (particleSensor.getIR() < FINGER_THRESHOLD) {
      Serial.println(F("Finger removed. Returning to idle..."));
      break; 
    }

    // Shift window by 25 samples
    for (byte i = 25; i < 100; i++) {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    // Read 25 new samples
    for (byte i = 75; i < 100; i++) {
      while (!particleSensor.available()) particleSensor.check();
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    processAndPrintDiagnostics();
  }
}
