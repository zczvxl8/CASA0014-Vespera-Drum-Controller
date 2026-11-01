/*
  RHYTHM OF LIGHT — TEST 06: REAL RECORDING + REPLAY

  Board: Arduino MKR WiFi 1010
  Piezo sensors: A0, A1, A2 (with 3.3V input protection)
  Latching switch: D2 to GND; INPUT_PULLUP
  SSD1306 OLED: existing I2C SDA/SCL wiring
  Lighting: existing 72 LEDs via MQTT (3 zones, 24 LEDs each)

  SWITCH: each stable flip is ONE action, regardless of position.
    1st flip: START RECORDING (clears the old take)
    2nd flip: STOP and SAVE
    3rd flip: PLAY BACK actual recorded lights
    During playback, flip again: STOP playback
    After a completed/stopped playback, flip: REPLAY again
    Special gesture: after stopping playback, another flip within
    650 ms starts a NEW recording instead of replaying.

  Playback automatically stops at the last hit + the LED fade tail.
  The first recorded hit is placed at t=0: leading silence is removed.
  Serial Monitor: 115200; 'n' resets for a new take, 'p' replays,
  's' stops. Serial commands help with testing, but are optional.

  RECORDING/PLAYBACK text is shown only when that action is real.
  This test stores events in RAM, not in nonvolatile memory:
  powering off erases the take. MAX_EVENTS = 160 hits.

  Libraries: WiFiNINA, PubSubClient, Adafruit GFX, Adafruit SSD1306.
  Private arduino_secrets.h: SECRET_SSID, SECRET_PASS,
  SECRET_MQTTUSER, SECRET_MQTTPASS; optional SECRET_WIFIUSER for
  eduroam and university-verified SECRET_WIFI_CA. NEVER commit secrets.
*/

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "arduino_secrets.h"

const char* const WIFI_SSID = SECRET_SSID;
const char* const WIFI_PASS = SECRET_PASS;
const char* const MQTT_USER = SECRET_MQTTUSER;
const char* const MQTT_PASS = SECRET_MQTTPASS;
const char* const MQTT_SERVER = "mqtt.cetools.org";
const uint16_t MQTT_PORT = 1884;
const char* const MQTT_TOPIC = "student/CASA0014/luminaire/17";

const byte DRUM_COUNT = 3;
const byte DRUM_PINS[DRUM_COUNT] = {A0, A1, A2};
const byte SWITCH_PIN = 2;
const int LED_COUNT = 72;
const int ZONE_LEDS = 24;
const int PAYLOAD_SIZE = LED_COUNT * 3;

int HIT_THRESHOLD[DRUM_COUNT] = {40, 48, 55};
const int STRONG_PEAK[DRUM_COUNT] = {300, 150, 550};

const unsigned long PEAK_WINDOW_MS = 12;
const unsigned long COOLDOWN_MS = 150;
const unsigned long REARM_QUIET_MS = 25;
const unsigned long SWITCH_DEBOUNCE_MS = 45;
const unsigned long QUICK_NEW_TAKE_MS = 650;
const unsigned long FUSION_MS = 115;
const unsigned long LED_FADE_MS = 370;
const unsigned long LIGHT_FRAME_MS = 85;
const unsigned long OLED_FRAME_MS = 180;
const unsigned long MESSAGE_MS = 1250;

const int MAX_EVENTS = 160;
struct RhythmEvent {
  unsigned long timeMs;  // Relative to FIRST genuine recorded hit.
  uint16_t peakDelta;
  uint8_t drum;          // 0, 1, 2
  uint8_t strength;      // 1..100
};
RhythmEvent events[MAX_EVENTS];
int eventCount = 0;
bool firstRecordedHitSeen = false;
unsigned long firstRecordedHitTime = 0;
bool capacityReached = false;

enum RunMode {
  MODE_READY,
  MODE_RECORDING,
  MODE_SAVED,
  MODE_PLAYING,
  MODE_STOPPED,
  MODE_DONE
};
RunMode mode = MODE_READY;
unsigned long playbackStartTime = 0;
int playbackIndex = 0;
unsigned long playbackStoppedAt = 0;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
String mqttClientId;
uint8_t rgbPayload[PAYLOAD_SIZE];
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastLightFrame = 0;

