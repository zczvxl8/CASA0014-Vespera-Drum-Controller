
// =============================================
// CASA0014 - Interactive Three Drum Controller
// Arduino MKR WiFi 1010
//
// Drum 1: A0 -> LEDs 0-23
// Drum 2: A1 -> LEDs 24-47
// Drum 3: A2 -> LEDs 48-71
//
// Three independent piezo drum pads
// MQTT RGB output to Lumi / Vespera
// =============================================

#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"
#include <utility/wifi_drv.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
// =============================================
// 1. WiFi and MQTT
// =============================================

const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;

const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;

const char* mqtt_server = "mqtt.cetools.org";
const int mqtt_port = 1884;

String lightId = "17";

String mqtt_topic =
  "student/CASA0014/luminaire/" + lightId;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

String clientId = "";

unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;

// =============================================
// 2. TEST MODE
// =============================================


const bool SERIAL_TEST_ONLY = false;

// =============================================
// 3. Three piezo sensors
// =============================================

const int drumCount = 3;

// =====================================
// Buzzer Settings 
// =====================================

// =====================================
// OLED Display
// =====================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 oled(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1);

bool oledReady = false;

const char* oledMode = "READY";


// =====================================
// OLED Drum Animation
// =====================================


byte oledScene = 0;

bool drumStickDown = false;

unsigned long lastOledAnimation = 0;

char oledHitTitle[24] = "";
char oledHitStrength[24] = "";

// =====================================
// Two-player Score
// =====================================

int player1Score = 0;
int player2Score = 0;

int oledDrum = 0;
int oledPeak = 0;
int oledLevel = 0;
int oledComboCount = 0;

unsigned long lastOledHitTime = 0;
unsigned long lastOledUpdate = 0;

const int buzzerPin = 3;

bool drumSoundPlaying = false;

int soundDrum = 0;

int soundLevel = 0;

unsigned long soundStartTime = 0;

unsigned long lastSoundUpdate = 0;

const int drumPins[drumCount] = {
  A0, A1, A2
};

int hitThreshold[drumCount] = {
  20, 20, 20
};

int releaseThreshold[drumCount] = {
  30, 30, 30
};

const unsigned long cooldown = 30;

const unsigned long sampleWindow = 15;

unsigned long lastHitTime[drumCount] = {
  0, 0, 0
};

unsigned long hitStartTime[drumCount] = {
  0, 0, 0
};

bool drumActive[drumCount] = {
  false, false, false
};

bool drumArmed[drumCount] = {
  true, true, true
};

int drumStrength[drumCount] = {
  0, 0, 0
};

// =============================================
// 4. Light settings
// =============================================

const int num_leds = 72;
const int payload_size = num_leds * 3;

byte RGBpayload[payload_size];


const byte idlePalette[3][3] = {

  { 4, 28, 23 }, 

  { 25, 6, 29 },  

  { 30, 12, 3 }  
};



const byte levelBrightness[5] = {
  90, 130, 170, 215, 255
};

const byte levelLedCount[5] = {
  6, 10, 15, 20, 24
};

const unsigned long levelFadeMs[5] = {
  600, 850, 1100, 1500, 1900
};

const byte drumPalette[3][5][3] = {

  {
    { 70, 235, 205 },
    { 5, 210, 205 },
    { 5, 165, 240 },
    { 60, 110, 250 },
    { 125, 75, 245 }
  },

  {
    { 205, 165, 250 },
    { 220, 110, 245 },
    { 250, 70, 215 },
    { 250, 55, 160 },
    { 225, 35, 115 }
  },

  {
    { 255, 205, 105 },
    { 255, 170, 70 },
    { 255, 130, 65 },
    { 250, 95, 90 },
    { 230, 65, 105 }
  }
};


const int strengthBreaks[3][4] = {

  { 85, 170, 300, 470 },  // Drum 1

  { 85, 170, 300, 470 },  // Drum 2

  { 85, 170, 300, 470 }  // Drum 3
};

byte hitR[3] = { 0, 0, 0 };
byte hitG[3] = { 0, 0, 0 };
byte hitB[3] = { 0, 0, 0 };

byte hitLevel[3] = { 0, 0, 0 };

unsigned long comboPulseUntil[3] = { 0, 0, 0 };

