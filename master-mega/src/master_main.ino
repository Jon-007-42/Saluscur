/* =========================================================
 *  MASTER_V5.11 – 3-Jul-2025
 * =======================================================*/
#include <Wire.h>
#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include <SPI.h>
#include <stdio.h>

/* ---- TFT / TOUCH ---- */
#define YP A1
#define XM A2
#define YM 7
#define XP 6
#define TS_LEFT  945
#define TS_RIGHT 171
#define TS_TOP   187
#define TS_BOT   914
#define Z_MIN    10
#define Z_MAX    1000
#define CL_BLUE  0x001F
#define CL_GREEN 0x07E0
#define CL_WHITE 0xFFFF
#define CL_BLACK 0x0000
MCUFRIEND_kbv tft;
TouchScreen   ts(XP,YP,XM,YM,300);

/* ---- BUS & FILES ---- */
constexpr uint8_t ADR_ROLLS = 0x04;
constexpr uint8_t ADR_VAL   = 0x05;
constexpr unsigned long BAUD_LASER = 115200;
constexpr unsigned long BAUD_ULTRA = 115200;

constexpr char F_LAS_HOME[] = "LAS_HO~1.GC";
constexpr char F_UL_HOME[]  = "UL_HOM~1.GC";
constexpr char F_LAS_VAL[]  = "LAS_VAL.GC";
constexpr char F_LAS_KONT[] = "LAS_KONT.GC";
constexpr char F_UL_VAL[]   = "UL_VAL.GC";
constexpr char F_UL_KONT[]  = "UL_KONT.GC";

/* ---- ENUMS ---- */
enum class UiS { HOME, START, STATUS };
enum class St  { IDLE,HOMING,READY,VF,V90_1,LAS_VAL,V90_2,UL_VAL,ROLL,UKON,LKON,DONE };
struct Op { bool a=false; uint32_t s=0,t=0; void set(bool A,uint32_t S,uint32_t T){a=A;s=S;t=T;} } op;

/* ---- LOG ---- */
void log(const char* tag,const char* msg){ Serial.print('[');Serial.print(tag);Serial.print("] ");Serial.println(msg); }

/* ---- UI ---- */
UiS scr = UiS::HOME;
void home(){ tft.fillScreen(CL_BLUE); tft.setTextColor(CL_WHITE,CL_BLUE); tft.setTextSize(3); tft.setCursor(60,120); tft.println("TRYK HOMING"); scr=UiS::HOME; }
void startBtn(){ tft.fillScreen(CL_GREEN); tft.setTextColor(CL_BLACK,CL_GREEN); tft.setTextSize(3); tft.setCursor(80,120); tft.println("TRYK START"); scr=UiS::START; }
void status(const char* txt){ tft.fillScreen(CL_BLACK); tft.setTextColor(CL_WHITE,CL_BLACK); tft.setTextSize(2); int16_t x=(480-strlen(txt)*12)/2; if(x<0)x=0; tft.setCursor(x,100); tft.println(txt); scr=UiS::STATUS; }
bool tapped(){ TSPoint p=ts.getPoint(); if(p.z<Z_MIN||p.z>Z_MAX) return false; pinMode(XM,OUTPUT); pinMode(YP,OUTPUT); return true; }

/* ---- I2C (latch DONE) ---- */
namespace I2C{ bool rol=false,val=false; void reset(){rol=val=false;}
bool done(uint8_t adr){ bool& l=(adr==ADR_ROLLS)?rol:val; if(l) return true;
  Wire.requestFrom((int)adr,4); char r[5]={0}; for(int i=0;i<4&&Wire.available();++i) r[i]=Wire.read();
  if(!strcmp(r,"DONE")) l=true; return l;}
void send(uint8_t adr,const char* cmd){ Wire.beginTransmission(adr); Wire.write(cmd); Wire.endTransmission(); log("I2C",cmd);}}

/* ---- G-code driver ---- */
class GDrv{
  HardwareSerial& p; const char* tag; Op h;
public: GDrv(HardwareSerial&pr,const char*tg):p(pr),tag(tg){}
  bool run(const char* f){ if(h.a) return false; p.print(F("M23 ")); p.println(f); p.println(F("M24"));
    log(tag,f); h.set(true,millis(),480000UL); return true; }
  bool busy(){ return h.a; }
  void svc(){ if(!h.a) return; if(millis()-h.s>h.t){ h.a=false; log(tag,"TIMEOUT"); return;}
    while(p.available()){ String l=p.readStringUntil('\n'); if(l.indexOf("Done printing file")>=0||l.indexOf("Print finished")>=0){ h.a=false; log(tag,"DONE"); } } }
};
GDrv laser(Serial1,"LAS"), ultra(Serial2,"UL ");

