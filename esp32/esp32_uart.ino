#include <Wire.h>
#include "AS5600.h"

/*
 * ESP32-C6: UART-controlled spinup system
 * 12V 2A power source — solenoid (TIP120) + Motor (L298N)
 *
 * Receives commands from Raspberry Pi over Serial (USB-CDC):
 *   PWM:<value>\n  — set target PWM and run spinup cycle
 *   STOP\n         — emergency stop
 *   PING\n         — health check
 *
 * Sends back:
 *   OK\n           — command acknowledged / cycle complete
 *   DETACHED\n     — spinup solenoid released
 *   PONG\n         — ping response
 */

const int buttonPin   = 4;
const int solenoidPin = 5;
const int motorIn1    = 6;
const int motorIn2    = 7;
const int motorEna    = 20;

// Pump pins (L298N Channel B)
const int pumpIn3     = 1;
const int pumpIn4     = 2;
const int pumpEnb     = 3;

// Encoder pins
const int SDA_PIN = 23;
const int SCL_PIN = 22;

AS5600 as5600;
int targetPWM = 200;  // default, overridden by PWM:<value> command
bool cycling  = false;

// Variables for RPM calculation
float currentRPM = 0;
float lastAngle = 0;
unsigned long lastRPMTime = 0;
unsigned long lastSerialUpdate = 0;

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("\n--- ESP32 UART spinup controller ready ---");

    // Initialize I2C for AS5600
    Wire.begin(SDA_PIN, SCL_PIN);
    as5600.begin();
    
    lastAngle = getAngleDegrees();
    lastRPMTime = millis();

    pinMode(buttonPin, INPUT_PULLUP);

    pinMode(solenoidPin, OUTPUT);
    digitalWrite(solenoidPin, LOW);

    pinMode(motorIn1, OUTPUT);
    pinMode(motorIn2, OUTPUT);
    pinMode(motorEna, OUTPUT);
    ledcAttach(motorEna, 5000, 8);

    // Initialize Pump pins
    pinMode(pumpIn3, OUTPUT);
    pinMode(pumpIn4, OUTPUT);
    pinMode(pumpEnb, OUTPUT);
    ledcAttach(pumpEnb, 5000, 8);

    stopMotor();
    stopPump();
}

void loop() {
    // Update RPM background calculation
    updateRPM();

    // UART commands from Pi
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd.startsWith("PWM:")) {
            int val = cmd.substring(4).toInt();
            if (val >= 50 && val <= 200) {
                targetPWM = val;
            }
            // targetPWM keeps its current value (default 200) if parse fails
            runSpinupCycle();
        } else if (cmd == "STOP") {
            emergencyStop();
            Serial.println("OK");
        } else if (cmd == "PING") {
            Serial.println("PONG");
        } else if (cmd == "ANGLE") {
            Serial.println(getAngleDegrees());
        }
    }

    // Physical button trigger (for testing without Pi)
    if (digitalRead(buttonPin) == LOW && !cycling) {
        delay(50);  // debounce
        if (digitalRead(buttonPin) == LOW) {
            Serial.println("Button pressed — running cycle");
            runSpinupCycle();
            while (digitalRead(buttonPin) == LOW);  // wait for release
            delay(200);
        }
    }
}

// Background RPM calculation without delays
void updateRPM() {
    unsigned long now = millis();
    if (now - lastRPMTime >= 100) { 
        float angle = getAngleDegrees();
        float diff = angle - lastAngle;
        
        if (diff < -180) diff += 360;
        else if (diff > 180) diff -= 360;

        currentRPM = abs((diff / (float)(now - lastRPMTime)) * (1000.0 / 360.0) * 60.0);

        lastAngle = angle;
        lastRPMTime = now;
    }
}

float getAngleDegrees() {
    return as5600.readAngle() * 360.0 / 4096.0;
}

void runSpinupCycle() {
    cycling = true;

    // Start pump 3 seconds before spinup
    startPump(255);
    delay(3000);

    if (checkStop()) return;

    // 1. Solenoid ON
    digitalWrite(solenoidPin, HIGH);
    delay(400);

    // Check for STOP during cycle
    if (checkStop()) return;

    // 2. Ramp motor up to targetPWM
    digitalWrite(motorIn1, HIGH);
    digitalWrite(motorIn2, LOW);
    for (int i = 0; i <= targetPWM; i++) {
        ledcWrite(motorEna, i);
        updateRPM();
        delay(10);
    }
    
    // 3. Spin at target speed - Extended to 12 seconds
    unsigned long spinStart = millis();
    while (millis() - spinStart < 12000) {
        updateRPM();
        // Stream RPM to Pi every 200ms during spinup
        if (millis() - lastSerialUpdate > 200) {
            Serial.print("LIVE_RPM:");
            Serial.println(currentRPM);
            lastSerialUpdate = millis();
        }
        if (checkStop()) return;
    }

    if (checkStop()) return;

    // 4. Release solenoid — notify Pi
    digitalWrite(solenoidPin, LOW);
    Serial.print("DETACHED Angle: ");
    Serial.print(getAngleDegrees());
    Serial.print(" FinalRPM: ");
    Serial.println(currentRPM);

    delay(500);

    // 5. Ramp motor down
    for (int i = targetPWM; i >= 0; i--) {
        ledcWrite(motorEna, i);
        updateRPM();
        delay(10);
    }

    stopMotor();
    
    // Keep pump on for additional time to reach total ~30s
    unsigned long pumpExtraStart = millis();
    while (millis() - pumpExtraStart < 15000) {
        updateRPM();
        if (checkStop()) return;
    }
    
    stopPump();

    cycling = false;
    Serial.println("OKAY");
}

void startPump(int power) {
    digitalWrite(pumpIn3, HIGH);
    digitalWrite(pumpIn4, LOW);
    ledcWrite(pumpEnb, power);
}

void stopPump() {
    digitalWrite(pumpIn3, LOW);
    digitalWrite(pumpIn4, LOW);
    ledcWrite(pumpEnb, 0);
}

void emergencyStop() {
    digitalWrite(solenoidPin, LOW);
    stopMotor();
    stopPump();
    cycling = false;
}

void stopMotor() {
    digitalWrite(motorIn1, LOW);
    digitalWrite(motorIn2, LOW);
    ledcWrite(motorEna, 0);
}

bool checkStop() {
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd == "STOP") {
            emergencyStop();
            Serial.println("OK");
            return true;
        }
    }
    return false;
}
