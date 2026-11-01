/*
  RHYTHM OF LIGHT — TEST 05: OLED DISPLAY

  Arduino MKR WiFi 1010, 128x64 SSD1306 I2C OLED.
  Real drum sensors: A0 / A1 / A2
  Existing latching switch: D2 to GND, INPUT_PULLUP

  Tests:
    - OLED connection and 128x64 layout
    - Live real drum hits, peak and relative strength
    - Animated drum drawing on the right
    - Switch ON/OFF state text using the physical switch

  IMPORTANT: Switch ON/OFF does NOT record or replay anything.
  Recording and playback are implemented later, in Test 06.

  Libraries: Wire (built in), Adafruit GFX, Adafruit SSD1306.
  No WiFi, MQTT, LED output, or buzzer output in this test.
  Keep the three piezo input protection circuits in place.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const byte DRUM_COUNT = 3;
const byte DRUM_PINS[DRUM_COUNT] = {A0, A1, A2};
const byte SWITCH_PIN = 2;  // D2, latching switch to GND

const byte OLED_WIDTH = 128;
const byte OLED_HEIGHT = 64;
const int OLED_RESET = -1;  // The four-pin I2C module has no reset line.
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;
byte oledAddress = 0x3C;

// Independent trigger thresholds. Raise a value for false hits;
// lower it carefully for missed gentle taps.
int HIT_THRESHOLD[DRUM_COUNT] = {40, 48, 55};
const int STRONG_PEAK[DRUM_COUNT] = {300, 150, 550};

const unsigned long PEAK_WINDOW_MS = 12;
const unsigned long COOLDOWN_MS = 150;
const unsigned long REARM_QUIET_MS = 25;
const unsigned long SWITCH_DEBOUNCE_MS = 35;
const unsigned long OLED_FRAME_MS = 160;
const unsigned long HIT_VISIBLE_MS = 1450;
const unsigned long SWITCH_VISIBLE_MS = 1250;

float baseline[DRUM_COUNT] = {0, 0, 0};
bool capturing[DRUM_COUNT] = {false, false, false};
bool armed[DRUM_COUNT] = {true, true, true};
bool hasHit[DRUM_COUNT] = {false, false, false};
int capturePeak[DRUM_COUNT] = {0, 0, 0};
unsigned long captureStart[DRUM_COUNT] = {0, 0, 0};
unsigned long lastHitTime[DRUM_COUNT] = {0, 0, 0};
unsigned long quietStart[DRUM_COUNT] = {0, 0, 0};

int latestDrum = -1;
int latestPeak = 0;
int latestStrength = 0;
unsigned long latestHitAt = 0;

int rawSwitchState = HIGH;
int stableSwitchState = HIGH;
unsigned long rawSwitchChangedAt = 0;
unsigned long latestSwitchChangeAt = 0;
bool switchHasChanged = false;

unsigned long lastOledFrame = 0;

int relativeStrength(byte drum, int peak) {
  int lower = HIT_THRESHOLD[drum];
  int upper = STRONG_PEAK[drum];
  if (upper <= lower) upper = lower + 1;
  long result = 1L + (long)(peak - lower) * 99L / (upper - lower);
  if (result < 1) result = 1;
  if (result > 100) result = 100;
  return (int)result;
}

bool switchIsOn() {
  return stableSwitchState == LOW;
}

void reportRealHit(byte drum, int peak, unsigned long now) {
  latestDrum = drum + 1;
  latestPeak = peak;
  latestStrength = relativeStrength(drum, peak);
  latestHitAt = now;
  hasHit[drum] = true;
  lastHitTime[drum] = now;

  Serial.print(F("[HIT] DRUM "));
  Serial.print(latestDrum);
  Serial.print(F("  PEAK="));
  Serial.print(latestPeak);
  Serial.print(F("  STRENGTH="));
  Serial.print(latestStrength);
  Serial.println(F("/100"));
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
        reportRealHit(drum, capturePeak[drum], now);
        armed[drum] = false;
        quietStart[drum] = 0;
      }
      continue;
    }

    if (!armed[drum]) {
      if (delta <= releaseLimit) {
        if (quietStart[drum] == 0) quietStart[drum] = now;
        if (now - quietStart[drum] >= REARM_QUIET_MS &&
            now - lastHitTime[drum] >= COOLDOWN_MS) {
          armed[drum] = true;
        }
      } else {
        quietStart[drum] = 0;
      }
      continue;
    }

    if (delta >= HIT_THRESHOLD[drum] &&
        (!hasHit[drum] || now - lastHitTime[drum] >= COOLDOWN_MS)) {
      capturing[drum] = true;
      captureStart[drum] = now;
      capturePeak[drum] = delta;
      armed[drum] = false;
    }
  }
}

void pollSwitch(unsigned long now) {
  int reading = digitalRead(SWITCH_PIN);
  if (reading != rawSwitchState) {
    rawSwitchState = reading;
    rawSwitchChangedAt = now;
  }

  if (rawSwitchState != stableSwitchState &&
      now - rawSwitchChangedAt >= SWITCH_DEBOUNCE_MS) {
    stableSwitchState = rawSwitchState;
    latestSwitchChangeAt = now;
    switchHasChanged = true;
    Serial.print(F("[SWITCH] "));
    Serial.println(switchIsOn() ? F("ON") : F("OFF"));
    Serial.println(F("[INFO] Switch test only; no recording/playback yet."));
  }
}

// The right-hand icon is hand-drawn with Adafruit GFX primitives.
// The sticks move slowly on READY and rapidly after a real drum hit.
void drawAnimatedDrum(unsigned long now, bool hitRecent) {
  const int left = 92;
  const int top = 33;
  const int width = 31;
  const int height = 19;

  // Body and drum rim.
  oled.drawRoundRect(left, top, width, height, 5, SSD1306_WHITE);
  oled.drawLine(left + 2, top + 4, left + width - 3,
                top + 4, SSD1306_WHITE);
  oled.drawLine(left + 7, top + height, left + 5,
                top + height + 6, SSD1306_WHITE);
  oled.drawLine(left + width - 8, top + height,
                left + width - 6, top + height + 6, SSD1306_WHITE);
  oled.drawPixel(left + 5, top + 11, SSD1306_WHITE);
  oled.drawPixel(left + width - 6, top + 11, SSD1306_WHITE);

  // Two animated drum sticks.
  unsigned long period = hitRecent ? 110UL : 420UL;
  bool beat = ((now / period) % 2UL) == 0UL;
  int tipY = beat ? top + 2 : top - 5;
  oled.drawLine(left + 4, top - 19,
                left + 13, tipY, SSD1306_WHITE);
  oled.drawLine(left + width - 5, top - 19,
                left + width - 14, beat ? top - 5 : top + 2,
                SSD1306_WHITE);
}

void drawOLED(unsigned long now) {
  if (!oledReady) return;

  bool recentHit = latestDrum > 0 && now - latestHitAt < HIT_VISIBLE_MS;
  bool showSwitchStatus = switchHasChanged &&
                          now - latestSwitchChangeAt < SWITCH_VISIBLE_MS;

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  // The left section occupies x=0..83. Icon occupies x=90..125.
  oled.drawLine(86, 0, 86, 63, SSD1306_WHITE);

  if (showSwitchStatus) {
    oled.setCursor(0, 4);
    oled.print(F("SWITCH"));
    oled.setCursor(0, 18);
    oled.print(switchIsOn() ? F("ON") : F("OFF"));
    oled.setCursor(0, 34);
    oled.print(F("TEST ONLY"));
    oled.setCursor(0, 48);
    oled.print(F("NO RECORD"));
  } else if (recentHit) {
    oled.setCursor(0, 4);
    oled.print(F("DRUM "));
    oled.print(latestDrum);
    oled.print(F(" HIT!"));
    oled.setCursor(0, 19);
    oled.print(F("STR: "));
    oled.print(latestStrength);
    oled.print(F("/100"));
    oled.setCursor(0, 34);
    oled.print(F("PEAK: "));
    oled.print(latestPeak);
    oled.setCursor(0, 50);
    oled.print(F("SW: "));
    oled.print(switchIsOn() ? F("ON") : F("OFF"));
  } else {
    oled.setCursor(0, 4);
    oled.print(F("READY"));
    oled.setCursor(0, 20);
    oled.print(F("TAP A DRUM"));
    oled.setCursor(0, 37);
    oled.print(F("LIVE INPUT"));
    oled.setCursor(0, 51);
    oled.print(F("SW: "));
    oled.print(switchIsOn() ? F("ON") : F("OFF"));
  }

  drawAnimatedDrum(now, recentHit);
  oled.display();
}

bool detectAndStartOLED() {
  // Check the I2C bus: begin() alone does not confirm a display ACK.
  const byte candidates[2] = {0x3C, 0x3D};
  bool found = false;
  for (byte index = 0; index < 2; ++index) {
    Wire.beginTransmission(candidates[index]);
    if (Wire.endTransmission() == 0) {
      oledAddress = candidates[index];
      found = true;
      break;
    }
  }

  if (!found) {
    Serial.println(F("[OLED ERROR] No device ACK at 0x3C or 0x3D."));
    Serial.println(F("Check 3.3V, GND, SDA and SCL wiring."));
    return false;
  }

  if (!oled.begin(SSD1306_SWITCHCAPVCC, oledAddress)) {
    Serial.println(F("[OLED ERROR] SSD1306 begin failed."));
    return false;
  }

  Serial.print(F("[OLED] Initialized at address 0x"));
  Serial.println(oledAddress, HEX);
  oled.clearDisplay();
  oled.display();
  return true;
}

void setup() {
  Serial.begin(115200);
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 2500UL) {}

  analogReadResolution(10);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  rawSwitchState = digitalRead(SWITCH_PIN);
  stableSwitchState = rawSwitchState;

  Serial.println(F("=========================================="));
  Serial.println(F("TEST 05: OLED DISPLAY + REAL DRUM DATA"));
  Serial.println(F("A0/A1/A2 = drums, D2 = latching switch"));
  Serial.println(F("Switch ON/OFF is display-only in this test."));
  Serial.println(F("Keep drums quiet during calibration."));

  for (byte drum = 0; drum < DRUM_COUNT; ++drum) {
    pinMode(DRUM_PINS[drum], INPUT);
    long sum = 0;
    for (int sample = 0; sample < 100; ++sample) {
      sum += analogRead(DRUM_PINS[drum]);
      delay(2);
    }
    baseline[drum] = sum / 100.0f;
    Serial.print(F("[CAL] DRUM "));
    Serial.print(drum + 1);
    Serial.print(F(" BASELINE="));
    Serial.println((int)baseline[drum]);
  }

  Wire.begin();
  Wire.setClock(400000UL);  // Faster full-screen transfers.
  oledReady = detectAndStartOLED();

  Serial.println(F("[READY] Tap each drum. Toggle D2 to check status text."));
  drawOLED(millis());
  lastOledFrame = millis();
}

void loop() {
  unsigned long now = millis();
  sampleRealDrums(now);
  pollSwitch(now);

  // Rate-limit I2C screen refreshes so analog sampling remains responsive.
  if (now - lastOledFrame >= OLED_FRAME_MS) {
    lastOledFrame = now;
    drawOLED(now);
  }
}
