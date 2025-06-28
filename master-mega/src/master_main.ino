/* =========================================================
 *  MASTER_V5.3 – fuld firmware 30-Jun-2025
 *  ---------------------------------------------------------
 *  Board      : Arduino Mega 2560
 *  Display    : 3.5" ILI9486 (MCUFRIEND_kbv) 480×320
 *  Touch-zoner: Øverste halvdel  →  HOMING
 *               Nederste halvdel →  START
 *
 *  I²C-kommandoer
 *    ROLLS_HOME / ROLLS_MAIN  (addr 0x04)
 *    VAL_HOME   / VAL_90 / VAL_180 / VAL_STOP  (addr 0x05)
 *
 *  UART1 → Laser-BTT  @115 200 bps
 *  UART2 → Ultra-BTT  @115 200 bps
 * =======================================================*/

/**************** 1. LIBRARIES & GLOBALS ******************/
#include <Wire.h>
#include <MCUFRIEND_kbv.h>
#include <Adafruit_GFX.h>
#include <TouchScreen.h>
#include <SPI.h>
#include <stdio.h>           // til snprintf()

/* ---- TFT / Touch-shield pins ---- */
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

/* ---- Farver ---- */
#define CL_BLUE   0x001F
#define CL_GREEN  0x07E0
#define CL_WHITE  0xFFFF
#define CL_BLACK  0x0000

MCUFRIEND_kbv tft;
TouchScreen   ts(XP, YP, XM, YM, 300);

/* ---- I²C adresser ---- */
constexpr uint8_t ADR_ROLLS = 0x04;
constexpr uint8_t ADR_VAL   = 0x05;

/* ---- UART baud ---- */
constexpr unsigned long BAUD_LASER = 115200;
constexpr unsigned long BAUD_ULTRA = 115200;

/* ---- Filnavne (læses af Marlin-boards) ---- */
constexpr char F_LAS_HOME[] = "/LAS_HOME.gc";
constexpr char F_UL_HOME[]  = "/UL_HOME.gc";
constexpr char F_LAS_VAL[]  = "/LAS_VAL.gc";
constexpr char F_LAS_KONT[] = "/LAS_KONT.gc";
constexpr char F_UL_VAL[]   = "/UL_VAL.gc";
constexpr char F_UL_KONT[]  = "/UL_KONT.gc";

/**************** 2. ENUMS & STRUKTURER *******************/
// Touch-events
enum class UiEvent : uint8_t { NONE, HOMING, START };

// Hoved-statemachine
enum class State : uint8_t {
  IDLE,
  HOMING_INIT, HOMING_WAIT, READY,
  VALVE_FEED,
  VAL_90, LAS_VAL, VAL_180,
  UL_VAL, ROLLS_FEED,
  UL_KONT, LAS_KONT,
  COMPLETE
};

// Asynkront handle
struct OpHandle {
  bool     active  {false};
  uint32_t started {0};
  uint32_t timeout {0};
  void set(bool a, uint32_t s, uint32_t t) { active=a; started=s; timeout=t; }
};

/**************** 3. LOG-HJÆLPER **************************/
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

    // restore pin-funktioner
    pinMode(XM, OUTPUT);
    pinMode(YP, OUTPUT);

    int sx = map(p.y, TS_LEFT, TS_RIGHT, 0, 480);
    int sy = map(p.x, TS_TOP,  TS_BOT,   320, 0);
    return (sy < 160) ? UiEvent::HOMING : UiEvent::START;
  }
} // namespace TouchUI

/**************** 5. I²C DRIVER ***************************/
namespace I2C {
  bool send(uint8_t adr, const char *cmd) {
    Wire.beginTransmission(adr);
    Wire.write(cmd);
    return (Wire.endTransmission() == 0);
  }
  bool done(uint8_t adr) {
    Wire.requestFrom((int)adr, 4);
    char r[5] = {0};
    for (int i = 0; i < 4 && Wire.available(); ++i)
      r[i] = Wire.read();
    return !strcmp(r, "DONE");
  }
} // namespace I2C

/**************** 6. G-CODE DRIVER ************************/
class GcodeDriver {
public:
  GcodeDriver(HardwareSerial &p, const char *tg) : port(p), tag(tg) {}

  bool run(const char *file) {
    if (h.active) return false;           // allerede i gang
    port.print(F("M23 "));
    port.println(file);

    char buf[40];
    snprintf(buf, sizeof(buf), "FILE %s START", file);
    log(tag, buf);

    h.set(true, millis(), 180000UL);      // 3 min timeout
    return true;
  }

  bool service() {
    if (!h.active) return true;
    if (millis() - h.started > h.timeout) {
      log(tag, "TIMEOUT");
      h.active = false;
      return true;
    }
    if (!port.available()) return false;

    String l = port.readStringUntil('\n');
    l.trim();
    if (!l.length()) return false;

    if (l.indexOf("Done printing file") >= 0) {
      h.active = false;
      log(tag, "DONE");
    }
    return !h.active;
  }

  bool busy() const { return h.active; }

private:
  HardwareSerial &port;
  const char     *tag;
  OpHandle        h;
};

GcodeDriver laser(Serial1, "LAS");
GcodeDriver ultra(Serial2, "UL ");

/**************** 7. GLOBALE VARIABLER ********************/
State     state    = State::IDLE;
OpHandle  op;
uint32_t  lastTick = 0;