Adafruit_SSD1306 oled(128, 64, &Wire, -1);
bool oledReady = false;
unsigned long lastOledFrame = 0;
const char* noticeLine1 = "";
const char* noticeLine2 = "";
unsigned long noticeTime = 0;
bool noticePresent = false;

int rawSwitch = HIGH;
int stableSwitch = HIGH;
unsigned long rawSwitchChangedAt = 0;

float baseline[DRUM_COUNT] = {0, 0, 0};
bool capturing[DRUM_COUNT] = {false, false, false};
bool armed[DRUM_COUNT] = {true, true, true};
bool hasSensorHit[DRUM_COUNT] = {false, false, false};
int capturePeak[DRUM_COUNT] = {0, 0, 0};
unsigned long captureStart[DRUM_COUNT] = {0, 0, 0};
unsigned long lastSensorHit[DRUM_COUNT] = {0, 0, 0};
unsigned long quietStart[DRUM_COUNT] = {0, 0, 0};

// These are the current visual hits, used by BOTH recording and playback.
bool lit[DRUM_COUNT] = {false, false, false};
unsigned long litAt[DRUM_COUNT] = {0, 0, 0};
float litStrength[DRUM_COUNT] = {0, 0, 0};
uint8_t fusionMask = 0;
unsigned long fusionFirstTime = 0;
unsigned long fusionLatestTime = 0;

int latestDrum = -1;
int latestStrength = 0;
unsigned long latestHitAt = 0;

struct RGB { float r; float g; float b; };

