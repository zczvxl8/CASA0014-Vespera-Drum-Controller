// =====================================================
// RHYTHM OF LIGHT
// Stage 03 - Single Piezo Drum Sensor
//
// Arduino MKR WiFi 1010
// MQTT + Vespera + Neon Rainbow + Piezo Sensor
//
// Features:
// 1. Dynamic neon rainbow animation
// 2. Real piezo knock detection on A0
// 3. Knock strength detection
// 4. Cyan light pulse when struck
// 5. Smooth light fading
// 6. Serial Monitor sensor debugging
// =====================================================


// =====================================================
// 1. LIBRARIES
// =====================================================

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <math.h>
#include "arduino_secrets.h"


// =====================================================
// 2. WIFI AND MQTT SETTINGS
// =====================================================

const char* ssid = SECRET_SSID;
const char* wifiPassword = SECRET_PASS;

const char* mqttUser = SECRET_MQTTUSER;
const char* mqttPassword = SECRET_MQTTPASS;

const char* mqttServer = "mqtt.cetools.org";
const uint16_t mqttPort = 1884;


// Your allocated Vespera light ID
const char* lightTopic =
  "student/CASA0014/luminaire/17";


// =====================================================
// 3. SENSOR SETTINGS
// =====================================================

// First piezo sensor
const uint8_t piezoPin = A0;

// Sensitivity threshold
// Lower = more sensitive
// Higher = less sensitive
const int hitThreshold = 65;

// Prevent repeated triggers from one strike
const unsigned long hitCooldownMs = 130;

// Time used to capture peak vibration
const unsigned long captureWindowMs = 15;

// Light fade duration
const unsigned long fadeMs = 550;

// LED animation update interval
const unsigned long frameIntervalMs = 85;


// =====================================================
// 4. VESPERA LED CONFIGURATION
// =====================================================

const int numLeds = 72;

const int payloadSize = numLeds * 3;

uint8_t rgbPayload[payloadSize];

WiFiClient wifiClient;

PubSubClient mqttClient(wifiClient);

String clientId;


// =====================================================
// 5. SENSOR VARIABLES
// =====================================================

// Resting voltage baseline
float baseline = 0.0f;

// Whether currently collecting a knock peak
bool collectingHit = false;

// Knock measurement
unsigned long captureStartMs = 0;

int capturePeak = 0;

// Most recent knock
unsigned long lastHitMs = 0;

// Knock brightness intensity
float hitIntensity = 0.0f;


// =====================================================
// 6. ANIMATION VARIABLES
// =====================================================

uint8_t rainbowPhase = 0;

unsigned long lastFrameMs = 0;

unsigned long lastDebugMs = 0;

unsigned long lastWifiAttemptMs = 0;

unsigned long lastMqttAttemptMs = 0;


// =====================================================
// 7. RAINBOW COLOUR FUNCTION
// =====================================================

void rainbowRGB(
  uint8_t pos,
  uint8_t& r,
  uint8_t& g,
  uint8_t& b
) {

  if (pos < 85) {

    r = 255 - pos * 3;

    g = pos * 3;

    b = 0;

  } else if (pos < 170) {

    pos -= 85;

    r = 0;

    g = 255 - pos * 3;

    b = pos * 3;

  } else {

    pos -= 170;

    r = pos * 3;

    g = 0;

    b = 255 - pos * 3;
  }
}


// =====================================================
// 8. PIEZO SENSOR DETECTION
// =====================================================

void samplePiezo(unsigned long now) {

  // Read actual voltage value
  int raw = analogRead(piezoPin);

  // Calculate difference from resting voltage
  int delta = abs(raw - (int)baseline);


  // -------------------------------------
  // Slowly update resting voltage
  // -------------------------------------

  if (!collectingHit && delta < 15) {

    baseline =
      baseline * 0.995f +
      raw * 0.005f;
  }


  // -------------------------------------
  // Capture peak knock strength
  // -------------------------------------

  if (collectingHit) {

    if (delta > capturePeak) {

      capturePeak = delta;
    }


    // Finish collecting peak
    if (now - captureStartMs >= captureWindowMs) {

      collectingHit = false;

      lastHitMs = now;


      // Convert knock strength to light intensity
      float scaled =
        (capturePeak - hitThreshold) / 350.0f;

      hitIntensity = constrain(
        0.35f + scaled,
        0.35f,
        1.0f
      );


      // -------------------------------------
      // Print actual knock information
      // -------------------------------------

      Serial.print("Drum 1 HIT! Peak delta: ");

      Serial.print(capturePeak);

      Serial.print(" | Strength: ");

      Serial.println(
        (int)(hitIntensity * 100.0f)
      );
    }

  } else if (

    delta >= hitThreshold &&

    now - lastHitMs >= hitCooldownMs

  ) {

    // Start detecting a new knock
    collectingHit = true;

    captureStartMs = now;

    capturePeak = delta;
  }


  // -------------------------------------
  // Print sensor readings every 500 ms
  // -------------------------------------

  if (now - lastDebugMs >= 500) {

    lastDebugMs = now;

    Serial.print("A0 RAW: ");

    Serial.print(raw);

    Serial.print(" | idle: ");

    Serial.print((int)baseline);

    Serial.print(" | delta: ");

    Serial.println(delta);
  }
}


// =====================================================
// 9. NEON RAINBOW + DRUM LIGHT EFFECT
// =====================================================

