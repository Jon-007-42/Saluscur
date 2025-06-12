#include <TMCStepper.h>
#include <SPI.h>

// Define SPI and motor pins
#define EN_PIN 53                        // Enable pin for all motors
#define MOSI_PIN 51                      // Explicitly define MOSI
#define SCK_PIN 52                       // Explicitly define SCK
const int CS_PINS[] = {48, A9, A8, 22, 49, 24, 28, 26, 30, 50}; // Chip Select pins for M1, M2, M3, M4, M10, M5, M6, M7, M8, M9
const int DIR_PINS[] = {47, 43, 39, 35, 34, 31, 25, 46, 40, 36}; // Direction pins for M1, M2, M3, M4, M10, M5, M6, M7, M8, M9
const int STEP_PINS[] = {45, 41, 37, 33, 32, 29, 27, 44, 42, 38}; // Step pins for M1, M2, M3, M4, M10, M5, M6, M7, M8, M9
const int NUM_MOTORS = sizeof(DIR_PINS) / sizeof(DIR_PINS[0]); // Number of motors

// Motor and microstepping settings
const int baseSteps = 200;        // Base steps per rotation for a typical stepper motor (1.8° per step)
const int targetMicrosteps = 16;  // Target microstepping setting
const int stepsPerRotation = baseSteps * targetMicrosteps;  // Total steps per rotation with microsteps

// Default speed settings
const int currentRPM = 30;        // Motor speed in RPM

TMC5160Stepper* drivers[NUM_MOTORS];

void enableMotors(bool enable) {
  digitalWrite(EN_PIN, enable ? LOW : HIGH);  // Enable/disable motors
  if (!enable) {
    for (int i = 0; i < NUM_MOTORS; i++) {
      drivers[i]->toff(0);  // Fully disable driver output
    }
  }
}

unsigned long calculateTimePerStep(int targetRPM) {
  int totalStepsPerRevolution = baseSteps * targetMicrosteps;
  float rotationsPerSecond = targetRPM / 60.0;
  float stepsPerSecond = rotationsPerSecond * totalStepsPerRevolution;
  return (unsigned long)(1000000.0 / stepsPerSecond); // Time per step in microseconds
}

void runMotorsSimultaneously1(int steps, bool M1Clockwise, bool M4Clockwise) {
  Serial.println("Running M1-M10 at 30 RPM...");
  digitalWrite(DIR_PINS[0], LOW); // Set direction for M1 (ClockWise)
  digitalWrite(DIR_PINS[1], LOW); // Set direction for M2 (ClockWise)
  digitalWrite(DIR_PINS[2], HIGH); // Set direction for M3 (CounterClockWise)
  digitalWrite(DIR_PINS[3], HIGH); // Set direction for M4 (CounterClockWise)
  digitalWrite(DIR_PINS[4], LOW); // Set direction for M10 (ClockWise)
  digitalWrite(DIR_PINS[5], HIGH); // Set direction for M5 (CounterClockWise)
  digitalWrite(DIR_PINS[6], HIGH); // Set direction for M6 (CounterClockWise)
  digitalWrite(DIR_PINS[7], LOW); // Set direction for M7 (ClockWise)
  digitalWrite(DIR_PINS[8], LOW); // Set direction for M8 (ClockWise)
  digitalWrite(DIR_PINS[9], HIGH); // Set direction for M9 (CounterClockWise)

  unsigned long timePerStep = calculateTimePerStep(currentRPM);
  unsigned long lastStepTime = micros();

  for (int i = 0; i < steps; i++) {
    // Debug: Step count temporarily removed for speed testing
    while (micros() - lastStepTime < timePerStep) {
      // Wait for the required time per step
    }
    lastStepTime = micros();

    // Generate step pulses for all motors
    for (int j = 0; j < NUM_MOTORS; j++) {
      // Debug: Motor pulsing temporarily removed for speed testing
      digitalWrite(STEP_PINS[j], HIGH);
    }
    delayMicroseconds(20); // STEP pulse width (unchanged for correct timing)
    for (int j = 0; j < NUM_MOTORS; j++) {
      digitalWrite(STEP_PINS[j], LOW);
    }
  }

  Serial.println("M1-M10 completed their movements.");
}