const unsigned long comboPulseMs = 160;


const unsigned long comboWindow = 200;

unsigned long hitGroup[3] = { 0, 0, 0 };

unsigned long hitGroupStart[3] = { 0, 0, 0 };

unsigned long currentGroup = 0;

unsigned long lastGroupStart = 0;


const int ledsPerDrum = 24;

const unsigned long fadeDuration = 2000;

const unsigned long frameInterval = 100;

unsigned long lastFrameTime = 0;

bool previousFrameActive = false;

bool forceLightPublish = true;

// =============================================
// Button test: record / stop / playback
// =============================================


const int recordButtonPin = 2;

const unsigned long buttonDebounce = 30;

bool lastButtonRaw = HIGH;
bool stableButtonState = HIGH;

unsigned long lastButtonChange = 0;


byte switchStep = 0;

bool testRecording = false;

bool imitating = false;

const int MAX_FRAMES = 300;

struct SavedFrame {

  unsigned long timeMs;

  byte rgb[3][3];

  byte activeCount[3];
};

SavedFrame savedFrames[MAX_FRAMES];

int savedFrameCount = 0;

int nextPlaybackFrame = 0;

unsigned long recordStartMs = 0;

unsigned long recordDurationMs = 0;

unsigned long playbackStartMs = 0;

bool playingBack = false;

void saveCurrentFrame();


const int MAX_RHYTHM_HITS = 60;

struct RhythmHit {

  byte drum;

  byte strength;

  unsigned long timeMs;
};

RhythmHit originalRhythm[MAX_RHYTHM_HITS];

RhythmHit imitationRhythm[MAX_RHYTHM_HITS];

int originalHitCount = 0;
int imitationHitCount = 0;

unsigned long firstOriginalHit = 0;
unsigned long firstImitationHit = 0;



void recordRhythmHit(int drum, int level) {

  unsigned long now = millis();

  
  if (testRecording) {

    if (originalHitCount >= MAX_RHYTHM_HITS) {
      return;
    }

    if (originalHitCount == 0) {
      firstOriginalHit = now;
    }

    RhythmHit& hit =
      originalRhythm[originalHitCount];

    hit.drum = drum;
    hit.strength = level;
    hit.timeMs = now - firstOriginalHit;

    originalHitCount++;

    Serial.print("P1 HIT: Drum ");
    Serial.print(drum + 1);

    Serial.print(" Strength ");
    Serial.print(level);

    Serial.print(" Time ");
    Serial.println(hit.timeMs);
  }


  else if (imitating) {

    if (imitationHitCount >= MAX_RHYTHM_HITS) {
      return;
    }

    if (imitationHitCount == 0) {
      firstImitationHit = now;
    }

    RhythmHit& hit =
      imitationRhythm[imitationHitCount];

    hit.drum = drum;
    hit.strength = level;
    hit.timeMs = now - firstImitationHit;

    imitationHitCount++;

    Serial.print("P2 HIT: Drum ");
    Serial.print(drum + 1);

    Serial.print(" Strength ");
    Serial.print(level);

    Serial.print(" Time ");
    Serial.println(hit.timeMs);
  }
}



int calculateImitationScore() {

  if (originalHitCount == 0 || imitationHitCount == 0) {

    return 0;
  }

  int compareCount = min(
    originalHitCount,
    imitationHitCount);

 
  int totalHits = max(
    originalHitCount,
    imitationHitCount);

  int totalPoints = 0;

  for (int i = 0; i < compareCount; i++) {

   
    if (originalRhythm[i].drum != imitationRhythm[i].drum) {

      continue;
    }

    totalPoints += 40;


    
    int rhythmPoints = 40;

    if (i > 0) {

      long originalGap =
        (long)originalRhythm[i].timeMs - (long)originalRhythm[i - 1].timeMs;

      long imitationGap =
        (long)imitationRhythm[i].timeMs - (long)imitationRhythm[i - 1].timeMs;

      long timingError =
        abs(originalGap - imitationGap);

      
      rhythmPoints = constrain(
        40 - (int)(timingError / 10),
        0,
        40);
    }

    totalPoints += rhythmPoints;



    int strengthError = abs(
      (int)originalRhythm[i].strength - (int)imitationRhythm[i].strength);

    int strengthPoints = constrain(
      20 - strengthError * 5,
      0,
      20);

    totalPoints += strengthPoints;
  }

  int finalScore =
    (totalPoints + totalHits / 2) / totalHits;

  return constrain(finalScore, 0, 100);
}

