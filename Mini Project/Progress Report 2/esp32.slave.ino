/*
 * ESP32 SLAVE - HARDWARE FIX VERSION
 * Hardware: RoboESP32 (Built-in Motor Driver)
 */

#define BLYNK_TEMPLATE_ID "TMPL6IY5lT_-W"
#define BLYNK_TEMPLATE_NAME "smartwashingmachine"
#define BLYNK_AUTH_TOKEN "3Cor6dVp2gGGBwM58SEbCPMj_lGFNSNW" 

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

char ssid[] = "encik garam";       
char pass[] = "aininlah";     

#define TRIG_PIN     25  
#define ECHO_PIN     26  
#define SERVO_PIN    19  
#define MOTOR_PWM    12  // RoboESP32 Built-in Driver PWM
#define MOTOR_DIR    13  // RoboESP32 Built-in Driver DIR

#define DIST_LIMIT   15.0 
#define SHAKE_THRESHOLD 15.0

Servo doorLock;
Adafruit_MPU6050 mpu;

int systemState = 0; 
int washMode = 0;    
bool doorClosed = false;
bool appControl = false; 
bool isConnected = false; 
bool lastMotorRunning = false;
String inputString = "";

BLYNK_WRITE(V0) { if (appControl) { washMode = param.asInt() - 1; syncToMega(); } }
BLYNK_WRITE(V1) { if (appControl) { systemState = param.asInt() ? 1 : 2; syncToMega(); } }
BLYNK_WRITE(V3) { 
  appControl = (param.asInt() == 1); 
  Serial.println(appControl ? "MODE: APP" : "MODE: MANUAL");
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 4, 5);
  Wire.begin(); 

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  // CRITICAL: Ensure DIR is output
  pinMode(MOTOR_PWM, OUTPUT);
  pinMode(MOTOR_DIR, OUTPUT);
  
  doorLock.attach(SERVO_PIN);
  doorLock.write(0); 

  if (mpu.begin()) mpu.setAccelerometerRange(MPU6050_RANGE_8_G);

  WiFi.begin(ssid, pass);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 8) { delay(500); attempts++; }

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.config(BLYNK_AUTH_TOKEN);
    isConnected = true;
    Blynk.syncVirtual(V3); 
  }
}

void loop() {
  if (isConnected) Blynk.run();

  // 1. SENSE DOOR
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  float dist = pulseIn(ECHO_PIN, HIGH, 30000) * 0.034 / 2;
  doorClosed = (dist > 1.0 && dist < DIST_LIMIT);
  
  static unsigned long lastSync = 0;
  if (millis() - lastSync > 200) {
    Serial2.print(doorClosed ? 'C' : 'O');
    lastSync = millis();
  }

  // 2. SENSE VIBRATION
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float totalAccel = sqrt(sq(a.acceleration.x) + sq(a.acceleration.y) + sq(a.acceleration.z));
  if (totalAccel > SHAKE_THRESHOLD) handleInstability();

  // 3. LISTEN TO MEGA
  while (Serial2.available()) {
    char inChar = (char)Serial2.read();
    if (inChar == '\n') {
      processMegaCommand();
      inputString = "";
    } else {
      inputString += inChar;
    }
  }

  handleHardware();
  delay(20);
}

void processMegaCommand() {
  inputString.trim();
  if (!appControl && inputString.startsWith("S")) {
    systemState = inputString.substring(1, 2).toInt();
    int mIdx = inputString.indexOf('M');
    if (mIdx != -1) washMode = inputString.substring(mIdx + 1).toInt();
    
    if (isConnected) {
      Blynk.virtualWrite(V1, (systemState == 1)); 
      Blynk.virtualWrite(V0, washMode + 1);
    }
  }
}

void handleHardware() {
  // Logic: Only run if state is 1 and door is closed
  bool motorShouldRun = (systemState == 1 && doorClosed);

  if (motorShouldRun) {
    if (!lastMotorRunning) {
      Serial.println(">>> MOTOR RUNNING");
      lastMotorRunning = true;
    }

    doorLock.write(90); 
    
    // --- MOTOR DRIVER FIX ---
    digitalWrite(MOTOR_DIR, LOW); // Change to HIGH if you want opposite direction
    int speeds[] = {120, 180, 220, 255}; // Increased minimum speed to 120
    
    if(washMode >= 0 && washMode <= 3) {
      analogWrite(MOTOR_PWM, speeds[washMode]);
    }
  } 
  else {
    if (lastMotorRunning) {
      Serial.println(">>> MOTOR STOPPED");
      lastMotorRunning = false;
    }
    doorLock.write(0); 
    analogWrite(MOTOR_PWM, 0);
    digitalWrite(MOTOR_DIR, LOW);
  }
}

void handleInstability() {
  systemState = 2;
  if (isConnected) Blynk.logEvent("shake_alert", "Vibration!");
  Serial2.print('V'); 
  handleHardware(); 
}

void syncToMega() {
  char modes[] = {'w', 'x', 'y', 'z'};
  if (washMode >= 0 && washMode <= 3) Serial2.print(modes[washMode]);
  
  if (systemState == 1) Serial2.print('r');
  else if (systemState == 2) Serial2.print('p');
  else Serial2.print('m');
}