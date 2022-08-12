#include <AccelStepper.h>
#include <MultiStepper.h>

#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <GyverEncoder.h>

// pins for encoder
#define CLK 3  //D3 INT1 input works better with interruption handler
#define DT 10  //D10      input
#define SW 9   //D9      input

// pins for rotation sensor
#define RS 2  //D2 Revolution Signal (RS) INT0 interruption when coil has been rotated on angle 360˙.

// pins for wire adapter
#define AL 11  //D11
#define AR 12  //D12

// pins for Motor1
#define M1DIR 6   //D6
#define M1STEP 5  //D5

#define ENABLE_MOTORS 8  //D8 Switch on the motors 1 & 2

// pins for Motor2
#define M2DIR 4   //D4
#define M2STEP 7  //D7

// pins for motor N1
// #define PIN_ENA 9  //D9 The input to control speed of motor
// #define PIN_IN1 10 //D10 The input to manage direction of the rotation; Right direction;
// #define PIN_IN2 11 //D11 The input to manage direction of the rotation; Left direction;

/**********************************************************************************************/
// Menu data types
typedef enum EncoderEventType {
  EncoderEventTypeNone,
  EncoderEventTypeClicked,
  EncoderEventTypeRight,
  EncoderEventTypeRightHolded,
  EncoderEventTypeLeft,
  EncoderEventTypeLeftHolded,
} EncoderEventType;

typedef enum Statements {
  Statement1,
  Statement2,
  Statement3,
  Statement4,
  Statement5,
  Statement6
} Statements;

typedef struct MenuItem {
  const char *title;
  void (* displayMenu)(const MenuItem *);
  bool (* entryToMenu)(MenuItem *);
  bool (* exitFromMenu)(MenuItem *);
  void (* changeValue)(EncoderEventType);
  bool isActive;
  Statements currentStatement;
  Statements maxStatments;
} MenuItem;
/* end menu data types */


// environment
LiquidCrystal_I2C screen(0x27, 20, 4);
Encoder encoder(CLK, DT, SW, TYPE2);
AccelStepper coilMotor = AccelStepper(1, M1STEP, M1DIR);
AccelStepper wireMotor = AccelStepper(1, M2STEP, M2DIR);

// constants
const int coilMotorSpeed = 1000;
const int wireMotorSpeed = 1000;

// input params
int revolutions = 0;
// inputs:
float frequency = 452.781; // Frequency, ƒ (KHz)
float stepFrequency = 0.01; // KHz
unsigned int lengthOfTurn = 364; // mm
unsigned int stepLengthOfTurn = 1; // mm
// Wire, l (m)
// Coil, ø (mm)
// Wire, ø (mm)
// Number of turns, n
float turns = 0;
// Revolutions per minutes, rpm
// Liambda, ƛ1/4
// Liambda, ƛ1/2
//
// c = 299150133.603714 m/s
// c = 299792458 m/s
//
//
const float c = 299792458;// 299150133.603714;
const float pi = 3.14159265359;

// output params
float coilDiameter()
{
  return (float)lengthOfTurn / pi;
}

float wireLengthWave4()
{
  return frequency == 0 ? 0.0 : c / frequency / 1000 / 4.0;
}

float wireLengthWave2()
{
  return frequency == 0 ? 0.0 : c / frequency / 1000 / 2.0;
}

float numberOfTurns(float length)
{
  return lengthOfTurn == 0.0 ? 0.0 : length * 1000 / lengthOfTurn;
}

void (*job)();
void (*updateJob)();
void motorControl();

// menu exec functions
void displayMenuTitle(const MenuItem * item)
{
  screen.clear();
  screen.home();
  const char * title = (item != NULL && item->title != NULL)
    ? item->title
    : "It's weird, no title!";
  screen.print(title);
  Serial.println(title);
}

void activateMenu() {
  screen.setCursor(0,1);
  screen.cursor();
  screen.blink();
}

void deactivateMenu()
{
  screen.noBlink();
  screen.noCursor();
}

/*
  Setup result menu
*/
void printRevolution()
{
  screen.setCursor(9,3);
  screen.print(" rev:");
  screen.print(revolutions);
}

void displayResultMenu(const MenuItem * item)
{
  displayMenuTitle(item);
  screen.print(" KHz:");
  screen.print(frequency);
  screen.setCursor(0,1);
  screen.print("L/4,m:");
  float wireLength = wireLengthWave4();
  screen.print(wireLength);

  screen.setCursor(0,2);
  screen.print("D,mm:");
  screen.print(coilDiameter());

  screen.setCursor(0,3);
  screen.print("n:");
  turns = numberOfTurns(wireLength);
  screen.print(turns);
 
  printRevolution();
}

