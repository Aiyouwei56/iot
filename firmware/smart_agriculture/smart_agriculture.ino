#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>

// AgriSense canonical firmware for Modules 1-3.
// Control priority: LOW-WATER SAFETY LOCK > MANUAL > AUTO.
//
// Module 1 — Smart Irrigation Management
//   Inputs : Soil Moisture Sensor (GPIO34)
//            Humidity from DHT11
//            Weather/Rain decision from Node-RED (MQTT)
//   Outputs: Water Pump Relay (GPIO26), LCD, Dashboard alert
//
// Module 2 — Climate Control
//   Inputs : DHT11 (GPIO4), LDR (GPIO32)
//   Outputs: Fan Relay (GPIO27), Yellow LED grow indicator (GPIO18),
//            LCD, Dashboard
//
// Module 3 — Water Tank & Resource Management
//   Inputs : Analog Water Level Brick Sensor (GPIO35)
//   Outputs: Low-water alert, Pump safety lock, LCD, Dashboard
//
// Module 4 — Farm Safety & Crop Health (Laptop/Python + USB webcam)
//   ESP32 receives Module 4 status/alert via MQTT and drives:
//   Outputs: RGB LED (GPIO16/17/19), Buzzer (GPIO25)

const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
#define MQTT_BASE "smartfarm/rsd2s3g3"

// ── MQTT Topics ──────────────────────────────────────────────────────────────

// Module 1 sensors & actuator feedback
#define TOPIC_SOIL           MQTT_BASE "/module1/soil"
#define TOPIC_SOIL_PERCENT   MQTT_BASE "/module1/soil_percent"
#define TOPIC_PUMP           MQTT_BASE "/module1/pump"
#define TOPIC_PUMP_MODE      MQTT_BASE "/module1/pump/mode"

// Module 2 sensors & actuator feedback
#define TOPIC_TEMPERATURE    MQTT_BASE "/module2/temperature"
#define TOPIC_HUMIDITY       MQTT_BASE "/module2/humidity"
#define TOPIC_LIGHT          MQTT_BASE "/module2/light"
#define TOPIC_FAN            MQTT_BASE "/module2/fan"
#define TOPIC_FAN_MODE       MQTT_BASE "/module2/fan/mode"
#define TOPIC_LED            MQTT_BASE "/module2/led"
#define TOPIC_LED_MODE       MQTT_BASE "/module2/led/mode"

// Module 3 sensors
#define TOPIC_WATER          MQTT_BASE "/module3/water"
#define TOPIC_WATER_PERCENT  MQTT_BASE "/module3/water_percent"
#define TOPIC_LOW_WATER      MQTT_BASE "/module3/low_water"
#define TOPIC_PUMP_LOCK      MQTT_BASE "/module3/pump_lock"
#define TOPIC_PUMP_RUNTIME   MQTT_BASE "/module3/pump_runtime_seconds"
#define TOPIC_WATER_USAGE    MQTT_BASE "/module3/estimated_water_litres"

// Module 4 inbound (from Laptop Python)
#define TOPIC_MODULE4_STATUS MQTT_BASE "/module4/status"
#define TOPIC_MODULE4_ALERT  MQTT_BASE "/module4/alert"

// System
#define TOPIC_SYSTEM_ALERT   MQTT_BASE "/system/alert"

// Dashboard controls
#define TOPIC_CONTROL_PUMP      MQTT_BASE "/control/pump"
#define TOPIC_CONTROL_PUMP_MODE MQTT_BASE "/control/pump/mode"
#define TOPIC_CONTROL_PUMP_AUTO MQTT_BASE "/control/pump/auto"
#define TOPIC_CONTROL_FAN       MQTT_BASE "/control/fan"
#define TOPIC_CONTROL_FAN_MODE  MQTT_BASE "/control/fan/mode"
#define TOPIC_CONTROL_LED       MQTT_BASE "/control/led"
#define TOPIC_CONTROL_LED_MODE  MQTT_BASE "/control/led/mode"

// Runtime automation settings from the Dashboard
#define TOPIC_CONFIG_FAN_ON     MQTT_BASE "/config/fan/on_temperature_c"
#define TOPIC_CONFIG_FAN_OFF    MQTT_BASE "/config/fan/off_temperature_c"
#define TOPIC_CONFIG_LDR_DARK   MQTT_BASE "/config/light/dark_raw"
#define TOPIC_CONFIG_LOW_WATER  MQTT_BASE "/config/water/low_percent"
#define TOPIC_CONFIG_SOIL_ON    MQTT_BASE "/config/soil/pump_on_percent"
#define TOPIC_CONFIG_SOIL_OFF   MQTT_BASE "/config/soil/pump_off_percent"
#define TOPIC_CONFIG_PUMP_PULSE MQTT_BASE "/config/irrigation/pump_pulse_seconds"

// ── Confirmed Pin Assignment ─────────────────────────────────────────────────
// ADC1 pins are used for analog sensors so they remain compatible with ESP32 Wi-Fi.

const uint8_t SOIL_PIN        = 34;  // ADC1_CH6 — Soil moisture sensor
const uint8_t WATER_LEVEL_PIN = 35;  // ADC1_CH7 — Water level brick sensor
const uint8_t LDR_PIN         = 32;  // ADC1_CH4 — LDR light sensor
const uint8_t DHT_PIN         = 4;   // DHT11 temperature & humidity

const uint8_t PUMP_RELAY_PIN  = 26;  // Module 1 — Water pump relay
const uint8_t FAN_RELAY_PIN   = 27;  // Module 2 — Fan relay
const uint8_t GROW_LED_PIN    = 18;  // Module 2 — Yellow LED (prototype grow-light indicator)
const uint8_t RGB_RED_PIN     = 16;  // Module 4 — RGB LED red channel
const uint8_t RGB_GREEN_PIN   = 17;  // Module 4 — RGB LED green channel
const uint8_t RGB_BLUE_PIN    = 19;  // Module 4 — RGB LED blue channel
const uint8_t BUZZER_PIN      = 25;  // Module 4 — Buzzer alert

const uint8_t LCD_SDA_PIN     = 21;  // I2C SDA for 16×2 LCD
const uint8_t LCD_SCL_PIN     = 22;  // I2C SCL for 16×2 LCD
const uint8_t PREV_BUTTON_PIN = 13;  // LCD page PREV (connects GPIO to GND)
const uint8_t NEXT_BUTTON_PIN = 14;  // LCD page NEXT (connects GPIO to GND)

