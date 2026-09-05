# ESP32 Plane Radar
*A standalone desktop flight tracking radar displaying real-time local air traffic on an OLED screen.*

---

## 📌 Project Overview
This project is a standalone desktop air traffic radar I built using an ESP32 microcontroller and an SSD1306 OLED display. It continuously fetches live ADS-B flight data from a public aviation API, calculates the exact distance and bearing of flying aircraft relative to my current coordinates, and plots them in real time as a circular radar sweep.

---

## System & Hardware Setup

The system consists of the ESP32 handling network communications and coordinate math, coupled with an I2C OLED screen for graphic rendering.

```text
[ Live Aircraft Data (opendata.adsb.fi) ]
                  │
                  ▼ (HTTPS / JSON over Wi-Fi)
       [ ESP32 Microcontroller ]
                  │
        ┌─────────┴────────────────────────┐
        ▼                                  ▼
[ SSD1306 128x64 OLED ]          [ BOOT Button (GPIO 0) ]
- Radar rings & targets          - Short press: Change range
- Flight numbers & distance      - Long press: Reset setup
```

### Pin Connections (ESP32)
| SSD1306 OLED Pin | ESP32 Pin | Function |
| :--- | :--- | :--- |
| **VCC** | 3V3 | 3.3V Power Rail |
| **GND** | GND | Ground |
| **SDA** | GPIO 21 | I2C Data Line |
| **SCL** | GPIO 22 | I2C Clock Line |

*The onboard **BOOT button (GPIO 0)** on the ESP32 DevKit is configured as an input with internal pull-up, so no external buttons or resistors are needed.*

---

##  Firmware Implementation

### 1. Captive Portal & Credential Management
* On the first boot, the ESP32 starts an Access Point named `PlaneRadar-Setup`.
* Connecting to this AP and navigating to `192.168.4.1` opens a lightweight HTML configuration form served directly by the ESP32.
* Once the user inputs Wi-Fi SSID, password, and home Latitude/Longitude, the data is saved into flash memory using the ESP32 `Preferences` library.

### 2. Live Data Ingestion & JSON Parsing
* Every 5 seconds, the ESP32 makes an HTTPS GET request to `opendata.adsb.fi` based on the user's location and selected radar range.
* To prevent excessive memory consumption and SSL certificate maintenance overhead on a small microcontroller, `WiFiClientSecure` is used in insecure mode (`client.setInsecure()`).
* Inbound JSON flight streams are deserialized using `ArduinoJson` directly into a fixed-size buffer tracking up to 12 nearby aircraft to avoid memory leaks.

### 3. Coordinate Math & Radar Mapping
To convert global GPS coordinates into 2D display coordinates on the screen, the firmware executes three mathematical calculations for each target:

* **Haversine Distance:** Calculates the great-circle surface distance in kilometers between the radar's home coordinates and the aircraft's current GPS position:
```text
dlat = (lat2 - lat1) * (PI / 180.0)
dlon = (lon2 - lon1) * (PI / 180.0)
a = sin(dlat / 2)^2 + cos(lat1) * cos(lat2) * sin(dlon / 2)^2
Distance = 6371.0 * 2.0 * atan2(sqrt(a), sqrt(1.0 - a))
```

* **Initial Bearing:** Computes the clockwise heading angle (0 degrees = True North) from the home position to the plane:
```text
y = sin(dlon) * cos(lat2)
x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dlon)
Bearing = fmod((atan2(y, x) * (180.0 / PI) + 360.0), 360.0)
```

* **Polar-to-Screen Mapping:** Maps the calculated bearing and normalized distance onto pixel coordinates around the radar center:
```text
angle = (Bearing - 90.0) * (PI / 180.0)
radius = (Distance / radarRangeKm) * RADAR_RADIUS
px = RADAR_CENTER_X + cos(angle) * radius
py = RADAR_CENTER_Y + sin(angle) * radius
```

### 4. Interactive Range & Reset Controls
* **Short Press (< 3s):** Cycles the operational radar radius between 5 km, 10 km, 15 km, and 25 km on the fly.
* **Long Press (>= 3s):** Erases saved Wi-Fi and location settings from NVS flash and relaunches the setup captive portal.

---

## 📁 Repository Contents

```text
├── src/
│   └── main.cpp                # Core C++ firmware (Wi-Fi, API fetch, coordinate math, OLED)
├── schematics/
│   └── connection_guide.png    # Hardware pinout & wiring reference
├── photos/
│   └── radar_demo.jpg          # Running OLED radar photo
└── README.md                   # Project overview & documentation
```
## 📸 Prototype Photos


<img width="1500" height="2000" alt="radar" src="https://github.com/user-attachments/assets/9f24d8a2-bcb4-440a-b494-a64bd3da2425" />


---

## 💡 Acknowledgements & Inspiration
* Inspired by MatixYo's [ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar).
* Live ADS-B data provided by [opendata.adsb.fi](https://opendata.adsb.fi/).