void drumCheckNetwork() {

  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED) {

    if (lastWiFiAttempt == 0 || now - lastWiFiAttempt >= 10000) {

      lastWiFiAttempt = now;

      Serial.println("Connecting to WiFi...");

      WiFi.begin(ssid, password);
    }

    return;
  }

  if (!mqttClient.connected()) {

    if (lastMQTTAttempt == 0 || now - lastMQTTAttempt >= 3000) {

      lastMQTTAttempt = now;

      Serial.println("Connecting to MQTT...");

      if (mqttClient.connect(
            clientId.c_str(),
            mqtt_username,
            mqtt_password)) {

        Serial.println("MQTT connected!");
        forceLightPublish = true;

      } else {

        Serial.print("MQTT failed: ");
        Serial.println(mqttClient.state());
      }
    }

    return;
  }

  mqttClient.loop();
}

// =============================================
// 6. Read all three sensors
// =============================================

// =====================================
// OLED 
// =====================================

// =====================================
// Draw Animated Drum Icon
// =====================================

void drawMiniDrum(int x, int y, bool hit) {

  // 鼓的主体
  oled.drawRoundRect(
    x + 3,
    y + 14,
    21,
    13,
    3,
    SSD1306_WHITE);

  // 鼓面
  oled.drawFastHLine(
    x + 5,
    y + 16,
    17,
    SSD1306_WHITE);

  // 鼓的两条支撑腿
  oled.drawLine(
    x + 7, y + 27,
    x + 5, y + 31,
    SSD1306_WHITE);

  oled.drawLine(
    x + 20, y + 27,
    x + 22, y + 31,
    SSD1306_WHITE);

  
  if (hit) {

    oled.drawLine(
      x + 5, y + 5,
      x + 14, y + 14,
      SSD1306_WHITE);

    oled.drawLine(
      x + 23, y + 5,
      x + 15, y + 14,
      SSD1306_WHITE);

    oled.drawPixel(
      x + 1, y + 13,
      SSD1306_WHITE);

    oled.drawPixel(
      x + 26, y + 13,
      SSD1306_WHITE);

  } else {

    oled.drawLine(
      x, y,
      x + 11, y + 10,
      SSD1306_WHITE);

    oled.drawLine(
      x + 27, y,
      x + 16, y + 10,
      SSD1306_WHITE);
  }
}

// =====================================
// OLED Starting Animation
// =====================================

void drawOLEDStartScreen() {

  if (!oledReady) {
    return;
  }

  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(28, 3);
  oled.println("RHYTHM LIGHT");

  drawMiniDrum(
    50,
    19,
    drumStickDown);

  oled.setCursor(22, 55);
  oled.println("READY TO PLAY");

  oled.display();
}

// =====================================
// OLED Drum Hit Screen
// =====================================

void drawOLEDHitScreen() {

  if (!oledReady) {
    return;
  }

  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 8);
  oled.println(oledHitTitle);

  oled.setCursor(0, 27);
  oled.println(oledHitStrength);

  oled.setCursor(0, 46);
  oled.println(oledMode);

  drawMiniDrum(
    100,
    18,
    drumStickDown);

  oled.display();
}

// =====================================
// Update OLED Drum Animation
// =====================================

void updateDrumOLEDAnimation() {

  if (!oledReady) {
    return;
  }

  if (oledScene == 2) {
    return;
  }

  if (millis() - lastOledAnimation < 180) {
    return;
  }

  lastOledAnimation = millis();

  drumStickDown = !drumStickDown;

  if (oledScene == 0) {

    drawOLEDStartScreen();

  } else if (oledScene == 1) {

    drawOLEDHitScreen();
  }
}

void oledShowMessage(
  const char* line1,
  const char* line2,
  const char* line3) {

  if (!oledReady) {
    return;
  }

  oledScene = 2;

  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  oled.setCursor(0, 8);
  oled.println(line1);

  oled.setCursor(0, 27);
  oled.println(line2);

  oled.setCursor(0, 46);
  oled.println(line3);

  oled.display();
}