// ── Hardware Configuration ───────────────────────────────────────────────────

// Confirmed from physical ON/OFF behaviour: this relay board is active-HIGH.
// ON drives the selected GPIO HIGH; OFF drives it LOW.
const bool RELAYS_ACTIVE_LOW = false;

// DEVELOPMENT TEST ONLY: allow a MANUAL pump command to bypass the low-water
// lock briefly so relay/pump wiring can be tested with supervision.
// Set this to false before the final demonstration/submission.
const bool ENABLE_MANUAL_PUMP_TEST_BYPASS = false;
const unsigned long MANUAL_PUMP_TEST_MAX_MS = 10UL * 1000UL;

// Set true after confirming a common-anode RGB LED is wired.
// Leave false for common-cathode.
const bool RGB_COMMON_ANODE = false;

// ── Calibration Constants ────────────────────────────────────────────────────
// Replace these with physical dry/wet and empty/full ADC readings after
// calibrating against your actual sensors.

const int SOIL_DRY_RAW   = 3200;   // ADC reading in completely dry soil
const int SOIL_WET_RAW   = 1200;   // ADC reading in saturated soil
const int WATER_DRY_RAW  = 0;      // ADC reading when tank is empty
const int WATER_FULL_RAW = 2500;   // ADC reading when tank is full

// ── Threshold Constants ──────────────────────────────────────────────────────

const float DEFAULT_LOW_WATER_PERCENT     = 20.0f;
const float DEFAULT_SOIL_PUMP_ON_PERCENT  = 30.0f;
const float DEFAULT_SOIL_PUMP_OFF_PERCENT = 45.0f;
const float DEFAULT_FAN_ON_TEMPERATURE_C  = 30.0f;
const float DEFAULT_FAN_OFF_TEMPERATURE_C = 28.0f;
const int   DEFAULT_LDR_DARK_THRESHOLD    = 1200;
const unsigned long SENSOR_INTERVAL_MS     = 5000;
const unsigned long WATER_SAFETY_INTERVAL_MS = 1000;
const unsigned long LCD_INTERVAL_MS        = 300;
const unsigned long WEATHER_DECISION_TIMEOUT_MS = 2UL * 60UL * 1000UL;
const unsigned long BUTTON_DEBOUNCE_MS     = 50;
const unsigned long DEFAULT_AUTO_PUMP_MAX_ON_MS = 1UL * 1000UL;
const unsigned long AUTO_PUMP_BURST_ON_MS   = 500;
const unsigned long AUTO_PUMP_BURST_GAP_MS  = 2500;
const unsigned long AUTO_PUMP_SOAK_MS      = 60UL * 1000UL;

// Prototype-only flow estimate, calibrated from physical measurements:
// 3 s total ON time produced about 5 mL and 10 s produced about 15 mL.
// A conservative 1.5 mL/s estimate is used. No flow sensor is installed, so
// the dashboard must never describe this as a precise reading.
const float PUMP_ESTIMATED_FLOW_LITRES_PER_SECOND = 0.0015f;

// ── Control Mode ─────────────────────────────────────────────────────────────

enum ControlMode { AUTO_MODE, MANUAL_MODE };

// ── Objects ───────────────────────────────────────────────────────────────────

WiFiManager wifiManager;
WiFiClient  wifiClient;
PubSubClient mqtt(wifiClient);
DHT dht(DHT_PIN, DHT11);

// ── Seeed Grove 16×2 LCD (White on Blue) Driver ─────────────────────────────
// The actual project display uses the JHD1802-compatible Grove I2C protocol
// at address 0x3E. It is not a PCF8574 0x27/0x3F backpack display.

class GroveJhd1802Lcd {
 public:
  explicit GroveJhd1802Lcd(uint8_t address) : address_(address) {}

  bool begin() {
    delay(50);

    Wire.beginTransmission(address_);
    bool found = Wire.endTransmission() == 0;
    if (!found) return false;

    // Initialization sequence used by Seeed's Grove LCD library.
    command(0x28);             // 2 lines, 5×8 font
    delayMicroseconds(4500);
    command(0x28);
    delayMicroseconds(150);
    command(0x28);
    command(0x0C);             // Display ON, cursor OFF
    clear();
    command(0x06);             // Left-to-right entry mode
    return true;
  }

  void clear() {
    command(0x01);
    delayMicroseconds(2000);
  }

  void setCursor(uint8_t column, uint8_t row) {
    command(0x80 | (column + (row == 0 ? 0x00 : 0x40)));
  }

  void print(const String& text) {
    for (size_t i = 0; i < text.length(); i++) writeData((uint8_t)text[i]);
  }

 private:
  uint8_t address_;

  void send(uint8_t control, uint8_t value) {
    Wire.beginTransmission(address_);
    Wire.write(control);
    Wire.write(value);
    Wire.endTransmission();
  }

  void command(uint8_t value) { send(0x80, value); }
  void writeData(uint8_t value) { send(0x40, value); }
};

GroveJhd1802Lcd lcd(0x3E);
bool lcdReady = false;

// ── Global State ─────────────────────────────────────────────────────────────

ControlMode pumpMode = AUTO_MODE;
ControlMode fanMode  = AUTO_MODE;
ControlMode ledMode  = AUTO_MODE;

bool pumpOn   = false;
bool fanOn    = false;
bool ledOn    = false;
bool pumpLocked   = true;   // Starts locked until first valid water reading
bool lowWater     = true;

bool autoPumpRequested = false;  // Set by Node-RED weather decision
bool autoPumpDelayed   = false;  // DELAY = rain expected, defer irrigation
bool autoPumpLocalFallback = true; // Used until a fresh weather decision arrives
bool localSoilDemand = false;
bool dhtValid = false;

int   soilRaw     = 0;
int   lightRaw    = 0;
int   waterRaw    = 0;
float soilPercent  = 0;
float waterPercent = 0;
float temperatureC  = NAN;
float humidityPercent = NAN;

