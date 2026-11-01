/*
  RHYTHM OF LIGHT — TEST 04: PASSIVE BUZZER

  Arduino MKR WiFi 1010
  REAL piezo drums: A0, A1, A2
  ONE passive buzzer: signal (+) -> D3, negative (-) -> GND

  Expected sounds:
    Drum 1 / A0: KICK (short descending low pitch)
    Drum 2 / A1: SNARE (rapid alternating pitches)
    Drum 3 / A2: HI-HAT (short high-pitched ticks)
    Two hits within 90 ms: DOUBLE sound
    Three hits within 90 ms: TRIPLE sound

  Hit strength changes pitch and duration. A simple passive buzzer
  cannot reproduce a sampled drum or offer genuine volume control.

  This standalone test uses no WiFi, MQTT, OLED or LED code.
  Keep all piezo input protection circuits connected correctly.
*/

const byte BUZZER_PIN = 3;  // D3, not A3
const byte DRUM_COUNT = 3;
const byte DRUM_PINS[DRUM_COUNT] = {A0, A1, A2};

// Adjust separately if a drum triggers without a hit, or misses light taps.
int HIT_THRESHOLD[DRUM_COUNT] = {40, 48, 55};
const int STRONG_PEAK[DRUM_COUNT] = {300, 150, 550};

const unsigned long PEAK_WINDOW_MS = 12;
const unsigned long COOLDOWN_MS = 150;
const unsigned long REARM_QUIET_MS = 25;
const unsigned long COMBO_WINDOW_MS = 90;

float baseline[DRUM_COUNT] = {0, 0, 0};
bool capturing[DRUM_COUNT] = {false, false, false};
bool armed[DRUM_COUNT] = {true, true, true};
bool hasHit[DRUM_COUNT] = {false, false, false};
int capturePeak[DRUM_COUNT] = {0, 0, 0};
unsigned long captureStart[DRUM_COUNT] = {0, 0, 0};
unsigned long lastHitTime[DRUM_COUNT] = {0, 0, 0};
unsigned long quietStart[DRUM_COUNT] = {0, 0, 0};
unsigned long totalHits[DRUM_COUNT] = {0, 0, 0};

byte comboMask = 0;
unsigned long comboFirstTime = 0;
unsigned long lastSummary = 0;

enum SoundKind {
  SOUND_NONE = -1,
  SOUND_KICK = 0,
  SOUND_SNARE = 1,
  SOUND_HAT = 2,
  SOUND_DOUBLE = 3,
  SOUND_TRIPLE = 4
};

// Frequencies are a deliberately buzzer-friendly approximation,
// not samples of an acoustic drum kit.
const uint16_t SOUND_FREQ[5][8] = {
  {175, 145, 115, 95, 80, 70, 60, 55},       // KICK
  {370, 780, 320, 720, 260, 610, 0, 0},    // SNARE
  {2100, 1550, 2500, 1750, 0, 0, 0, 0},    // HI-HAT
  {750, 1280, 850, 1450, 690, 1100, 0, 0},// DOUBLE
  {1950, 1580, 1220, 900, 710, 520, 380, 260} // TRIPLE
};

const byte SOUND_STEPS[5] = {8, 6, 4, 6, 8};
const byte SOUND_STEP_MS[5] = {12, 10, 10, 11, 12};

SoundKind currentSound = SOUND_NONE;
unsigned long soundStart = 0;
byte previousSoundStep = 255;
int soundStrength = 50;

int countBits(byte mask) {
  int count = 0;
  for (byte i = 0; i < DRUM_COUNT; ++i) {
    if (mask & (1 << i)) ++count;
  }
  return count;
}

int hitStrength(byte drum, int peak) {
  int bottom = HIT_THRESHOLD[drum];
  int upper = STRONG_PEAK[drum];
  if (upper <= bottom) upper = bottom + 1;
  long value = 1 + (long)(peak - bottom) * 99 / (upper - bottom);
  if (value < 1) value = 1;
  if (value > 100) value = 100;
  return (int)value;
}

const char* soundName(SoundKind sound) {
  switch (sound) {
    case SOUND_KICK: return "KICK";
    case SOUND_SNARE: return "SNARE";
    case SOUND_HAT: return "HI-HAT";
    case SOUND_DOUBLE: return "DOUBLE";
    case SOUND_TRIPLE: return "TRIPLE";
    default: return "NONE";
  }
}

void updateSound(unsigned long now) {
  if (currentSound == SOUND_NONE) return;
  byte kind = (byte)currentSound;

  // Stronger hits sound slightly longer and higher in pitch.
  unsigned long stepMs = SOUND_STEP_MS[kind] + soundStrength / 25;
  byte step = (byte)((now - soundStart) / stepMs);

  if (step >= SOUND_STEPS[kind]) {
    noTone(BUZZER_PIN);
    currentSound = SOUND_NONE;
    return;
  }

  if (step != previousSoundStep) {
    previousSoundStep = step;
    int freq = SOUND_FREQ[kind][step];
    freq = freq * (100 + soundStrength / 4) / 100;
    if (freq > 0) tone(BUZZER_PIN, freq);
    else noTone(BUZZER_PIN);
  }
}