void renderNeonFrame(unsigned long now) {

  // Avoid using TWO_PI as a variable name
  const float neonTau = 6.2831853f;


  // -------------------------------------
  // Breathing light animation
  // -------------------------------------

  float breathing =
    0.5f +
    0.5f * sinf(now * 0.0015f);


  // -------------------------------------
  // Calculate knock flash intensity
  // -------------------------------------

  float flash = 0.0f;

  unsigned long age = now - lastHitMs;

  if (age < fadeMs) {

    flash =
      hitIntensity *
      (1.0f - age / (float)fadeMs);
  }


  // -------------------------------------
  // Generate colours for all 72 LEDs
  // -------------------------------------

  for (int pixel = 0; pixel < numLeds; pixel++) {

    // Rainbow position
    uint8_t hue =

      (pixel * 256 / numLeds +
       rainbowPhase) & 255;


    uint8_t r, g, b;

    rainbowRGB(hue, r, g, b);


    // -------------------------------------
    // Moving neon wave
    // -------------------------------------

    float wave =

      0.5f +

      0.5f * sinf(

        neonTau *
        pixel /
        (float)numLeds *
        2.0f +

        now * 0.0025f
      );


    // -------------------------------------
    // Background brightness
    // -------------------------------------

    float brightness =

      0.28f +

      0.22f * wave +

      0.10f * breathing;


    // -------------------------------------
    // Apply cyan knock flash
    // -------------------------------------

    rgbPayload[pixel * 3 + 0] =

      (uint8_t)constrain(

        r * brightness +
        80.0f * flash,

        0.0f,
        255.0f
      );


    rgbPayload[pixel * 3 + 1] =

      (uint8_t)constrain(

        g * brightness +
        205.0f * flash,

        0.0f,
        255.0f
      );


    rgbPayload[pixel * 3 + 2] =

      (uint8_t)constrain(

        b * brightness +
        255.0f * flash,

        0.0f,
        255.0f
      );
  }


  // Move rainbow forward
  rainbowPhase += 2;
}


// =====================================================
// 10. WIFI AND MQTT CONNECTION
// =====================================================

void serviceNetwork(unsigned long now) {


  // -------------------------------------
  // Check WiFi
  // -------------------------------------

  if (WiFi.status() != WL_CONNECTED) {

    if (

      now - lastWifiAttemptMs >= 10000UL ||

      lastWifiAttemptMs == 0

    ) {

      lastWifiAttemptMs = now;

      Serial.println("Connecting to WiFi...");

      WiFi.begin(ssid, wifiPassword);
    }

    return;
  }


  // -------------------------------------
  // Check MQTT
  // -------------------------------------

  if (!mqttClient.connected()) {

    if (

      now - lastMqttAttemptMs >= 5000UL ||

      lastMqttAttemptMs == 0

    ) {

      lastMqttAttemptMs = now;

      Serial.println("Connecting to MQTT...");


      if (

        mqttClient.connect(

          clientId.c_str(),

          mqttUser,

          mqttPassword

        )

      ) {

        Serial.println("MQTT connected");

      } else {

        Serial.print("MQTT error state: ");

        Serial.println(mqttClient.state());
      }
    }

    return;
  }


  // Keep MQTT connection active
  mqttClient.loop();
}


// =====================================================
// 11. SETUP
// =====================================================

void setup() {

  Serial.begin(115200);


  // 10-bit analog input
  analogReadResolution(10);

  pinMode(piezoPin, INPUT);


  Serial.println(
    "Stage 03: Single Piezo + Neon Rainbow"
  );


  // -------------------------------------
  // Calibrate sensor resting value
  // -------------------------------------

  Serial.println(
    "Keep the piezo still while calibrating..."
  );


  long total = 0;

  for (int sample = 0; sample < 100; sample++) {

    total += analogRead(piezoPin);

    delay(2);
  }


  baseline = total / 100.0f;


  Serial.print("Calibrated A0 idle = ");

  Serial.println((int)baseline);


  // -------------------------------------
  // Create unique MQTT client ID
  // -------------------------------------

  byte mac[6] = {0};

  WiFi.macAddress(mac);


  char macText[13];

  snprintf(

    macText,

    sizeof(macText),

    "%02X%02X%02X%02X%02X%02X",

    mac[5],

    mac[4],

    mac[3],

    mac[2],

    mac[1],

    mac[0]
  );


  clientId =

    String("MKR1010_Drum_") +

    macText;


  // -------------------------------------
  // MQTT configuration
  // -------------------------------------

  mqttClient.setServer(

    mqttServer,

    mqttPort
  );


  mqttClient.setBufferSize(512);

  mqttClient.setKeepAlive(30);

  mqttClient.setSocketTimeout(2);


  // Start connection
  serviceNetwork(millis());
}


// =====================================================
// 12. MAIN LOOP
// =====================================================

void loop() {

  unsigned long now = millis();


  // -------------------------------------
  // Step 1: Read real piezo sensor
  // -------------------------------------

  samplePiezo(now);


  // -------------------------------------
  // Step 2: Maintain network connection
  // -------------------------------------

  serviceNetwork(now);


  // -------------------------------------
  // Step 3: Update animation
  // -------------------------------------

  if (now - lastFrameMs >= frameIntervalMs) {

    lastFrameMs = now;


    // Create neon rainbow + knock response
    renderNeonFrame(now);


    // -------------------------------------
    // Step 4: Publish to Vespera
    // -------------------------------------

    if (mqttClient.connected()) {

      bool success = mqttClient.publish(

        lightTopic,

        rgbPayload,

        payloadSize
      );


      if (!success) {

        Serial.println("MQTT publish failed");
      }
    }
  }
}