// Dashboard-adjustable runtime thresholds. Retained MQTT settings restore
// these values whenever the ESP32 reconnects; safe defaults apply before then.
float lowWaterThresholdPercent = DEFAULT_LOW_WATER_PERCENT;
float soilPumpOnPercent        = DEFAULT_SOIL_PUMP_ON_PERCENT;
float soilPumpOffPercent       = DEFAULT_SOIL_PUMP_OFF_PERCENT;
float fanOnTemperatureC        = DEFAULT_FAN_ON_TEMPERATURE_C;
float fanOffTemperatureC       = DEFAULT_FAN_OFF_TEMPERATURE_C;
int   ldrDarkThreshold         = DEFAULT_LDR_DARK_THRESHOLD;
unsigned long autoPumpMaxOnMs = DEFAULT_AUTO_PUMP_MAX_ON_MS;

String cropStatus   = "GREEN";    // Received from Module 4 via MQTT
String systemWarning = "";

unsigned long lastSensorAt          = 0;
unsigned long lastWaterSafetyAt     = 0;
unsigned long lastLcdAt             = 0;
unsigned long lastWeatherDecisionAt = 0;
unsigned long lastPrevChangeAt      = 0;
unsigned long lastNextChangeAt      = 0;
unsigned long manualPumpTestStartedAt = 0;
unsigned long autoPumpPulseStartedAt  = 0;
unsigned long autoPumpGapStartedAt    = 0;
unsigned long autoPumpDeliveredOnMs   = 0;
unsigned long autoPumpSoakStartedAt   = 0;
unsigned long lastWifiReconnectAttemptAt = 0;
unsigned long lastMqttReconnectAttemptAt = 0;
uint64_t pumpRuntimeMs = 0;
unsigned long pumpStartedAt = 0;

bool previousPrevReading = HIGH;
bool previousNextReading = HIGH;
uint8_t lcdPage = 0;  // 0=Temp&Hum | 1=Soil&Pump | 2=Water&Tank | 3=Crop&Status

// ── Helper: Mode Name ─────────────────────────────────────────────────────────

const char* modeName(ControlMode mode) {
  return mode == AUTO_MODE ? "AUTO" : "MANUAL";
}

bool parseNumberInRange(const String& text, float minimum, float maximum, float& result) {
  char* endPointer = nullptr;
  result = strtof(text.c_str(), &endPointer);
  return endPointer != text.c_str() && *endPointer == '\0' &&
         isfinite(result) && result >= minimum && result <= maximum;
}

// ── Helper: Calibrated Percentage ────────────────────────────────────────────
// Maps a raw ADC reading onto 0–100 % using two reference points.
// Direction-independent: works whether sensor reads high or low when full.

float calibratedPercent(int raw, int dryRaw, int fullRaw) {
  if (dryRaw == fullRaw) return 0;
  float pct = (float)(raw - dryRaw) * 100.0f / (float)(fullRaw - dryRaw);
  return constrain(pct, 0.0f, 100.0f);
}

// ── Helper: Write Digital Output ─────────────────────────────────────────────

void writeOutput(uint8_t pin, bool on, bool activeLow = false) {
  digitalWrite(pin, activeLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW));
}

// ── MQTT Publish Helpers ──────────────────────────────────────────────────────

void publishRetained(const char* topic, const char* value) {
  if (mqtt.connected()) mqtt.publish(topic, value, true);
}

void publishNumber(const char* topic, float value, uint8_t decimals = 1) {
  char payload[20];
  dtostrf(value, 1, decimals, payload);
  publishRetained(topic, payload);
}

void publishInteger(const char* topic, int value) {
  char payload[12];
  snprintf(payload, sizeof(payload), "%d", value);
  publishRetained(topic, payload);
}

uint64_t currentPumpRuntimeMs() {
  uint64_t total = pumpRuntimeMs;
  if (pumpOn) total += (uint32_t)(millis() - pumpStartedAt);
  return total;
}

void publishPumpResourceEstimate() {
  uint64_t runtimeSeconds = currentPumpRuntimeMs() / 1000ULL;
  char runtimePayload[24];
  snprintf(runtimePayload, sizeof(runtimePayload), "%llu",
           (unsigned long long)runtimeSeconds);
  publishRetained(TOPIC_PUMP_RUNTIME, runtimePayload);

  float estimatedLitres =
      (currentPumpRuntimeMs() / 1000.0f) * PUMP_ESTIMATED_FLOW_LITRES_PER_SECOND;
  publishNumber(TOPIC_WATER_USAGE, estimatedLitres, 3);
}

// ── Actuator Control Functions ────────────────────────────────────────────────
// All actuator state changes go through these functions so that logging,
// MQTT feedback, and safety-lock enforcement are always applied consistently.

bool manualPumpTestBypassActive() {
  return ENABLE_MANUAL_PUMP_TEST_BYPASS &&
         pumpMode == MANUAL_MODE && pumpLocked && pumpOn;
}

void setPump(bool requestedOn, const char* reason, bool manualTestRequest = false) {
  bool bypassLock = requestedOn && pumpLocked && manualTestRequest &&
                    ENABLE_MANUAL_PUMP_TEST_BYPASS &&
                    pumpMode == MANUAL_MODE;
  bool allowedOn = requestedOn && (!pumpLocked || bypassLock);

  if (bypassLock) {
    if (!pumpOn) manualPumpTestStartedAt = millis();
    systemWarning = "TEST MODE: LOW WATER BYPASS";
    publishRetained(TOPIC_SYSTEM_ALERT, systemWarning.c_str());
  } else if (requestedOn && pumpLocked) {
    // Attempted to turn pump on while locked — publish system alert
    systemWarning = "LOW WATER: PUMP LOCKED";
    publishRetained(TOPIC_SYSTEM_ALERT, systemWarning.c_str());
  }

  if (!allowedOn) manualPumpTestStartedAt = 0;
  bool changed = pumpOn != allowedOn;
  if (changed) {
    unsigned long now = millis();
    if (pumpOn && !allowedOn) {
      pumpRuntimeMs += (uint32_t)(now - pumpStartedAt);
    } else if (!pumpOn && allowedOn) {
      pumpStartedAt = now;
    }
    pumpOn = allowedOn;
    writeOutput(PUMP_RELAY_PIN, pumpOn, RELAYS_ACTIVE_LOW);
    Serial.printf("Pump %s (%s)\n", pumpOn ? "ON" : "OFF", reason);
    publishPumpResourceEstimate();
  }
  if (changed || manualTestRequest)
    publishRetained(TOPIC_PUMP, pumpOn ? "ON" : "OFF");
}

