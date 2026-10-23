// Open-loop BLDC, 3x low-side N-channel MOSFET, center-tapped motor (unipolar 3-step)
// Target: ESP32 (Arduino-ESP32 core 2.x or 3.x), no external libraries.
// Serial 115200, commands end with newline:
//   d<0..100> duty %   f<1..500> elec Hz   p<1000..40000> PWM Hz   r<ms> ramp
//   x0/x1 direction    e0/e1 disable/enable   ? status

#include <Arduino.h>

// ---------------- User settings (defaults) ----------------
static const uint8_t PHASE_PIN[3] = {25, 26, 27};   // GPIO -> driver IN_A, IN_B, IN_C

uint32_t PWM_FREQ_HZ        = 20000;
float    DUTY_PERCENT       = 40.0f;
float    ELEC_FREQ_HZ       = 20.0f;
uint32_t RAMP_TIME_MS       = 2000;
float    START_FREQ_HZ      = 3.0f;
float    START_DUTY_PERCENT = 20.0f;
uint8_t  DIRECTION          = 0;       // 0 forward, 1 reverse
bool     ENABLE_ON_BOOT     = false;
bool     GATE_ACTIVE_HIGH   = true;     // false if driver inverts the gate signal

// ---------------- Internals ----------------
static const uint8_t  PWM_BITS = 10;
static const uint32_t PWM_MAX  = (1u << PWM_BITS) - 1;

bool     enabled     = false;
uint8_t  stepIdx     = 0;
uint32_t lastStepUs  = 0;
uint32_t runStartMs  = 0;

// ---- PWM abstraction (swap these three for another MCU) ----
void pwmAttach() {
  for (uint8_t i = 0; i < 3; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PHASE_PIN[i], PWM_FREQ_HZ, PWM_BITS);
#else
    ledcSetup(i, PWM_FREQ_HZ, PWM_BITS);
    ledcAttachPin(PHASE_PIN[i], i);
#endif
  }
}

void pwmSetFreq() {
  for (uint8_t i = 0; i < 3; i++) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcChangeFrequency(PHASE_PIN[i], PWM_FREQ_HZ, PWM_BITS);
#else
    ledcSetup(i, PWM_FREQ_HZ, PWM_BITS);
#endif
  }
}

void pwmWrite(uint8_t i, uint32_t duty) {
  if (!GATE_ACTIVE_HIGH) duty = PWM_MAX - duty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PHASE_PIN[i], duty);
#else
  ledcWrite(i, duty);
#endif
}

// ---- Output ----
void allOff() {
  for (uint8_t i = 0; i < 3; i++) pwmWrite(i, 0);
}

void applyOutput(float dutyPct) {
  uint32_t d = (uint32_t)(constrain(dutyPct, 0.0f, 100.0f) * PWM_MAX / 100.0f);
  for (uint8_t i = 0; i < 3; i++) pwmWrite(i, (i == stepIdx) ? d : 0);
}

// ---- Enable / disable ----
void setEnable(bool on) {
  enabled = on;
  if (on) {
    stepIdx    = 0;
    lastStepUs = micros();
    runStartMs = millis();
  } else {
    allOff();
  }
}

// ---- Status ----
void printStatus() {
  Serial.printf("en=%d dir=%d duty=%.1f%% f=%.1fHz pwm=%luHz ramp=%lums start=%.1fHz/%.1f%%\n",
                enabled, DIRECTION, DUTY_PERCENT, ELEC_FREQ_HZ,
                (unsigned long)PWM_FREQ_HZ, (unsigned long)RAMP_TIME_MS,
                START_FREQ_HZ, START_DUTY_PERCENT);
}

// ---- Serial commands ----
void handleCommand(const char *s) {
  char  c = tolower(s[0]);
  float v = atof(s + 1);
  switch (c) {
    case 'd': DUTY_PERCENT  = constrain(v, 0.0f, 100.0f); break;
    case 'f': ELEC_FREQ_HZ  = constrain(v, 1.0f, 500.0f); break;
    case 'p': PWM_FREQ_HZ   = (uint32_t)constrain(v, 1000.0f, 40000.0f); pwmSetFreq(); break;
    case 'r': RAMP_TIME_MS  = (uint32_t)max(v, 0.0f); runStartMs = millis(); break;
    case 'x': DIRECTION     = (v > 0.5f) ? 1 : 0; break;
    case 'e': setEnable(v > 0.5f); break;
    case '?': break;
    default:  Serial.println("cmds: d f p r x e ?"); return;
  }
  printStatus();
}

void readSerial() {
  static char    buf[24];
  static uint8_t n = 0;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (n) { buf[n] = 0; handleCommand(buf); n = 0; }
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = ch;
    }
  }
}

// ---- Arduino ----
void setup() {
  Serial.begin(115200);
  pwmAttach();
  allOff();
  setEnable(ENABLE_ON_BOOT);
  Serial.println("BLDC open-loop ready. Send ? for status, e1 to run.");
  printStatus();
}

void loop() {
  readSerial();
  if (!enabled) return;

  // Soft start: ramp frequency and duty from start values to targets
  uint32_t elapsed = millis() - runStartMs;
  float k = (RAMP_TIME_MS == 0) ? 1.0f : min(1.0f, elapsed / (float)RAMP_TIME_MS);

  float f0 = min(START_FREQ_HZ, ELEC_FREQ_HZ);
  float d0 = min(START_DUTY_PERCENT, DUTY_PERCENT);
  float f  = f0 + (ELEC_FREQ_HZ - f0) * k;
  float d  = d0 + (DUTY_PERCENT - d0) * k;

  // 3 steps per electrical cycle
  uint32_t stepPeriodUs = (uint32_t)(1000000.0f / (3.0f * f));
  uint32_t now = micros();
  if ((uint32_t)(now - lastStepUs) >= stepPeriodUs) {
    lastStepUs += stepPeriodUs;
    stepIdx = (DIRECTION == 0) ? (stepIdx + 1) % 3 : (stepIdx + 2) % 3;
  }
  applyOutput(d);
}
