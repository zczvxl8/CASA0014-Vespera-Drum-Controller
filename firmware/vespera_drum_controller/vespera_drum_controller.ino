// =====================================================
// RHYTHM OF LIGHT
// Stage 05 - Three Piezo Drums
//
// Arduino MKR WiFi 1010
// MQTT + Vespera + 3 Piezo Sensors
//
// Drum 1: A0 -> LEDs 0-23
// Drum 2: A1 -> LEDs 24-47
// Drum 3: A2 -> LEDs 48-71
//
// Features:
// - Three independent drum sensors
// - Three neon colour palettes
// - Different colours for different strengths
// - Simultaneous hit colour fusion
// - Smooth colour fading
// - Animated neon rainbow background
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
// 2. WIFI AND MQTT
// =====================================================

const char* WIFI_SSID = SECRET_SSID;

const char* WIFI_PASSWORD = SECRET_PASS;

const char* MQTT_USER = SECRET_MQTTUSER;

const char* MQTT_PASSWORD = SECRET_MQTTPASS;

const char* MQTT_SERVER = "mqtt.cetools.org";

const uint16_t MQTT_PORT = 1884;


// Your Vespera light ID
const char* LIGHT_TOPIC =
  "student/CASA0014/luminaire/17";


// =====================================================
// 3. THREE DRUM CONFIGURATION
// =====================================================

const int NUM_DRUMS = 3;

const int NUM_LEDS = 72;

const int LEDS_PER_DRUM = 24;

const int PAYLOAD_SIZE = NUM_LEDS * 3;


// Three actual piezo inputs
const uint8_t PIEZO_PINS[NUM_DRUMS] = {

  A0,
  A1,
  A2

};


// Each drum has its own sensitivity setting
const int HIT_THRESHOLD[NUM_DRUMS] = {

  25,   // Drum 1
  25,   // Drum 2
  25    // Drum 3

};


// Prevent one hit from registering repeatedly
const unsigned long COOLDOWN_MS = 130;


// Time to measure knock peak
const unsigned long PEAK_WINDOW_MS = 15;


// Hits within this window are treated as simultaneous
const unsigned long FUSION_WINDOW_MS = 130;


// Light fade duration
const unsigned long FADE_MS = 480;


// Animation update interval
const unsigned long FRAME_MS = 85;


// =====================================================
// 4. MQTT OBJECTS
// =====================================================

WiFiClient wifiClient;

PubSubClient mqttClient(wifiClient);

String clientId;


// Array storing the colours of 72 LEDs
uint8_t rgbPayload[PAYLOAD_SIZE];


// =====================================================
// 5. DRUM SENSOR VARIABLES
// =====================================================

// Resting voltage of each sensor
float baseline[NUM_DRUMS] = {

  0, 0, 0

};


// Latest analog readings
int latestRaw[NUM_DRUMS] = {

  0, 0, 0

};


// Whether each drum is currently measuring a hit
bool capturing[NUM_DRUMS] = {

  false, false, false

};


// Start time of each hit measurement
unsigned long captureStart[NUM_DRUMS] = {

  0, 0, 0

};


// Peak measurement of each hit
int capturePeak[NUM_DRUMS] = {

  0, 0, 0

};


// Most recent hit time of each drum
unsigned long lastHit[NUM_DRUMS] = {

  0, 0, 0

};


// Strength of each drum: 0.0 to 1.0
float force[NUM_DRUMS] = {

  0, 0, 0

};


// =====================================================
// 6. COLOUR FUSION VARIABLES
// =====================================================

// Indicates which drums are in the fusion group
uint8_t fusionMask = 0;


// First hit in the current group
unsigned long fusionStart = 0;


// Latest hit in the group
unsigned long fusionLastHit = 0;


// =====================================================
// 7. ANIMATION VARIABLES
// =====================================================

uint8_t rainbowPhase = 0;

unsigned long lastFrame = 0;

unsigned long lastDebug = 0;

unsigned long lastWifiAttempt = 0;

unsigned long lastMqttAttempt = 0;


// =====================================================
// 8. RGB COLOUR STRUCTURE
// =====================================================

struct RGB {

  float r;
  float g;
  float b;

};