// =====================================
// OLED 
// =====================================

void oledShowHit(int drumIndex, int peak) {

  if (!oledReady) {
    return;
  }

  char drumText[24];
  char strengthText[24];

  snprintf(
    drumText,
    sizeof(drumText),
    "DRUM %d HIT!",
    drumIndex + 1);

  int strength = map(
    peak,
    hitThreshold[drumIndex],
    700,
    0,
    100);

  strength = constrain(strength, 0, 100);

  snprintf(
    strengthText,
    sizeof(strengthText),
    "Strength: %d/100",
    strength);

  snprintf(
    oledHitTitle,
    sizeof(oledHitTitle),
    "%s",
    drumText);

  snprintf(
    oledHitStrength,
    sizeof(oledHitStrength),
    "%s",
    strengthText);

  oledScene = 1;

  drawOLEDHitScreen();
}

void readDrumPeaks(int peaks[]) {

  for (int i = 0; i < drumCount; i++) {
    peaks[i] = 0;
  }

  unsigned long start = millis();

  while (millis() - start < sampleWindow) {

    for (int i = 0; i < drumCount; i++) {

      int value = analogRead(drumPins[i]);

      if (value > peaks[i]) {
        peaks[i] = value;
      }
    }
  }
}


void startDrumSound(int drum, int level) {

  soundDrum = drum;
  soundLevel = level;

  soundStartTime = millis();
  lastSoundUpdate = 0;

  drumSoundPlaying = true;
}



void updateDrumSound() {

  if (!drumSoundPlaying) {
    return;
  }

  unsigned long now = millis();

  unsigned long elapsed =
    now - soundStartTime;

  unsigned long soundDuration;

  if (soundDrum == 0) {

    soundDuration = 220 + soundLevel * 12;

  } else if (soundDrum == 1) {

    soundDuration = 100 + soundLevel * 8;

  } else if (soundDrum == 2) {

    soundDuration = 60 + soundLevel * 6;

  } else if (soundDrum == 3) {

    soundDuration = 240 + soundLevel * 12;

  } else {

    soundDuration = 330 + soundLevel * 12;
  }

  if (elapsed >= soundDuration) {

    noTone(buzzerPin);
    drumSoundPlaying = false;

    return;
  }

  if (now - lastSoundUpdate < 10) {
    return;
  }

  lastSoundUpdate = now;

  int frequency = 0;


  if (soundDrum == 0) {

    int startFrequency = 1200 + soundLevel * 100;
    int endFrequency = 400;

    frequency =
      startFrequency - (long)(startFrequency - endFrequency) * elapsed / soundDuration;
  }


  
  else if (soundDrum == 2) {

    if ((elapsed / 10) % 2 == 0) {

      frequency = 2800 + soundLevel * 120;

    } else {

      frequency = 3500 + soundLevel * 120;
    }
  }


  else if (soundDrum == 3) {

    if ((elapsed / 30) % 2 == 0) {

      frequency = 1100 + soundLevel * 100;

    } else {

      frequency = 1900 + soundLevel * 100;
    }
  }


  else if (soundDrum == 4) {

    int sweep =
      2500 + soundLevel * 90 - (long)800 * elapsed / soundDuration;

    if ((elapsed / 15) % 2 == 0) {

      frequency = sweep + 350;

    } else {

      frequency = sweep - 350;
    }
  }


  
  else {

    if ((elapsed / 10) % 2 == 0) {

      frequency = 2800 + soundLevel * 120;

    } else {

      frequency = 3500 + soundLevel * 120;
    }
  }

  tone(buzzerPin, frequency);
}

// =============================================
// 7. Detect drum hits
// =============================================


