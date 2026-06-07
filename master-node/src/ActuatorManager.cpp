#include "ActuatorManager.h"

ActuatorManager::ActuatorManager() {
    lastBuzzerToggle = 0;
    buzzerState = false;
}

void ActuatorManager::begin() {
    pinMode(PIN_RELAY_FAN,  OUTPUT);
    pinMode(PIN_RELAY_PUMP, OUTPUT);
    pinMode(PIN_BUZZER,     OUTPUT);
    pinMode(PIN_LED,        OUTPUT);

    digitalWrite(PIN_RELAY_FAN,  LOW);
    digitalWrite(PIN_RELAY_PUMP, LOW);
    digitalWrite(PIN_BUZZER,     LOW);
    digitalWrite(PIN_LED,        LOW);

    // Force the use of a dedicated timer to avoid PWM conflicts between ESP32Servo and other peripherals
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // Init Servo
    doorServo.setPeriodHertz(50);
    doorServo.attach(PIN_SERVO, 500, 2400);

    controlDoor(false);
}

void ActuatorManager::controlRelays(bool fanOn, bool pumpOn) {
    digitalWrite(PIN_RELAY_FAN, fanOn ? HIGH : LOW);
    digitalWrite(PIN_RELAY_PUMP, pumpOn ? HIGH : LOW);
}

void ActuatorManager::controlDoor(bool open) {
    // Update servo position only when the door state changes
    if (isDoorInit || currentDoorState != open) {
        doorServo.write(open ? SERVO_OPEN_ANGLE : SERVO_CLOSE_ANGLE);

        currentDoorState = open;
        isDoorInit = false;
    }
}

void ActuatorManager::setLED(bool on) {
    digitalWrite(PIN_LED, on ? HIGH : LOW);
}

void ActuatorManager::handleBuzzer(bool active) {
    // If SAFE, OFF Buzzer and Reset
    if (!active) {
        digitalWrite(PIN_BUZZER, LOW);
        buzzerState = false;

        return;
    }

    unsigned long currentMillis = millis();
    
    if (buzzerState) { 
        if (currentMillis - lastBuzzerToggle >= 1000) {
            buzzerState = false;
            digitalWrite(PIN_BUZZER, LOW);
            lastBuzzerToggle = currentMillis;
        }
    } 
    else { 
        if (currentMillis - lastBuzzerToggle >= 100) {
            buzzerState = true;
            digitalWrite(PIN_BUZZER, HIGH);
            lastBuzzerToggle = currentMillis;
        }
    }
}