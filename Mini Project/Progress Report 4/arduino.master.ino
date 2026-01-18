/*
 * ARDUINO MEGA - FINAL WORKING VERSION
 * Controls: LCD, 3 LEDs, 2 Buttons, Pot, Buzzer
 * Communicates with ESP32 via Serial1 (TX18, RX19)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// --- PIN DEFINITIONS ---
#define POT_PIN      A0
#define START_BTN    A4
#define STOP_BTN     A3
#define GREEN_LED    4  // Ready
#define YELLOW_LED   3  // Running
#define RED_LED      2  // Stopped/Alert
#define BUZZER_PIN   9

LiquidCrystal_I2C lcd(0x27, 20, 4);

enum SystemState { STATE_MENU = 0, STATE_RUNNING = 1, STATE_STOPPED = 2 };
SystemState currentState = STATE_MENU;

int washMode = 0; 
bool isDoorClosed = false;
bool lastStopState = HIGH;
bool lastStartState = HIGH;
unsigned long lastCommTime = 0;
int lastPotVal = 0;

void setup() {
  Serial.begin(9600);   
  Serial1.begin(9600);
  
  delay(100);
  while(Serial1.available()) Serial1.read();
  
  pinMode(START_BTN, INPUT_PULLUP);
  pinMode(STOP_BTN, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(GREEN_LED, LOW);
  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(RED_LED, LOW);

  lcd.init();
  lcd.backlight();
  
  tone(BUZZER_PIN, 1000, 200); delay(200);
  tone(BUZZER_PIN, 1500, 200); delay(200);
  
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM INITIALIZED");
  delay(1000);
  lcd.clear();
  
  lastPotVal = analogRead(POT_PIN); 
  updateLCDMode(); 
  
  Serial.println("Mega Ready!");
}

void loop() {
  // --- 1. RECEIVE DATA FROM ESP32 ---
  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (c < 32 && c != '\n') continue;
    if (c > 126) continue;
    
    if (c == 'C') isDoorClosed = true;
    if (c == 'O') isDoorClosed = false;
    if (c == 'V') triggerVibrationAlarm();

    // Sync from App (ESP32 sends these when Blynk changes)
    if (c == 'w') { washMode = 0; updateLCDMode(); }
    if (c == 'x') { washMode = 1; updateLCDMode(); }
    if (c == 'y') { washMode = 2; updateLCDMode(); }
    if (c == 'z') { washMode = 3; updateLCDMode(); }

    if (c == 'r') { currentState = STATE_RUNNING; Serial.println("App: RUN"); }
    if (c == 'p') { currentState = STATE_STOPPED; Serial.println("App: STOP"); }
    if (c == 'm') { currentState = STATE_MENU; Serial.println("App: MENU"); }
  }

  // --- 2. PERIODIC UPDATE TO ESP32 ---
  if (millis() - lastCommTime > 200) {
    sendStateToESP();
    lastCommTime = millis();
  }

  // --- 3. READ BUTTONS ---
  bool currentStopState = digitalRead(STOP_BTN);
  bool currentStartState = digitalRead(START_BTN);

  switch (currentState) {
    case STATE_MENU:
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(YELLOW_LED, LOW);
      digitalWrite(RED_LED, LOW);
      handlePotentiometer(); 
      if (currentStartState == LOW && lastStartState == HIGH) {
        delay(50);
        if (digitalRead(START_BTN) == LOW) {
          if (isDoorClosed) startCycle();
          else showDoorError();
        }
      }
      break;

    case STATE_RUNNING:
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(YELLOW_LED, HIGH);
      digitalWrite(RED_LED, LOW);
      handleRunningDisplay();
      if (!isDoorClosed) { stopCycle(); showDoorError(); }
      if (currentStopState == LOW && lastStopState == HIGH) {
        delay(50);
        if (digitalRead(STOP_BTN) == LOW) stopCycle();
      }
      break;

    case STATE_STOPPED:
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(YELLOW_LED, LOW);
      digitalWrite(RED_LED, HIGH);
      if (currentStartState == LOW && lastStartState == HIGH) {
        delay(50);
        if (digitalRead(START_BTN) == LOW) {
          if (isDoorClosed) startCycle();
          else showDoorError();
        }
      }
      if (currentStopState == LOW && lastStopState == HIGH) {
        delay(50);
        if (digitalRead(STOP_BTN) == LOW) resetToMenu();
      }
      break;
  }

  lastStopState = currentStopState;
  lastStartState = currentStartState;
  delay(20);
}

void sendStateToESP() {
  Serial1.print('S'); 
  Serial1.print((int)currentState);
  Serial1.print('M'); 
  Serial1.print(washMode);
  Serial1.print('\n');
  Serial1.flush();
}

void triggerVibrationAlarm() {
  currentState = STATE_STOPPED;
  sendStateToESP(); 
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("!!! DANGER !!!");
  lcd.setCursor(0, 1); lcd.print("HIGH VIBRATION");
  for(int i=0; i<3; i++) {
    tone(BUZZER_PIN, 2000); delay(300);
    tone(BUZZER_PIN, 1000); delay(300);
  }
  noTone(BUZZER_PIN);
  lcd.clear();
  updateLCDMode();
}

void handlePotentiometer() {
  int curPotVal = analogRead(POT_PIN);
  if (abs(curPotVal - lastPotVal) > 30) {
    int newMode = washMode;
    if (curPotVal <= 250) newMode = 0;
    else if (curPotVal <= 500) newMode = 1;
    else if (curPotVal <= 750) newMode = 2;
    else newMode = 3;
    
    if (newMode != washMode) {
      washMode = newMode;
      updateLCDMode();
      lastPotVal = curPotVal;
      
      char modes[] = {'w', 'x', 'y', 'z'};
      Serial1.print(modes[washMode]);
      Serial1.flush();
      delay(10);
      sendStateToESP();
      
      Serial.print("Mode changed to ");
      Serial.println(washMode);
    }
  }
}

void updateLCDMode() {
  lcd.setCursor(0, 0);
  lcd.print("SELECT MODE:    ");
  lcd.setCursor(0, 1);
  switch(washMode) {
    case 0: lcd.print("> Mode A (Gentle)"); break;
    case 1: lcd.print("> Mode B (Normal)"); break;
    case 2: lcd.print("> Mode C (Strong)"); break;
    case 3: lcd.print("> Mode D (Spin)  "); break;
  }
}

void startCycle() {
  currentState = STATE_RUNNING;
  
  Serial1.print('r');
  Serial1.flush();
  delay(10);
  sendStateToESP();
  
  Serial.println("Manual: STARTING MOTOR");
  int melody[] = {523, 659, 784, 1046};
  for (int i = 0; i < 4; i++) { 
    tone(BUZZER_PIN, melody[i], 150); 
    delay(100); 
  }
  noTone(BUZZER_PIN);
  lcd.clear();
  handleRunningDisplay();
}

void stopCycle() {
  currentState = STATE_STOPPED;
  
  Serial1.print('p');
  Serial1.flush();
  delay(10);
  sendStateToESP();
  
  Serial.println("Manual: STOPPING MOTOR");
  tone(BUZZER_PIN, 400, 500);
  delay(500);
  noTone(BUZZER_PIN);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PAUSED: START->CON");
  lcd.setCursor(0, 1);
  lcd.print("        STOP ->RES");
}

void resetToMenu() {
  currentState = STATE_MENU;
  
  Serial1.print('m');
  Serial1.flush();
  delay(10);
  sendStateToESP();
  
  Serial.println("Manual: RESET TO MENU");
  int melody[] = {1046, 784, 659, 523};
  for (int i = 0; i < 4; i++) { 
    tone(BUZZER_PIN, melody[i], 150); 
    delay(100); 
  }
  noTone(BUZZER_PIN);
  lcd.clear();
  updateLCDMode();
}

void showDoorError() {        
  lcd.setCursor(0, 0);
  lcd.print("ERROR: DOOR OPEN");
  lcd.setCursor(0, 1);
  lcd.print("CLOSE TO RUN    ");
  tone(BUZZER_PIN, 200, 500);
  delay(500);
  noTone(BUZZER_PIN);
  delay(500);
  updateLCDMode(); 
}

void handleRunningDisplay() {
  lcd.setCursor(0, 0);
  lcd.print("STATUS: RUNNING ");
  lcd.setCursor(0, 1);
  lcd.print("MODE: "); 
  lcd.print((char)('A' + washMode));
  lcd.print("           "); 
}