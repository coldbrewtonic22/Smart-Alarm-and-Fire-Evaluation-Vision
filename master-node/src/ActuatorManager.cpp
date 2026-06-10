#include "ActuatorManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

ActuatorManager::ActuatorManager() {
    lastBuzzerToggle = 0;
    buzzerState = false;
    isDoorInit = true;        // FIX: ensure first controlDoor() call always writes servo
    currentDoorState = false;
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
    
    // Init Servo - drive to CLOSE position at startup
    // CRITICAL: Do NOT call controlDoor() here.
    // begin() runs inside setup(), BEFORE the FreeRTOS scheduler starts.
    // controlDoor() calls vTaskDelay() which panics if scheduler is not running.
    // This is why the system works on first flash but crashes on every subsequent reboot.
    // Fix: write servo directly here using blocking delay() which is safe pre-scheduler.
    doorServo.setPeriodHertz(50);
    doorServo.attach(PIN_SERVO, 500, 2400);
    delay(100);
    doorServo.write(SERVO_CLOSE_ANGLE);
    currentDoorState = false;
    isDoorInit       = false;
    delay(600);         // wait for servo to reach position (blocking delay OK here - pre-scheduler)
    doorServo.detach();
}

void ActuatorManager::controlRelays(bool fanOn, bool pumpOn) {
    digitalWrite(PIN_RELAY_FAN, fanOn ? HIGH : LOW);
    digitalWrite(PIN_RELAY_PUMP, pumpOn ? HIGH : LOW);
}

void ActuatorManager::controlDoor(bool open) {
    // Only move servo when state actually changes
    if (isDoorInit || currentDoorState != open) {
        // Re-attach if detached
        if (!doorServo.attached()) {
            doorServo.setPeriodHertz(50);
            doorServo.attach(PIN_SERVO, 500, 2400);
        }

        doorServo.write(open ? SERVO_OPEN_ANGLE : SERVO_CLOSE_ANGLE);

        currentDoorState = open;
        isDoorInit = false;

        // Detach after 600ms so servo reaches position then stops receiving PWM
        // This prevents SG90 from buzzing/twitching while holding position
        // FIX: use vTaskDelay instead of delay() to yield properly in FreeRTOS
        //      delay() spin-blocks the CPU core and starves Task_Blynk → disconnect
        vTaskDelay(pdMS_TO_TICKS(600));
        doorServo.detach();
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