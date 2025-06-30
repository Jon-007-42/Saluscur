/* =========================================================
 *  MASTER_V5.6 – 30-Jun-2025
 * =======================================================*/

/**************** 1. LIBRARIES & GLOBALS ******************/
#include <Wire.h>
#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include <SPI.h>
#include <stdio.h>

/* ---- TFT / Touch pins ---- */
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

/* ---- Colours ---- */
#define CL_BLUE   0x001F
#define CL_GREEN  0x07E0
#define CL_WHITE  0xFFFF
#define CL_BLACK  0x0000

MCUFRIEND_kbv tft;
TouchScreen   ts(XP, YP, XM, YM, 300);

/* ---- I²C slave-adresser ---- */
constexpr uint8_t ADR_ROLLS = 0x04;
constexpr uint8_t ADR_VAL   = 0x05;

/* ---- UART baud ---- */
constexpr unsigned long BAUD_LASER = 115200;
constexpr unsigned long BAUD_ULTRA = 115200;

/* ---- SD-filnavne (8.3 / tilde) ---- */
constexpr char F_LAS_HOME[] = "LAS_HO~1.GC";
constexpr char F_UL_HOME[]  = "UL_HOM~1.GC";
constexpr char F_LAS_VAL[]  = "LAS_VA~1.GC";
constexpr char F_LAS_KONT[] = "LAS_KO~1.GC";
constexpr char F_UL_VAL[]   = "UL_VAL~1.GC";
constexpr char F_UL_KONT[]  = "UL_KO~1.GC";

/**************** 2. ENUMS & STRUCTS **********************/
enum class UiEvent : uint8_t { NONE, HOMING, START };

enum class State : uint8_t {
  IDLE,
  HOMING_INIT, READY,
  VALVE_FEED,
  VAL_90, LAS_VAL, VAL_180,
  UL_VAL, ROLLS_FEED,
  UL_KONT, LAS_KONT,
  COMPLETE
};

struct OpHandle {
  bool     active  {false};
  uint32_t started {0};
  uint32_t timeout {0};
  void set(bool a, uint32_t s, uint32_t t) { active=a; started=s; timeout=t; }
};

/**************** 3. LOG HELPERS **************************/
void log(const char *tag, const char *msg)
{
  Serial.print('['); Serial.print(tag); Serial.print("] ");
  Serial.println(msg);
}

/**************** 4. TOUCH UI *****************************/
namespace TouchUI {
  void drawIdle() {
    tft.fillScreen(CL_BLUE);
    tft.setTextColor(CL_WHITE, CL_BLUE);
    tft.setTextSize(2);
    tft.setCursor(70, 60);   tft.println("TRYK HOMING");

    tft.fillRect(0, 160, 480, 160, CL_GREEN);
    tft.setTextColor(CL_BLACK, CL_GREEN);
    tft.setCursor(120, 220); tft.println("TRYK START");
  }

  void status(const char *txt) {
    tft.fillRect(0, 0, 480, 160, CL_BLUE);
    tft.setTextColor(CL_WHITE, CL_BLUE);
    tft.setTextSize(2);
    int16_t x = (480 - (int)strlen(txt)*12) / 2;
    if (x < 0) x = 0;
    tft.setCursor(x, 60);
    tft.println(txt);
  }

  UiEvent poll() {
    TSPoint p = ts.getPoint();
    if (p.z < Z_MIN || p.z > Z_MAX) return UiEvent::NONE;
    pinMode(XM, OUTPUT); pinMode(YP, OUTPUT);
    int sy = map(p.x, TS_TOP, TS_BOT, 320, 0);
    return (sy < 160) ? UiEvent::HOMING : UiEvent::START;
  }
} // namespace TouchUI

