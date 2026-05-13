#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "Config.h"

class UIManager {
private:
    LiquidCrystal_I2C lcd;
    
    char keys[4][4] = {
        {'1','2','3','A'},
        {'4','5','6','B'},
        {'7','8','9','C'},
        {'*','0','#','D'}
    };
    byte rowPins[4] = { KP_ROW_1, KP_ROW_2, KP_ROW_3, KP_ROW_4} ;
    byte colPins[4] = { KP_COL_1, KP_COL_2, KP_COL_3, KP_COL_4 };
    Keypad keypad;

public:
    UIManager();
    
    void begin();
    
    void showStartupScreen();
    
    void updateMainScreen(int gas, bool fire, int relayState, bool doorOpen, bool wifiOk, SystemMode mode);
    
    void showMessage(int col, int row, String msg, bool clearScreen = false);
    
    char getPressedKey();
};

#endif