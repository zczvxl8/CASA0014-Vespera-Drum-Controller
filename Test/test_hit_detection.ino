/*
  Rhythm of Light
  TEST 02 - DRUM HIT DETECTION

  Arduino MKR WiFi 1010

  Drum 1 -> A0
  Drum 2 -> A1
  Drum 3 -> A2

  Test:
  - Hit detection
  - Relative strength
  - Repeated triggers
  - Multi-drum triggering

  No WiFi / MQTT / Lights / OLED / Buzzer

  IMPORTANT:
  Verify piezo input protection before testing.
*/

const byte DRUM_COUNT = 3;

const int DRUM_PINS[DRUM_COUNT] = {
  A0, A1, A2
};


// =====================================
// 1. Hit detection settings
// =====================================

// Initial detection thresholds
int hitThreshold[DRUM_COUNT] = {
  40, 40, 40
};

// Signal must fall below these values
// before another hit can be detected.
int releaseThreshold[DRUM_COUNT] = {
  18, 28, 28
};

// Preliminary reference values
// for relative strength calculation.
// These are not calibrated physical forces.
int peakFor100[DRUM_COUNT] = {
  300, 130, 550
};

// Sampling time
const unsigned long SAMPLE_WINDOW_MS = 15;

// Prevent one hit from triggering repeatedly
const unsigned long COOLDOWN_MS = 160;

// Display summary every 5 seconds
const unsigned long SUMMARY_INTERVAL_MS = 5000;


// =====================================
// 2. Drum states
// =====================================

bool drumArmed[DRUM_COUNT] = {
  true, true, true
};

unsigned long lastHitMs[DRUM_COUNT] = {
  0, 0, 0
};

unsigned long hitCount[DRUM_COUNT] = {
  0, 0, 0
};

unsigned long lastSummaryMs = 0;


// =====================================
// 3. Read real piezo signals
// =====================================

void readDrumPeaks(int peaks[]) {

  for (byte i = 0; i < DRUM_COUNT; i++) {
    peaks[i] = 0;
  }

  unsigned long startMs = millis();

  while (
    millis() - startMs < SAMPLE_WINDOW_MS
  ) {

    for (byte i = 0; i < DRUM_COUNT; i++) {

      int value = analogRead(DRUM_PINS[i]);

      if (value > peaks[i]) {
        peaks[i] = value;
      }
    }
  }
}


// =====================================
// 4. Calculate relative strength
// =====================================

int relativeStrength(byte drum, int peak) {

  long result = map(
    peak,
    hitThreshold[drum],
    peakFor100[drum],
    1,
    100
  );

  if (result < 1) {
    result = 1;
  }

  if (result > 100) {
    result = 100;
  }

  return (int)result;
}


// =====================================
// 5. Strength classification
// =====================================

const char* strengthLevel(int strength) {

  if (strength < 34) {
    return "LIGHT";
  }

  if (strength < 67) {
    return "MEDIUM";
  }

  return "STRONG";
}


// =====================================
// 6. Detect drum hits
// =====================================

void detectDrumHits(int peaks[]) {

  unsigned long now = millis();

  byte detectedInWindow = 0;

  for (byte i = 0; i < DRUM_COUNT; i++) {

    // Re-arm after signal returns near idle
    if (peaks[i] < releaseThreshold[i]) {
      drumArmed[i] = true;
    }

    // Check for a real hit
    if (
      drumArmed[i] &&
      peaks[i] > hitThreshold[i] &&
      now - lastHitMs[i] >= COOLDOWN_MS
    ) {

      drumArmed[i] = false;

      lastHitMs[i] = now;

      hitCount[i]++;

      detectedInWindow++;

      int strength = relativeStrength(
        i,
        peaks[i]
      );

      // Print hit information
      Serial.print("[HIT] DRUM ");
      Serial.print(i + 1);

      Serial.print(" (A");
      Serial.print(i);

      Serial.print(") PEAK=");
      Serial.print(peaks[i]);

      Serial.print(" RELATIVE_STRENGTH=");
      Serial.print(strength);

      Serial.print("/100 LEVEL=");
      Serial.print(strengthLevel(strength));

      Serial.print(" COUNT=");
      Serial.println(hitCount[i]);
    }
  }

  // Multiple drums triggered in same window
  if (detectedInWindow >= 2) {

    Serial.println(
      "[MULTI] Multiple drums triggered together."
    );

    Serial.println(
      "Check vibration crosstalk if only one drum was tapped."
    );
  }
}


// =====================================
// 7. Print hit counts
// =====================================

void printHitCounts() {

  Serial.print("[5s SUMMARY] Drum 1=");
  Serial.print(hitCount[0]);

  Serial.print(" Drum 2=");
  Serial.print(hitCount[1]);

  Serial.print(" Drum 3=");
  Serial.println(hitCount[2]);
}


// =====================================
// 8. SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  unsigned long waitStartMs = millis();

  while (
    !Serial &&
    millis() - waitStartMs < 2500
  ) {
  }

  analogReadResolution(10);

  for (byte i = 0; i < DRUM_COUNT; i++) {

    pinMode(DRUM_PINS[i], INPUT);

    lastHitMs[i] =
      millis() - COOLDOWN_MS;
  }

  lastSummaryMs = millis();

  Serial.println(
    "===================================="
  );

  Serial.println(
    "RHYTHM OF LIGHT - TEST 02"
  );

  Serial.println(
    "DRUM HIT DETECTION"
  );

  Serial.println(
    "Arduino MKR WiFi 1010"
  );

  Serial.println(
    "A0 / A1 / A2"
  );

  Serial.println(
    "Baud Rate: 115200"
  );

  Serial.println(
    "===================================="
  );

  Serial.println(
    "First: leave drums untouched for 20-30 seconds."
  );

  Serial.println(
    "Then: tap each drum separately five times."
  );

  Serial.println(
    "Strength values are relative, not calibrated force."
  );
}


// =====================================
// 9. LOOP
// =====================================

void loop() {

  int peaks[DRUM_COUNT];

  // Read all three sensors
  readDrumPeaks(peaks);

  // Detect hits
  detectDrumHits(peaks);

  // Print summary
  unsigned long now = millis();

  if (
    now - lastSummaryMs >=
    SUMMARY_INTERVAL_MS
  ) {

    printHitCounts();

    lastSummaryMs = now;
  }
}