void detectDrums(int peaks[]) {

  unsigned long now = millis();


  for (int i = 0; i < drumCount; i++) {

    if (peaks[i] < releaseThreshold[i]) {
      drumArmed[i] = true;
    }

    if (drumArmed[i] && peaks[i] > hitThreshold[i] && now - lastHitTime[i] > cooldown) {

      drumArmed[i] = false;

      lastHitTime[i] = now;
      hitStartTime[i] = now;

      drumActive[i] = true;

      
      int level = 0;

      while (level < 4 && peaks[i] >= strengthBreaks[i][level]) {

        level++;
      }
      recordRhythmHit(i, level);
      oledShowHit(i, peaks[i]);

      // =====================================
      // Update OLED Hit Data
      // =====================================

      oledDrum = i + 1;

      oledPeak = peaks[i];

      oledLevel = level + 1;

      lastOledHitTime = now;


      // =====================================
      // Add Competition Score
      // =====================================

      if (i == 0) {

        // A0 = Player 1
        player1Score++;

      } else if (i == 1) {

        // A1 = Player 2
        player2Score++;
      }
      startDrumSound(i, level);

      hitR[i] = drumPalette[i][level][0];
      hitG[i] = drumPalette[i][level][1];
      hitB[i] = drumPalette[i][level][2];

      hitLevel[i] = level;

      drumStrength[i] = levelBrightness[level];

      
      if (currentGroup == 0 || now - lastGroupStart > comboWindow) {

        currentGroup++;
        lastGroupStart = now;
      }

      hitGroup[i] = currentGroup;

      hitGroupStart[i] = lastGroupStart;


      oledComboCount = 0;

      for (int k = 0; k < drumCount; k++) {

        if (hitGroup[k] == currentGroup && now - lastHitTime[k] <= comboWindow) {

          oledComboCount++;
        }
      }

      int comboCount = 0;

      for (int k = 0; k < drumCount; k++) {

        if (hitGroup[k] == currentGroup &&
            now - lastHitTime[k] <= comboWindow) {

          comboCount++;
        }
      }


      if (comboCount >= 2) {

        for (int k = 0; k < drumCount; k++) {

          if (hitGroup[k] == currentGroup &&
              now - lastHitTime[k] <= comboWindow) {

            comboPulseUntil[k] =
              now + comboPulseMs;
          }
        }
      }
 
      if (comboCount == 1) {

        startDrumSound(i, level);

      } else if (comboCount == 2) {

        startDrumSound(3, level);

      } else if (comboCount >= 3) {

        startDrumSound(4, level);
      }



      Serial.print("DRUM ");
      Serial.print(i + 1);

      Serial.print(" HIT! Peak=");
      Serial.print(peaks[i]);

      Serial.print(" Level=");
      Serial.print(level + 1);

      Serial.print(" Group=");
      Serial.println(hitGroup[i]);
    }
  }
}


// =============================================
// 8. Generate 3 independent light zones
// =============================================


// =============================================
// 8. Generate 3 independent light zones
// =============================================


void fillIdleLights() {

  for (int drum = 0; drum < drumCount; drum++) {

    for (int p = drum * ledsPerDrum;
         p < (drum + 1) * ledsPerDrum;
         p++) {

      for (int c = 0; c < 3; c++) {

        RGBpayload[p * 3 + c] =
          idlePalette[drum][c];
      }
    }
  }
}


void updateDrumLights() {

  unsigned long now = millis();


  
  if (now - lastFrameTime < frameInterval) {
    return;
  }

  lastFrameTime = now;


  fillIdleLights();

  bool anyActive = false;


  
  for (int drum = 0; drum < drumCount; drum++) {


    if (!drumActive[drum]) {
      continue;
    }


    byte level = hitLevel[drum];


    unsigned long duration =
      levelFadeMs[level];


    unsigned long elapsed =
      now - hitStartTime[drum];


    if (elapsed >= duration) {

      drumActive[drum] = false;

      continue;
    }


    anyActive = true;


    int brightness =
      drumStrength[drum];



    if ((long)(comboPulseUntil[drum] - now) > 0) {

      brightness =
        min(255, brightness + 35);
    }


    int targetR =
      hitR[drum] * brightness / 255;

    int targetG =
      hitG[drum] * brightness / 255;

    int targetB =
      hitB[drum] * brightness / 255;


  
    unsigned long remaining =
      duration - elapsed;


    int r =
      idlePalette[drum][0] +
      (long)(targetR - idlePalette[drum][0])
      * remaining / duration;


    int g =
      idlePalette[drum][1] +
      (long)(targetG - idlePalette[drum][1])
      * remaining / duration;


    int b =
      idlePalette[drum][2] +
      (long)(targetB - idlePalette[drum][2])
      * remaining / duration;


  

    byte count =
      levelLedCount[level];


    int firstPixel =
      drum * ledsPerDrum +
      (ledsPerDrum - count) / 2;



    for (int p = firstPixel;
         p < firstPixel + count;
         p++) {

      RGBpayload[p * 3 + 0] = (byte)r;

      RGBpayload[p * 3 + 1] = (byte)g;

      RGBpayload[p * 3 + 2] = (byte)b;
    }
  }


 

  saveCurrentFrame();


  bool shouldPublish =
    anyActive ||
    previousFrameActive ||
    forceLightPublish;


  previousFrameActive = anyActive;


  // 串口测试模式不发送MQTT
  if (SERIAL_TEST_ONLY || !shouldPublish) {
    return;
  }



  if (mqttClient.connected()) {

    bool success = mqttClient.publish(
      mqtt_topic.c_str(),
      RGBpayload,
      payload_size
    );


    if (success) {

      forceLightPublish = false;

    } else {

      Serial.println("MQTT publish failed");

      forceLightPublish = true;
    }
  }
}

