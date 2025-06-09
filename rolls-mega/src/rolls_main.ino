/*********************************************************
 *  Rolls_v4_TFT – I2C-slave 0x04  +  3,5" MCUFRIEND TFT
 *  ------------------------------------------------------
 *  Kommander:
 *      "TENSION"  →  10 mm  frem
 *      "MAIN"     →  375 mm frem
 *
 *  Hardware:
 *      • Arduino Mega 2560
 *      • TMC2130 / TMC5160 driver (STEP/DIR + SPI)
 *      • 3,5" UNO-shield TFT (ILI9486/ILI9488/…)
 *        Library: MCUFRIEND_kbv  +  Adafruit_GFX
 *
 *  Testet med:
 *      avr-gcc 7.3,  Arduino IDE 2.x
 *      TMCStepper 0.7.3
 *      MCUFRIEND_kbv 2.9.x
 *********************************************************/

#include <MCUFRIEND_kbv.h>
#include <TMCStepper.h>
#include <Wire.h>
#include <SPI.h>

/*********************  TFT  ****************************/
MCUFRIEND_kbv tft;

#define BLACK   0x0000
#define WHITE   0xFFFF
#define CYAN    0x07FF
#define YELLOW  0xFFE0

void showStatus(const char *line1, const char *line2 = "") {
    tft.fillScreen(BLACK);
    tft.setTextColor(CYAN, BLACK);
    tft.setTextSize(3);
    tft.setCursor(10, 30);
    tft.print(line1);
    tft.setTextSize(2);
    tft.setCursor(10, 90);
    tft.setTextColor(YELLOW, BLACK);
    tft.print(line2);
}

/*********************  Motor-pins  *********************/
#define EN_PIN     53      // Driver enable
#define CS_PIN     23      // TMC2130/5160 CS
#define DIR_PIN    27
#define STEP_PIN   25
#define ENDSTOP    A15     // valgfri homing (NC)

/*********************  Motor-konstanter  ***************/
#define R_SENSE      0.11f
#define MICROSTEPS   16
const int  FULL_STEPS_PER_REV = 200;
const int  STEPS_PER_MM       = 80;      // justér hvis nødvendig

const float FEED_RPM = 20.0f;
inline unsigned long usPerStep(float rpm) {
    return (unsigned long)(60.0e6 /
            (rpm * FULL_STEPS_PER_REV * MICROSTEPS));
}

/*********************  Længder  ************************/
const float PRETENSION_MM = 10.0f;
const float MAINFEED_MM   = 375.0f;

/*********************  I²C  ****************************/
#define I2C_ADDR  0x04
volatile char  rxBuf[10] = "";
volatile bool  newCmd    = false;

/*********************  Objekter  ************************/
TMC2130Stepper drv(CS_PIN, R_SENSE);

/*********************  Puls-generator  *****************/
void pulses(long uSteps, bool dir) {
    digitalWrite(DIR_PIN, dir ? HIGH : LOW);

    const unsigned long deltaT = usPerStep(FEED_RPM);
    unsigned long t0 = micros();

    for (long s = 0; s < uSteps; ++s) {
        while (micros() - t0 < deltaT) {}
        t0 = micros();

        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(2);
        digitalWrite(STEP_PIN, LOW);
    }
}

void moveMM(float mm) {
    long fullSteps = (long)(mm * STEPS_PER_MM);
    long uSteps    = fullSteps * MICROSTEPS;
    pulses(uSteps, HIGH);
}

/*********************  I²C callbacks  ******************/
void receiveEvent(int howMany) {
    int i = 0;
    while (Wire.available() && i < 9) rxBuf[i++] = Wire.read();
    rxBuf[i] = '\0';
    newCmd   = true;
}

void busyRequest() { Wire.write("BUSY"); }

/*********************  setup()  ************************/
void setup() {
    Serial.begin(115200);

    /* --- TFT init --- */
    uint16_t ID = tft.readID();
    if (ID == 0xD3D3) ID = 0x9486;   // safety-fix til visse shields
    tft.begin(ID);
    tft.setRotation(1);              // Landscape
    showStatus("Rolls v4", "Init...");

    /* --- motor-pins --- */
    pinMode(EN_PIN,  OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(STEP_PIN,OUTPUT);
    pinMode(CS_PIN,  OUTPUT);
    digitalWrite(CS_PIN, HIGH);

    SPI.begin();
    drv.begin();
    drv.rms_current(600);            // 600 mA
    drv.microsteps(MICROSTEPS);
    drv.en_pwm_mode(true);
    drv.toff(4);

    digitalWrite(EN_PIN, LOW);       // enable driver

    /* --- I²C --- */
    Wire.begin(I2C_ADDR);
    Wire.onReceive(receiveEvent);
    Wire.onRequest(busyRequest);

    showStatus("READY", "Waiting I2C");
    Serial.println(F("Rolls_v4 ready – awaiting I2C."));
}

/*********************  loop()  *************************/
void loop() {
    if (!newCmd) return;
    newCmd = false;

    String cmd = String((char*)rxBuf);
    Serial.print(F("Rx: ")); Serial.println(cmd);
    showStatus("BUSY", cmd.c_str());

    if (cmd == "TENSION") {
        moveMM(PRETENSION_MM);
        Serial.println(F("Pretension DONE"));
        showStatus("DONE", "Pretension");
    }
    else if (cmd == "MAIN") {
        moveMM(MAINFEED_MM);
        Serial.println(F("Main feed DONE"));
        showStatus("DONE", "Main feed");
    }
    else {
        Serial.println(F("Ukendt kommando"));
        showStatus("ERROR", "Unknown cmd");
    }

    /* svar DONE til master */
    Wire.onRequest([](){ Wire.write("DONE"); });
    delay(20);                         // kort vindue til master
    Wire.onRequest(busyRequest);       // tilbage til BUSY-default
}