void setFan(bool on, const char* reason, bool forceFeedback = false) {
  bool changed = fanOn != on;
  if (changed) {
    fanOn = on;
    writeOutput(FAN_RELAY_PIN, fanOn, RELAYS_ACTIVE_LOW);
    Serial.printf("Fan %s (%s)\n", fanOn ? "ON" : "OFF", reason);
  }
  if (changed || forceFeedback)
    publishRetained(TOPIC_FAN, fanOn ? "ON" : "OFF");
}

// Module 2 grow-light: ordinary yellow LED used as a prototype indicator.
// This is NOT a real agricultural grow light; it simulates grow-light output
// for demonstration purposes only.
void setGrowIndicator(bool on, const char* reason, bool forceFeedback = false) {
  bool changed = ledOn != on;
  if (changed) {
    ledOn = on;
    writeOutput(GROW_LED_PIN, ledOn);
    Serial.printf("Prototype grow indicator %s (%s)\n", ledOn ? "ON" : "OFF", reason);
  }
  if (changed || forceFeedback)
    publishRetained(TOPIC_LED, ledOn ? "ON" : "OFF");
}

// ── RGB LED Helper ─────────────────────────────────────────────────────────────

void setRgb(bool red, bool green, bool blue) {
  writeOutput(RGB_RED_PIN,   red,   RGB_COMMON_ANODE);
  writeOutput(RGB_GREEN_PIN, green, RGB_COMMON_ANODE);
  writeOutput(RGB_BLUE_PIN,  blue,  RGB_COMMON_ANODE);
}

// ── Module 4 Status → RGB LED + Buzzer ───────────────────────────────────────
// GREEN → solid green, AMBER → yellow (red+green), RED → red + buzzer.
// Buzzer also sounds on low water regardless of crop status.

void applyCropStatus() {
  if      (cropStatus == "RED")   setRgb(true,  false, false);
  else if (cropStatus == "AMBER") setRgb(true,  true,  false);
  else                            setRgb(false, true,  false);

  // Buzzer on RED crop risk OR low water
  writeOutput(BUZZER_PIN, cropStatus == "RED" || lowWater);
}

// ── Safety Lock Logic ─────────────────────────────────────────────────────────
// Recalculates low-water state from current waterPercent.
// If tank is critically low, engages safety lock and forces pump OFF.
// If tank recovers, releases lock and clears alert.

void updateSafetyLock() {
  bool newLowWater = waterPercent < lowWaterThresholdPercent;
  if (newLowWater != lowWater) {
    lowWater = newLowWater;
    publishRetained(TOPIC_LOW_WATER, lowWater ? "TRUE" : "FALSE");
    Serial.printf("Water safety: %.1f%% -> %s\n",
                  waterPercent, lowWater ? "LOW WATER" : "LEVEL OK");
  }

  bool newLock = lowWater;
  if (newLock != pumpLocked) {
    pumpLocked = newLock;
    publishRetained(TOPIC_PUMP_LOCK, pumpLocked ? "LOCKED" : "CLEAR");
  }

  if (pumpLocked && manualPumpTestBypassActive()) {
    systemWarning = "TEST MODE: LOW WATER BYPASS";
    publishRetained(TOPIC_SYSTEM_ALERT, systemWarning.c_str());
  } else if (pumpLocked) {
    setPump(false, "low-water safety lock");
    systemWarning = "LOW WATER: PUMP LOCKED";
  } else if (systemWarning == "LOW WATER: PUMP LOCKED" ||
             systemWarning == "TEST MODE: LOW WATER BYPASS") {
    manualPumpTestStartedAt = 0;
    systemWarning = "";
    publishRetained(TOPIC_SYSTEM_ALERT, "");
  }

  applyCropStatus();
}

// Refresh the safety-critical water reading independently from the slower full
// sensor publish cycle. This lets the LCD and pump lock recover within 1 second
// after the sensor rises above the configured threshold, even while MQTT is offline.
void refreshWaterSafetyReading() {
  waterRaw = analogRead(WATER_LEVEL_PIN);
  waterPercent = calibratedPercent(waterRaw, WATER_DRY_RAW, WATER_FULL_RAW);
  updateSafetyLock();
}

// ── Automatic Control Rules ───────────────────────────────────────────────────
// Only runs for actuators currently in AUTO_MODE.
// Safety lock always takes precedence over AUTO decisions.
// MANUAL mode actuators are NOT touched here — manual command holds.

void updateLocalSoilDemand() {
  if (soilPumpOffPercent <= soilPumpOnPercent) return;
  if (soilPercent < soilPumpOnPercent)
    localSoilDemand = true;
  else if (soilPercent >= soilPumpOffPercent)
    localSoilDemand = false;
}

void cancelAutoPumpCycle() {
  autoPumpPulseStartedAt = 0;
  autoPumpGapStartedAt = 0;
  autoPumpDeliveredOnMs = 0;
  autoPumpSoakStartedAt = 0;
}

