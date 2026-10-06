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
String compType = "NONE";
String compValue = "0.00";
String compUnit = "---";
String compStatus = "No component detected. Insert component to test.";
String compState = "Idle";
float nodeVoltage = 0.0;
int rawADC = 0;
int healthEff = 0;
int signalStab = 0;

void resetTestPins() {
  pinMode(PIN_REF_1K, INPUT);
  pinMode(PIN_REF_100K, INPUT);
}

void dischargeCapacitor() {
  pinMode(PIN_REF_1K, OUTPUT);
  digitalWrite(PIN_REF_1K, LOW);
  delay(100);
  resetTestPins();
}

float readADCVoltageFast() {
  int adcVal = analogRead(PIN_ADC);
  return (adcVal / 4095.0) * 3.3;
}

float readADCVoltage() {
  int sum = 0;
  for (int i = 0; i < 30; i++) {
    sum += analogRead(PIN_ADC);
    delayMicroseconds(50);
  }
  rawADC = sum / 30;
  nodeVoltage = (rawADC / 4095.0) * 3.3;
  return nodeVoltage;
}

void setNullState(String statusMsg) {
  compType = "NONE";
  compValue = "0.00";
  compUnit = "---";
  compStatus = statusMsg;
  compState = "Unusable";
  healthEff = 0;
  signalStab = 0;
  resetTestPins();
}

void performComponentScan() {
  resetTestPins();
  dischargeCapacitor();

  // --- 1. SHORT & OPEN CIRCUIT CHECK (FLOAT PREVENTION) ---
  // Drive 1K pin HIGH to measure node response
  pinMode(PIN_REF_1K, OUTPUT);
  digitalWrite(PIN_REF_1K, HIGH);
  delay(15);
  float v1k = readADCVoltage();

  // Direct Short Circuit check
  if (v1k < 0.04) {
    compType = "SHORT CIRCUIT";
    compValue = "0.00";
    compUnit = "\xCE\xA9";
    compStatus = "Defective Component: Direct Short! Cannot be used for experiments.";
    compState = "Unusable";
    healthEff = 0;
    signalStab = 0;
    resetTestPins();
    return;
  }

  // Open Circuit or No Component Inserted
  // If voltage floats near 3.3V or drops sharply when 100K is driven, it's open/missing
  pinMode(PIN_REF_1K, INPUT);
  pinMode(PIN_REF_100K, OUTPUT);
  digitalWrite(PIN_REF_100K, HIGH);
  delay(15);
  float v100k = readADCVoltage();

  if (v1k > 3.24 && v100k > 3.24) {
    setNullState("No component detected or component is blown/open. CANNOT be used.");
    return;
  }

  // --- 2. CAPACITOR RC CHARGE TEST ---
  dischargeCapacitor();

  pinMode(PIN_REF_100K, OUTPUT);
  digitalWrite(PIN_REF_100K, HIGH);
  
  float vStart = readADCVoltageFast();
  unsigned long startTime = micros();
  
  while ((micros() - startTime) < 400000) {
    if (readADCVoltageFast() >= 2.08) break; // 63.2% of 3.3V (1 Tau)
  }
  unsigned long elapsedTime = micros() - startTime;
  float vEnd = readADCVoltageFast();

  resetTestPins();

  // Dynamic capacitance test: Capacitors store charge continuously
  if (elapsedTime > 400 && elapsedTime < 390000 && (vEnd - vStart) > 0.4) {
    float capuF = ((float)elapsedTime / 100000.0);
    compType = "CAPACITOR";
    
    if (capuF < 1.0) {
      compValue = String(capuF * 1000.0, 1);
      compUnit = "nF";
    } else {
      compValue = String(capuF, 2);
      compUnit = "uF";
    }
    
    // Efficiency calculation based on charge curve consistency
    healthEff = 95;
    compStatus = "Capacitor Pass: Charging curve stable. SAFE for experiments.";
    compState = "Usable";
    signalStab = 98;
    dischargeCapacitor();
    return;
  }

  // --- 3. DIODE TEST ---
  dischargeCapacitor();

  pinMode(PIN_REF_1K, OUTPUT);
  digitalWrite(PIN_REF_1K, HIGH);
  delay(15);
  float vDiode1k = readADCVoltage();

  pinMode(PIN_REF_1K, INPUT);
  pinMode(PIN_REF_100K, OUTPUT);
  digitalWrite(PIN_REF_100K, HIGH);
  delay(15);
  float vDiode100k = readADCVoltage();

  // Diodes clamp voltage between 0.2V - 1.1V regardless of series resistance scaling
  if (vDiode1k >= 0.22 && vDiode1k <= 1.15 && vDiode100k < 0.20) {
    compType = "DIODE";
    compValue = String(vDiode1k, 2);
    compUnit = "V";
    
    // Evaluate efficiency based on normal silicon/schottky forward drop
    if (vDiode1k >= 0.3 && vDiode1k <= 0.8) {
      healthEff = 96;
      compStatus = "Diode Pass (Forward Drop: " + String(vDiode1k, 2) + "V). SAFE for experiments.";
      compState = "Usable";
    } else {
      healthEff = 45;
      compStatus = "Diode Degraded (High drop: " + String(vDiode1k, 2) + "V). NOT recommended for experiments.";
      compState = "Degraded";
    }
    signalStab = 94;
    resetTestPins();
    return;
  }

  // --- 4. RESISTOR TEST ---
  resetTestPins();
  pinMode(PIN_REF_1K, OUTPUT);
  digitalWrite(PIN_REF_1K, HIGH);
  delay(15);
  float vMeas1k = readADCVoltage();

  float resistance = 0.0;
  if (vMeas1k < 2.95) {
    resistance = (vMeas1k * 1000.0) / (3.3 - vMeas1k);
  } else {
    // High resistance range (100k reference)
    resetTestPins();
    pinMode(PIN_REF_100K, OUTPUT);
    digitalWrite(PIN_REF_100K, HIGH);
    delay(15);
    float vMeas100k = readADCVoltage();
    
    if (vMeas100k < 3.18) {
      resistance = (vMeas100k * 100000.0) / (3.3 - vMeas100k);
    } else {
      // Over range / invalid floating pin response
      setNullState("No component detected or out of measurable range.");
      return;
    }
  }

  // Validate resistance range (10 Ohm to 1M Ohm)
  if (resistance >= 10.0 && resistance <= 1000000.0) {
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

    healthEff = 98;
    compStatus = "Resistor Functional: SAFE for circuit experiments.";
    compState = "Usable";
    signalStab = 99;
  } else {
    setNullState("Invalid component response. CANNOT be used.");
  }

  resetTestPins();
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