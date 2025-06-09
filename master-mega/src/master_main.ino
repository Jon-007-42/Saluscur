/* =========================================================
 *  MASTER – sekvens­styring  (Arduino Mega 2560)
 *  ---------------------------------------------------------
 *  Touch-skærm:  øverste halvdel STOP, nederste START
 *  I²C-slaver :  Rolls 0x04  – Valve 0x05
 *  UART-enheder: Laser  (Serial1) – Ultralyd (Serial2)
 * =======================================================*/
#include <Wire.h>
#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>

/* ---------- touch-hw (4-wire) ---------- */
#define YP A1
#define YM 7
#define XM A2
#define XP 6
#define TS_LEFT  945
#define TS_RIGHT 171
#define TS_TOP   187
#define TS_BOT   914
#define MINP 10
#define MAXP 1000
TouchScreen ts(XP,YP,XM,YM,300);

/* ---------- TFT ---------- */
MCUFRIEND_kbv tft;
#define BLUE   0x001F
#define RED    0xF800
#define GREEN  0x07E0
#define WHITE  0xFFFF
#define BLACK  0x0000

/* ---------- I²C adresser ---------- */
const uint8_t ADR_ROLLS  = 0x04;
const uint8_t ADR_VALVE  = 0x05;

/* ---------- UART-baud ---------- */
const unsigned long LASER_BAUD  = 115200;
const unsigned long ULTRA_BAUD  = 115200;

/* ---------- SD-filer (rodmappe) ---------- */
const char *F_LAS_VAL  = "/LAS-VAL.gc";
const char *F_LAS_KONT = "/LAS-KONT.gc";
const char *F_UL_VAL   = "/UL-VAL.gc";
const char *F_UL_KONT  = "/UL-KONT.gc";

/* =========================================================
 *  små helper-funktioner
 * =======================================================*/
void dbg(const char *tag,const char *msg){
  Serial.print(tag); Serial.print("] "); Serial.println(msg);
}

/* Send én linje til et g-kode-kort og vent på OK -------- */
bool sendAndWaitOK(HardwareSerial &port,const char *line,
                   unsigned long tout=5000){
  port.println(line);
  unsigned long t0=millis();
  while(millis()-t0<tout){
    if(port.available()){
      String ln=port.readStringUntil('\n');
      ln.trim();
      if(ln.length()){
        Serial.print("[BTT] "); Serial.println(ln);
        if(ln.startsWith("ok")) return true;
      }
    }
  }
  return false;           // timeout
}

/* Åbn og kør en G-kode-fil -------------------------------- */
bool runFile(HardwareSerial &port,const char *fname,
             const char *tag){
  char buf[40];
  sprintf(buf,"M23 %s",fname);
  dbg(tag,buf);
  if(!sendAndWaitOK(port,buf)) return false;
  if(!sendAndWaitOK(port,"M24")) return false;
  return true;
}

/* ---------- I²C kommando ---------- */
bool i2cCmd(uint8_t adr,const char *cmd,
            unsigned long pollDelay=500){
  dbg("I2C",cmd);
  Wire.beginTransmission(adr);
  Wire.write(cmd);
  Wire.endTransmission();
  delay(50);

  /* vent på DONE/BUSY svar */
  while(true){
    Wire.requestFrom((int)adr,4);
    char r[5]={0}; int i=0;
    while(Wire.available() && i<4) r[i++]=Wire.read();
    if(strcmp(r,"DONE")==0) return true;
    delay(pollDelay);
  }
}

/* =========================================================
 *  vis START / STOP knapper
 * =======================================================*/
void drawScreen(){
  tft.fillScreen(BLUE);

  /* STOP */
  tft.fillRect(0,0,480,160,RED);
  tft.setTextColor(WHITE,RED);
  tft.setCursor(160,60); tft.setTextSize(3);
  tft.println("STOP");

  /* START */
  tft.fillRect(0,160,480,160,GREEN);
  tft.setTextColor(BLACK,GREEN);
  tft.setCursor(150,220); tft.setTextSize(3);
  tft.println("START");
}

/* =========================================================
 *  kør hele produkt­sekvensen
 * =======================================================*/
void runSequence(){
  Serial.println("=== SEKVENT START ===");

  /* init SD på begge BTT-kort */
  sendAndWaitOK(Serial1,"M21");
  sendAndWaitOK(Serial2,"M21");

  /* 1) Rolls 10 mm tension */
  i2cCmd(ADR_ROLLS,"TENSION");

  /* 2) Valve – fysisk bevægelse + DONE */
  i2cCmd(ADR_VALVE,"START");

  /* 3) Laser: hul til ventil */
  if(!runFile(Serial1,F_LAS_VAL,"LASER")){
    dbg("ERROR","Laser hul kunne ikke starte");
    return;
  }

  /* 4) Rolls 375 mm main feed */
  i2cCmd(ADR_ROLLS,"MAIN");

  /* 5) Ultralyd: kontur­svejs ventil-kant */
  if(!runFile(Serial2,F_UL_VAL,"ULTRA")){
    dbg("ERROR","Ultra VAL kunne ikke starte");
    return;
  }

  /* 6) Ultralyd: kontur­svejs pude */
  if(!runFile(Serial2,F_UL_KONT,"ULTRA")){
    dbg("ERROR","Ultra KONT kunne ikke starte");
    return;
  }

  /* 7) Laser: frilæg pudekontur */
  if(!runFile(Serial1,F_LAS_KONT,"LASER")){
    dbg("ERROR","Laser KONT kunne ikke starte");
    return;
  }

  Serial.println("=== SEKVENT SLUT ===");
}

/* =========================================================
 *  SETUP
 * =======================================================*/
void setup(){
  Serial.begin(115200);
  Serial1.begin(LASER_BAUD);
  Serial2.begin(ULTRA_BAUD);
  Wire.begin();

  uint16_t id=tft.readID(); if(id==0xD3D3) id=0x9486;
  tft.begin(id); tft.setRotation(3);
  drawScreen();
  Serial.println("Master klar – tryk START");
}

/* =========================================================
 *  LOOP – touch detektering + log fra kortene
 * =======================================================*/
bool prevPress=false;
void loop(){

  /* dump evt. linjer fra kortene til debug */
  if(Serial1.available()){
    String l=Serial1.readStringUntil('\n'); l.trim();
    if(l.length()) { Serial.print("[LASER] "); Serial.println(l); }
  }
  if(Serial2.available()){
    String l=Serial2.readStringUntil('\n'); l.trim();
    if(l.length()) { Serial.print("[ULTRA] "); Serial.println(l); }
  }

  /* touch */
  TSPoint p=ts.getPoint();
  bool pressed=(p.z>MINP && p.z<MAXP);
  if(pressed && !prevPress){
    int sx=map(p.y,TS_LEFT,TS_RIGHT,0,480);
    int sy=map(p.x,TS_TOP ,TS_BOT  ,320,0);

    if(sy<160){                // STOP
      i2cCmd(ADR_ROLLS ,"STOP");
      i2cCmd(ADR_VALVE,"STOP");
      Serial.println(">> STOP SENDT");
    }else{                     // START
      Serial.println("[MASTER] START trykket");
      runSequence();
    }
  }
  prevPress=pressed;
  delay(50);
}
