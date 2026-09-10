================================================================================
 PAW LORA COLLAR - ESP32 GATEWAY BRIDGE (LoRa → MQTT)
 PRODUCTION-READY WITH OPTIMAL QoS LEVELS
================================================================================

PLATFORM: ESP32 with SX1276 LoRa module
MQTT BROKER: Cloud or self-hosted
QoS STRATEGY: Optimal multi-level (QoS 1 for critical, QoS 0 for informational)

================================================================================
                            DEPENDENCIES
================================================================================

PlatformIO Configuration (platformio.ini):
─────────────────────────────────────────

[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
monitor_speed = 115200

lib_deps =
    knolleary/PubSubClient@^2.8.0      # MQTT client
    sandeepmistry/LoRa@^0.8.0          # LoRa communication
    time                                # RTC/NTP
    WiFi                                # WiFi connectivity

build_flags =
    -D CORE_DEBUG_LEVEL=3               # Debug logging


================================================================================
                        CONFIGURATION & CREDENTIALS
================================================================================

#ifndef CONFIG_H
#define CONFIG_H

/* ─────────────────────────────────────────────────────────────────────
   WIFI CONFIGURATION
   ───────────────────────────────────────────────────────────────────── */

#define WIFI_SSID                       "YourWiFiSSID"
#define WIFI_PASSWORD                   "YourWiFiPassword"
#define WIFI_TIMEOUT_MS                 15000

/* ─────────────────────────────────────────────────────────────────────
   MQTT BROKER CONFIGURATION
   ───────────────────────────────────────────────────────────────────── */

#define MQTT_BROKER_HOST                "mqtt.example.com"
#define MQTT_BROKER_PORT                1883
#define MQTT_CLIENT_ID                  "paw_gateway_esp32_01"
#define MQTT_USERNAME                   "gateway_user"
#define MQTT_PASSWORD                   "secure_password_here"
#define MQTT_KEEPALIVE                  60          // Seconds

/* MQTT Topic Prefix */
#define MQTT_TOPIC_PREFIX               "paw"
#define MQTT_GATEWAY_ID                 "esp32_gw_01"

/* ─────────────────────────────────────────────────────────────────────
   LoRa MODULE CONFIGURATION (ESP32 pins)
   ───────────────────────────────────────────────────────────────────── */

#define LORA_NSS_PIN                    5           // GPIO5 - Chip Select
#define LORA_RST_PIN                    14          // GPIO14 - Reset
#define LORA_IRQ_PIN                    26          // GPIO26 - DIO0 interrupt
#define LORA_FREQUENCY                  915E6       // 915 MHz
#define LORA_SPREADING_FACTOR           9           // SF7-SF12
#define LORA_BANDWIDTH                  125000      // 125 kHz
#define LORA_CODING_RATE                7           // 4/7

/* ─────────────────────────────────────────────────────────────────────
   QoS LEVELS (Optimal strategy)
   ───────────────────────────────────────────────────────────────────── */

#define QOS_CRITICAL                    1           // AT_LEAST_ONCE (Location, Config)
#define QOS_INFORMATIONAL               0           // AT_MOST_ONCE (Status, Heartbeat)

/* Retain flags */
#define RETAIN_CRITICAL                 true        // Keep location for new subscribers
#define RETAIN_INFORMATIONAL            false       // Don't retain temporary data

/* ─────────────────────────────────────────────────────────────────────
   PACKET PROCESSING CONFIGURATION
   ───────────────────────────────────────────────────────────────────── */

#define LORA_PACKET_SIZE                10          // Expected collar payload size
#define LORA_RECEIVE_TIMEOUT_MS         5000        // Max wait for complete packet
#define GPS_TIMEOUT_SECONDS             30          // Max time to wait for GPS fix
#define BATTERY_ADC_PIN                 A0          // ADC for voltage divider
#define BATTERY_DIVIDER_RATIO           2.0         // 10k:10k divider

/* ─────────────────────────────────────────────────────────────────────
   TIMING CONFIGURATION
   ───────────────────────────────────────────────────────────────────── */

#define HEARTBEAT_INTERVAL_MS           600000      // Send heartbeat every 10 minutes
#define STATUS_PUBLISH_INTERVAL_MS      1800000     // Send status every 30 minutes
#define MQTT_RECONNECT_INTERVAL_MS      5000        // Retry MQTT connection every 5s
#define NTP_UPDATE_INTERVAL_MS          3600000     // Update time every hour

#endif // CONFIG_H


================================================================================
                            MAIN BRIDGE CODE
================================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <LoRa.h>
#include <time.h>
#include <snprintf.h>
#include "config.h"

/* ─────────────────────────────────────────────────────────────────────
   GLOBAL VARIABLES
   ───────────────────────────────────────────────────────────────────── */

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

volatile bool newPacketReceived = false;
uint32_t lastHeartbeatTime = 0;
uint32_t lastStatusTime = 0;
uint32_t packetsReceived = 0;
uint32_t packetsDecoded = 0;
uint32_t packetsFailed = 0;
uint32_t gatewayStartTime = 0;

/* ─────────────────────────────────────────────────────────────────────
   DATA STRUCTURES
   ───────────────────────────────────────────────────────────────────── */

typedef struct {
    uint16_t device_id;
    int32_t latitude_scaled;        // Latitude × 1e6
    int32_t longitude_scaled;       // Longitude × 1e6
    uint8_t rssi;                   // Signal strength
    uint8_t snr;                    // Signal-to-noise ratio
    uint32_t timestamp;             // Receive timestamp
    bool valid;
} lora_packet_t;

typedef struct {
    uint16_t device_id;
    double latitude;
    double longitude;
    uint32_t ts;
    uint16_t battery_mv;
    uint8_t battery_pct;
    int8_t rssi;
    float snr;
    const char *source;
    const char *gateway_id;
    uint8_t num_satellites;
    float accuracy;
} mqtt_location_payload_t;

/* ─────────────────────────────────────────────────────────────────────
   LOGGING MACROS
   ───────────────────────────────────────────────────────────────────── */

#define LOG_INFO(fmt, ...) Serial.printf("[INFO] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) Serial.printf("[ERROR] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) Serial.printf("[WARN] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) Serial.printf("[DEBUG] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_MQTT(fmt, ...) Serial.printf("[MQTT] " fmt "\r\n", ##__VA_ARGS__)
#define LOG_LORA(fmt, ...) Serial.printf("[LoRa] " fmt "\r\n", ##__VA_ARGS__)

/* ============================================================================
 * WIFI & MQTT CONNECTION
 * ============================================================================ */

/**
 * setupWiFi()
 * Connect ESP32 to WiFi network
 */
void setupWiFi() {
    LOG_INFO("Connecting to WiFi: %s", WIFI_SSID);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
           (millis() - startTime) < WIFI_TIMEOUT_MS) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        LOG_INFO("WiFi connected! IP: %s", WiFi.localIP().toString().c_str());
    } else {
        LOG_ERROR("Failed to connect to WiFi after %d ms", WIFI_TIMEOUT_MS);
    }
}