void runMotorsSimultaneously2(int steps, bool M1Clockwise) {
  Serial.println("Running M9-M10 at 30 RPM...");
  digitalWrite(DIR_PINS[9], M1Clockwise ? LOW : HIGH); // Set direction for M10
  digitalWrite(DIR_PINS[8], !digitalRead(DIR_PINS[9])); // Set direction for M9 opposite of M10 // Set direction for M9 to match Run 1
  digitalWrite(DIR_PINS[9], M1Clockwise ? LOW : HIGH); // Set direction for M10 to match Run 1

  unsigned long timePerStep = calculateTimePerStep(currentRPM);
  unsigned long lastStepTime = micros();

  for (int i = 0; i < steps; i++) {
    Serial.print("Step ");
    Serial.println(i + 1);
    while (micros() - lastStepTime < timePerStep) {
      // Wait for the required time per step
    }
    lastStepTime = micros();

    // Generate step pulses for M9 and M10
    for (int j = 8; j < 10; j++) {
      Serial.print("Pulsing motor ");
      Serial.println(j + 1);
      digitalWrite(STEP_PINS[j], HIGH);
    }
    delayMicroseconds(20); // STEP pulse width
    for (int j = 8; j < 10; j++) {
      digitalWrite(STEP_PINS[j], LOW);
    }
  }

  Serial.println("M9-M10 completed their movements.");
}

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing motors...");

  // Initialize SPI communication
  SPI.begin();

  // Set A9 and A8 as digital outputs
  pinMode(A9, OUTPUT);
  pinMode(A8, OUTPUT);

  // Initialize motor driver pins
  pinMode(EN_PIN, OUTPUT);
  for (int i = 0; i < NUM_MOTORS; i++) {
    pinMode(STEP_PINS[i], OUTPUT);
    pinMode(DIR_PINS[i], OUTPUT);
    pinMode(CS_PINS[i], OUTPUT);
    digitalWrite(CS_PINS[i], HIGH); // Ensure all CS pins are high initially
  }

  digitalWrite(EN_PIN, HIGH); // Disable motors initially
  for (int i = 0; i < NUM_MOTORS; i++) {
    digitalWrite(DIR_PINS[i], LOW);  // Set default direction
    digitalWrite(STEP_PINS[i], LOW); // Ensure STEP is low
  }

  // Initialize TMC5160 drivers
  for (int i = 0; i < NUM_MOTORS; i++) {
    drivers[i] = new TMC5160Stepper(CS_PINS[i], 0.11);
    drivers[i]->begin();
    drivers[i]->rms_current(1600);         // Set motor RMS current to 1800 mA
    drivers[i]->microsteps(targetMicrosteps);
    drivers[i]->en_pwm_mode(false);        // Disable StealthChop, use SpreadCycle
    drivers[i]->pwm_freq(2);               // Set PWM frequency
    drivers[i]->TPOWERDOWN(10);            // Power down delay
    drivers[i]->TCOOLTHRS(0xFFFFF);        // Enable coolstep
    drivers[i]->THIGH(300);                // Adjust high threshold
  }

  enableMotors(true); // Ensure motors are ready
}

void loop() {
  // Run motors in the specified direction for 10 rotations
  Serial.println("Starting M1, M2, M3, M4, M10, M5, M6, M7, M8, and M9 movements...");
  // Calculate steps for 375mm using roll diameter of 60mm
  float rollCircumference = 60 * 3.14159; // Circumference in mm
  int stepsFor375mm = (375.0 / rollCircumference) * stepsPerRotation;
  Serial.println("Setting directions for M1-M10 for first run...");
  Serial.println("Running M1-M10 for 375mm...");
  runMotorsSimultaneously1(stepsFor375mm, true, false);  Serial.println("All motors have completed their movements. Stopping program...");
    enableMotors(false); // Disable motors after first run
  while (true); // Halt execution
}