float clampUnit(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

uint8_t clampByte(float v) {
  if (v < 0.0f) return 0;
  if (v > 255.0f) return 255;
  return (uint8_t)v;
}

void setNotice(const char* line1, const char* line2) {
  noticeLine1 = line1;
  noticeLine2 = line2;
  noticePresent = true;
  noticeTime = millis();
}

void clearLighting() {
  for (byte i = 0; i < DRUM_COUNT; ++i) {
    lit[i] = false;
    litStrength[i] = 0;
    litAt[i] = 0;
  }
  fusionMask = 0;
  fusionFirstTime = 0;
  fusionLatestTime = 0;
  for (int i = 0; i < PAYLOAD_SIZE; ++i) rgbPayload[i] = 0;
}

RGB getDrumColour(byte drum, float strength) {
  strength = clampUnit(strength);
  if (drum == 0) {
    return {150 + strength * 105, 65 - strength * 35,
            245 - strength * 65};
  }
  if (drum == 1) {
    return {20 + strength * 30, 165 + strength * 90,
            255 - strength * 105};
  }
  return {255, 205 - strength * 125, 35 + strength * 25};
}

int countFusionMembers(uint8_t mask) {
  int n = 0;
  for (byte i = 0; i < DRUM_COUNT; ++i) {
    if (mask & (1 << i)) ++n;
  }
  return n;
}

RGB getFusionColour() {
  RGB total = {0, 0, 0};
  float weightTotal = 0;
  for (byte i = 0; i < DRUM_COUNT; ++i) {
    if (!(fusionMask & (1 << i))) continue;
    RGB c = getDrumColour(i, litStrength[i]);
    float weight = 0.55f + litStrength[i] * 0.45f;
    total.r += c.r * weight;
    total.g += c.g * weight;
    total.b += c.b * weight;
    weightTotal += weight;
  }
  if (weightTotal > 0) {
    total.r /= weightTotal;
    total.g /= weightTotal;
    total.b /= weightTotal;
  }
  return total;
}

void triggerLight(byte drum, byte strength, unsigned long now) {
  lit[drum] = true;
  litAt[drum] = now;
  litStrength[drum] = clampUnit(strength / 100.0f);
  latestDrum = drum + 1;
  latestStrength = strength;
  latestHitAt = now;

  if (fusionMask == 0 || now - fusionFirstTime > FUSION_MS) {
    fusionMask = (1 << drum);
    fusionFirstTime = now;
  } else {
    fusionMask |= (1 << drum);
  }
  fusionLatestTime = now;
}

void renderLights(unsigned long now) {
  bool together = countFusionMembers(fusionMask) >= 2 &&
                  now - fusionLatestTime < LED_FADE_MS;
  RGB blend = getFusionColour();

  for (int pixel = 0; pixel < LED_COUNT; ++pixel) {
    byte drum = pixel / ZONE_LEDS;
    RGB c = {0, 0, 0};
    float brightness = 0;

    if (lit[drum]) {
      unsigned long age = now - litAt[drum];
      if (age < LED_FADE_MS) {
        float fade = 1.0f - age / (float)LED_FADE_MS;
        c = getDrumColour(drum, litStrength[drum]);
        brightness = (0.50f + 0.50f * litStrength[drum]) * fade * fade;
      }
    }

    if (together && (fusionMask & (1 << drum))) {
      float fade = 1.0f -
                   (now - fusionLatestTime) / (float)LED_FADE_MS;
      c = blend;
      brightness = (0.55f + litStrength[drum] * 0.45f) * fade * fade;
    }

    rgbPayload[pixel * 3 + 0] = clampByte(c.r * brightness);
    rgbPayload[pixel * 3 + 1] = clampByte(c.g * brightness);
    rgbPayload[pixel * 3 + 2] = clampByte(c.b * brightness);
  }
}

int strengthFromPeak(byte drum, int peak) {
  int bottom = HIT_THRESHOLD[drum];
  int top = STRONG_PEAK[drum];
  if (top <= bottom) top = bottom + 1;
  long strength = 1L + (long)(peak - bottom) * 99L / (top - bottom);
  if (strength < 1) strength = 1;
  if (strength > 100) strength = 100;
  return (int)strength;
}

void startRecording() {
  if (!mqttClient.connected()) {
    Serial.println(F("[ERROR] Cannot record: MQTT is offline."));
    setNotice("MQTT OFFLINE", "WAIT / RETRY");
    return;
  }

  eventCount = 0;
  firstRecordedHitSeen = false;
  firstRecordedHitTime = 0;
  capacityReached = false;
  playbackIndex = 0;
  clearLighting();
  mode = MODE_RECORDING;
  setNotice("RECORDING", "STARTED");
  Serial.println(F("[REC] Started: waiting for first genuine drum hit."));
}

void stopRecording() {
  if (mode != MODE_RECORDING) return;

  if (eventCount == 0) {
    mode = MODE_READY;
    setNotice("NO HITS", "TRY AGAIN");
    Serial.println(F("[REC] Stopped: no hits recorded. Next flip starts again."));
    return;
  }

  mode = MODE_SAVED;
  setNotice("RECORDING", "SAVED");
  Serial.print(F("[REC] Saved "));
  Serial.print(eventCount);
  Serial.print(F(" events. Duration from FIRST to LAST hit: "));
  Serial.print(events[eventCount - 1].timeMs);
  Serial.println(F(" ms."));
  if (capacityReached) {
    Serial.println(F("[REC] Reached maximum 160 events."));
  }
}

void startPlayback() {
  if (eventCount == 0) {
    setNotice("NO RECORDING", "MAKE A TAKE");
    Serial.println(F("[PLAYBACK] Refused: no events are stored."));
    return;
  }
  if (!mqttClient.connected()) {
    setNotice("MQTT OFFLINE", "NO PLAYBACK");
    Serial.println(F("[PLAYBACK] Refused: MQTT is offline."));
    return;
  }

  mode = MODE_PLAYING;
  playbackIndex = 0;
  playbackStartTime = millis();
  clearLighting();
  setNotice("PLAYBACK", "STARTED");
  Serial.print(F("[PLAYBACK] Started; events="));
  Serial.println(eventCount);
  Serial.println(F("[PLAYBACK] The first recorded hit happens at t=0 ms."));
}

void stopPlayback(const char* reason) {
  if (mode != MODE_PLAYING) return;
  mode = MODE_STOPPED;
  playbackStoppedAt = millis();
  clearLighting();
  setNotice("PLAYBACK", "STOPPED");
  Serial.print(F("[PLAYBACK] Stopped: "));
  Serial.println(reason);
}

void finishPlayback() {
  if (mode != MODE_PLAYING) return;
  mode = MODE_DONE;
  clearLighting();
  setNotice("PLAYBACK", "COMPLETE");
  Serial.println(F("[PLAYBACK] Complete. Flip the switch to replay again."));
}

void addRecordedHit(byte drum, int peak, unsigned long now) {
  if (mode != MODE_RECORDING) return;

  int strength = strengthFromPeak(drum, peak);

  if (eventCount >= MAX_EVENTS) {
    capacityReached = true;
    stopRecording();
    return;
  }

  if (!firstRecordedHitSeen) {
    firstRecordedHitSeen = true;
    firstRecordedHitTime = now;
    Serial.println(F("[REC] First hit recorded: timeline starts at 0 ms."));
  }

  RhythmEvent &event = events[eventCount];
  event.timeMs = now - firstRecordedHitTime;
  event.peakDelta = peak;
  event.drum = drum;
  event.strength = (uint8_t)strength;
  eventCount++;

  triggerLight(drum, event.strength, now);
  Serial.print(F("[REC HIT] Drum "));
  Serial.print(drum + 1);
  Serial.print(F(" t="));
  Serial.print(event.timeMs);
  Serial.print(F("ms strength="));
  Serial.print(strength);
  Serial.println(F("/100"));

  if (eventCount == MAX_EVENTS) {
    capacityReached = true;
    stopRecording();
  }
}

void sampleRealDrums(unsigned long now) {
  for (byte drum = 0; drum < DRUM_COUNT; ++drum) {
    int raw = analogRead(DRUM_PINS[drum]);
    int delta = raw - (int)baseline[drum];
    if (delta < 0) delta = -delta;
    int releaseLimit = HIT_THRESHOLD[drum] * 45 / 100;

    if (!capturing[drum] && delta < releaseLimit) {
      baseline[drum] = 0.998f * baseline[drum] + 0.002f * raw;
    }

    if (capturing[drum]) {
      if (delta > capturePeak[drum]) capturePeak[drum] = delta;
      if (now - captureStart[drum] >= PEAK_WINDOW_MS) {
        capturing[drum] = false;
        if (mode == MODE_RECORDING) {
          addRecordedHit(drum, capturePeak[drum], now);
        }
        hasSensorHit[drum] = true;
        lastSensorHit[drum] = now;
        armed[drum] = false;
        quietStart[drum] = 0;
      }
      continue;
    }

    if (!armed[drum]) {
      if (delta <= releaseLimit) {
        if (quietStart[drum] == 0) quietStart[drum] = now;
        if (now - quietStart[drum] >= REARM_QUIET_MS &&
            now - lastSensorHit[drum] >= COOLDOWN_MS) {
          armed[drum] = true;
        }
      } else {
        quietStart[drum] = 0;
      }
      continue;
    }

    if (delta >= HIT_THRESHOLD[drum] &&
        (!hasSensorHit[drum] || now - lastSensorHit[drum] >= COOLDOWN_MS)) {
      capturing[drum] = true;
      captureStart[drum] = now;
      capturePeak[drum] = delta;
      armed[drum] = false;
    }
  }
}

void updatePlayback(unsigned long now) {
  if (mode != MODE_PLAYING) return;

  if (!mqttClient.connected()) {
    stopPlayback("MQTT connection lost");
    return;
  }

  unsigned long elapsed = now - playbackStartTime;
  while (playbackIndex < eventCount &&
         elapsed >= events[playbackIndex].timeMs) {
    const RhythmEvent &event = events[playbackIndex];
    triggerLight(event.drum, event.strength, now);

    Serial.print(F("[PLAY EVENT] Drum "));
    Serial.print(event.drum + 1);
    Serial.print(F(" at "));
    Serial.print(event.timeMs);
    Serial.print(F("ms strength="));
    Serial.println(event.strength);
    playbackIndex++;
  }

  if (playbackIndex >= eventCount &&
      elapsed >= events[eventCount - 1].timeMs + LED_FADE_MS) {
    finishPlayback();
  }
}

void handleSwitchFlip(unsigned long now) {
  Serial.println(F("[SWITCH] Stable physical switch flip detected."));

  switch (mode) {
    case MODE_READY:
      startRecording();
      break;
    case MODE_RECORDING:
      stopRecording();
      break;
    case MODE_SAVED:
    case MODE_DONE:
      startPlayback();
      break;
    case MODE_PLAYING:
      stopPlayback("switch toggled");
      break;
    case MODE_STOPPED:
      if (now - playbackStoppedAt <= QUICK_NEW_TAKE_MS) {
        Serial.println(F("[SWITCH] Quick second flip: new take."));
        startRecording();
      } else {
        startPlayback();
      }
      break;
  }
}

void pollSwitch(unsigned long now) {
  int reading = digitalRead(SWITCH_PIN);
  if (reading != rawSwitch) {
    rawSwitch = reading;
    rawSwitchChangedAt = now;
  }

  if (rawSwitch != stableSwitch &&
      now - rawSwitchChangedAt >= SWITCH_DEBOUNCE_MS) {
    stableSwitch = rawSwitch;
    handleSwitchFlip(now);
  }
}

void handleSerialCommands() {
  while (Serial.available() > 0) {
    char command = (char)Serial.read();
    if (command == 'n' || command == 'N') {
      mode = MODE_READY;
      eventCount = 0;
      firstRecordedHitSeen = false;
      capacityReached = false;
      clearLighting();
      setNotice("NEW TAKE", "FLIP TO REC");
      Serial.println(F("[TEST] Reset to READY; next switch flip starts recording."));
    } else if (command == 'p' || command == 'P') {
      if (mode != MODE_RECORDING) startPlayback();
    } else if (command == 's' || command == 'S') {
      if (mode == MODE_RECORDING) stopRecording();
      else if (mode == MODE_PLAYING) stopPlayback("serial stop command");
    }
  }
}

void beginWifiConnection() {
#if defined(SECRET_WIFIUSER) && defined(SECRET_WIFI_CA)
  WiFi.beginEnterprise(WIFI_SSID, SECRET_WIFIUSER, WIFI_PASS,
                       SECRET_WIFIUSER, SECRET_WIFI_CA);
#elif defined(SECRET_WIFIUSER)
  // No CA provided. Use only for diagnostics; ask university IT
  // for the validated eduroam CA/identity configuration.
  WiFi.beginEnterprise(WIFI_SSID, SECRET_WIFIUSER, WIFI_PASS);
#else
  WiFi.begin(WIFI_SSID, WIFI_PASS);
#endif
}

void serviceNetwork(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    if (mode == MODE_PLAYING) stopPlayback("WiFi disconnected");
    if (lastWifiAttempt == 0 || now - lastWifiAttempt >= 10000UL) {
      lastWifiAttempt = now;
      Serial.println(F("[WIFI] Connecting..."));
      beginWifiConnection();
    }
    return;
  }

  if (!mqttClient.connected()) {
    if (mode == MODE_PLAYING) stopPlayback("MQTT disconnected");
    if (lastMqttAttempt == 0 || now - lastMqttAttempt >= 5000UL) {
      lastMqttAttempt = now;
      Serial.println(F("[MQTT] Connecting..."));
      if (mqttClient.connect(mqttClientId.c_str(), MQTT_USER, MQTT_PASS)) {
        Serial.println(F("[MQTT] Connected. Ready to control real lights."));
      } else {
        Serial.print(F("[MQTT] Failed. State="));
        Serial.println(mqttClient.state());
      }
    }
    return;
  }
  mqttClient.loop();
}