bool entryToResultMenu(MenuItem * item)
{
  activateMenu();
  job = motorControl;
  digitalWrite(ENABLE_MOTORS, LOW);
  return true;
}

bool exitFromResultMenu(MenuItem * item)
{
  deactivateMenu();
  job = NULL;
  digitalWrite(ENABLE_MOTORS, HIGH);
  return true;
}

/*
 Setup Frequency parameters menu
*/
void updateDisplayedFrequencyStep()
{
  screen.setCursor(0, 3);
  screen.print("          ");
  screen.setCursor(0, 3);
  screen.print(stepFrequency);
}

void displayFrequencyMenu(const MenuItem * item)
{
  displayMenuTitle(item);
  screen.setCursor(0, 2);
  screen.print("Step, KHz:");
  updateDisplayedFrequencyStep();

  screen.setCursor(0, 1);
  screen.print(frequency);
}

bool entryToFrequencyMenu(MenuItem * item)
{
  activateMenu();
  item->currentStatement = Statement1;
  stepFrequency = 0.01;
  updateDisplayedFrequencyStep();
  return true;
}

bool exitFromFrequencyMenu(MenuItem * item)
{
  if (item->currentStatement == item->maxStatments)
  {
    deactivateMenu();
    return true;
  }
  item->currentStatement = item->currentStatement + 1;
  stepFrequency *= 10;
  updateDisplayedFrequencyStep();
  return false;
}

void changeFrequency(EncoderEventType event)
{
  // if (event == EncoderEventTypeRightHolded) stepFrequency += 0.01;
  // if (event == EncoderEventTypeLeftHolded) stepFrequency -= 0.01;

  if (event == EncoderEventTypeRight) frequency += stepFrequency;
  if (event == EncoderEventTypeLeft) frequency -= stepFrequency;

  screen.setCursor(0, 1);
  screen.print("          ");
  screen.setCursor(0, 1);
  screen.print(frequency);
}
/*End Setup Frequency Parameters menu*/

/*
  Menu API to setup turn length of wire based on coil diameter
*/
void displayWireTurnLength(const MenuItem * item)
{
  displayMenuTitle(item);
  screen.setCursor(0,1);
  screen.print(lengthOfTurn);
  screen.setCursor(0,2);
  screen.print("Step in mm:");
  screen.setCursor(0,3);
  screen.print(stepLengthOfTurn);
}

void changeTurnLength(EncoderEventType event)
{
  if (event == EncoderEventTypeRight) lengthOfTurn += stepLengthOfTurn;
  if (event == EncoderEventTypeLeft) lengthOfTurn -= stepLengthOfTurn;

  screen.setCursor(0,1);
  screen.print("      ");
  screen.setCursor(0,1);
  screen.print(lengthOfTurn);
}

bool entryToWireTurnLengthMenu(MenuItem * item)
{
  activateMenu();
  item->currentStatement = Statement1;
  stepLengthOfTurn = 1;
  screen.setCursor(0,3);
  screen.print("      ");
  screen.setCursor(0,3);
  screen.print(stepLengthOfTurn);
  return true;
}

bool exitFromWireTurnLengthMenu(MenuItem * item)
{
  if (item->currentStatement == item->maxStatments)
  {
    deactivateMenu();
    return true;
  }
  item->currentStatement = item->currentStatement + 1;
  stepLengthOfTurn *= 10;

  screen.setCursor(0,3);
  screen.print("      ");
  screen.setCursor(0,3);
  screen.print(stepLengthOfTurn);
  return false;
}

/* End of menu */

//Frequency, ƒ (KHz)
//Number of turns, n
//Liambda, ƛ1/4

// menu structure
const MenuItem menu[3] = {
  { "Frequency, KHz:", &displayFrequencyMenu, &entryToFrequencyMenu, &exitFromFrequencyMenu, &changeFrequency, false, Statement1, Statement6},
  { "Length of turn, mm:", &displayWireTurnLength, &entryToWireTurnLengthMenu, &exitFromWireTurnLengthMenu, &changeTurnLength, false, Statement1, Statement3},
  { "Result:", &displayResultMenu, &entryToResultMenu, &exitFromResultMenu, NULL, false, Statement1, Statement1}
};
const int menuCount = sizeof(menu) / sizeof(MenuItem);
int currentMenuIndex = 0;
/*******************************************************************************************
*/