/**************** 5. I²C DRIVER – kun ændringer ***********/
namespace I2C {

// husker sidst viste tekst pr. enhed
struct DevLog { char last[12] = ""; };
DevLog dRol, dVal;

static DevLog &dev(const char *tag) { return (tag[0]=='R') ? dRol : dVal; }

void logI2C(const char *tag, const char *msg)
{
  DevLog &d = dev(tag);
  if (strcmp(d.last, msg) != 0) {              // print KUN hvis ændret
    Serial.print("[I2C "); Serial.print(tag); Serial.print("] ");
    Serial.println(msg);
    strncpy(d.last, msg, sizeof(d.last)-1);
    d.last[sizeof(d.last)-1] = '\0';
  }
}

bool send(uint8_t adr, const char *cmd, const char *tag)
{
  Wire.beginTransmission(adr);
  Wire.write(cmd);
  bool ok = (Wire.endTransmission() == 0);
  logI2C(tag, ok ? cmd : "TX ERR");
  return ok;
}

bool done(uint8_t adr, const char *tag)
{
  Wire.requestFrom((int)adr, 4);
  char r[5] = {0};
  for (int i = 0; i < 4 && Wire.available(); ++i) r[i] = Wire.read();
  if (!*r) strcpy(r, "ERR");
  logI2C(tag, r);
  return !strcmp(r, "DONE");
}

} // namespace I2C

/**************** 6. G-CODE DRIVER (uændret) **************/
class GcodeDriver {
public:
  GcodeDriver(HardwareSerial &p, const char *tg) : port(p), tag(tg) {}
  bool run(const char *file) {
    if (h.active) return false;
    port.print(F("M23 ")); port.println(file);
    port.println(F("M24"));
    char b[40]; snprintf(b,sizeof(b),"FILE %s START",file); log(tag,b);
    h.set(true, millis(), 180000UL);
    return true;
  }
  bool service() {
    if (!h.active) return true;
    if (millis()-h.started > h.timeout) { log(tag,"TIMEOUT"); h.active=false; return true; }
    if (!port.available()) return false;
    String l = port.readStringUntil('\n'); l.trim();
    if (!l.length()) return false;
    if (l.indexOf("Done printing file") >= 0) { h.active=false; log(tag,"DONE"); }
    else if (l.startsWith("open failed"))      { h.active=false; log(tag,l.c_str()); }
    return !h.active;
  }
  bool busy() const { return h.active; }
private:
  HardwareSerial &port; const char *tag; OpHandle h;
};

GcodeDriver laser(Serial1,"LAS");
GcodeDriver ultra(Serial2,"UL ");

/**************** 7. GLOBAL VARS **************************/
State     state   = State::IDLE;
OpHandle  op;
uint32_t  lastTick=0;
void startOp(uint32_t t){ op.set(true,millis(),t); }
bool opExpired(){ return op.active && millis()-op.started>op.timeout; }
void clearOp(){ op.active=false; }