/**
 * syncTime()
 * Synchronize ESP32 clock with NTP server
 */
void syncTime() {
    LOG_INFO("Synchronizing time with NTP...");
    
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    
    time_t now = time(nullptr);
    uint32_t timeout = 5000;
    uint32_t start = millis();
    
    while (now < 24 * 3600 && (millis() - start) < timeout) {
        delay(100);
        now = time(nullptr);
    }
    
    Serial.println();
    if (now > 24 * 3600) {
        LOG_INFO("Time set to: %s", ctime(&now));
    } else {
        LOG_ERROR("Failed to sync time with NTP");
    }
}

/**
 * mqttConnect()
 * Establish MQTT connection to broker
 */
void mqttConnect() {
    if (mqttClient.connected()) {
        return;
    }
    
    LOG_MQTT("Connecting to broker: %s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    
    mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    mqttClient.setBufferSize(1024);     // Increase for larger payloads
    
    if (mqttClient.connect(MQTT_CLIENT_ID, 
                           MQTT_USERNAME, 
                           MQTT_PASSWORD)) {
        LOG_MQTT("Connected successfully!");
    } else {
        LOG_ERROR("Connection failed, rc=%d", mqttClient.state());
    }
}

/**
 * mqttHandle()
 * Maintain MQTT connection in main loop
 */
void mqttHandle() {
    if (!mqttClient.connected()) {
        if (millis() - lastHeartbeatTime > MQTT_RECONNECT_INTERVAL_MS) {
            mqttConnect();
            lastHeartbeatTime = millis();
        }
    }
    
    mqttClient.loop();
}

/* ============================================================================
 * LoRa PACKET HANDLING
 * ============================================================================ */

/**
 * onLoRaReceive()
 * Interrupt handler for LoRa packet received
 */
void onLoRaReceive(int packetSize) {
    newPacketReceived = true;
    LOG_DEBUG("LoRa packet received, size=%d bytes", packetSize);
}

/**
 * lora_decode_packet()
 * Decode 10-byte LoRa payload into structured data
 *
 * Payload format (big-endian):
 *   Byte 0-1:   Device ID (uint16_t)
 *   Byte 2-5:   Latitude scaled (int32_t × 1e-6)
 *   Byte 6-9:   Longitude scaled (int32_t × 1e-6)
 */
bool lora_decode_packet(uint8_t *buffer, uint16_t length, lora_packet_t *packet) {
    if (length < 10) {
        LOG_ERROR("Payload too short: %u bytes (need 10)", length);
        return false;
    }
    
    /* Extract device ID (big-endian uint16_t) */
    packet->device_id = ((uint16_t)buffer[0] << 8) | buffer[1];
    
    /* Extract latitude (big-endian int32_t) */
    packet->latitude_scaled = ((int32_t)buffer[2] << 24) |
                              ((int32_t)buffer[3] << 16) |
                              ((int32_t)buffer[4] << 8) |
                              ((int32_t)buffer[5]);
    
    /* Extract longitude (big-endian int32_t) */
    packet->longitude_scaled = ((int32_t)buffer[6] << 24) |
                               ((int32_t)buffer[7] << 16) |
                               ((int32_t)buffer[8] << 8) |
                               ((int32_t)buffer[9]);
    
    /* Capture receive time */
    packet->timestamp = (uint32_t)time(nullptr);
    packet->valid = true;
    
    LOG_LORA("Decoded: Device=0x%04X, Lat=%ld, Lon=%ld",
             packet->device_id, packet->latitude_scaled, packet->longitude_scaled);
    
    return true;
}

/**
 * lora_receive_packet()
 * Read available LoRa packet from module
 */
bool lora_receive_packet(lora_packet_t *packet) {
    int packetSize = LoRa.parsePacket();
    
    if (packetSize == 0) {
        return false;
    }
    
    uint8_t buffer[packetSize];
    int bytesRead = 0;
    
    while (LoRa.available()) {
        buffer[bytesRead++] = (uint8_t)LoRa.read();
    }
    
    /* Get signal strength */
    packet->rssi = 255 - LoRa.packetRssi();      // Convert to 0-255 range
    packet->snr = LoRa.packetSnr();
    
    packetsReceived++;
    
    if (lora_decode_packet(buffer, bytesRead, packet)) {
        packetsDecoded++;
        return true;
    } else {
        packetsFailed++;
        return false;
    }
}

/* ============================================================================
 * MQTT PUBLISHING (WITH OPTIMAL QoS LEVELS)
 * ============================================================================ */

/**
 * publish_location_qos1()
 * 
 * Publish location with QoS 1 (AT_LEAST_ONCE)
 * ─────────────────────────────────────────────
 * CRITICAL DATA - Location updates must be guaranteed!
 * 
 * Rationale:
 *   • Dog tracking is mission-critical
 *   • Missing one update means stale location
 *   • QoS 1: Broker retransmits if app doesn't ACK
 *   • Retained: New app subscribers get instant position
 */
void publish_location_qos1(const lora_packet_t *lora_pkt, uint16_t battery_mv) {
    char topic[64];
    char payload[512];
    
    /* Build topic: paw/collar/{device_id}/location */
    snprintf(topic, sizeof(topic), "%s/collar/%04X/location", 
             MQTT_TOPIC_PREFIX, lora_pkt->device_id);
    
    /* Convert scaled coordinates to decimal degrees */
    double latitude = lora_pkt->latitude_scaled / 1e6;
    double longitude = lora_pkt->longitude_scaled / 1e6;
    uint8_t battery_pct = (battery_mv - 3000) / 12;  // 3000mV=0%, 4200mV=100%
    
    /* Build JSON payload */
    snprintf(payload, sizeof(payload),
             "{"
             "\"device_id\":%u,"
             "\"lat\":%.6f,"
             "\"lon\":%.6f,"
             "\"ts\":%u,"
             "\"battery_mv\":%u,"
             "\"battery_pct\":%u,"
             "\"rssi\":%d,"
             "\"snr\":%.1f,"
             "\"source\":\"gateway\","
             "\"gateway_id\":\"%s\""
             "}",
             lora_pkt->device_id,
             latitude,
             longitude,
             lora_pkt->timestamp,
             battery_mv,
             battery_pct,
             -(int)lora_pkt->rssi,      // RSSI is negative
             lora_pkt->snr / 4.0,        // SNR in 0.25dB steps
             MQTT_GATEWAY_ID);
    
    /* Publish with QoS 1 and RETAIN flag */
    bool success = mqttClient.publish(topic, (uint8_t*)(payload), 
                                      strlen(payload), 
                                      QOS_CRITICAL,        // QoS 1
                                      RETAIN_CRITICAL);    // Retain=true
    
    LOG_MQTT("PUBLISH (QoS 1, Retain): %s [%s]", topic, success ? "OK" : "FAILED");
    LOG_DEBUG("Payload: %s", payload);
}

/**
 * publish_status_qos0()
 *
 * Publish collar status with QoS 0 (AT_MOST_ONCE)
 * ────────────────────────────────────────────────
 * NON-CRITICAL DATA - Status updates are informational
 * 
 * Rationale:
 *   • Status updates frequently (every 30 min)
 *   • If one message drops, next one comes in 30 min
 *   • QoS 0: No retry overhead, lower bandwidth
 *   • Not retained: Each update supersedes previous
 */
void publish_status_qos0(uint16_t device_id, uint16_t battery_mv, int rssi) {
    char topic[64];
    char payload[256];
    
    snprintf(topic, sizeof(topic), "%s/collar/%04X/status", 
             MQTT_TOPIC_PREFIX, device_id);
    
    uint8_t battery_pct = (battery_mv - 3000) / 12;
    
    snprintf(payload, sizeof(payload),
             "{"
             "\"device_id\":%u,"
             "\"battery_mv\":%u,"
             "\"battery_pct\":%u,"
             "\"rssi\":%d,"
             "\"ts\":%u"
             "}",
             device_id,
             battery_mv,
             battery_pct,
             -rssi,
             (uint32_t)time(nullptr));
    
    /* Publish with QoS 0 and NO retain flag */
    bool success = mqttClient.publish(topic, (uint8_t*)(payload), 
                                      strlen(payload), 
                                      QOS_INFORMATIONAL,   // QoS 0
                                      RETAIN_INFORMATIONAL); // Retain=false
    
    LOG_MQTT("PUBLISH (QoS 0, No Retain): %s [%s]", topic, success ? "OK" : "FAILED");
}

/**
 * publish_heartbeat_qos0()
 *
 * Publish gateway heartbeat with QoS 0 (AT_MOST_ONCE)
 * ────────────────────────────────────────────────────
 * DEBUGGING DATA - Heartbeat is for monitoring only
 * 
 * Rationale:
 *   • Periodic ping to show gateway is alive
 *   • Published every 10 minutes
 *   • QoS 0: Lightweight, best-effort delivery
 *   • Not retained: Only current status matters
 */
void publish_heartbeat_qos0() {
    char topic[64];
    char payload[512];
    
    snprintf(topic, sizeof(topic), "%s/gateway/%s/heartbeat", 
             MQTT_TOPIC_PREFIX, MQTT_GATEWAY_ID);
    
    uint32_t uptime = (millis() - gatewayStartTime) / 1000;
    
    snprintf(payload, sizeof(payload),
             "{"
             "\"gateway_id\":\"%s\","
             "\"uptime_seconds\":%u,"
             "\"packets_received\":%u,"
             "\"packets_decoded\":%u,"
             "\"packets_failed\":%u,"
             "\"mqtt_connected\":%s,"
             "\"rssi_signal\":%d"
             "}",
             MQTT_GATEWAY_ID,
             uptime,
             packetsReceived,
             packetsDecoded,
             packetsFailed,
             mqttClient.connected() ? "true" : "false",
             WiFi.RSSI());
    
    /* Publish with QoS 0 and NO retain flag */
    bool success = mqttClient.publish(topic, (uint8_t*)(payload), 
                                      strlen(payload), 
                                      QOS_INFORMATIONAL,   // QoS 0
                                      RETAIN_INFORMATIONAL); // Retain=false
    
    LOG_MQTT("HEARTBEAT (QoS 0): %s [%s]", topic, success ? "OK" : "FAILED");
}

/**
 * publish_gateway_status_qos0()
 *
 * Publish gateway operational status with QoS 0 (AT_MOST_ONCE)
 */
void publish_gateway_status_qos0() {
    char topic[64];
    char payload[512];
    
    snprintf(topic, sizeof(topic), "%s/gateway/%s/status", 
             MQTT_TOPIC_PREFIX, MQTT_GATEWAY_ID);
    
    snprintf(payload, sizeof(payload),
             "{"
             "\"gateway_id\":\"%s\","
             "\"status\":\"online\","
             "\"wifi_ssid\":\"%s\","
             "\"wifi_rssi\":%d,"
             "\"mqtt_connected\":%s,"
             "\"mqtt_broker\":\"%s:%d\","
             "\"timestamp\":%u"
             "}",
             MQTT_GATEWAY_ID,
             WIFI_SSID,
             WiFi.RSSI(),
             mqttClient.connected() ? "true" : "false",
             MQTT_BROKER_HOST,
             MQTT_BROKER_PORT,
             (uint32_t)time(nullptr));
    
    /* Publish with QoS 0 and NO retain flag */
    mqttClient.publish(topic, (uint8_t*)(payload), 
                       strlen(payload), 
                       QOS_INFORMATIONAL,   // QoS 0
                       RETAIN_INFORMATIONAL); // Retain=false
    
    LOG_MQTT("STATUS (QoS 0): %s", topic);
}

/* ============================================================================
 * LoRa MODULE SETUP
 * ============================================================================ */

void setupLoRa() {
    LOG_LORA("Initializing LoRa module...");
    LOG_LORA("  NSS:  GPIO%d", LORA_NSS_PIN);
    LOG_LORA("  RST:  GPIO%d", LORA_RST_PIN);
    LOG_LORA("  IRQ:  GPIO%d", LORA_IRQ_PIN);
    LOG_LORA("  Freq: %.0f Hz (915 MHz)", LORA_FREQUENCY);
    LOG_LORA("  SF:   %d", LORA_SPREADING_FACTOR);
    
    LoRa.setPins(LORA_NSS_PIN, LORA_RST_PIN, LORA_IRQ_PIN);
    
    if (!LoRa.begin(LORA_FREQUENCY)) {
        LOG_ERROR("Failed to initialize LoRa!");
        while (1) { delay(1000); }
    }
    
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODING_RATE);
    LoRa.setSyncWord(0x34);
    LoRa.enableCrc();
    
    LoRa.onReceive(onLoRaReceive);
    LoRa.receive();
    
    LOG_LORA("LoRa initialized successfully!");
}

