/**
 * ESP32 Health Monitor - Dynamically Adaptive Respiration Subsystem
 * Logic: Tracks a 'base_value' from the last 20 readings. 
 * Sets BREATH_THRESHOLD to base_value + 20.
 */

#define MIC_PIN 35

// Sampling configuration
const int sampleWindow = 50; 

// Drift Tracking & Waveform Smoothing
float dcOffset = 2048.0;       
float smoothedWaveform = 0.0;  
const float DC_ALPHA = 0.01;   
const float SMOOTH_ALPHA = 0.15; 

// Dynamic Thresholding Configuration
const int BASE_WINDOW_SIZE = 20;   // Keep track of the last 20 values
float baseValuesHistory[BASE_WINDOW_SIZE];
int baseIndex = 0;
float base_value = 0.0;

// Breath Counting Logic
const int BREATH_DEBOUNCE = 1500;  // 1.5 seconds cooldown between breaths
unsigned long lastBreathTime = 0;
int breathCount = 0;
const int rel_threshold = 20;

void setup() {
  Serial.begin(115200);
  pinMode(MIC_PIN, INPUT);
  
  // Initialize history array to 0
  for(int i = 0; i < BASE_WINDOW_SIZE; i++) {
    baseValuesHistory[i] = 0.0;
  }

  Serial.println("--- Adaptive Threshold Monitor Started ---");
  Serial.println("Format: Smoothed_Waveform,Base_Value,Threshold,Breath_Count");
}

void loop() {
  unsigned long startMillis = millis();
  unsigned long sumAbsoluteDeviation = 0;
  unsigned int sampleCount = 0;

  // 1. Collect and track raw signal variance away from DC baseline
  while (millis() - startMillis < sampleWindow) {
    int rawSample = analogRead(MIC_PIN);
    
    if (rawSample < 4095) { 
      dcOffset = (DC_ALPHA * rawSample) + ((1.0 - DC_ALPHA) * dcOffset);
      sumAbsoluteDeviation += abs(rawSample - dcOffset);
      sampleCount++;
    }
  }

  // 2. Compute the current clean volume envelope magnitude
  float currentMagnitude = (sampleCount > 0) ? (sumAbsoluteDeviation / sampleCount) : 0;
  
  // Flatten minor idle noise
  if (currentMagnitude < 30) currentMagnitude = 0;

  // 3. Smooth the output waveform
  smoothedWaveform = (SMOOTH_ALPHA * currentMagnitude) + ((1.0 - SMOOTH_ALPHA) * smoothedWaveform);

  // 4. Update the 20-sample history buffer for base_value
  baseValuesHistory[baseIndex] = smoothedWaveform;
  baseIndex = (baseIndex + 1) % BASE_WINDOW_SIZE; // Roll index over from 19 back to 0

  // Calculate the average of the previous 20 values
  float totalHistorySum = 0;
  for(int i = 0; i < BASE_WINDOW_SIZE; i++) {
    totalHistorySum += baseValuesHistory[i];
  }
  base_value = totalHistorySum / BASE_WINDOW_SIZE;

  // 5. Explicitly apply your dynamic threshold logic
  float BREATH_THRESHOLD = base_value + rel_threshold;

  // 6. Breath detection check
  if (smoothedWaveform > BREATH_THRESHOLD && (millis() - lastBreathTime > BREATH_DEBOUNCE)) {
    breathCount++;
    lastBreathTime = millis();
  }

  // --- OUTPUT FOR SERIAL PLOTTER ---
  //Serial.print(smoothedWaveform);  // Blue Line: Your perfectly clean wave
  Serial.print(",");
  Serial.print(base_value);        // Orange Line: Rolling background baseline
  Serial.print(",");
  Serial.print(BREATH_THRESHOLD);  // Green Line: Threshold line floating 20 units above base
  Serial.print(",");
  Serial.println(breathCount);     // Red Line: Step counter

  delay(5); 
}