bool startOLED() {
  const byte addresses[2] = {0x3C, 0x3D};
  for (byte i = 0; i < 2; ++i) {
    Wire.beginTransmission(addresses[i]);
    if (Wire.endTransmission() != 0) continue;
    if (oled.begin(SSD1306_SWITCHCAPVCC, addresses[i])) {
      Serial.print(F("[OLED] Connected at 0x"));
      Serial.println(addresses[i], HEX);
      return true;
    }
  }
  Serial.println(F("[OLED] Not found. Serial diagnostics will still work."));
  return false;
}

void drawMiniDrum(unsigned long now) {
  const int x = 98;
  const int y = 37;
  oled.drawRoundRect(x, y, 25, 18, 5, SSD1306_WHITE);
  oled.drawLine(x + 2, y + 4, x + 23, y + 4, SSD1306_WHITE);
  oled.drawLine(x + 4, y + 18, x + 2, y + 23, SSD1306_WHITE);
  oled.drawLine(x + 21, y + 18, x + 23, y + 23, SSD1306_WHITE);
  bool strike = ((now / 150) % 2UL) == 0UL;
  oled.drawLine(x + 1, y - 16, x + 8, strike ? y + 3 : y - 4,
                SSD1306_WHITE);
  oled.drawLine(x + 24, y - 16, x + 17, strike ? y - 4 : y + 3,
                SSD1306_WHITE);
}