void applyAutomaticControl() {
  // Module 1 — Irrigation AUTO rule
  if (pumpMode == AUTO_MODE) {
    bool decisionFresh = lastWeatherDecisionAt > 0 &&
      millis() - lastWeatherDecisionAt <= WEATHER_DECISION_TIMEOUT_MS;
    bool soaking = autoPumpSoakStartedAt > 0 &&
      millis() - autoPumpSoakStartedAt < AUTO_PUMP_SOAK_MS;
    bool microPulseActive = autoPumpPulseStartedAt > 0 || autoPumpGapStartedAt > 0;

    bool useLocalFallback = autoPumpLocalFallback || !decisionFresh;
    if (!useLocalFallback && systemWarning == "WEATHER OFFLINE: SOIL FALLBACK") {
      systemWarning = "";
      publishRetained(TOPIC_SYSTEM_ALERT, "");
    }

    if (pumpLocked) {
      cancelAutoPumpCycle();
      setPump(false, "low-water AUTO lock");
    } else if (useLocalFallback) {
      // Weather/Node-RED unavailable: keep the plant watered from the local
      // soil sensor, while retaining the low-water lock and pulse limits.
      updateLocalSoilDemand();
      if (systemWarning == "" || systemWarning == "WEATHER DATA STALE") {
        systemWarning = "WEATHER OFFLINE: SOIL FALLBACK";
        publishRetained(TOPIC_SYSTEM_ALERT, systemWarning.c_str());
      }
      if (!localSoilDemand) {
        cancelAutoPumpCycle();
        setPump(false, "local soil fallback stop");
      } else if (!pumpOn && !soaking && !microPulseActive) {
        setPump(true, "local soil fallback micro-pulse");
        if (pumpOn) autoPumpPulseStartedAt = millis();
      }
    } else if (autoPumpDelayed || !autoPumpRequested) {
      cancelAutoPumpCycle();
      setPump(false, autoPumpDelayed ? "rain delay" : "soil moisture AUTO stop");
    } else if (!pumpOn && !soaking && !microPulseActive) {
      setPump(true, "AUTO irrigation micro-pulse");
      if (pumpOn) autoPumpPulseStartedAt = millis();
    }
  }

  // Module 2 — Fan AUTO rule (hysteresis)
  if (fanMode == AUTO_MODE && dhtValid && fanOnTemperatureC > fanOffTemperatureC) {
    if (!fanOn && temperatureC >= fanOnTemperatureC)
      setFan(true, "temperature AUTO rule");
    else if (fanOn && temperatureC <= fanOffTemperatureC)
      setFan(false, "temperature AUTO hysteresis");
  }

  // Module 2 — Grow light indicator AUTO rule
  if (ledMode == AUTO_MODE) {
    setGrowIndicator(lightRaw < ldrDarkThreshold, "LDR AUTO rule");
  }
}

// AUTO irrigation uses non-blocking micro-pulses. The default 1-second water
// budget is delivered as 0.5 s ON, 2.5 s OFF, 0.5 s ON. A 60-second soil soak
// follows before the latest soil/weather demand is evaluated again.
void updateAutoPumpCycle() {
  if (pumpMode != AUTO_MODE) return;

  unsigned long now = millis();
  unsigned long remainingOnMs = autoPumpMaxOnMs > autoPumpDeliveredOnMs
    ? autoPumpMaxOnMs - autoPumpDeliveredOnMs
    : 0;
  unsigned long currentBurstTargetMs = min(AUTO_PUMP_BURST_ON_MS, remainingOnMs);

  if (pumpOn && autoPumpPulseStartedAt > 0 && currentBurstTargetMs > 0 &&
      now - autoPumpPulseStartedAt >= currentBurstTargetMs) {
    unsigned long deliveredThisBurst = now - autoPumpPulseStartedAt;
    if (deliveredThisBurst > currentBurstTargetMs)
      deliveredThisBurst = currentBurstTargetMs;
    autoPumpDeliveredOnMs += deliveredThisBurst;
    setPump(false, "AUTO micro-pulse pause");
    autoPumpPulseStartedAt = 0;
    if (autoPumpDeliveredOnMs >= autoPumpMaxOnMs) {
      autoPumpDeliveredOnMs = 0;
      autoPumpGapStartedAt = 0;
      autoPumpSoakStartedAt = now;
      Serial.printf("AUTO pump: %lu ms total ON time delivered; soaking for 60 seconds.\n",
                    autoPumpMaxOnMs);
    } else {
      autoPumpGapStartedAt = now;
      Serial.printf("AUTO pump: 500 ms burst complete; pausing 2500 ms (%lu ms remaining).\n",
                    autoPumpMaxOnMs - autoPumpDeliveredOnMs);
    }
    return;
  }

  if (!pumpOn && autoPumpGapStartedAt > 0 &&
      now - autoPumpGapStartedAt >= AUTO_PUMP_BURST_GAP_MS) {
    autoPumpGapStartedAt = 0;
    applyAutomaticControl();
    return;
  }

  if (autoPumpSoakStartedAt > 0 &&
      now - autoPumpSoakStartedAt >= AUTO_PUMP_SOAK_MS) {
    autoPumpSoakStartedAt = 0;
    Serial.println("AUTO pump: soak period complete, re-evaluating demand.");
    applyAutomaticControl();
  }
}

// ── Parse Mode String ─────────────────────────────────────────────────────────

ControlMode parseMode(const String& value, ControlMode fallback) {
  if (value == "AUTO")   return AUTO_MODE;
  if (value == "MANUAL") return MANUAL_MODE;
  return fallback;
}

// ── MQTT Callback ─────────────────────────────────────────────────────────────
// Handles all inbound messages.  Exception handling: unknown topics and
// malformed payloads are silently discarded — the system never halts.
// AUTO/MANUAL separation: direct ON/OFF commands are only accepted in MANUAL
// mode; in AUTO mode a warning is published instead of overwriting relay state.

