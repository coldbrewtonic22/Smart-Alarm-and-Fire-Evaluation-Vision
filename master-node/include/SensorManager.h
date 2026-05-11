#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <Arduino.h>
#include <SimpleKalmanFilter.h>
#include "Config.h"

class SensorManager {
private:
    SimpleKalmanFilter kalmanFilter;
    int currentGasValue;
    bool fireDetected;

public:
    SensorManager();
    
    void begin();
    
    void readSensors();
    
    int getGasValue();
    
    bool isFireDetected();
};

#endif