void saveCurrentFrame() {

  if (!testRecording || playingBack) {
    return;
  }

  if (savedFrameCount >= MAX_FRAMES) {

    testRecording = false;

    recordDurationMs = millis() - recordStartMs;

    Serial.println("RECORD FULL - AUTO STOP");

    return;
  }

  SavedFrame& frame = savedFrames[savedFrameCount++];

  frame.timeMs = millis() - recordStartMs;

  for (int drum = 0; drum < drumCount; drum++) {

    int centrePixelByte =
      (drum * ledsPerDrum + ledsPerDrum / 2) * 3;

    frame.activeCount[drum] = drumActive[drum]
      ? levelLedCount[hitLevel[drum]]
      : 0;

    for (int colour = 0; colour < 3; colour++) {

      frame.rgb[drum][colour] =
        RGBpayload[centrePixelByte + colour];
    }
  }
}

void startFramePlayback() {

  if (testRecording) {

    Serial.println("STOP RECORDING FIRST");

    return;
  }

  if (savedFrameCount == 0) {

    Serial.println("NOTHING RECORDED");

    return;
  }
  if (SERIAL_TEST_ONLY || WiFi.status() != WL_CONNECTED || !mqttClient.connected()) {

    Serial.println("PLAYBACK FAILED: MQTT NOT CONNECTED");

    oledMode = "READY";

    oledShowMessage(
      "PLAYBACK",
      "NO MQTT",
      "Check connection");

    return;
  }

  playingBack = true;

  nextPlaybackFrame = 0;

  playbackStartMs = millis();

  for (int drum = 0; drum < drumCount; drum++) {

    drumActive[drum] = false;
  }

  previousFrameActive = false;

  fillIdleLights();

  if (!SERIAL_TEST_ONLY && mqttClient.connected()) {

    mqttClient.publish(
      mqtt_topic.c_str(),
      RGBpayload,
      payload_size);
  }

  Serial.print("PLAYBACK START, frames = ");

  Serial.println(savedFrameCount);

  oledMode = "PLAYBACK";

  oledShowMessage(
    "PLAYBACK",
    "STARTED",
    "Playing lights...");
}