void mqttCallback(char* topicChars, byte* payload, unsigned int length) {
  String topic(topicChars);
  String value;
  for (unsigned int i = 0; i < length; i++) value += (char)payload[i];
  value.trim();
  value.toUpperCase();

  // ── Mode switching ─────────────────────────────────────────────────────────
  if (topic == TOPIC_CONTROL_PUMP_MODE) {
    pumpMode = parseMode(value, pumpMode);
    publishRetained(TOPIC_PUMP_MODE, modeName(pumpMode));
    if (pumpMode == AUTO_MODE) {
      applyAutomaticControl();
    } else {
      cancelAutoPumpCycle();
      setPump(false, "MANUAL mode safe stop", true);
    }
  }
  else if (topic == TOPIC_CONTROL_FAN_MODE) {
    fanMode = parseMode(value, fanMode);
    publishRetained(TOPIC_FAN_MODE, modeName(fanMode));
    if (fanMode == AUTO_MODE) applyAutomaticControl();
  }
  else if (topic == TOPIC_CONTROL_LED_MODE) {
    ledMode = parseMode(value, ledMode);
    publishRetained(TOPIC_LED_MODE, modeName(ledMode));
    if (ledMode == AUTO_MODE) applyAutomaticControl();
  }

  // ── Dashboard-adjustable AUTO thresholds ─────────────────────────────────
  else if (topic == TOPIC_CONFIG_LOW_WATER) {
    float parsed;
    if (parseNumberInRange(value, 5.0f, 50.0f, parsed)) {
      lowWaterThresholdPercent = parsed;
      Serial.printf("Setting updated: low-water lock below %.1f%%\n", parsed);
      updateSafetyLock();
    }
  }
  else if (topic == TOPIC_CONFIG_SOIL_ON) {
    float parsed;
    if (parseNumberInRange(value, 5.0f, 60.0f, parsed)) {
      soilPumpOnPercent = parsed;
      Serial.printf("Setting updated: local fallback pump ON below %.1f%%\n", parsed);
      updateLocalSoilDemand();
      if (pumpMode == AUTO_MODE) applyAutomaticControl();
    }
  }
  else if (topic == TOPIC_CONFIG_SOIL_OFF) {
    float parsed;
    if (parseNumberInRange(value, 10.0f, 90.0f, parsed)) {
      soilPumpOffPercent = parsed;
      Serial.printf("Setting updated: local fallback pump OFF at %.1f%%\n", parsed);
      updateLocalSoilDemand();
      if (pumpMode == AUTO_MODE) applyAutomaticControl();
    }
  }
  else if (topic == TOPIC_CONFIG_PUMP_PULSE) {
    float parsed;
    if (parseNumberInRange(value, 0.5f, 10.0f, parsed)) {
      autoPumpMaxOnMs = (unsigned long)lroundf(parsed * 1000.0f);
      Serial.printf("Setting updated: AUTO pump total ON time %.1f second(s)\n", parsed);
      if (pumpMode == AUTO_MODE) {
        cancelAutoPumpCycle();
        setPump(false, "AUTO pulse setting changed");
        applyAutomaticControl();
      }
    }
  }
  else if (topic == TOPIC_CONFIG_FAN_ON) {
    float parsed;
    if (parseNumberInRange(value, 15.0f, 50.0f, parsed)) {
      fanOnTemperatureC = parsed;
      Serial.printf("Setting updated: fan ON at %.1f C\n", parsed);
      if (fanMode == AUTO_MODE) applyAutomaticControl();
    }
  }
  else if (topic == TOPIC_CONFIG_FAN_OFF) {
    float parsed;
    if (parseNumberInRange(value, 10.0f, 45.0f, parsed)) {
      fanOffTemperatureC = parsed;
      Serial.printf("Setting updated: fan OFF at %.1f C\n", parsed);
      if (fanMode == AUTO_MODE) applyAutomaticControl();
    }
  }
  else if (topic == TOPIC_CONFIG_LDR_DARK) {
    float parsed;
    if (parseNumberInRange(value, 0.0f, 4095.0f, parsed)) {
      ldrDarkThreshold = (int)lroundf(parsed);
      Serial.printf("Setting updated: grow indicator ON below ADC %d\n", ldrDarkThreshold);
      if (ledMode == AUTO_MODE) applyAutomaticControl();
    }
  }

  // ── Node-RED weather/irrigation decision ───────────────────────────────────
  // Payload: ON/OFF/DELAY when weather is available; LOCAL asks the ESP32 to
  // fall back to its soil sensor because Node-RED or the weather API is stale.
  else if (topic == TOPIC_CONTROL_PUMP_AUTO &&
           (value == "ON" || value == "OFF" || value == "DELAY" || value == "LOCAL")) {
    autoPumpRequested = (value == "ON");
    autoPumpDelayed   = (value == "DELAY");
    autoPumpLocalFallback = (value == "LOCAL");
    lastWeatherDecisionAt = millis();
    if (systemWarning == "WEATHER DATA STALE") {
      systemWarning = "";
      publishRetained(TOPIC_SYSTEM_ALERT, "");
    }
    if (pumpMode == AUTO_MODE) applyAutomaticControl();
  }

  // ── Direct actuator commands (MANUAL mode only) ────────────────────────────
  else if (topic == TOPIC_CONTROL_PUMP && (value == "ON" || value == "OFF")) {
    if (pumpMode == MANUAL_MODE)
      setPump(value == "ON", "dashboard MANUAL command", true);
    else
      publishRetained(TOPIC_SYSTEM_ALERT, "PUMP COMMAND IGNORED: NOT MANUAL");
  }
  else if (topic == TOPIC_CONTROL_FAN && (value == "ON" || value == "OFF")) {
    if (fanMode == MANUAL_MODE)
      setFan(value == "ON", "dashboard MANUAL command", true);
    else
      publishRetained(TOPIC_SYSTEM_ALERT, "FAN COMMAND IGNORED: NOT MANUAL");
  }
  else if (topic == TOPIC_CONTROL_LED && (value == "ON" || value == "OFF")) {
    if (ledMode == MANUAL_MODE)
      setGrowIndicator(value == "ON", "dashboard MANUAL command", true);
    else
      publishRetained(TOPIC_SYSTEM_ALERT, "LED COMMAND IGNORED: NOT MANUAL");
  }

  // ── Module 4 inbound (from Laptop Python / Node-RED) ──────────────────────
  else if (topic == TOPIC_MODULE4_STATUS &&
           (value == "GREEN" || value == "AMBER" || value == "RED")) {
    cropStatus = value;
    applyCropStatus();
  }
  else if (topic == TOPIC_MODULE4_ALERT) {
    systemWarning = value;  // Will appear on LCD page 3
  }
}

// ── MQTT Subscription & Reconnect ────────────────────────────────────────────

void subscribeTopics() {
  mqtt.subscribe(TOPIC_CONTROL_PUMP);
  mqtt.subscribe(TOPIC_CONTROL_PUMP_MODE);
  mqtt.subscribe(TOPIC_CONTROL_PUMP_AUTO);
  mqtt.subscribe(TOPIC_CONTROL_FAN);
  mqtt.subscribe(TOPIC_CONTROL_FAN_MODE);
  mqtt.subscribe(TOPIC_CONTROL_LED);
  mqtt.subscribe(TOPIC_CONTROL_LED_MODE);
  mqtt.subscribe(TOPIC_CONFIG_FAN_ON);
  mqtt.subscribe(TOPIC_CONFIG_FAN_OFF);
  mqtt.subscribe(TOPIC_CONFIG_LDR_DARK);
  mqtt.subscribe(TOPIC_CONFIG_LOW_WATER);
  mqtt.subscribe(TOPIC_CONFIG_SOIL_ON);
  mqtt.subscribe(TOPIC_CONFIG_SOIL_OFF);
  mqtt.subscribe(TOPIC_CONFIG_PUMP_PULSE);
  mqtt.subscribe(TOPIC_MODULE4_STATUS);
  mqtt.subscribe(TOPIC_MODULE4_ALERT);
}

