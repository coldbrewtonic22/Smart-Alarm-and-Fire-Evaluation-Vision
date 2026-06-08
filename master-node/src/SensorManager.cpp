#include "SensorManager.h"

// e_mea = 2, e_est = 2, q = 0.1
SensorManager::SensorManager() : kalmanFilter(2, 2, 0.1) {
    currentGasValue = 0;
    fireDetected = false;
}

void SensorManager::begin() {
    pinMode(PIN_MQ2, INPUT);
    pinMode(PIN_FLAME, INPUT);
}

void SensorManager::readSensors() {
    float rawSum = 0;
    for (int i = 0; i < 5; i++) {
        rawSum += analogRead(PIN_MQ2);
        delayMicroseconds(200);
    }
    float rawMQ2 = rawSum / 5.0f;
    
    float filteredMQ2 = kalmanFilter.updateEstimate(rawMQ2);
    currentGasValue = map(filteredMQ2, 0, 4095, 0, 10000);

    int fireValue = digitalRead(PIN_FLAME);
    fireDetected = (fireValue == 0); 
}

int SensorManager::getGasValue() {
    return currentGasValue;
}

bool SensorManager::isFireDetected() {
    return fireDetected;
}