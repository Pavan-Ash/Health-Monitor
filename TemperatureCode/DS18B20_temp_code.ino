#include <OneWire.h>
#include <DallasTemperature.h>

// Data pin connected to ESP32 GPIO 23
#define ONE_WIRE_BUS 23

// Setup a oneWire instance to communicate with any OneWire device
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature sensor handler
DallasTemperature sensors(&oneWire);

void setup() {
  // Start the serial monitor
  Serial.begin(115200);
  
  // Start up the DallasTemperature library
  sensors.begin();

  // Print total sensors found on the bus
  int deviceCount = sensors.getDS18Count();
  Serial.print("Found ");
  Serial.print(deviceCount);
  Serial.println(" DS18B20 sensor(s) on GPIO 23.");
}

void loop() {
  // Issue a global temperature request to all devices on the bus
  sensors.requestTemperatures(); 

  // Fetch temperature in Celsius (Index 0 gets the first sensor on the wire)
  float tempC = sensors.getTempCByIndex(0);

  // Check if reading was successful
  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("Error: Could not read temperature data. Check wiring!");
  } else {
    // Convert to Fahrenheit
    float tempF = DallasTemperature::toFahrenheit(tempC);

    // Print values
    Serial.print("Temperature: ");
    Serial.print(tempC);
    Serial.print(" °C | ");
    Serial.print(tempF);
    Serial.println(" °F");
  }

  // Wait 2 seconds before next reading
  delay(1000);
}