bool connectMqtt() {
  // Use MAC-derived client ID for uniqueness (avoids broker kick-out).
  String clientId = "AgriSense-ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(clientId.c_str())) {
    subscribeTopics();
    // Re-publish retained actuator and mode state so a reboot cannot leave
    // stale ON/OFF feedback on the dashboard.
    publishRetained(TOPIC_PUMP,       pumpOn ? "ON" : "OFF");
    publishRetained(TOPIC_FAN,        fanOn  ? "ON" : "OFF");
    publishRetained(TOPIC_LED,        ledOn  ? "ON" : "OFF");
    publishRetained(TOPIC_PUMP_MODE,  modeName(pumpMode));
    publishRetained(TOPIC_FAN_MODE,   modeName(fanMode));
    publishRetained(TOPIC_LED_MODE,   modeName(ledMode));
    publishRetained(TOPIC_PUMP_LOCK,  pumpLocked ? "LOCKED" : "CLEAR");
    publishPumpResourceEstimate();
    Serial.println("MQTT connected and topics subscribed.");
    return true;
  }

  Serial.printf("MQTT failed, state=%d — local controls remain active.\n", mqtt.state());
  return false;
}

// ── Sensor Read & Publish ────────────────────────────────────────────────────
// Input validation: DHT readings outside physical range are rejected.
// Water-level percentage drives the safety lock on every cycle.

void readAndPublishSensors() {
  soilRaw   = analogRead(SOIL_PIN);
  waterRaw  = analogRead(WATER_LEVEL_PIN);
  lightRaw  = analogRead(LDR_PIN);

  temperatureC  = dht.readTemperature();
  humidityPercent = dht.readHumidity();

  // Validate DHT11 output; reject NaN and physically impossible values
  dhtValid = !isnan(temperatureC) && !isnan(humidityPercent) &&
             temperatureC >= -40 && temperatureC <= 85 &&
             humidityPercent >= 0 && humidityPercent <= 100;

  soilPercent  = calibratedPercent(soilRaw,  SOIL_DRY_RAW,  SOIL_WET_RAW);
  waterPercent = calibratedPercent(waterRaw, WATER_DRY_RAW, WATER_FULL_RAW);

  // ── Publish sensor values ──────────────────────────────────────────────────
  publishInteger(TOPIC_SOIL, soilRaw);
  publishNumber(TOPIC_SOIL_PERCENT, soilPercent);
  publishInteger(TOPIC_WATER, waterRaw);
  publishNumber(TOPIC_WATER_PERCENT, waterPercent);
  publishPumpResourceEstimate();
  publishInteger(TOPIC_LIGHT, lightRaw);

  if (dhtValid) {
    publishNumber(TOPIC_TEMPERATURE, temperatureC);
    publishNumber(TOPIC_HUMIDITY, humidityPercent);
  } else {
    // DHT read failed — publish alert but do NOT restart; system continues
    publishRetained(TOPIC_SYSTEM_ALERT, "DHT11 INVALID READING");
    Serial.println("WARNING: DHT11 invalid reading — skipping publish.");
  }

  // ── Update safety lock, then apply automatic rules ─────────────────────────
  updateSafetyLock();
  applyAutomaticControl();

  // Re-publish current mode and lock state for dashboard visibility
  publishRetained(TOPIC_LOW_WATER,  lowWater   ? "TRUE"   : "FALSE");
  publishRetained(TOPIC_PUMP_LOCK,  pumpLocked ? "LOCKED" : "CLEAR");
  publishRetained(TOPIC_PUMP_MODE,  modeName(pumpMode));
  publishRetained(TOPIC_FAN_MODE,   modeName(fanMode));
  publishRetained(TOPIC_LED_MODE,   modeName(ledMode));

  Serial.printf("Soil=%d (%.1f%%), Water=%d (%.1f%%), LDR=%d, Temp=%.1f°C, Hum=%.1f%%\n",
    soilRaw, soilPercent, waterRaw, waterPercent, lightRaw, temperatureC, humidityPercent);
}

// ── Button Read (Software Debounce) ──────────────────────────────────────────
// Buttons are wired from GPIO to GND; INPUT_PULLUP means idle = HIGH.
// Page changes trigger on falling edge (button pressed).

void readButtons() {
  bool prevReading = digitalRead(PREV_BUTTON_PIN);
  bool nextReading = digitalRead(NEXT_BUTTON_PIN);
  unsigned long now = millis();

  if (prevReading != previousPrevReading && now - lastPrevChangeAt >= BUTTON_DEBOUNCE_MS) {
    lastPrevChangeAt = now;
    previousPrevReading = prevReading;
    if (prevReading == LOW) {
      uint8_t previousPage = lcdPage;
      lcdPage = (lcdPage + 3) % 4;  // Wrap backwards
      Serial.printf("Button PREV GPIO%d: page %u -> %u\n",
                    PREV_BUTTON_PIN, previousPage + 1, lcdPage + 1);
    }
  }
  if (nextReading != previousNextReading && now - lastNextChangeAt >= BUTTON_DEBOUNCE_MS) {
    lastNextChangeAt = now;
    previousNextReading = nextReading;
    if (nextReading == LOW) {
      uint8_t previousPage = lcdPage;
      lcdPage = (lcdPage + 1) % 4;
      Serial.printf("Button NEXT GPIO%d: page %u -> %u\n",
                    NEXT_BUTTON_PIN, previousPage + 1, lcdPage + 1);
    }
  }
}

// ── LCD Update ────────────────────────────────────────────────────────────────
// Pads all lines to exactly 16 characters to prevent ghost characters.

void printLcdLine(uint8_t row, String text) {
  if (!lcdReady) return;
  if (text.length() > 16) text = text.substring(0, 16);
  while (text.length() < 16) text += ' ';
  lcd.setCursor(0, row);
  lcd.print(text);
}

