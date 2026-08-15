#include <OneWire.h>
#include <DallasTemperature.h>

#define MIC_PIN 12
#define DS18B20_PIN 23

// ================= TEMPERATURE =================

OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);

float temperatureC = 0.0;
unsigned long lastTempRequest = 0;

const unsigned long TEMP_INTERVAL = 1000;  // Read every 1 second


// ================= RESPIRATION =================

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


// ================= SETUP =================

void setup() {

  Serial.begin(115200);

  pinMode(MIC_PIN, INPUT);

  // Temperature sensor
  sensors.begin();

  // IMPORTANT:
  // Don't wait for DS18B20 conversion
  sensors.setWaitForConversion(false);

  // Start first temperature conversion
  sensors.requestTemperatures();
  lastTempRequest = millis();

  // Initialize history
  for (int i = 0; i < BASE_WINDOW_SIZE; i++) {
    baseValuesHistory[i] = 0.0;
  }

  Serial.println("ESP32 Health Monitor Started");
}


// ================= LOOP =================

void loop() {

  // =================================================
  // 1. RESPIRATION SAMPLING
  // =================================================

  unsigned long startMillis = millis();

  unsigned long sumAbsoluteDeviation = 0;
  unsigned int sampleCount = 0;

  while (millis() - startMillis < sampleWindow) {

    int rawSample = analogRead(MIC_PIN);

    if (rawSample < 4095) {

      dcOffset =
        (DC_ALPHA * rawSample) +
        ((1.0 - DC_ALPHA) * dcOffset);

      sumAbsoluteDeviation +=
        abs(rawSample - dcOffset);

      sampleCount++;
    }
  }


  // =================================================
  // 2. CALCULATE SIGNAL MAGNITUDE
  // =================================================

  float currentMagnitude =
    (sampleCount > 0) ?
    (sumAbsoluteDeviation / sampleCount) :
    0;

  if (currentMagnitude < 30)
    currentMagnitude = 0;


  // =================================================
  // 3. SMOOTH SIGNAL
  // =================================================

  smoothedWaveform =
    (SMOOTH_ALPHA * currentMagnitude) +
    ((1.0 - SMOOTH_ALPHA) * smoothedWaveform);


  // =================================================
  // 4. UPDATE BASE VALUE
  // =================================================

  baseValuesHistory[baseIndex] = smoothedWaveform;

  baseIndex =
    (baseIndex + 1) %
    BASE_WINDOW_SIZE;


  float totalHistorySum = 0;

  for (int i = 0; i < BASE_WINDOW_SIZE; i++) {
    totalHistorySum += baseValuesHistory[i];
  }

  base_value =
    totalHistorySum / BASE_WINDOW_SIZE;


  // =================================================
  // 5. DYNAMIC THRESHOLD
  // =================================================

  float BREATH_THRESHOLD =
    base_value + rel_threshold;


  // =================================================
  // 6. BREATH DETECTION
  // =================================================

  if (
    smoothedWaveform > BREATH_THRESHOLD &&
    millis() - lastBreathTime > BREATH_DEBOUNCE
  ) {

    breathCount++;

    lastBreathTime = millis();

    Serial.print("BREATH DETECTED! Count = ");
    Serial.println(breathCount);
  }


  // =================================================
  // 7. TEMPERATURE
  // =================================================

  // Check if previous temperature conversion is finished
  if (millis() - lastTempRequest >= 750) {

    temperatureC = sensors.getTempCByIndex(0);

    if (temperatureC == DEVICE_DISCONNECTED_C) {

      Serial.println("DS18B20 disconnected!");

    } else {

      Serial.print("Temperature: ");
      Serial.print(temperatureC);
      Serial.println(" °C");
    }

    // Start NEXT conversion
    sensors.requestTemperatures();

    lastTempRequest = millis();
  }


  // =================================================
  // 8. SERIAL OUTPUT
  // =================================================

  Serial.print("Waveform: ");
  Serial.print(smoothedWaveform);

  Serial.print(" | Base: ");
  Serial.print(base_value);

  Serial.print(" | Threshold: ");
  Serial.print(BREATH_THRESHOLD);

  Serial.print(" | Breaths: ");
  Serial.print(breathCount);

  Serial.print(" | Temp: ");
  Serial.print(temperatureC);

  Serial.println(" C");


  // Small delay only
  delay(5);
}