void playSound(SoundKind kind, int strength, unsigned long now) {
  noTone(BUZZER_PIN); // One buzzer can play one tone at a time.
  currentSound = kind;
  soundStrength = strength;
  if (soundStrength < 1) soundStrength = 1;
  if (soundStrength > 100) soundStrength = 100;
  soundStart = now;
  previousSoundStep = 255;
  updateSound(now);
}

void registerHit(byte drum, int peak, unsigned long now) {
  hasHit[drum] = true;
  lastHitTime[drum] = now;
  totalHits[drum]++;
  int strength = hitStrength(drum, peak);

  if (comboMask == 0 || now - comboFirstTime > COMBO_WINDOW_MS) {
    comboMask = (1 << drum);
    comboFirstTime = now;
  } else {
    comboMask |= (1 << drum);
  }

  int hitCount = countBits(comboMask);
  SoundKind kind = (SoundKind)drum;
  if (hitCount == 2) kind = SOUND_DOUBLE;
  if (hitCount >= 3) kind = SOUND_TRIPLE;
  playSound(kind, strength, now);

  Serial.print(F("[HIT] DRUM "));
  Serial.print(drum + 1);
  Serial.print(F(" | PEAK_DELTA="));
  Serial.print(peak);
  Serial.print(F(" | THRESHOLD="));
  Serial.print(HIT_THRESHOLD[drum]);
  Serial.print(F(" | STRENGTH="));
  Serial.print(strength);
  Serial.print(F("/100 | SOUND="));
  Serial.println(soundName(kind));

  if (hitCount >= 2) {
    Serial.print(F("[COMBO] "));
    Serial.print(hitCount);
    Serial.println(F(" drums detected. The buzzer switched to a combined sound."));
  }
}

void sampleRealDrums(unsigned long now) {
  for (byte drum = 0; drum < DRUM_COUNT; ++drum) {
    int raw = analogRead(DRUM_PINS[drum]);
    int delta = raw - (int)baseline[drum];
    if (delta < 0) delta = -delta;
    int releaseLimit = HIT_THRESHOLD[drum] * 45 / 100;

    // Slowly follow the resting voltage, but never an active hit.
    if (!capturing[drum] && delta < releaseLimit) {
      baseline[drum] = 0.998f * baseline[drum] + 0.002f * raw;
    }

    if (capturing[drum]) {
      if (delta > capturePeak[drum]) capturePeak[drum] = delta;
      if (now - captureStart[drum] >= PEAK_WINDOW_MS) {
        capturing[drum] = false;
        registerHit(drum, capturePeak[drum], now);
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

void printSummary() {
  Serial.print(F("[5s TOTALS] Drum1="));
  Serial.print(totalHits[0]);
  Serial.print(F(" Drum2="));
  Serial.print(totalHits[1]);
  Serial.print(F(" Drum3="));
  Serial.println(totalHits[2]);
}

void setup() {
  Serial.begin(115200);
  unsigned long waitStart = millis();
  while (!Serial && millis() - waitStart < 2500UL) {}

  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  analogReadResolution(10);

  Serial.println(F("=========================================="));
  Serial.println(F("TEST 04 — PASSIVE BUZZER + REAL DRUMS"));
  Serial.println(F("A0=KICK | A1=SNARE | A2=HI-HAT | BUZZER=D3"));
  Serial.println(F("Only ONE passive buzzer is required."));
  Serial.println(F("Keep all pads still for baseline calibration."));

  for (byte drum = 0; drum < DRUM_COUNT; ++drum) {
    pinMode(DRUM_PINS[drum], INPUT);
    long sum = 0;
    for (int sample = 0; sample < 100; ++sample) {
      sum += analogRead(DRUM_PINS[drum]);
      delay(2);
    }
    baseline[drum] = sum / 100.0f;
    Serial.print(F("[CAL] Drum "));
    Serial.print(drum + 1);
    Serial.print(F(" baseline="));
    Serial.println((int)baseline[drum]);
  }

  // Hardware self-test: this drives D3 directly. It does not pretend
  // that any piezo input was hit.
  Serial.println(F("[SELF TEST] Three sounds: kick, snare, hi-hat"));
  for (int kind = SOUND_KICK; kind <= SOUND_HAT; ++kind) {
    playSound((SoundKind)kind, 60, millis());
    while (currentSound != SOUND_NONE) updateSound(millis());
    delay(180);
  }
  Serial.println(F("[READY] Tap each real drum gently, then firmly."));
  Serial.println(F("Try two or three drums almost together for combined sounds."));
  lastSummary = millis();
}

void loop() {
  unsigned long now = millis();
  sampleRealDrums(now);
  updateSound(now); // Non-blocking sound envelope.
  if (now - lastSummary >= 5000UL) {
    printSummary();
    lastSummary = now;
  }
}