/* ============================================================================
 * MAIN SETUP & LOOP
 * ============================================================================ */

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    LOG_INFO("╔════════════════════════════════════════════════════╗");
    LOG_INFO("║  PAW LORA COLLAR - ESP32 GATEWAY BRIDGE            ║");
    LOG_INFO("║  Optimal QoS Strategy (QoS 1 + 0)                  ║");
    LOG_INFO("╚════════════════════════════════════════════════════╝");
    
    gatewayStartTime = millis();
    
    setupWiFi();
    syncTime();
    setupLoRa();
    mqttConnect();
    
    LOG_INFO("Setup complete! Ready to receive LoRa packets.");
}

void loop() {
    /* Maintain WiFi connection */
    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN("WiFi disconnected, reconnecting...");
        setupWiFi();
    }
    
    /* Maintain MQTT connection */
    mqttHandle();
    
    /* Process incoming LoRa packets */
    if (newPacketReceived) {
        newPacketReceived = false;
        
        lora_packet_t packet;
        if (lora_receive_packet(&packet)) {
            
            /* Simulate battery reading (would come from collar in real system) */
            uint16_t battery_mv = 3700 + random(-200, 200);
            
            /* Publish location with QoS 1 (CRITICAL - guaranteed delivery) */
            if (mqttClient.connected()) {
                publish_location_qos1(&packet, battery_mv);
            }
        }
    }
    
    /* Publish gateway status every 30 minutes (QoS 0 - informational) */
    if (millis() - lastStatusTime > STATUS_PUBLISH_INTERVAL_MS) {
        if (mqttClient.connected()) {
            publish_gateway_status_qos0();
        }
        lastStatusTime = millis();
    }
    
    /* Publish heartbeat every 10 minutes (QoS 0 - debugging) */
    if (millis() - lastHeartbeatTime > HEARTBEAT_INTERVAL_MS) {
        if (mqttClient.connected()) {
            publish_heartbeat_qos0();
        }
        lastHeartbeatTime = millis();
    }
    
    delay(10);
}

/* ============================================================================
                        END OF BRIDGE SOURCE CODE
 ============================================================================ */