void updateLcd() {
  // ── Priority overrides ─────────────────────────────────────────────────────
  // Critical warnings temporarily replace whichever page is selected.

  if (lowWater) {
    if (manualPumpTestBypassActive()) {
      unsigned long elapsed = millis() - manualPumpTestStartedAt;
      unsigned long remaining = elapsed < MANUAL_PUMP_TEST_MAX_MS
        ? (MANUAL_PUMP_TEST_MAX_MS - elapsed + 999) / 1000
        : 0;
      printLcdLine(0, "TEST: LOW WATER");
      printLcdLine(1, "Pump ON " + String(remaining) + "s left");
    } else {
      printLcdLine(0, "LOW WATER!");
      printLcdLine(1, "PUMP LOCKED");
    }
    return;
  }
  if (cropStatus == "RED") {
    printLcdLine(0, "CROP RISK: RED");
    printLcdLine(1, "CHECK PLANTS");
    return;
  }

  // ── Normal 4-page display ──────────────────────────────────────────────────
  if (lcdPage == 0) {
    // Page 0 — Temperature & Humidity + Fan status
    printLcdLine(0, dhtValid
      ? "T:" + String(temperatureC, 1) + "C H:" + String(humidityPercent, 0) + "%"
      : "DHT11 ERROR");
    printLcdLine(1, "Fan:" + String(fanOn ? "ON " : "OFF") + " " + modeName(fanMode));

  } else if (lcdPage == 1) {
    // Page 1 — Soil Moisture + Pump status
    printLcdLine(0, "Soil:" + String(soilPercent, 0) + "%");
    printLcdLine(1, "Pump:" + String(pumpOn ? "ON " : "OFF") + " " + modeName(pumpMode));

  } else if (lcdPage == 2) {
    // Page 2 — Water Level + Tank lock status
    printLcdLine(0, "Water:" + String(waterPercent, 0) + "%");
    printLcdLine(1, pumpLocked ? "Tank:LOW LOCKED" : "Tank:OK");

  } else {
    // Page 3 — Crop Risk (Module 4) + System warning or ONLINE status
    printLcdLine(0, "Crop:" + cropStatus);
    printLcdLine(1, systemWarning.length() ? systemWarning : "System:ONLINE");
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);  // 12-bit ADC → 0–4095

  // Sensor input pins
  pinMode(SOIL_PIN,        INPUT);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(LDR_PIN,         INPUT);

  // Actuator output pins
  pinMode(PUMP_RELAY_PIN, OUTPUT);
  pinMode(FAN_RELAY_PIN,  OUTPUT);
  pinMode(GROW_LED_PIN,   OUTPUT);
  pinMode(RGB_RED_PIN,    OUTPUT);
  pinMode(RGB_GREEN_PIN,  OUTPUT);
  pinMode(RGB_BLUE_PIN,   OUTPUT);
  pinMode(BUZZER_PIN,     OUTPUT);

  // Button input pins with internal pull-up (buttons connect GPIO → GND)
  pinMode(PREV_BUTTON_PIN, INPUT_PULLUP);
  pinMode(NEXT_BUTTON_PIN, INPUT_PULLUP);

  // Safe initial state for all actuators
  writeOutput(PUMP_RELAY_PIN, false, RELAYS_ACTIVE_LOW);
  writeOutput(FAN_RELAY_PIN,  false, RELAYS_ACTIVE_LOW);
  writeOutput(GROW_LED_PIN,   false);
  writeOutput(BUZZER_PIN,     false);
  setRgb(false, true, false);  // Default: GREEN (system healthy)

  // LCD + I2C
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcdReady = lcd.begin();
  if (lcdReady) {
    Serial.println("Seeed Grove LCD detected at I2C address 0x3E.");
    printLcdLine(0, "AgriSense v1.0");
    printLcdLine(1, "WiFi setup...");
  } else {
    Serial.println("WARNING: Seeed Grove LCD not found at I2C address 0x3E.");
  }

  // Brief non-relay startup test for the external RGB LED and active buzzer.
  // No pump or fan output is touched here.
  setRgb(true, false, false);  delay(200);
  setRgb(false, true, false);  delay(200);
  setRgb(false, false, true);  delay(200);
  writeOutput(BUZZER_PIN, true);  delay(200);
  writeOutput(BUZZER_PIN, false);
  setRgb(false, true, false);  // Default system state: GREEN

  // DHT11
  dht.begin();

  Serial.println("\n=== AgriSense Boot ===");
  Serial.println("Connecting to WiFi...");

  // WiFiManager: uses saved credentials or creates SmartFarm-Setup hotspot
  if (!wifiManager.autoConnect("SmartFarm-Setup")) {
    Serial.println("WiFi failed — restarting.");
    delay(3000);
    ESP.restart();
  }

  WiFi.setAutoReconnect(true);

  Serial.printf("WiFi connected: %s (%d dBm)\n", WiFi.SSID().c_str(), WiFi.RSSI());
  printLcdLine(0, "WiFi connected!");
  printLcdLine(1, WiFi.localIP().toString());
  delay(1500);

  // MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  Serial.println("Setup complete.");
}

// ── Loop ──────────────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // Network recovery must never block local water safety, LCD or buttons.
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiReconnectAttemptAt >= 5000) {
      lastWifiReconnectAttemptAt = now;
      Serial.println("WiFi disconnected — reconnect requested; local controls continue.");
      WiFi.reconnect();
    }
  } else if (!mqtt.connected()) {
    if (now - lastMqttReconnectAttemptAt >= 5000) {
      lastMqttReconnectAttemptAt = now;
      connectMqtt();
    }
  } else {
    mqtt.loop();
  }

  // A low-water MANUAL bypass is deliberately short-lived for bench testing.
  if (manualPumpTestBypassActive() &&
      millis() - manualPumpTestStartedAt >= MANUAL_PUMP_TEST_MAX_MS) {
    setPump(false, "manual low-water test timeout", true);
    systemWarning = "TEST COMPLETE: PUMP OFF";
    publishRetained(TOPIC_SYSTEM_ALERT, systemWarning.c_str());
  }

  updateAutoPumpCycle();

  // Button polling (runs every loop iteration for responsive feel)
  readButtons();

  // Safety-critical water level refreshes every second and is independent of
  // MQTT availability or the five-second cloud publish cycle.
  if (now - lastWaterSafetyAt >= WATER_SAFETY_INTERVAL_MS) {
    lastWaterSafetyAt = now;
    refreshWaterSafetyReading();
  }

  // Sensor read and publish on interval
  if (now - lastSensorAt >= SENSOR_INTERVAL_MS) {
    lastSensorAt = now;
    readAndPublishSensors();
  }

  // LCD update on interval (faster than sensor publish to feel responsive)
  if (now - lastLcdAt >= LCD_INTERVAL_MS) {
    lastLcdAt = now;
    updateLcd();
  }
}
