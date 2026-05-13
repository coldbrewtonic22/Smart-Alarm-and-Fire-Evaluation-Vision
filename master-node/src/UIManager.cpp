#include "UIManager.h"

UIManager::UIManager() : 
    lcd(LCD_ADDR, LCD_COLS, LCD_ROWS),
    keypad(makeKeymap(keys), rowPins, colPins, 4, 4) 
{}

void UIManager::begin() {
    lcd.init();
    lcd.backlight();
}

void UIManager::showStartupScreen() {
    lcd.clear();
    
    // Row 0: Title
    lcd.setCursor(0, 0);
    lcd.print("   S.A.F.E Vision   ");
    
    // Row 1: Group name
    lcd.setCursor(0, 1);
    lcd.print(GROUP_NAME);
    
    // Row 2: Class
    lcd.setCursor(0, 2);
    lcd.print("Class: ");
    lcd.print(CLASS_ID);
    
    // Hàng 3: Trạng thái khởi tạo
    lcd.setCursor(0, 3);
    lcd.print("  Initializing...   ");
}

void UIManager::updateMainScreen(int gas, bool fire, int relayState, bool doorOpen, bool wifiOk, SystemMode mode) {
    char buffer[21];
    
    // ROW 1
    sprintf(buffer, "GAS: %-4dPPM FIRE: %d", gas, fire ? 1 : 0);
    lcd.setCursor(0, 0);
    lcd.print(buffer);

    // ROW 2
    String doorStr = doorOpen ? "OPEN " : "CLOSE";
    sprintf(buffer, "RELAY: %d DOOR: %s", relayState, doorStr.c_str());
    lcd.setCursor(0, 1);
    lcd.print(buffer);

    // ROW 3
    String wifiStr = wifiOk ? "ON " : "OFF";
    String modeStr = (mode == MODE_AUTO) ? "AUTO  " : "MANUAL";
    sprintf(buffer, "WIFI: %s MODE: %s", wifiStr.c_str(), modeStr.c_str());
    lcd.setCursor(0, 2);
    lcd.print(buffer);

    // ROW 4
    lcd.setCursor(0, 3);
    lcd.print(" > Press * for Menu ");
}

void UIManager::showMessage(int col, int row, String msg, bool clearScreen) {
    if (clearScreen) {
        lcd.clear();
    }

    lcd.setCursor(col, row);
    lcd.print(msg);
}

char UIManager::getPressedKey() {
    return keypad.getKey(); // Return NO_KEY if there was nothing pressed
}