// Managing Menu
void processEncoder()
{
  EncoderEventType event = proceedEncoder();
  if (event == EncoderEventTypeNone) return;
  Serial.print("Received encoder event: ");
  Serial.println(event, HEX);

  // entry & exit to/from menu item
  if (event == EncoderEventTypeClicked) {
    MenuItem * item = &menu[currentMenuIndex];
    item->isActive = item->isActive
    ? !item->exitFromMenu(item)
    : item->isActive = item->entryToMenu(item);
    return;
  }

  //Setup Parameters
  const MenuItem item = menu[currentMenuIndex];
  if (item.isActive && item.changeValue != NULL)
  {
    item.changeValue(event);
    return;
  }

  //navigate on menu
  int newMenuIndex = nextMenuIndex(event);
  if (currentMenuIndex != newMenuIndex) {
    // Serial.print("Menu row activated: ");
    // Serial.println(newMenuIndex, HEX);
    const MenuItem item = menu[newMenuIndex];
    item.displayMenu(&item);
    currentMenuIndex = newMenuIndex;
  }
  // event = EncoderEventTypeNone;
}

int nextMenuIndex(EncoderEventType event) {
  int menuIndex = currentMenuIndex;
  switch (event) {
    case EncoderEventTypeRight:
      menuIndex = menuIndex + 1;
      break;
    case EncoderEventTypeLeft:
      menuIndex = menuIndex - 1;
      break;
    default:
      break;
  }
  if (menuIndex > menuCount - 1) return menuCount - 1;
  if (menuIndex < 0) return 0;
  return menuIndex;
}

// Encoder statements
EncoderEventType proceedEncoder() {
  encoder.tick();
  if (encoder.isRightH()) return EncoderEventTypeRightHolded;
  if (encoder.isLeftH()) return EncoderEventTypeLeftHolded;
  if (encoder.isRight()) return EncoderEventTypeRight;
  if (encoder.isLeft()) return EncoderEventTypeLeft;
  if (encoder.isPress()) return EncoderEventTypeClicked;
  return EncoderEventTypeNone;
}

// Setup API
void setupRevolutionSensor() {
  pinMode(RS, INPUT_PULLUP);
  attachInterrupt(0, revolutionSensorActivated, FALLING);
}

void setupEncoder() {
  attachInterrupt(1, encoderActivated, CHANGE);
}

void setupStepperMotors() {
  pinMode(ENABLE_MOTORS, OUTPUT);
  digitalWrite(ENABLE_MOTORS, HIGH);
  // pinMode(M1DIR, OUTPUT);
  // pinMode(M1STEP, OUTPUT);
  // pinMode(M2DIR, OUTPUT);
  // pinMode(M2STEP, OUTPUT);

  // digitalWrite(M1DIR, LOW);
  // digitalWrite(M1STEP, HIGH);
  // digitalWrite(M2DIR, HIGH);
  // digitalWrite(M2STEP, LOW);

  coilMotor.setMaxSpeed(1000);
  wireMotor.setMaxSpeed(1000);

  coilMotor.setCurrentPosition(0);
  wireMotor.setCurrentPosition(0);

  coilMotor.setSpeed(-1000); // Right coil winding;
}

void setupWireAdapter() {
  pinMode(AL, INPUT_PULLUP);
  pinMode(AR, INPUT_PULLUP);
}

void setupLCD() {
  screen.begin();      //initialize the lcd
  screen.backlight();  //open the backlight

  screen.setCursor(0, 1);                // go to the top left corner
  screen.print("Coil Winding Machine");  // write this string on the top row
  screen.setCursor(0, 2);                // go to the fourth row
  screen.print(" calmpod@gmail.com  ");
}

void setupMenu() {
  delay(3000);
  screen.clear();
  const MenuItem item = menu[currentMenuIndex];
  item.displayMenu(&item);
}

// Main setup
void setup() {
  Serial.begin(9600);

  // setup devices:
  setupLCD();
  setupEncoder();
  setupRevolutionSensor();
  setupWireAdapter();
  setupStepperMotors();

  //setup menu
  setupMenu();
}

// Interruptions API
void revolutionSensorActivated() {
  // change revolution counter
  revolutions++;
  updateJob = printRevolution;
  // it's better to increase the counter and setup output to LCD
}

void encoderActivated() {
  encoder.tick();
}

void motorControl()
{
  if (revolutions > turns) {
    coilMotor.stop();
    wireMotor.stop();
    return;
  }
  coilMotor.runSpeed();

  if (digitalRead(AR) == HIGH && digitalRead(AL) == HIGH)
  {
    wireMotor.stop();
    return;
  }

  if (digitalRead(AL) == LOW)
  {
    wireMotor.setSpeed(-wireMotorSpeed);
    wireMotor.runSpeed();
    return;
  }
  
  if (digitalRead(AR) == LOW)
  {
    wireMotor.setSpeed(wireMotorSpeed);
    wireMotor.runSpeed();
    return;
  }
}

// Main loop
void loop() {
  processEncoder();
  if (job != NULL)
  {
    job();
  }
  if (updateJob != NULL)
  {
    updateJob();
    updateJob = NULL; 
  }
}
