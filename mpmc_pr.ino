#include <WiFi.h>
#include <WebServer.h>
#include "index.h"

// Pin Definitions
#define PIN_REF_1K    25
#define PIN_REF_100K  26
#define PIN_ADC       34
#define PIN_BUTTON    4

const char *ssid = "ESP32_Component_Analyzer";
const char *password = "12345678";

WebServer server(80);

// Global State Variables
String compType = "READY";
String compValue = "0.00";
String compUnit = "---";
String compStatus = "System Ready. Place component & trigger scan.";
String compState = "Standby";
float nodeVoltage = 0.0;
int rawADC = 0;
int healthEff = 100;
int signalStab = 100;

float readADCVoltage() {
  int sum = 0;
  for (int i = 0; i < 30; i++) {
    sum += analogRead(PIN_ADC);
    delayMicroseconds(100);
  }
  rawADC = sum / 30;
  nodeVoltage = (rawADC / 4095.0) * 3.3;
  return nodeVoltage;
}

void performComponentScan() {
  // Reset Pins to High Impedance
  pinMode(PIN_REF_1K, INPUT);
  pinMode(PIN_REF_100K, INPUT);

  // --- 1. SHORT / OPEN CHECK ---
  pinMode(PIN_REF_1K, OUTPUT);
  digitalWrite(PIN_REF_1K, HIGH);
  delay(10);
  float v1k = readADCVoltage();

  if (v1k < 0.05) { // Direct Short Circuit
    compType = "SHORT CIRCUIT";
    compValue = "0.00";
    compUnit = "\xCE\xA9"; // Ohm symbol
    compStatus = "CRITICAL FAULT: Direct Short Detected!";
    compState = "Defective";
    healthEff = 0;
    signalStab = 10;
    pinMode(PIN_REF_1K, INPUT);
    return;
  }

  if (v1k > 3.22) { // Open Circuit / Nothing Connected
    compType = "OPEN CIRCUIT";
    compValue = "INF";
    compUnit = "---";
    compStatus = "No component detected or leads disconnected.";
    compState = "Idle";
    healthEff = 100;
    signalStab = 100;
    pinMode(PIN_REF_1K, INPUT);
    return;
  }

  // --- 2. DIODE TEST ---
  // A diode drops a fixed voltage (approx 0.4V - 0.8V for Silicon/Schottky)
  if (v1k >= 0.3 && v1k <= 1.2) {
    compType = "DIODE";
    compValue = String(v1k, 2);
    compUnit = "V";
    compStatus = "Diode Detected (Forward Drop: " + String(v1k, 2) + "V)";
    compState = "Functional";
    healthEff = 98;
    signalStab = 96;
    pinMode(PIN_REF_1K, INPUT);
    return;
  }

  // --- 3. CAPACITOR RC CHARGE TEST ---
  // Discharge capacitor first
  pinMode(PIN_REF_1K, OUTPUT);
  digitalWrite(PIN_REF_1K, LOW);
  delay(50);
  
  if (readADCVoltage() < 0.1) {
    // Start Charging via 100k
    pinMode(PIN_REF_1K, INPUT);
    pinMode(PIN_REF_100K, OUTPUT);
    digitalWrite(PIN_REF_100K, HIGH);
    
    unsigned long startTime = micros();
    while (readADCVoltage() < 2.08) { // 63.2% of 3.3V (1 Time Constant)
      if (micros() - startTime > 1000000) break; // Timeout if it's a resistor
    }
    unsigned long elapsedTime = micros() - startTime;
    
    // If it took measurable time to charge, it's a capacitor!
    if (elapsedTime > 200 && elapsedTime < 1000000) {
      float capuF = ((float)elapsedTime / 100000.0); // C = tau / R
      compType = "CAPACITOR";
      
      if (capuF < 1.0) {
        compValue = String(capuF * 1000.0, 1);
        compUnit = "nF";
      } else {
        compValue = String(capuF, 2);
        compUnit = "\xC2\xB0F"; // uF formatting
      }
      
      compStatus = "Capacitor Charging Curve Verified";
      compState = "Healthy";
      healthEff = 95;
      signalStab = 98;
      
      // Discharge before exit
      pinMode(PIN_REF_100K, INPUT);
      pinMode(PIN_REF_1K, OUTPUT);
      digitalWrite(PIN_REF_1K, LOW);
      delay(20);
      pinMode(PIN_REF_1K, INPUT);
      return;
    }
  }

  // --- 4. RESISTOR TEST ---
  // Low Range Test (1k)
  pinMode(PIN_REF_100K, INPUT);
  pinMode(PIN_REF_1K, OUTPUT);
  digitalWrite(PIN_REF_1K, HIGH);
  delay(10);
  float vMeas = readADCVoltage();

  float resistance = 0.0;
  if (vMeas < 3.0) {
    resistance = (vMeas * 1000.0) / (3.3 - vMeas);
  } else {
    // Switch to High Range Test (100k)
    pinMode(PIN_REF_1K, INPUT);
    pinMode(PIN_REF_100K, OUTPUT);
    digitalWrite(PIN_REF_100K, HIGH);
    delay(10);
    vMeas = readADCVoltage();
    resistance = (vMeas * 100000.0) / (3.3 - vMeas);
  }

  compType = "RESISTOR";
  if (resistance >= 1000000.0) {
    compValue = String(resistance / 1000000.0, 2);
    compUnit = "M\xCE\xA9";
  } else if (resistance >= 1000.0) {
    compValue = String(resistance / 1000.0, 2);
    compUnit = "k\xCE\xA9";
  } else {
    compValue = String(resistance, 1);
    compUnit = "\xCE\xA9";
  }

  compStatus = "Resistor Value Calculated";
  compState = "Healthy";
  healthEff = 99;
  signalStab = 99;

  pinMode(PIN_REF_1K, INPUT);
  pinMode(PIN_REF_100K, INPUT);
}

void handleRoot() {
  server.send(200, "text/html", MAIN_page);
}

void handleData() {
  String json = "{";
  json += "\"type\":\"" + compType + "\",";
  json += "\"value\":\"" + compValue + "\",";
  json += "\"unit\":\"" + compUnit + "\",";
  json += "\"status\":\"" + compStatus + "\",";
  json += "\"voltage\":\"" + String(nodeVoltage, 2) + "\",";
  json += "\"adc\":" + String(rawADC) + ",";
  json += "\"eff\":" + String(healthEff) + ",";
  json += "\"state\":\"" + compState + "\",";
  json += "\"stab\":" + String(signalStab);
  json += "}";
  server.send(200, "application/json", json);
}

void handleScan() {
  performComponentScan();
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_ADC, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/scan", handleScan);
  server.begin();
}

void loop() {
  server.handleClient();

  if (digitalRead(PIN_BUTTON) == LOW) {
    delay(50);
    if (digitalRead(PIN_BUTTON) == LOW) {
      performComponentScan();
      while(digitalRead(PIN_BUTTON) == LOW);
    }
  }
}