void playbackSavedFrames() {

  if (!playingBack) {
    return;
  }

  unsigned long elapsed =
    millis() - playbackStartMs
    + savedFrames[0].timeMs;

  int newestDue = -1;

  while (
    nextPlaybackFrame < savedFrameCount && elapsed >= savedFrames[nextPlaybackFrame].timeMs) {

    newestDue = nextPlaybackFrame;

    nextPlaybackFrame++;
  }

  if (newestDue >= 0) {

    SavedFrame& frame = savedFrames[newestDue];

    
    fillIdleLights();


    for (int drum = 0; drum < drumCount; drum++) {

      byte count = frame.activeCount[drum];


      int firstPixel =
        drum * ledsPerDrum +
        (ledsPerDrum - count) / 2;


      for (int pixel = firstPixel;
           pixel < firstPixel + count;
           pixel++) {

        for (int colour = 0; colour < 3; colour++) {

          RGBpayload[pixel * 3 + colour] =
            frame.rgb[drum][colour];
        }
      }
    }

    if (!SERIAL_TEST_ONLY && mqttClient.connected()) {

      mqttClient.publish(
        mqtt_topic.c_str(),
        RGBpayload,
        payload_size);
    }

    if (
      newestDue % 10 == 0 || newestDue == savedFrameCount - 1) {

      Serial.print("PLAY FRAME ");

      Serial.println(newestDue + 1);
    }
  }

  if (
    nextPlaybackFrame >= savedFrameCount && elapsed >= savedFrames[savedFrameCount - 1].timeMs + frameInterval) {

    
    if (switchStep == 3 && stableButtonState == LOW) {

      Serial.println("PLAYBACK REPEAT");

      nextPlaybackFrame = 0;

      playbackStartMs = millis();

      return;
    }

    playingBack = false;

    fillIdleLights();

    forceLightPublish = true;

    if (!SERIAL_TEST_ONLY && mqttClient.connected()) {

      mqttClient.publish(
        mqtt_topic.c_str(),
        RGBpayload,
        payload_size);
    }

    Serial.println("PLAYBACK FINISHED");

    oledMode = "READY";

    oledShowMessage(
      "PLAYBACK",
      "FINISHED",
      "");
  }
}

// =============================================
// 9. Setup
// =============================================
// =============================================
// Check external button
// =============================================


void testRecordButton() {

  unsigned long now = millis();

  bool reading = digitalRead(recordButtonPin);

  if (reading != lastButtonRaw) {

    lastButtonRaw = reading;
    lastButtonChange = now;
  }

  if (now - lastButtonChange < buttonDebounce) {
    return;
  }

  if (reading == stableButtonState) {
    return;
  }

  stableButtonState = reading;

  Serial.print("SWITCH: ");
  Serial.println(reading == LOW ? "ON" : "OFF");


  if (switchStep == 0 && reading == LOW) {

    originalHitCount = 0;
    imitationHitCount = 0;

    firstOriginalHit = 0;
    firstImitationHit = 0;

    savedFrameCount = 0;
    nextPlaybackFrame = 0;

    playingBack = false;
    imitating = false;
    testRecording = true;

    recordStartMs = now;
    recordDurationMs = 0;

    switchStep = 1;

    Serial.println("PLAYER 1 RECORD START");

    oledMode = "RECORDING";

    oledShowMessage(
      "PLAYER 1",
      "RECORDING",
      "Play your rhythm!");
  }




  else if (switchStep == 1 && reading == HIGH) {

    testRecording = false;

    recordDurationMs = now - recordStartMs;

    Serial.println("PLAYER 1 RECORD STOP");

    Serial.print("Recorded hits: ");
    Serial.println(originalHitCount);

    Serial.print("Saved light frames: ");
    Serial.println(savedFrameCount);

    oledMode = "READY";

    if (originalHitCount > 0 && savedFrameCount > 0) {

      switchStep = 2;

      oledShowMessage(
        "PLAYER 1",
        "RHYTHM SAVED",
        "ON: Playback");

    } else {

      switchStep = 0;

      oledShowMessage(
        "NO RECORDING",
        "DATA FOUND",
        "Try again");
    }
  }


 

  else if (switchStep == 2 && reading == LOW) {

    Serial.println("PLAYBACK REQUESTED");

    if (SERIAL_TEST_ONLY || WiFi.status() != WL_CONNECTED || !mqttClient.connected()) {

      oledMode = "READY";

      oledShowMessage(
        "PLAYBACK",
        "NO MQTT",
        "Check connection");

      Serial.println("PLAYBACK FAILED: NO MQTT");

    } else {

      startFramePlayback();

      if (playingBack) {

        switchStep = 3;

        noTone(buzzerPin);
        drumSoundPlaying = false;

        oledMode = "PLAYBACK";

        oledShowMessage(
          "PLAYBACK",
          "LOOPING",
          "OFF: Next stage");
      }
    }
  }


  
  else if (switchStep == 3 && reading == HIGH) {

      playingBack = false;

    fillIdleLights();

    forceLightPublish = true;

    if (!SERIAL_TEST_ONLY && mqttClient.connected()) {

      mqttClient.publish(
        mqtt_topic.c_str(),
        RGBpayload,
        payload_size);
    }

    switchStep = 4;

    oledMode = "READY";

    Serial.println("PLAYBACK STOPPED");
    Serial.println("PLAYER 2 GET READY");

    oledShowMessage(
      "PLAYER 2",
      "GET READY",
      "ON: Imitation");
  }


 

  else if (switchStep == 4 && reading == LOW) {

    imitationHitCount = 0;
    firstImitationHit = 0;

    testRecording = false;
    playingBack = false;

    imitating = true;

    switchStep = 5;

    oledMode = "IMITATING";

    Serial.println("PLAYER 2 IMITATION START");

    oledShowMessage(
      "PLAYER 2",
      "IMITATION",
      "STARTED");
  }




  else if (switchStep == 5 && reading == HIGH) {

    imitating = false;

    int finalScore = calculateImitationScore();

    Serial.println("IMITATION FINISHED");

    Serial.print("Player 1 hits: ");
    Serial.println(originalHitCount);

    Serial.print("Player 2 hits: ");
    Serial.println(imitationHitCount);

    Serial.print("FINAL SCORE: ");
    Serial.println(finalScore);

    char scoreText[24];

    snprintf(
      scoreText,
      sizeof(scoreText),
      "SCORE: %d/100",
      finalScore);

    oledMode = "READY";

    oledShowMessage(
      "FINAL SCORE",
      scoreText,
      "ON: New round");

    switchStep = 0;
  }
}

