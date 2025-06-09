/*******************************************************
 * Valve-1-TFT – I2C-slave 0x05  •  Status på MCUFRIEND
 * Ready → Homing… → Rotating… → Done
 *******************************************************/
#include <Wire.h>
#include <SPI.h>
#include <TMCStepper.h>
#include <MCUFRIEND_kbv.h>

/* ---------- farver (fallback) ---------- */
#ifndef BLACK
#define BLACK  0x0000
#endif
#ifndef WHITE
#define WHITE  0xFFFF
#endif

/* ---------- pins ---------- */
#define I2C_ADDR 0x05
#define EN_PIN   53

const int STEP_PINS[4]   = {25, 48, 40, 36};
const int DIR_PINS [4]   = {27, 46, 42, 34};
const int CS_PINS  [4]   = {23, 44, 38, 32};
const int ENDSTOP_PINS[4]= {A15, A14, A13, A12};   /* NC */

/* ---------- driver-konstanter ---------- */
const float R_SENSE     = 0.11f;
const int   MICROSTEPS  = 32;
const int   STEPS_REV   = 200 * MICROSTEPS;
const int   CURR_MA     = 1200;
const int   HOMING_RPM  = 10;
const int   MOVE_RPM    = 10;
const int   STEPS_90deg = STEPS_REV / 4;

/* ---------- globale ---------- */
TMC5160Stepper* drv[4];
MCUFRIEND_kbv    tft;
volatile bool    busyFlag = false;

/* ---------- helpers ---------- */
inline unsigned long usPerStep(int rpm)
{
  return (unsigned long)(1000000.0f / ((rpm / 60.0f) * STEPS_REV));
}

void stepMotor(int i)
{
  digitalWrite(STEP_PINS[i], HIGH);
  delayMicroseconds(3);
  digitalWrite(STEP_PINS[i], LOW);
}

bool endstopHit(int i)     { return digitalRead(ENDSTOP_PINS[i]); }
void setDirCW (int i)      { digitalWrite(DIR_PINS[i], HIGH); }
void setDirCCW(int i)      { digitalWrite(DIR_PINS[i], LOW ); }

void showStatus(const char* txt)
{
  tft.fillScreen(BLACK);
  tft.setCursor(0, 90);
  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(3);
  tft.print(txt);
}

/* ---------- bevægelses-sekvens ---------- */
void doSequence()
{
  busyFlag = true;
  Serial.println(F("[VALVE] start"));
  digitalWrite(EN_PIN, LOW);

  /* skærm */
  showStatus("Homing...");
  unsigned long d = usPerStep(HOMING_RPM);
  bool allHomed = false;
  while (!allHomed)
  {
    allHomed = true;
    unsigned long t0 = micros();
    for (int i = 0; i < 4; ++i)
    {
      if (!endstopHit(i))
      {
        setDirCCW(i);
        stepMotor(i);
        allHomed = false;
      }
    }
    while (micros() - t0 < d) {}
  }
  delay(200);

  /* +90 deg */
  showStatus("Rotating...");
  d = usPerStep(MOVE_RPM);
  for (int s = 0; s < STEPS_90deg; ++s)
  {
    unsigned long t0 = micros();
    for (int i = 0; i < 4; ++i)
    {
      setDirCW(i);
      stepMotor(i);
    }
    while (micros() - t0 < d) {}
  }

  /* færdig */
  digitalWrite(EN_PIN, HIGH);
  showStatus("Done");
  busyFlag = false;
  Serial.println(F("[VALVE] DONE"));
}

/* ---------- I2C ---------- */
void onReceive(int)
{
  String cmd;
  while (Wire.available()) cmd += (char)Wire.read();

  if (cmd == "START" && !busyFlag) doSequence();
  else if (cmd == "STOP")
  {
    busyFlag = false;
    digitalWrite(EN_PIN, HIGH);
  }
}
void onRequest() { Wire.write(busyFlag ? "BUSY" : "DONE"); }

/* ---------- setup ---------- */
void setup()
{
  Serial.begin(115200);

  /* TFT */
  uint16_t id = tft.readID();
  if (id == 0xD3D3) id = 0x9486;
  tft.begin(id);
  tft.setRotation(1);
  showStatus("Ready");

  /* pins */
  pinMode(EN_PIN, OUTPUT);  digitalWrite(EN_PIN, HIGH);
  for (int i = 0; i < 4; ++i)
  {
    pinMode(STEP_PINS[i], OUTPUT);
    pinMode(DIR_PINS[i] , OUTPUT);
    pinMode(CS_PINS [i] , OUTPUT); digitalWrite(CS_PINS[i], HIGH);
    pinMode(ENDSTOP_PINS[i], INPUT_PULLUP);
  }

  /* TMC5160 */
  SPI.begin();
  for (int i = 0; i < 4; ++i)
  {
    drv[i] = new TMC5160Stepper(CS_PINS[i], R_SENSE);
    drv[i]->begin();
    drv[i]->rms_current(CURR_MA);
    drv[i]->microsteps(MICROSTEPS);
    drv[i]->en_pwm_mode(false);
  }

  /* I2C */
  Wire.begin(I2C_ADDR);
  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);

  Serial.println(F("Valve-1-TFT ready"));
}

void loop() {}
