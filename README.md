# Wireless Component Analyzer (ESP32)

An ESP32-based automated component analyzer that measures resistors, capacitors, and diodes wirelessly via a web dashboard.

---

## 📌 Hardware Pin Diagram & Circuit Schematic

### Circuit Schematic
### Pin Mapping Table

| ESP32 Pin | Function / Description | Connection Target |
| :--- | :--- | :--- |
| **GPIO 25** | Low-Range Reference (1kΩ Drive) | Connect via **1kΩ Resistor** to Test Node |
| **GPIO 26** | High-Range / RC Charge Drive (100kΩ) | Connect via **100kΩ Resistor** to Test Node |
| **GPIO 34** | ADC Input Node (Analog Read) | Connect directly to **Test Node** |
| **GPIO 4** | Physical Scan Trigger Button | Push Button to **GND** (Internal Pullup) |
| **GND** | System Ground Reference | Connect to **DUT Ground Leg** & Button |

---

## 🛠 Features
- **Resistor Identification**: Measures 100Ω to 1MΩ dynamically.
- **Capacitance Measurement**: Calculates values using RC charge curves ($\mu\text{F}$ / $\text{nF}$).
- **Diode Testing**: Detects forward voltage drop ($V_f$).
- **Fault Detection**: Warns against short circuits ($<0.03\text{V}$) or open circuits.