// =====================================
// OLED Message Display
// =====================================



// =====================================
// OLED Live Dashboard
// =====================================



void setup() {

  Serial.begin(115200);

  // =====================================
  // Initialize OLED
  // =====================================

  Wire.begin();
  Wire.setClock(400000);

  byte oledAddress = 0;

  Wire.beginTransmission(0x3C);

  if (Wire.endTransmission() == 0) {
    oledAddress = 0x3C;
  } else {

    Wire.beginTransmission(0x3D);

    if (Wire.endTransmission() == 0) {
      oledAddress = 0x3D;
    }
  }

  if (oledAddress != 0) {

    oledReady = oled.begin(
      SSD1306_SWITCHCAPVCC,
      oledAddress);

  } else {

    oledReady = false;
  }

  if (oledReady) {

    oledScene = 0;

    drumStickDown = false;

    drawOLEDStartScreen();

    Serial.println("OLED connected!");

  } else {

    Serial.println("OLED not detected.");
  }
  analogReadResolution(10);

  pinMode(recordButtonPin, INPUT_PULLUP);

  lastButtonRaw = digitalRead(recordButtonPin);

  stableButtonState = lastButtonRaw;

  switchStep = 0;

  pinMode(buzzerPin, OUTPUT);

  for (int i = 0; i < drumCount; i++) {
    pinMode(drumPins[i], INPUT);
  }

  Serial.println();
  Serial.println("THREE DRUM CONTROLLER STARTING");

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(30);

  byte mac[6];
  WiFi.macAddress(mac);

  char macStr[18];

  snprintf(
    macStr,
    sizeof(macStr),
    "%02X%02X%02X%02X%02X%02X",
    mac[5], mac[4], mac[3],
    mac[2], mac[1], mac[0]);

  clientId = "MKR1010_3DRUM_" + String(macStr);

  if (SERIAL_TEST_ONLY) {

    Serial.println("SERIAL TEST MODE");
    Serial.println("MQTT disabled");

  } else {

    Serial.print("MQTT topic: ");
    Serial.println(mqtt_topic);
  }

  Serial.println("Ready!");
}

// =============================================
// 10. Main loop
// =============================================

void loop() {

  testRecordButton();

  if (!SERIAL_TEST_ONLY) {
    drumCheckNetwork();
  }

  if (playingBack) {

    playbackSavedFrames();

    return;
  }

  int peaks[drumCount];
  readDrumPeaks(peaks);

  static unsigned long lastReport = 0;

  if (millis() - lastReport >= 1000) {

    lastReport = millis();

    Serial.print("Peaks A0=");
    Serial.print(peaks[0]);

    Serial.print(" A1=");
    Serial.print(peaks[1]);

    Serial.print(" A2=");
    Serial.println(peaks[2]);
  }

  detectDrums(peaks);

  updateDrumSound();

  updateDrumLights();
  updateDrumOLEDAnimation();
}