// =====================================================
// 9. THREE DRUM COLOUR PALETTES
// =====================================================

RGB drumColour(int drum, float strength) {

  strength = constrain(
    strength,
    0.0f,
    1.0f
  );


  // -------------------------------------
  // Drum 1: Purple -> Neon Pink
  // -------------------------------------

  if (drum == 0) {

    return {

      170.0f + 85.0f * strength,

      65.0f - 45.0f * strength,

      255.0f - 75.0f * strength

    };
  }


  // -------------------------------------
  // Drum 2: Cyan -> Mint Green
  // -------------------------------------

  if (drum == 1) {

    return {

      20.0f + 20.0f * strength,

      175.0f + 80.0f * strength,

      255.0f - 115.0f * strength

    };
  }


  // -------------------------------------
  // Drum 3: Gold -> Orange Red
  // -------------------------------------

  return {

    255.0f,

    175.0f - 120.0f * strength,

    45.0f - 25.0f * strength

  };
}


// =====================================================
// 10. RAINBOW COLOUR FUNCTION
// =====================================================

void rainbowRGB(
  uint8_t pos,
  uint8_t &r,
  uint8_t &g,
  uint8_t &b
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
// 11. COUNT HOW MANY DRUMS ARE IN A GROUP
// =====================================================

int countDrums(uint8_t mask) {

  int result = 0;

  for (int i = 0; i < NUM_DRUMS; i++) {

    if (mask & (1 << i)) {

      result++;
    }
  }

  return result;
}


// =====================================================
// 12. REGISTER A DRUM HIT
// =====================================================

void registerHit(
  int drum,
  int peak,
  unsigned long now
) {

  lastHit[drum] = now;


  // Convert peak value into strength
  float scaled =

    (peak - HIT_THRESHOLD[drum]) / 350.0f;


  force[drum] = constrain(

    scaled,

    0.0f,

    1.0f
  );


  // -------------------------------------
  // Print hit information
  // -------------------------------------

  Serial.print("DRUM ");

  Serial.print(drum + 1);

  Serial.print(" HIT! peak delta=");

  Serial.print(peak);

  Serial.print(" strength=");

  Serial.println(
    (int)(35.0f + force[drum] * 65.0f)
  );


  // -------------------------------------
  // Detect simultaneous hits
  // -------------------------------------

  if (

    fusionMask == 0 ||

    now - fusionStart > FUSION_WINDOW_MS

  ) {

    // Start a new group
    fusionMask = (1 << drum);

    fusionStart = now;

  } else {

    // Add drum to existing group
    fusionMask |= (1 << drum);
  }


  fusionLastHit = now;


  // -------------------------------------
  // Show fusion in Serial Monitor
  // -------------------------------------

  if (countDrums(fusionMask) > 1) {

    Serial.print("FUSION: ");

    for (int i = 0; i < NUM_DRUMS; i++) {

      if (fusionMask & (1 << i)) {

        Serial.print("Drum ");

        Serial.print(i + 1);

        Serial.print(" ");
      }
    }

    Serial.println();
  }
}


// =====================================================
// 13. READ ALL THREE PIEZO SENSORS
// =====================================================

void sampleAllDrums(unsigned long now) {

  for (int i = 0; i < NUM_DRUMS; i++) {


    // Read actual sensor voltage
    int raw = analogRead(PIEZO_PINS[i]);

    latestRaw[i] = raw;


    // Difference from resting value
    int delta = abs(
      raw - (int)baseline[i]
    );


    // -------------------------------------
    // Update resting baseline
    // -------------------------------------

    if (

      !capturing[i] &&

      delta < 15

    ) {

      baseline[i] =

        0.995f * baseline[i] +

        0.005f * raw;
    }


    // -------------------------------------
    // Capture hit peak
    // -------------------------------------

    if (capturing[i]) {

      if (delta > capturePeak[i]) {

        capturePeak[i] = delta;
      }


      if (

        now - captureStart[i] >= PEAK_WINDOW_MS

      ) {

        capturing[i] = false;

        registerHit(

          i,

          capturePeak[i],

          now
        );
      }

    } else if (

      delta >= HIT_THRESHOLD[i] &&

      now - lastHit[i] >= COOLDOWN_MS

    ) {

      capturing[i] = true;

      captureStart[i] = now;

      capturePeak[i] = delta;
    }
  }


  // -------------------------------------
  // Print all sensor readings
  // -------------------------------------

  if (now - lastDebug >= 650) {

    lastDebug = now;


    Serial.print("A0=");

    Serial.print(latestRaw[0]);


    Serial.print(" A1=");

    Serial.print(latestRaw[1]);


    Serial.print(" A2=");

    Serial.println(latestRaw[2]);
  }
}


// =====================================================
// 14. CALCULATE FUSED COLOUR
// =====================================================

RGB fusionColour() {

  RGB mixed = {

    0, 0, 0

  };


  float weightSum = 0.0f;


  for (int i = 0; i < NUM_DRUMS; i++) {


    // Skip drums not involved in fusion
    if (!(fusionMask & (1 << i))) {

      continue;
    }


    // Get each drum's current colour
    RGB c = drumColour(

      i,

      force[i]
    );


    // Stronger hits influence the mixture more
    float weight =

      0.45f +

      0.55f * force[i];


    mixed.r += c.r * weight;

    mixed.g += c.g * weight;

    mixed.b += c.b * weight;


    weightSum += weight;
  }


  if (weightSum > 0.0f) {

    mixed.r /= weightSum;

    mixed.g /= weightSum;

    mixed.b /= weightSum;
  }


  return mixed;
}


// =====================================================
// 15. CONVERT COLOUR TO BYTE
// =====================================================

uint8_t toByte(float value) {

  return (uint8_t)constrain(

    value,

    0.0f,

    255.0f
  );
}


// =====================================================
// 16. CREATE THREE-ZONE LIGHTING ANIMATION
// =====================================================

void renderLights(unsigned long now) {


  // -------------------------------------
  // Is there a current multi-drum fusion?
  // -------------------------------------

  bool fusionActive =

    countDrums(fusionMask) >= 2 &&

    now - fusionLastHit < FADE_MS;


  RGB blend = {

    0, 0, 0

  };


  float blendAlpha = 0.0f;


  // -------------------------------------
  // Calculate fusion colour and brightness
  // -------------------------------------

  if (fusionActive) {

    blend = fusionColour();


    float strongest = 0.0f;


    for (int i = 0; i < NUM_DRUMS; i++) {

      if (

        (fusionMask & (1 << i)) &&

        force[i] > strongest

      ) {

        strongest = force[i];
      }
    }


    float fade =

      1.0f -

      (now - fusionLastHit) / (float)FADE_MS;


    blendAlpha =

      (0.50f + 0.50f * strongest) *

      fade;
  }


  // -------------------------------------
  // Calculate every LED colour
  // -------------------------------------

  for (int pixel = 0; pixel < NUM_LEDS; pixel++) {


    // Determine which drum owns this LED
    int drum = pixel / LEDS_PER_DRUM;


    // -------------------------------------
    // Animated rainbow background
    // -------------------------------------

    uint8_t r, g, b;


    rainbowRGB(

      (pixel * 256 / NUM_LEDS +
       rainbowPhase) & 255,

      r,

      g,

      b
    );


    // Moving brightness wave
    float wave =

      0.5f +

      0.5f * sinf(

        pixel * 0.34f +

        now * 0.003f
      );


    float baseBrightness =

      0.17f +

      0.13f * wave;


    RGB base = {

      r * baseBrightness,

      g * baseBrightness,

      b * baseBrightness

    };


    RGB chosen = base;

    float alpha = 0.0f;


    // -------------------------------------
    // MULTIPLE DRUMS: FUSED COLOUR
    // -------------------------------------

    if (

      fusionActive &&

      (fusionMask & (1 << drum))

    ) {

      chosen = blend;

      alpha = blendAlpha;

    } else {


      // -------------------------------------
      // SINGLE DRUM: ITS OWN COLOUR
      // -------------------------------------

      unsigned long age =

        now - lastHit[drum];


      if (age < FADE_MS) {


        float fade =

          1.0f -

          age / (float)FADE_MS;


        chosen = drumColour(

          drum,

          force[drum]
        );


        // Minimum brightness makes light taps visible
        alpha =

          (0.48f + 0.52f * force[drum]) *

          fade;
      }
    }


    // -------------------------------------
    // BLEND ACTIVE COLOUR WITH BACKGROUND
    // -------------------------------------

    rgbPayload[pixel * 3 + 0] = toByte(

      base.r * (1.0f - alpha) +

      chosen.r * alpha
    );


    rgbPayload[pixel * 3 + 1] = toByte(

      base.g * (1.0f - alpha) +

      chosen.g * alpha
    );


    rgbPayload[pixel * 3 + 2] = toByte(

      base.b * (1.0f - alpha) +

      chosen.b * alpha
    );
  }


  // Move rainbow animation
  rainbowPhase += 2;
}


// =====================================================
// 17. WIFI AND MQTT CONNECTION
// =====================================================

void serviceNetwork(unsigned long now) {


  // -------------------------------------
  // WiFi connection
  // -------------------------------------

  if (WiFi.status() != WL_CONNECTED) {

    if (

      lastWifiAttempt == 0 ||

      now - lastWifiAttempt >= 10000UL

    ) {

      lastWifiAttempt = now;

      Serial.println("Connecting to WiFi...");

      WiFi.begin(

        WIFI_SSID,

        WIFI_PASSWORD
      );
    }

    return;
  }


  // -------------------------------------
  // MQTT connection
  // -------------------------------------

  if (!mqttClient.connected()) {

    if (

      lastMqttAttempt == 0 ||

      now - lastMqttAttempt >= 5000UL

    ) {

      lastMqttAttempt = now;

      Serial.println("Connecting to MQTT...");


      if (

        mqttClient.connect(

          clientId.c_str(),

          MQTT_USER,

          MQTT_PASSWORD

        )

      ) {

        Serial.println("MQTT connected");

      } else {

        Serial.print("MQTT state=");

        Serial.println(mqttClient.state());
      }
    }

    return;
  }


  mqttClient.loop();
}


// =====================================================
// 18. SETUP
// =====================================================

void setup() {

  Serial.begin(115200);


  // 10-bit analog readings: 0-1023
  analogReadResolution(10);


  Serial.println(
    "Stage 05: Three Drum Colour Fusion"
  );


  // -------------------------------------
  // Calibrate all three sensors
  // -------------------------------------

  // Do not hit drums during calibration

  for (int drum = 0; drum < NUM_DRUMS; drum++) {


    pinMode(

      PIEZO_PINS[drum],

      INPUT
    );


    long sum = 0;


    for (int n = 0; n < 100; n++) {

      sum += analogRead(

        PIEZO_PINS[drum]
      );

      delay(2);
    }


    baseline[drum] = sum / 100.0f;


    Serial.print("A");

    Serial.print(drum);

    Serial.print(" baseline=");

    Serial.println(
      (int)baseline[drum]
    );
  }


  // -------------------------------------
  // Unique MQTT client ID
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

    String("MKR1010_ThreeDrums_") +

    macText;


  // -------------------------------------
  // MQTT configuration
  // -------------------------------------

  mqttClient.setServer(

    MQTT_SERVER,

    MQTT_PORT
  );


  mqttClient.setBufferSize(512);

  mqttClient.setKeepAlive(30);

  mqttClient.setSocketTimeout(2);


  // Start WiFi / MQTT
  serviceNetwork(millis());
}


// =====================================================
// 19. MAIN LOOP
// =====================================================

void loop() {

  unsigned long now = millis();


  // -------------------------------------
  // Step 1: Read all three real sensors
  // -------------------------------------

  sampleAllDrums(now);


  // -------------------------------------
  // Step 2: Maintain network
  // -------------------------------------

  serviceNetwork(now);


  // -------------------------------------
  // Step 3: Generate light animation
  // -------------------------------------

  if (now - lastFrame >= FRAME_MS) {

    lastFrame = now;


    renderLights(now);


    // -------------------------------------
    // Step 4: Send all 72 LEDs to Vespera
    // -------------------------------------

    if (mqttClient.connected()) {

      bool success = mqttClient.publish(

        LIGHT_TOPIC,

        rgbPayload,

        PAYLOAD_SIZE
      );


      if (!success) {

        Serial.println("MQTT publish failed");
      }
    }
  }
}