void drawOLED(unsigned long now) {
  if (!oledReady) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.drawLine(91, 0, 91, 63, SSD1306_WHITE);

  bool showNotice = noticePresent && now - noticeTime < MESSAGE_MS;
  if (showNotice) {
    oled.setCursor(0, 6);
    oled.print(noticeLine1);
    oled.setCursor(0, 23);
    oled.print(noticeLine2);
  } else {
    oled.setCursor(0, 4);
    switch (mode) {
      case MODE_READY: oled.print(F("READY")); break;
      case MODE_RECORDING: oled.print(F("RECORDING")); break;
      case MODE_SAVED: oled.print(F("REC SAVED")); break;
      case MODE_PLAYING: oled.print(F("PLAYBACK")); break;
      case MODE_STOPPED: oled.print(F("PLAY STOP")); break;
      case MODE_DONE: oled.print(F("PLAY DONE")); break;
    }

    oled.setCursor(0, 19);
    oled.print(F("HITS: "));
    oled.print(eventCount);

    oled.setCursor(0, 34);
    if (mode == MODE_PLAYING) {
      oled.print(F("EVENT: "));
      oled.print(playbackIndex);
    } else if (latestDrum > 0) {
      oled.print(F("DRUM "));
      oled.print(latestDrum);
      oled.print(F(" HIT"));
    } else {
      oled.print(F("FLIP SWITCH"));
    }

    oled.setCursor(0, 50);
    if (mode == MODE_RECORDING || mode == MODE_PLAYING) {
      oled.print(F("STR: "));
      oled.print(latestStrength);
    } else if (mqttClient.connected()) {
      oled.print(F("MQTT: OK"));
    } else {
      oled.print(F("WAIT MQTT"));
    }
  }

  drawMiniDrum(now);
  oled.display();
}

