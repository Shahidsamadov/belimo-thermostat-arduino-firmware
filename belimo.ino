/*
 * Belimo room thermostat CR24-B1 - custom Arduino firmware
 *
 * See README.md for the project description, wiring and pinout.
 * License: MIT (see the LICENSE file).
 *
 * Operating modes: 1 - heat, 2 - cool, 3 - eco (actuator closed).
 */

#include <EEPROM.h>

/* ---------------------------------- Pins ---------------------------------- */
#define pwm            11   // PWM output to the Belimo actuator
#define reg            A3   // setpoint regulator input (potentiometer)
#define THERMISTORPIN  A2   // NTC 4.7k thermistor (B = 3950)
#define heat            5   // red LED - "heat" mode
#define cool            6   // green LED - "cool" mode
#define eko             7   // blue LED - "eco" mode
#define ButPin          0   // mode button (conflicts with Serial, see README)

/* -------------------------------- Thermistor -------------------------------- */
#define THERMISTORNOMINAL  4700.0f   // resistance at 25 deg C, ohms
#define TEMPERATURENOMINAL 25.0f     // deg C
#define NUMSAMPLES          5        // number of samples to average
#define BCOEFFICIENT       3950.0f   // thermistor B coefficient
#define SERIESRESISTOR     6700.0f   // series resistor, ohms

/* ------------------------------ Setpoint and PWM ------------------------------ */
#define SETTEMP_MIN   19    // deg C, regulator at minimum (ADC code < 120)
#define SETTEMP_MAX   25    // deg C, regulator at maximum (ADC code >= 1000)
#define PWM_OFF        0    // actuator closed
#define PWM_MED      130    // half power
#define PWM_FULL     255    // full power
#define HYST_HALF    0.5f   // deg C, half-step threshold
#define HYST_FULL    1.0f   // deg C, full-step threshold

/* --------------------------------- Button --------------------------------- */
#define BUTTON_PULLUP 0     // 0 - external pull-down to GND (as in the original),
                            // 1 - internal pull-up, button shorts the pin to GND
#if BUTTON_PULLUP
  #define BUTTON_PIN_MODE INPUT_PULLUP
  #define BUTTON_PRESSED  LOW
#else
  #define BUTTON_PIN_MODE INPUT
  #define BUTTON_PRESSED  HIGH
#endif
#define BUTTON_IDLE   (BUTTON_PRESSED == HIGH ? LOW : HIGH)
#define DEBOUNCE_MS   30    // debounce time, ms

/* -------------------------------- Settings -------------------------------- */
#define USE_EXTERNAL_AREF 1 // 1 - external reference voltage on AREF
#define DEBUG_SERIAL      0 // 1 - debug output on Serial (uses pins 0/1)

/* --------------------------------- Modes --------------------------------- */
#define MODE_HEAT      1
#define MODE_COOL      2
#define MODE_ECO       3
#define MODE_DEFAULT   MODE_ECO  // mode used with a blank EEPROM (0xFF)
#define EEPROM_MODE_ADDR 0

/* --------------------------- Global variables -------------------------- */
int samples[NUMSAMPLES];
int readreg;                       // ADC code of the setpoint regulator
int settemp = SETTEMP_MIN;         // current setpoint, deg C
int regim   = MODE_DEFAULT;        // current mode
int address = EEPROM_MODE_ADDR;    // EEPROM address of the mode byte

/* Setpoint from the regulator position: thresholds are tested from the highest
 * to the lowest, so the whole regulator travel is covered without gaps. */
int setpointFromRegulator(int value) {
  if (value >= 1000) return SETTEMP_MAX;
  if (value >=  850) return 24;
  if (value >=  680) return 23;
  if (value >=  500) return 22;
  if (value >=  340) return 21;
  if (value >=  120) return 20;
  return SETTEMP_MIN;              // codes 0..119
}

/* Average of NUMSAMPLES thermistor readings converted to deg C
 * (B-parameter equation). */