/* ---- HELPERS ---- */
void startOp(uint32_t t){ op.set(true,millis(),t); }
bool opExp(){ return op.a && millis()-op.s>op.t; }
void clrOp(){ op.a=false; }

/* ---- STATE MACHINE ---- */
St st = St::IDLE; uint32_t tick=0;
void SM(){
  static St last=St::IDLE; if(st!=last){ log("STATE",String((int)st).c_str()); last=st; }

  switch(st){
  case St::HOMING:
    laser.svc(); ultra.svc();
    if(!op.a){ I2C::reset(); I2C::send(ADR_ROLLS,"ROLLS_HOME"); I2C::send(ADR_VAL,"VAL_HOME");
      laser.run(F_LAS_HOME); ultra.run(F_UL_HOME); status("HOMING – vent ..."); startOp(480000UL);}
    if(I2C::done(ADR_ROLLS)&&I2C::done(ADR_VAL)&&!laser.busy()&&!ultra.busy()){ clrOp(); st=St::READY; }
    else if(opExp()){ clrOp(); st=St::IDLE; home(); log("ERR","Homing timeout"); }
    break;

  case St::VF:    st = St::V90_1; break;

  case St::V90_1:
    if(!op.a){ I2C::send(ADR_VAL,"VAL_90"); status("VALVES 90°"); startOp(20000);}
    if(I2C::done(ADR_VAL)||opExp()){ clrOp(); st=St::LAS_VAL; }
    break;

  case St::LAS_VAL:
    if(!op.a){ laser.run(F_LAS_VAL); status("LASER VALVE CUT"); startOp(480000UL);}
    if(!laser.busy()||opExp()){ clrOp(); st=St::V90_2; }
    break;

  case St::V90_2:
    if(!op.a){ I2C::send(ADR_VAL,"VAL_90"); status("VALVES 90° (2)"); startOp(20000);}
    if(I2C::done(ADR_VAL)||opExp()){ clrOp(); st=St::UL_VAL; }
    break;

  case St::UL_VAL:
    if(!op.a){ ultra.run(F_UL_VAL); status("ULTRA VALVE WELD"); startOp(480000UL);}
    if(!ultra.busy()||opExp()){ clrOp(); st=St::ROLL; }
    break;

  case St::ROLL:
    if(!op.a){ I2C::send(ADR_ROLLS,"ROLLS_MAIN"); status("ROLLS 375 mm"); startOp(300000UL);}
    if(I2C::done(ADR_ROLLS)||opExp()){ clrOp(); st=St::UKON; }
    break;

  case St::UKON:
    if(!op.a){ ultra.run(F_UL_KONT); status("ULTRA CONTOUR"); startOp(480000UL);}
    if(!ultra.busy()||opExp()){ clrOp(); st=St::LKON; }
    break;

  case St::LKON:
    if(!op.a){ laser.run(F_LAS_KONT); status("LASER CONTOUR"); startOp(480000UL);}
    if(!laser.busy()||opExp()){ clrOp(); st=St::DONE; }
    break;

  case St::DONE:
    if(!op.a){ I2C::send(ADR_VAL,"VAL_STOP"); home(); startOp(1); }
    break;

  default: break;
  }
}

/* ---- SETUP ---- */
void setup(){
  Serial.begin(115200); Serial1.begin(BAUD_LASER); Serial2.begin(BAUD_ULTRA);
  Wire.begin(); pinMode(20,INPUT_PULLUP); pinMode(21,INPUT_PULLUP);
  uint16_t id=tft.readID(); if(id==0xD3D3) id=0x9486; tft.begin(id); tft.setRotation(3);
  home(); log("MASTER","V5.11 klar – TRYK HOMING");
}

/* ---- LOOP ---- */
void loop(){
  laser.svc(); ultra.svc();
  if(st==St::READY && scr!=UiS::START) startBtn();

  if(tapped()){
    if(scr==UiS::HOME  && st==St::IDLE ){ st=St::HOMING; clrOp(); }
    if(scr==UiS::START && st==St::READY){ st=St::VF;     clrOp(); }
  }
  if(millis()-tick>=25){ tick=millis(); if(st!=St::IDLE) SM(); }
}