void startOp(uint32_t tout) { op.set(true, millis(), tout); }
bool opExpired()            { return op.active && millis() - op.started > op.timeout; }
void clearOp()              { op.active = false; }

/**************** 8. STATEMACHINE *************************/
void tickSM() {
  switch (state) {

  case State::IDLE:
    break;

  /* ---------- HOMING ---------- */
  case State::HOMING_INIT:
    if (!op.active) {
      log("STATE", "HOMING_INIT");
      I2C::send(ADR_ROLLS, "ROLLS_HOME");
      I2C::send(ADR_VAL,   "VAL_HOME");
      laser.run(F_LAS_HOME);
      ultra.run(F_UL_HOME);
      TouchUI::status("HOMING – vent ...");
      startOp(120000UL);
    }

    if (I2C::done(ADR_ROLLS) && I2C::done(ADR_VAL) &&
        !laser.busy() && !ultra.busy()) {
      clearOp(); state = State::READY;
      TouchUI::status("LÆG VENTIL – TRYK START");
    } else if (opExpired()) {
      clearOp(); state = State::IDLE;
      TouchUI::drawIdle();
      log("ERR", "Homing timeout");
    }
    break;

  /* ---------- VENTER PÅ BRUGER ---------- */
  case State::READY:
    break;

  /* ---------- AUTOMATISK SEKVENS -------- */
  case State::VALVE_FEED:
    state = State::VAL_90;
    break;

  case State::VAL_90:
    if (!op.active) { I2C::send(ADR_VAL, "VAL_90"); TouchUI::status("VALVES 90°"); startOp(20000); }
    if (I2C::done(ADR_VAL) || opExpired()) { clearOp(); state = State::LAS_VAL; }
    break;

  case State::LAS_VAL:
    if (!op.active) { laser.run(F_LAS_VAL); TouchUI::status("LASER VALVE CUT"); startOp(60000); }
    if (!laser.busy() || opExpired()) { clearOp(); state = State::VAL_180; }
    break;

  case State::VAL_180:
    if (!op.active) { I2C::send(ADR_VAL, "VAL_180"); TouchUI::status("VALVES 180°"); startOp(20000); }
    if (I2C::done(ADR_VAL) || opExpired()) { clearOp(); state = State::UL_VAL; }
    break;

  case State::UL_VAL:
    if (!op.active) { ultra.run(F_UL_VAL); TouchUI::status("ULTRA VALVE WELD"); startOp(60000); }
    if (!ultra.busy() || opExpired()) { clearOp(); state = State::ROLLS_FEED; }
    break;

  case State::ROLLS_FEED:
    if (!op.active) { I2C::send(ADR_ROLLS, "ROLLS_MAIN"); TouchUI::status("ROLLS 375 mm"); startOp(30000); }
    if (I2C::done(ADR_ROLLS) || opExpired()) { clearOp(); state = State::UL_KONT; }
    break;

  case State::UL_KONT:
    if (!op.active) { ultra.run(F_UL_KONT); TouchUI::status("ULTRA CONTOUR"); startOp(60000); }
    if (!ultra.busy() || opExpired()) { clearOp(); state = State::LAS_KONT; }
    break;

  case State::LAS_KONT:
    if (!op.active) { laser.run(F_LAS_KONT); TouchUI::status("LASER CONTOUR"); startOp(60000); }
    if (!laser.busy() || opExpired()) { clearOp(); state = State::COMPLETE; }
    break;

  case State::COMPLETE:
    if (!op.active) {
      I2C::send(ADR_VAL, "VAL_STOP");
      TouchUI::status("FJERN PRODUKTER – TRYK HOMING");
      startOp(1);   // dummy så vi kun kører én gang
    }
    break;
  }
}

/**************** 9. TOUCH-EVENT HANDLER ******************/
void handleEvt(UiEvent e) {
  if (e == UiEvent::NONE) return;

  switch (state) {
  case State::IDLE:
  case State::COMPLETE:
    if (e == UiEvent::HOMING) { state = State::HOMING_INIT; clearOp(); }
    break;

  case State::READY:
    if (e == UiEvent::START)  { state = State::VALVE_FEED;  clearOp(); }
    break;

  default:
    /* ignorer touch under aktiv kørsel */
    break;
  }
}

/**************** 10. SETUP *******************************/
void setup() {
  Serial.begin(115200);
  Serial1.begin(BAUD_LASER);
  Serial2.begin(BAUD_ULTRA);

  Wire.begin();
  pinMode(20, INPUT_PULLUP);   // SCL
  pinMode(21, INPUT_PULLUP);   // SDA

  uint16_t id = tft.readID();
  if (id == 0xD3D3) id = 0x9486;   // clone default
  tft.begin(id);
  tft.setRotation(3);

  TouchUI::drawIdle();
  log("MASTER", "V5.3 klar – tryk HOMING");
}

/**************** 11. LOOP ********************************/
void loop() {
  /* videresend relevante linjer fra Marlin-boards til USB serial */
  while (Serial1.available()) {
    String l = Serial1.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    if (l.startsWith("open failed") || l.indexOf("Done printing file") >= 0)
      log("LAS", l.c_str());
  }

  while (Serial2.available()) {
    String l = Serial2.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    if (l.startsWith("open failed") || l.indexOf("Done printing file") >= 0)
      log("UL ", l.c_str());
  }

  handleEvt(TouchUI::poll());

  if (millis() - lastTick >= 25) {
    lastTick = millis();
    laser.service();
    ultra.service();
    tickSM();
  }
}
