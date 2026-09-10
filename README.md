# Embedded-Engineer-take-home-assignment_Saptarshi-Sen
# PAW LoRa Collar - Pet Tracking System  Real-time pet tracking using LoRa radio (5-10km range),  ESP32 gateway, and Flutter mobile app.
## ✅ What's REAL (Production-Ready)

| Component | What's Real | Status |
|-----------|------------|--------|
| Gateway | ESP32 firmware, LoRa RX, WiFi, MQTT publishing | ✅ 100% Real |
| App | Flutter UI, Google Maps, MQTT subscription | ✅ 100% Real |
| QoS | QoS 1 for location, QoS 0 for status | ✅ 100% Real |

## ⚙️ What You CONFIGURE (Not Stubbed)

- WiFi SSID & password in `gateway/config.h`
- MQTT broker address
- Google Maps API key

## ❌ What's NOT Included (Stubbed/External)

- Collar firmware (uses existing RAK3172)
- MQTT broker (install Mosquitto or use cloud)
- TLS certificates (production only)
## 🚀 Quick Start

### Gateway Setup (30 min)
1. Flash `gateway/paw_gateway.ino` to ESP32
2. Edit `gateway/config.h` with WiFi/MQTT credentials
3. Open Serial Monitor at 115200 baud
4. Should show: "Connected to WiFi" + "MQTT Connected"

### Mobile App Setup (20 min)
1. Install Flutter: https://flutter.dev
2. Run: `cd app && flutter pub get`
3. Configure Google Maps API key
4. Run: `flutter run`

### MQTT Broker Setup (5 min)
1. Option A: Docker - `docker run -d -p 1883:1883 eclipse-mosquitto`
2. Option B: Cloud - AWS IoT Core or HiveMQ Cloud
3. Update broker address in code
## ⚠️ Assumptions Made

- **Hardware**: You have ESP32 + SX1276 LoRa module (~$25)
- **WiFi**: 2.4 GHz network (ESP32 doesn't support 5 GHz)
- **MQTT**: Running locally or in cloud
- **Collar**: Using existing RAK3172 with LoRa firmware
- **Map**: You'll create free Google Maps API key
- **Range**: 5-10 km line-of-sight LoRa coverage
- **Battery**: Collar has 14-day battery life with 30-min intervals
## 🔮 Future Improvements (If More Time)

- [ ] Add cloud backend (AWS Lambda/Firebase)
- [ ] Add web dashboard (React)
- [ ] Add collar firmware code (separate repo)
- [ ] Add unit tests for gateway
- [ ] Add CI/CD pipeline
- [ ] Add Docker containerization
- [ ] Add production TLS/SSL setup
- [ ] Add database logging
- [ ] Add geofencing alerts
- [ ] Add multi-user support
- [ ] Add Android native implementation
- [ ] Add iOS native implementation
