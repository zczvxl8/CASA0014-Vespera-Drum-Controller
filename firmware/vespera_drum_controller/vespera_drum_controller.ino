// =====================================================
// CASA0014 - Neon Rainbow Flow
// Arduino MKR WiFi 1010
//
// 72 RGB LEDs
// Dynamic Rainbow + Neon Waves + Breathing
// MQTT -> Vespera / Luminaire
// =====================================================


// =====================================================
// 1. Libraries
// =====================================================

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <math.h>
#include "arduino_secrets.h"


// =====================================================
// 2. WiFi Settings
// =====================================================

const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;


// =====================================================
// 3. MQTT Settings
// =====================================================

const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;

const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;

String lightId = "17";

String mqtt_topic =
  "student/CASA0014/luminaire/" + lightId;

// WiFi + MQTT client
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

String clientId = "";


// =====================================================
// 4. LED Settings
// =====================================================

const int num_leds = 72;

const int payload_size = num_leds * 3;

byte RGBpayload[payload_size];


// =====================================================
// 5. WiFi Connection
// =====================================================

void startWifi() {

  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  static bool attempted = false;
  static unsigned long lastAttempt = 0;

  unsigned long now = millis();

  if (attempted &&
      now - lastAttempt < 10000) {
    return;
  }

  attempted = true;
  lastAttempt = now;

  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  if (WiFi.status() == WL_CONNECTED) {

    Serial.println("WiFi connected!");

    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

  } else {

    Serial.println("WiFi not connected, will retry...");
  }
}


// =====================================================
// 6. MQTT Connection
// =====================================================

void reconnectMQTT() {

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqttClient.connected()) {
    return;
  }

  static bool attempted = false;
  static unsigned long lastAttempt = 0;

  unsigned long now = millis();

  if (attempted &&
      now - lastAttempt < 3000) {
    return;
  }

  attempted = true;
  lastAttempt = now;

  Serial.println("Connecting to MQTT...");

  if (mqttClient.connect(
        clientId.c_str(),
        mqtt_username,
        mqtt_password)) {

    Serial.println("MQTT connected!");

  } else {

    Serial.print("MQTT failed, state: ");
    Serial.println(mqttClient.state());
  }
}


// =====================================================
// 7. Setup
// =====================================================

void setup() {

  Serial.begin(115200);

  Serial.println();
  Serial.println("NEON RAINBOW STARTING...");

  byte mac[6];

  WiFi.macAddress(mac);

  char macStr[18];

  snprintf(
    macStr,
    sizeof(macStr),
    "%02X%02X%02X%02X%02X%02X",
    mac[5], mac[4], mac[3],
    mac[2], mac[1], mac[0]
  );

  clientId = "MKR1010_Neon_" + String(macStr);

  Serial.print("Client ID: ");
  Serial.println(clientId);

  Serial.print("MQTT Topic: ");
  Serial.println(mqtt_topic);

  mqttClient.setServer(
    mqtt_server,
    mqtt_port
  );

  if (!mqttClient.setBufferSize(512)) {
    Serial.println("MQTT buffer allocation failed");
  }

  mqttClient.setKeepAlive(30);

  startWifi();

  Serial.println("Setup complete!");
}


// =====================================================
// 8. Rainbow Colour Function
// =====================================================

void rainbowRGB(
  uint8_t pos,
  uint8_t &r,
  uint8_t &g,
  uint8_t &b
) {

  // Red -> Green
  if (pos < 85) {

    r = 255 - pos * 3;
    g = pos * 3;
    b = 0;

  }

  // Green -> Blue
  else if (pos < 170) {

    pos -= 85;

    r = 0;
    g = 255 - pos * 3;
    b = pos * 3;

  }

  // Blue -> Red
  else {

    pos -= 170;

    r = pos * 3;
    g = 0;
    b = 255 - pos * 3;
  }
}


// =====================================================
// 9. Main Neon Rainbow Animation
// =====================================================

void loop() {

  // -------------------------------------
  // Check WiFi
  // -------------------------------------

  if (WiFi.status() != WL_CONNECTED) {

    startWifi();
    return;
  }


  // -------------------------------------
  // Check MQTT
  // -------------------------------------

  if (!mqttClient.connected()) {

    reconnectMQTT();
    return;
  }

  mqttClient.loop();


  // -------------------------------------
  // Animation Settings
  // -------------------------------------

  static uint8_t phase = 0;

  const float NEON_TAU = 6.2831853f;

  float timePhase =
    (float)phase / 256.0f;


  // -------------------------------------
  // Global Breathing
  // -------------------------------------

  float breathing =
    0.5f +
    0.5f * sinf(
      NEON_TAU * timePhase
    );


  // =====================================
  // Generate All 72 LED Colours
  // =====================================

  for (int pixel = 0;
       pixel < num_leds;
       pixel++) {


    // -----------------------------------
    // A. Moving Rainbow
    // -----------------------------------

    uint8_t hue = (uint8_t)(
      (pixel * 256L / num_leds + phase) & 255
    );

    uint8_t r, g, b;

    rainbowRGB(hue, r, g, b);


    // -----------------------------------
    // B. Pixel Position
    // -----------------------------------

    float x =
      (float)pixel / (float)num_leds;


    // -----------------------------------
    // C. Main Neon Wave
    // -----------------------------------

    float neonWave =
      0.5f +
      0.5f * sinf(
        NEON_TAU *
        (2.0f * x - timePhase)
      );


    // -----------------------------------
    // D. Secondary Light Wave
    // -----------------------------------

    float shimmer =
      0.5f +
      0.5f * sinf(
        NEON_TAU *
        (4.0f * x + timePhase)
      );


    // -----------------------------------
    // E. Combine Brightness
    // -----------------------------------

    float brightness =
      0.32f +
      0.45f * neonWave +
      0.13f * shimmer +
      0.10f * breathing;


    // -----------------------------------
    // F. Neon Brightness Limit
    // -----------------------------------

    uint8_t scale =
      (uint8_t)(215.0f * brightness);


    // -----------------------------------
    // G. Apply Brightness
    // -----------------------------------

    RGBpayload[pixel * 3 + 0] =
      (uint16_t)r * scale / 255;

    RGBpayload[pixel * 3 + 1] =
      (uint16_t)g * scale / 255;

    RGBpayload[pixel * 3 + 2] =
      (uint16_t)b * scale / 255;
  }


  // =====================================
  // 10. Publish to Luminaire
  // =====================================

  bool success = mqttClient.publish(
    mqtt_topic.c_str(),
    RGBpayload,
    payload_size
  );

  if (!success) {

    Serial.println("MQTT publish failed");
  }


  // =====================================
  // 11. Animate Rainbow
  // =====================================

  phase += 2;

  delay(75);
}
