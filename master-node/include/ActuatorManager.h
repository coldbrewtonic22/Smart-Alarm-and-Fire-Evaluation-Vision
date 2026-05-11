#ifndef ACTUATOR_MANAGER_H
#define ACTUATOR_MANAGER_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "Config.h"

class ActuatorManager {
private:
    Servo doorServo;
    
    unsigned long lastBuzzerToggle;
    bool buzzerState;

public:
    ActuatorManager();
    
    void begin();
    
    void controlRelays(bool fanOn, bool pumpOn);
    
    void controlDoor(bool open);
    
    void setLED(bool on);
    
    void handleBuzzer(bool active);
};

#endif