float readTemperature(void) {
  long sum = 0;
  for (uint8_t i = 0; i < NUMSAMPLES; i++) {
    samples[i] = analogRead(THERMISTORPIN);
    sum += samples[i];
    delay(10);
  }
  float average = (float)sum / NUMSAMPLES;

  average = 1023.0f / average - 1.0f;
  average = SERIESRESISTOR / average;

  float steinhart = average / THERMISTORNOMINAL;       // (R/Ro)
  steinhart = log(steinhart);                          // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                           // 1/B * ln(R/Ro)
  steinhart += 1.0f / (TEMPERATURENOMINAL + 273.15f);  // + (1/To)
  steinhart = 1.0f / steinhart;                        // invert
  steinhart -= 273.15f;                                // to degrees Celsius
  return steinhart;
}

/* Mode indication: the LED of the active mode is on. */
void showMode(void) {
  digitalWrite(heat, regim == MODE_HEAT ? HIGH : LOW);
  digitalWrite(cool, regim == MODE_COOL ? HIGH : LOW);
  digitalWrite(eko,  regim == MODE_ECO  ? HIGH : LOW);
}

/* Mode button: debounce, mode change on press and EEPROM write on release
 * (only when the mode really changed). */
void handleButton(void) {
  static uint8_t lastReading = BUTTON_IDLE;
  static bool buttonDown = false;
  static unsigned long lastChange = 0;

  uint8_t reading = digitalRead(ButPin);
  if (reading != lastReading) {
    lastReading = reading;
    lastChange = millis();
  }
  if ((millis() - lastChange) < DEBOUNCE_MS) {
    return;                        // the level is still bouncing
  }

  if (reading == BUTTON_PRESSED) {
    if (!buttonDown) {             // press
      buttonDown = true;
      regim++;
      if (regim > MODE_ECO) {
        regim = MODE_HEAT;
      }
      showMode();
    }
  } else if (buttonDown) {         // release - store the mode
    buttonDown = false;
    if (EEPROM.read(address) != (uint8_t)regim) {
      EEPROM.write(address, (uint8_t)regim);
    }
  }
}

/* Actuator control: 0 / 130 / 255 depending on the deviation of the measured
 * temperature from the setpoint. The PWM value is always set explicitly. */
void updateActuator(float temperature) {
  int output = PWM_OFF;                                       // eco mode

  if (regim == MODE_HEAT) {                                   // heating
    if (temperature >= settemp)                  output = PWM_OFF;
    else if (temperature <= settemp - HYST_FULL) output = PWM_FULL;
    else if (temperature <= settemp - HYST_HALF) output = PWM_MED;
    else                                         output = PWM_OFF;
  } else if (regim == MODE_COOL) {                            // cooling
    if (temperature <= settemp)                  output = PWM_OFF;
    else if (temperature >= settemp + HYST_FULL) output = PWM_FULL;
    else if (temperature >= settemp + HYST_HALF) output = PWM_MED;
    else                                         output = PWM_OFF;
  }

  analogWrite(pwm, output);
}

void setup(void) {
  pinMode(pwm, OUTPUT);
  pinMode(heat, OUTPUT);
  pinMode(cool, OUTPUT);
  pinMode(eko, OUTPUT);
  pinMode(reg, INPUT);
  pinMode(ButPin, BUTTON_PIN_MODE);

  digitalWrite(heat, LOW);
  digitalWrite(cool, LOW);
  digitalWrite(eko, LOW);
  analogWrite(pwm, PWM_OFF);

#if DEBUG_SERIAL
  Serial.begin(9600);
#endif

#if USE_EXTERNAL_AREF
  analogReference(EXTERNAL);
#endif

  regim = EEPROM.read(address);
  if (regim < MODE_HEAT || regim > MODE_ECO) {
    regim = MODE_DEFAULT;          // a blank EEPROM returns 0xFF
  }
  showMode();
}

void loop(void) {
  readreg = analogRead(reg);
  settemp = setpointFromRegulator(readreg);

  handleButton();                       // mode change by button

  float steinhart = readTemperature();  // temperature, deg C
  updateActuator(steinhart);            // PWM to the Belimo actuator

#if DEBUG_SERIAL
  Serial.print(steinhart, 1);
  Serial.print(" *C, setpoint ");
  Serial.print(settemp);
  Serial.print(", mode ");
  Serial.println(regim);
#endif
}
