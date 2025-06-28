/* =========================================================
 *  VALVE_V9.1 – I²C‑slave for Valve rotor (28‑Jun‑2025)
 *  ---------------------------------------------------------
 *  Board     : Arduino Mega 2560
 *  I²C Addr  : 0x05
 *
 *  Commands accepted (case‑sensitive, CR/LF ignored):
 *     • "VAL_HOME"  or legacy "START"
 *     • "VAL_90"
 *     • "VAL_180"
 *  Replies: "BUSY" while running, "DONE" when finished
 *
 *  Fixes in 9.1:
 *     – RX buffer is now plain `char` (not volatile) so Serial.print works
 *     – Added clean cast in Serial.debug print
 * =========================================================*/
#include <Wire.h>
#include <TMCStepper.h>
#include <SPI.h>
#include <MCUFRIEND_kbv.h>

/******************** PIN MAPS *****************************/
#define EN_PIN 53
const int STEP_P[4] = {25, 48, 40, 36};
const int DIR_P [4] = {27, 46, 42, 34};
const int CS_P  [4] = {23, 44, 38, 32};
const int SW_P  [4] = {A15, A14, A13, A12};

/******************** TMC5160 ******************************/
#define R_SENSE 0.11f
TMC5160Stepper* drv[4];

/******************** MOTION CONST *************************/
const int pulseHomeUs = 600;        // fast homing pulse
const int pulseMoveUs = 1200;       // normal rotation pulse
const int settleDelay = 5;          // ms between pulses when homing
const int maxSteps    = 4000;       // homing timeout guard

const int offsetStep[4] = {2330, 2370, 2300, 2310}; // laser‑zero offsets
const int STEPS_90 = 800;           // 90° @16 µsteps

/******************** I²C STATE *****************************/
#define I2C_ADDR 0x05
char  cmdBuf[20] = "";             // room for longest cmd + CRLF
volatile bool newCmd = false;
volatile bool busy   = false;

/******************** TFT UTIL ******************************/
MCUFRIEND_kbv tft;
void msg(const char* m) {
  Serial.println(m);
  tft.fillScreen(0x0000);
  tft.setTextColor(0xFFFF);
  tft.setTextSize(3);
  tft.setCursor(0, 90);
  tft.print(m);
}

/******************** LOW‑LEVEL MOVES ***********************/
void moveSteps(int rem[4], int dUs) {
  for (int i = 0; i < 4; i++) digitalWrite(DIR_P[i], LOW); // CCW
  bool done = false;
  while (!done) {
    done = true;
    for (int i = 0; i < 4; i++) {
      if (rem[i] > 0) {
        digitalWrite(STEP_P[i], HIGH);
        rem[i]--; done = false;
      }
    }
    delayMicroseconds(dUs);
    for (int i = 0; i < 4; i++) digitalWrite(STEP_P[i], LOW);
    delayMicroseconds(dUs);
  }
}

/******************** HOMING *******************************/
bool homeAll() {
  msg("Homing …");
  for (int i = 0; i < 4; i++) digitalWrite(DIR_P[i], LOW);
  int steps = 0; bool allHome = false;

  while (!allHome && steps < maxSteps) {
    allHome = true;
    for (int i = 0; i < 4; i++) {
      if (digitalRead(SW_P[i]) == LOW) {
        digitalWrite(STEP_P[i], HIGH);
        allHome = false;
      }
    }
    delayMicroseconds(pulseHomeUs);
    for (int i = 0; i < 4; i++) digitalWrite(STEP_P[i], LOW);
    delayMicroseconds(pulseHomeUs);
    delay(settleDelay);
    steps++;
  }
  msg(allHome ? "Homed OK" : "Homing TIMEOUT");
  return allHome;
}

/******************** HIGH‑LEVEL CMDS ***********************/
void do_VAL_HOME() {
  if (!homeAll()) return;
  delay(500);
  int rem[4]; for (int i = 0; i < 4; i++) rem[i] = offsetStep[i];
  msg("VAL_HOME offset");
  moveSteps(rem, pulseMoveUs);
  msg("VAL_HOME DONE");
}

void rotateAll(int steps) {
  int rem[4] = {steps, steps, steps, steps};
  moveSteps(rem, pulseMoveUs);
}

void do_VAL_90()  { msg("VAL_90");  rotateAll(STEPS_90);       msg("VAL_90 DONE");  }
void do_VAL_180() { msg("VAL_180"); rotateAll(STEPS_90*2);    msg("VAL_180 DONE"); }

/******************** I²C CALLBACKS ************************/
void onRecv(int) {
  int i = 0;
  while (Wire.available() && i < 19) {
    char c = Wire.read();
    if (c >= 32 && c <= 126)   // skip CR/LF and other control chars
      cmdBuf[i++] = c;
  }
  cmdBuf[i] = '\0';
  newCmd = true;
}
void onReq() { Wire.write(busy ? "BUSY" : "DONE"); }

/******************** SETUP ********************************/
void setup() {
  Serial.begin(115200);
  Serial.println(F("[VAL] Booting VALVE_V9.1"));

  uint16_t id = tft.readID(); if (id == 0xD3D3) id = 0x9486;
  tft.begin(id); tft.setRotation(1); msg("VALVE V9");

  pinMode(EN_PIN, OUTPUT); digitalWrite(EN_PIN, HIGH);
  for (int i = 0; i < 4; i++) {
    pinMode(STEP_P[i], OUTPUT); pinMode(DIR_P[i], OUTPUT);
    pinMode(CS_P[i], OUTPUT);   digitalWrite(CS_P[i], HIGH);
    pinMode(SW_P[i], INPUT_PULLUP);
  }

  SPI.begin();
  for (int i = 0; i < 4; i++) {
    drv[i] = new TMC5160Stepper(CS_P[i], R_SENSE);
    drv[i]->begin(); drv[i]->rms_current(600);
    drv[i]->microsteps(16); drv[i]->pwm_autoscale(true);
  }

  Wire.begin(I2C_ADDR);
  Wire.onReceive(onRecv); Wire.onRequest(onReq);

  Serial.println(F("[VAL] Ready – waiting for master"));
}

/******************** LOOP *********************************/
void loop() {
  if (!newCmd) return;
  newCmd = false;

  Serial.print("[VAL] RX <"); Serial.print((const char*)cmdBuf); Serial.println('>');

  if (busy) { Serial.println(F("[VAL] BUSY – ignoring")); return; }

  busy = true; digitalWrite(EN_PIN, LOW);

  bool handled = false;
  if (strstr(cmdBuf, "VAL_HOME") || strstr(cmdBuf, "START")) { do_VAL_HOME(); handled = true; }
  else if (strstr(cmdBuf, "VAL_90" ))                           { do_VAL_90();   handled = true; }
  else if (strstr(cmdBuf, "VAL_180"))                           { do_VAL_180();  handled = true; }

  if (!handled) msg("UKENDT CMD");

  digitalWrite(EN_PIN, HIGH);
  busy = false;
  Serial.println(F("[VAL] DONE"));
}
