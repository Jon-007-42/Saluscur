#include <Wire.h>
#include <TMCStepper.h>
#include <SPI.h>
#include <MCUFRIEND_kbv.h>

/* ---------- PINs ---------- */
#define EN_PIN 53
const int STEP_P[4] = {25, 48, 40, 36};
const int DIR_P [4] = {27, 46, 42, 34};
const int CS_P  [4] = {23, 44, 38, 32};
const int SW_P  [4] = {A15, A14, A13, A12};

/* ---------- Driver ---------- */
#define R_SENSE 0.11f
TMC5160Stepper* drv[4];

/* ---------- Timing ---------- */
const int pulseHomeUs = 600;    // hurtig homing
const int pulseMoveUs = 1200;   // normal rotation
const int settleDelay = 5;
const int maxSteps    = 4000;

/* ---------- MANUELT LAS-OFFSET ----------
 * Microsteps pr. motor efter homing (CCW)
 */
int offsetStep[4] = {1530, 1570, 1500, 1510};

/* ---------- ULTRA drejning ----------
 * 90° = 800 µsteps ved 16 µsteps/fuldstep
 */
const int UL_STEPS = 800;     // én værdi, gælder alle motorer

/* ---------- I²C ---------- */
#define I2C_ADDR 0x05
volatile char cmdBuf[8]="";
volatile bool newCmd=false, busy=false;

/* ---------- TFT util ---------- */
MCUFRIEND_kbv tft;
void msg(const char* m){
  Serial.println(m);
  tft.fillScreen(0x0000);
  tft.setTextColor(0xFFFF);
  tft.setTextSize(3);
  tft.setCursor(0,90);
  tft.print(m);
}

/* ---------- fælles helpers ---------- */
void moveSteps(int rem[4], int dUs){
  for(int i=0;i<4;i++) digitalWrite(DIR_P[i],LOW);          // CCW
  bool done=false;
  while(!done){
    done=true;
    for(int i=0;i<4;i++){
      if(rem[i]>0){ digitalWrite(STEP_P[i],HIGH); rem[i]--; done=false; }
    }
    delayMicroseconds(dUs);
    for(int i=0;i<4;i++) digitalWrite(STEP_P[i],LOW);
    delayMicroseconds(dUs);
  }
}

/* ---------- homing ---------- */
bool homeAll(){
  msg("Homing...");
  for(int i=0;i<4;i++) digitalWrite(DIR_P[i],LOW);
  int steps=0; bool allHome=false;
  while(!allHome && steps<maxSteps){
    allHome=true;
    for(int i=0;i<4;i++){
      if(digitalRead(SW_P[i])==LOW){
        digitalWrite(STEP_P[i],HIGH);
        allHome=false;
      }
    }
    delayMicroseconds(pulseHomeUs);
    for(int i=0;i<4;i++) digitalWrite(STEP_P[i],LOW);
    delayMicroseconds(pulseHomeUs);
    delay(settleDelay);
    steps++;
  }
  msg(allHome ? "Homed" : "TIMEOUT");
  return allHome;
}

/* ---------- I²C ---------- */
void onRecv(int){ int i=0; while(Wire.available()&&i<7) cmdBuf[i++]=Wire.read();
                  cmdBuf[i]='\0'; newCmd=true; }
void onReq(){ Wire.write(busy ? "BUSY" : "DONE"); }

/* ---------- setup ---------- */
void setup(){
  Serial.begin(115200);
  uint16_t id=tft.readID(); if(id==0xD3D3) id=0x9486;
  tft.begin(id); tft.setRotation(1); msg("Valve V6");

  pinMode(EN_PIN,OUTPUT); digitalWrite(EN_PIN,HIGH);
  for(int i=0;i<4;i++){
    pinMode(STEP_P[i],OUTPUT); pinMode(DIR_P[i],OUTPUT);
    pinMode(CS_P[i],OUTPUT); digitalWrite(CS_P[i],HIGH);
    pinMode(SW_P[i],INPUT_PULLUP);
  }

  SPI.begin();
  for(int i=0;i<4;i++){
    drv[i]=new TMC5160Stepper(CS_P[i],R_SENSE);
    drv[i]->begin(); drv[i]->rms_current(600);
    drv[i]->microsteps(16); drv[i]->pwm_autoscale(true);
  }

  Wire.begin(I2C_ADDR);
  Wire.onReceive(onRecv); Wire.onRequest(onReq);

  Serial.println(F("✅ Valve V6 klar – send START"));
}

/* ---------- LASER-trin ---------- */
void runLAS(){
  if(!homeAll()) return;              // abort hvis TIMEOUT
  delay(500);
  int rem[4]; for(int i=0;i<4;i++) rem[i]=offsetStep[i];
  msg("LAS Offset");
  moveSteps(rem, pulseMoveUs);
}

/* ---------- ULTRA-trin ---------- */
void runUL(){
  delay(500);
  int rem[4] = {UL_STEPS, UL_STEPS, UL_STEPS, UL_STEPS};
  msg("ULTRA 90°");
  moveSteps(rem, pulseMoveUs);
}

/* ---------- komplet sekvens ---------- */
void runSequence(){
  busy=true; digitalWrite(EN_PIN,LOW);
  runLAS();          // homing + individuel offset
  runUL();           // +90° uden ny homing
  msg("Done");
  digitalWrite(EN_PIN,HIGH);
  busy=false;
}

/* ---------- loop ---------- */
void loop(){
  if(newCmd){
    newCmd=false;
    if(!busy && !strcmp(cmdBuf,"START")) runSequence();
    else if(!busy && !strcmp(cmdBuf,"STOP")) msg("STOP");
  }
}