/**************** 8. STATE MACHINE (samme som V5.5) ******/
void tickSM() {
  switch(state) {

  case State::HOMING_INIT:
    if(!op.active){
      log("STATE","HOMING_INIT");
      I2C::send(ADR_ROLLS,"ROLLS_HOME","ROL");
      I2C::send(ADR_VAL,  "VAL_HOME","VAL");
      laser.run(F_LAS_HOME);
      ultra.run(F_UL_HOME);
      TouchUI::status("HOMING – vent ...");
      startOp(120000UL);
    }
    if(I2C::done(ADR_ROLLS,"ROL") && I2C::done(ADR_VAL,"VAL") &&
       !laser.busy() && !ultra.busy()){
      clearOp(); state=State::READY;
      TouchUI::status("LÆG VENTIL – TRYK START");
    } else if(opExpired()){
      clearOp(); state=State::IDLE; TouchUI::drawIdle();
      log("ERR","Homing timeout");
    }
    break;

  case State::VALVE_FEED:        state=State::VAL_90; break;

  case State::VAL_90:
    if(!op.active){ I2C::send(ADR_VAL,"VAL_90","VAL");
      TouchUI::status("VALVES 90°"); startOp(20000);}
    if(I2C::done(ADR_VAL,"VAL")||opExpired()){ clearOp(); state=State::LAS_VAL; }
    break;

  case State::LAS_VAL:
    if(!op.active){ laser.run(F_LAS_VAL);
      TouchUI::status("LASER VALVE CUT"); startOp(60000);}
    if(!laser.busy()||opExpired()){ clearOp(); state=State::VAL_180; }
    break;

  case State::VAL_180:
    if(!op.active){ I2C::send(ADR_VAL,"VAL_180","VAL");
      TouchUI::status("VALVES 180°"); startOp(20000);}
    if(I2C::done(ADR_VAL,"VAL")||opExpired()){ clearOp(); state=State::UL_VAL; }
    break;

  case State::UL_VAL:
    if(!op.active){ ultra.run(F_UL_VAL);
      TouchUI::status("ULTRA VALVE WELD"); startOp(60000);}
    if(!ultra.busy()||opExpired()){ clearOp(); state=State::ROLLS_FEED; }
    break;

  case State::ROLLS_FEED:
    if(!op.active){ I2C::send(ADR_ROLLS,"ROLLS_MAIN","ROL");
      TouchUI::status("ROLLS 375 mm"); startOp(30000);}
    if(I2C::done(ADR_ROLLS,"ROL")||opExpired()){ clearOp(); state=State::UL_KONT; }
    break;

  case State::UL_KONT:
    if(!op.active){ ultra.run(F_UL_KONT);
      TouchUI::status("ULTRA CONTOUR"); startOp(60000);}
    if(!ultra.busy()||opExpired()){ clearOp(); state=State::LAS_KONT; }
    break;

  case State::LAS_KONT:
    if(!op.active){ laser.run(F_LAS_KONT);
      TouchUI::status("LASER CONTOUR"); startOp(60000);}
    if(!laser.busy()||opExpired()){ clearOp(); state=State::COMPLETE; }
    break;

  case State::COMPLETE:
    if(!op.active){
      I2C::send(ADR_VAL,"VAL_STOP","VAL");
      TouchUI::status("FJERN PRODUKTER – TRYK HOMING");
      startOp(1);
    }
    break;

  default: break;
  }
}

/**************** 9. TOUCH HANDLER ************************/
void handleEvt(UiEvent e){
  if(e==UiEvent::NONE) return;
  switch(state){
    case State::IDLE:
    case State::COMPLETE:
      if(e==UiEvent::HOMING){ state=State::HOMING_INIT; clearOp(); }
      break;
    case State::READY:
      if(e==UiEvent::START){ state=State::VALVE_FEED; clearOp(); }
      break;
    default: break;
  }
}

/**************** 10. SETUP *******************************/
void setup(){
  Serial.begin(115200);
  Serial1.begin(BAUD_LASER);
  Serial2.begin(BAUD_ULTRA);

  Wire.begin(); pinMode(20,INPUT_PULLUP); pinMode(21,INPUT_PULLUP);

  uint16_t id=tft.readID(); if(id==0xD3D3) id=0x9486;
  tft.begin(id); tft.setRotation(3);
  TouchUI::drawIdle();
  log("MASTER","V5.6 klar – tryk HOMING");
}

/**************** 11. LOOP ********************************/
void loop(){
  while(Serial1.available()){
    String l=Serial1.readStringUntil('\n'); l.trim();
    if(l.length() && (l.startsWith("open failed")||l.indexOf("Done printing file")>=0))
      log("LAS",l.c_str());
  }
  while(Serial2.available()){
    String l=Serial2.readStringUntil('\n'); l.trim();
    if(l.length() && (l.startsWith("open failed")||l.indexOf("Done printing file")>=0))
      log("UL ",l.c_str());
  }

  handleEvt(TouchUI::poll());

  if(millis()-lastTick>=25){
    lastTick=millis();
    laser.service();
    ultra.service();
    if(state!=State::IDLE) tickSM();
  }
}