void setup() {
  Serial.begin(115200);
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 2500UL) {}

  analogReadResolution(10);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  rawSwitch = digitalRead(SWITCH_PIN);
  stableSwitch = rawSwitch;

  Serial.println(F("=========================================="));
  Serial.println(F("TEST 06: REAL RHYTHM RECORDING + LIGHT REPLAY"));
  Serial.println(F("A0/A1/A2 drums; D2 latching switch; 72 MQTT LEDs"));
  Serial.println(F("Keep drums still during baseline calibration."));

  for (byte drum = 0; drum < DRUM_COUNT; ++drum) {
    pinMode(DRUM_PINS[drum], INPUT);
    long sum = 0;
    for (int s = 0; s < 100; ++s) {
      sum += analogRead(DRUM_PINS[drum]);
      delay(2);
    }
    baseline[drum] = sum / 100.0f;
  }

  Wire.begin();
  Wire.setClock(400000UL);
  oledReady = startOLED();

  byte mac[6] = {0};
  WiFi.macAddress(mac);
  char suffix[13];
  snprintf(suffix, sizeof(suffix), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  mqttClientId = String("CASA0014_Test06_") + suffix;
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  if (!mqttClient.setBufferSize(512)) {
    Serial.println(F("[ERROR] Cannot allocate 512-byte MQTT buffer."));
  }
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(2);

  clearLighting();
  setNotice("READY", "WAIT MQTT");
  Serial.println(F("Wait for [MQTT] Connected, then flip D2 to START."));
  Serial.println(F("Next flip STOP/SAVE, next flip REAL PLAYBACK."));
  Serial.println(F("Serial: n=reset, p=play, s=stop (optional)."));
}

void loop() {
  unsigned long now = millis();

  sampleRealDrums(now);
  pollSwitch(now);
  handleSerialCommands();
  serviceNetwork(now);
  updatePlayback(now);

  if (now - lastLightFrame >= LIGHT_FRAME_MS) {
    lastLightFrame = now;
    if (mqttClient.connected()) {
      renderLights(now);
      if (!mqttClient.publish(MQTT_TOPIC, rgbPayload, PAYLOAD_SIZE)) {
        Serial.println(F("[MQTT] Publish failed."));
        if (mode == MODE_PLAYING) stopPlayback("light publish failed");
      }
    }
  }

  if (now - lastOledFrame >= OLED_FRAME_MS) {
    lastOledFrame = now;
    drawOLED(now);
  }
}
