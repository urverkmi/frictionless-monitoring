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

int targetPWM = 200;  // default, overridden by PWM:<value> command
bool cycling  = false;

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("\n--- ESP32 UART spinup controller ready ---");

    pinMode(buttonPin, INPUT_PULLUP);

    pinMode(solenoidPin, OUTPUT);
    digitalWrite(solenoidPin, LOW);

    pinMode(motorIn1, OUTPUT);
    pinMode(motorIn2, OUTPUT);
    pinMode(motorEna, OUTPUT);
    ledcAttach(motorEna, 5000, 8);

    stopMotor();
}

void loop() {
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

void runSpinupCycle() {
    cycling = true;

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
        delay(10);
    }

    // 3. Spin at target speed
    delay(1000);

    if (checkStop()) return;

    // 4. Release solenoid — notify Pi
    digitalWrite(solenoidPin, LOW);
    Serial.println("DETACHED");

    delay(500);

    // 5. Ramp motor down
    for (int i = targetPWM; i >= 0; i--) {
        ledcWrite(motorEna, i);
        delay(10);
    }

    stopMotor();
    cycling = false;
    Serial.println("OK");
}

void emergencyStop() {
    digitalWrite(solenoidPin, LOW);
    stopMotor();
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
