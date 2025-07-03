/* --------- Rolls_V2_ASCII  -------------------------------------
   Status paa TFT + Serial  |  I2C-slave (addr 0x04)
   Kommandoer:  "ROLLS_HOME" -> Pretension  (M9+M10 10 mm, pause 4 s)
                "ROLLS_MAIN" -> Main Feed   (alle 375 mm)
------------------------------------------------------------------ */
#include <TMCStepper.h>
#include <SPI.h>
#include <Wire.h>
#include <MCUFRIEND_kbv.h>

/* ---------- TFT ------------------------------------------------- */
MCUFRIEND_kbv tft;
#define TFT_BG  0x0000   // black
#define TFT_TXT 0xFFFF   // white
void show(const char *txt)
{
  Serial.println(txt);
  tft.fillScreen(TFT_BG);
  tft.setCursor(0, 80);
  tft.setTextColor(TFT_TXT);
  tft.setTextSize(3);
  tft.print(txt);
}

/* ---------- I2C ------------------------------------------------- */
#define I2C_ADDR 0x04
volatile char rxBuf[12] = "";
volatile bool newCmd = false;
volatile bool busy   = false;
void receiveEvent(int)
{
  int i = 0;
  while (Wire.available() && i < 11) rxBuf[i++] = Wire.read();
  rxBuf[i] = '\0';
  newCmd   = true;
}
void requestEvent() { Wire.write(busy ? "BUSY" : "DONE"); }

/* ---------- Motor-hardware ------------------------------------- */
#define EN_PIN 53
const int CS[]   = {48, A9, A8, 22, 49, 24, 28, 26, 30, 50};
const int DIR[]  = {47, 43, 39, 35, 34, 31, 25, 46, 40, 36};
const int STEP[] = {45, 41, 37, 33, 32, 29, 27, 44, 42, 38};
const int N = 10;

/* ---------- Bevægelsesdata ------------------------------------- */
const int  uStep = 16;       // microsteps (ASCII navngivning!)
const int  base  = 200;      // fuldskridt pr. omdr.
const int  SPR   = base * uStep;
const float dia  = 60.0;     // mm
const float circ = dia * 3.14159;
const float PRE_MM  = 10.0;
const float MAIN_MM = 375.0;
const int   RPM = 30;

unsigned long usStep(int rpm)
{
  return (unsigned long)(1e6 / (rpm / 60.0 * SPR));
}
int mm2step(float mm)
{
  return (int)(mm / circ * SPR);
}

/* ---------- Driver-objekter ------------------------------------ */
TMC5160Stepper *drv[N];
void enableAll(bool on)
{
  digitalWrite(EN_PIN, on ? LOW : HIGH);
  if (!on)
    for (int i = 0; i < N; i++) drv[i]->toff(0);
}

/* ---------- Pretension (R5 = M10/M9) --------------------------- */
void pretension()
{
  const int M10 = 4;   // index i arrays
  const int M9  = 9;

  digitalWrite(DIR[M10], LOW);   // CW
  digitalWrite(DIR[M9 ], HIGH);  // CCW

  unsigned long dt = usStep(RPM), t0 = micros();
  int st = mm2step(PRE_MM);

  for (int s = 0; s < st; s++) {
    while (micros() - t0 < dt) {}
    t0 = micros();
    digitalWrite(STEP[M10], HIGH);
    digitalWrite(STEP[M9 ], HIGH);
    delayMicroseconds(20);
    digitalWrite(STEP[M10], LOW);
    digitalWrite(STEP[M9 ], LOW);
  }
}

/* ---------- Main Feed (alle) ----------------------------------- */
void mainFeed()
{
  digitalWrite(DIR[0], LOW);   digitalWrite(DIR[1], LOW);
  digitalWrite(DIR[2], HIGH);  digitalWrite(DIR[3], HIGH);
  digitalWrite(DIR[4], LOW);   digitalWrite(DIR[5], HIGH);
  digitalWrite(DIR[6], HIGH);  digitalWrite(DIR[7], LOW);
  digitalWrite(DIR[8], LOW);   digitalWrite(DIR[9], HIGH);

  unsigned long dt = usStep(RPM), t0 = micros();
  int st = mm2step(MAIN_MM);

  for (int s = 0; s < st; s++) {
    while (micros() - t0 < dt) {}
    t0 = micros();
    for (int i = 0; i < N; i++) digitalWrite(STEP[i], HIGH);
    delayMicroseconds(20);
    for (int i = 0; i < N; i++) digitalWrite(STEP[i], LOW);
  }
}

/* ---------- setup ---------------------------------------------- */
void setup()
{
  Serial.begin(115200);

  /* TFT init */
  uint16_t id = tft.readID();
  tft.begin(id);
  tft.setRotation(1);
  tft.fillScreen(TFT_BG);
  show("Waiting...");

  /* Pins */
  pinMode(EN_PIN, OUTPUT);          digitalWrite(EN_PIN, HIGH);
  for (int i = 0; i < N; i++) {
    pinMode(STEP[i], OUTPUT);
    pinMode(DIR[i] , OUTPUT);
    pinMode(CS[i]  , OUTPUT);       digitalWrite(CS[i], HIGH);
  }
  pinMode(A9, OUTPUT);  pinMode(A8, OUTPUT);

  /* Drivers */
  SPI.begin();
  for (int i = 0; i < N; i++) {
    drv[i] = new TMC5160Stepper(CS[i], 0.11);
    drv[i]->begin();
    drv[i]->rms_current(1600);
    drv[i]->microsteps(uStep);
    drv[i]->en_pwm_mode(false);
    drv[i]->toff(4);
  }
  enableAll(true);

  /* I2C */
  Wire.begin(I2C_ADDR);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
}

/* ---------- loop ----------------------------------------------- */
void loop()
{
  if (!newCmd) return;
  newCmd = false;

  if (!strcmp(rxBuf, "ROLLS_HOME")) {
    busy = true;
    show("Pretension...");
    pretension();
    show("Pause 4 s");
    delay(4000);
    busy = false;
  }
  else if (!strcmp(rxBuf, "ROLLS_MAIN")) {
    busy = true;
    show("Main feed...");
    mainFeed();
    show("Done");
    busy = false;
  }
  else show("Unknown cmd");
}

