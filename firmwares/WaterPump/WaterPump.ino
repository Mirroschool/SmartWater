/**@file WaterPump.ino */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <buttonMinim.h>

#define PIN_RELAY 3

#define PIN_LEFT_BTN 7
#define PIN_SELECT_BTN 6
#define PIN_RIGHT_BTN 5

#define PIN_HUMIDITY_SENSOR A0 // Analog pin 0 for soil moisture sensor

#define SECONDS_OFFSET 10 // EEPROM adress offset for stroing uint32_t pumping timer
#define PUMPING_SAFETY_INTERVAL 10000 // Minimal safe interval between pumping

buttonMinim btnLeft(PIN_LEFT_BTN);
buttonMinim btnSelect(PIN_SELECT_BTN);
buttonMinim btnRight(PIN_RIGHT_BTN);

// Half arrow chars to display menu selection position and type (full = edit mode, hollow = selected mode)
uint8_t hollowLeftArrow[8] = { 0b11111, 0b01001, 0b00101, 0b00011, 0b00001, 0b00000, 0b00000, 0b00000 };
uint8_t hollowRightArrow[8] = { 0b11111, 0b10010, 0b10100, 0b11000, 0b10000, 0b00000, 0b00000, 0b00000 };
uint8_t fullLeftArrow[8] = { 0b11111, 0b01111, 0b00111, 0b00011, 0b00001, 0b00000, 0b00000, 0b00000 };
uint8_t fullRightArrow[8] = { 0b11111, 0b11110, 0b11100, 0b11000, 0b10000, 0b00000, 0b00000, 0b00000 };

// Hours, minutes, seconds and minimal humidity percentage displayed on screen
// It is being written/read from EEPROM adresses 0 = hours, 1 = minutes, 2 = seconds, 3 = humidity
int8_t hoursInterval, minutesInterval, secondsInterval, humidityPercentage, selectionPosition = 1;

// Hollow arrow selection type
bool selectionType = false;

// Timers used in loop() to re-render UI, write and check eepprom time, and store last safely pumped time
long renderTimer, eepromSecondsTimer, lastPumpingTime = 0;

LiquidCrystal_I2C lcd(0x27, 20, 4);


int testHum = 400;

void setup()
{
    Serial.begin(9600);
    randomSeed(analogRead(A0));

    // Set relay pin to output and logic output to HIGH (HIGH means relay is closed)
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, HIGH);

    lcd.init(); // Initialize I2C connection to LCD screen
    lcd.backlight(); // Turn display backlight

    // Create arrow characters to use later
    lcd.createChar(1, hollowLeftArrow);
    lcd.createChar(2, hollowRightArrow);
    lcd.createChar(3, fullLeftArrow);
    lcd.createChar(4, fullRightArrow);

    // Load pumping interval from EEPROM
    hoursInterval = EEPROM.read(0);
    minutesInterval = EEPROM.read(1);
    secondsInterval = EEPROM.read(2);
    humidityPercentage = EEPROM.read(3);

    refresh(selectionType, selectionPosition);
}

void loop()
{
    // Menu interactions handling
    if (btnLeft.clicked() || btnLeft.holding()) {
        // Left button
        if (selectionType == 0) {
            selectionPosition--;
            if (selectionPosition < 1)
                selectionPosition = 1;
            refresh(selectionType, selectionPosition);
        }

        else {
            switch (selectionPosition) {
            case 1:
                hoursInterval--;
                if (hoursInterval < 0)
                    hoursInterval = 23;
                break;
            case 2:
                minutesInterval--;
                if (minutesInterval < 0)
                    minutesInterval = 59;
                break;
            case 3:
                secondsInterval--;
                if (secondsInterval < 0)
                    secondsInterval = 59;
                break;
            case 4:
                humidityPercentage--;
                if (humidityPercentage < 0)
                    humidityPercentage = 100;
                break;
            }
            refresh(selectionType, selectionPosition);
        }
    }

    // Select button
    if (btnSelect.clicked()) {
        if (selectionType == 0) {
            selectionType = 1;
            refresh(selectionType, selectionPosition);
        }
        else {
            selectionType = 0;
            EEPROM.write(0, hoursInterval);
            EEPROM.write(1, minutesInterval);
            EEPROM.write(2, secondsInterval);
            EEPROM.write(3, humidityPercentage);
        }
    }

    // Right button
    if (btnRight.clicked() || btnRight.holding()) {
        if (selectionType == 0) {
            selectionPosition++;
            if (selectionPosition > 4)
                selectionPosition = 4;
            refresh(selectionType, selectionPosition);
        }
        else {
            switch (selectionPosition) {
            case 1:
                hoursInterval++;
                if (hoursInterval > 23)
                    hoursInterval = 0;
                break;
            case 2:
                minutesInterval++;
                if (minutesInterval > 59)
                    minutesInterval = 0;
                break;
            case 3:
                secondsInterval++;
                if (secondsInterval > 59)
                    secondsInterval = 0;
                break;
            case 4:
                humidityPercentage++;
                if (humidityPercentage > 100)
                    humidityPercentage = 0;
                break;
            }
            refresh(selectionType, selectionPosition);
        }
    }

    if (millis() - renderTimer > 1000) {
        // Re-render screen each second
        refresh(selectionType, selectionPosition);
        renderTimer = millis();
    }

    if (millis() - eepromSecondsTimer > 60000) {
        // Every 60 seconds read stored time from EEPROM in seconds. Then compare it with set pumping interval
        // If this time is bigger than pumping interval, then start pumping and set pumping interval check timer to 0
        // Otherwise increment it by 60 seconds
        unsigned long secondsFromLastPumpingCheck = EEPROMReadlong(SECONDS_OFFSET);
        unsigned long pumpingInterval = hoursInterval * 3600 + minutesInterval * 60 + secondsInterval;

        if (secondsFromLastPumpingCheck > pumpingInterval) {
            if (startPumping()) {
                Serial.println("Humidity is too low. Pumping!");
                EEPROMWritelong(SECONDS_OFFSET, 0);
            }
        }
        else {
            EEPROMWritelong(SECONDS_OFFSET, secondsFromLastPumpingCheck + 60);
        }

        eepromSecondsTimer = millis();
    }

    if (getHumidityPercentage() < humidityPercentage) {
        Serial.println("Humidity is too low. Pumping!");
        startPumping();
    }
}

void EEPROMWritelong(int address, long value)
{
    //Decomposition from a long to 4 bytes by using bitshift.
    //One = Most significant -> Four = Least significant byte
    byte four = (value & 0xFF);
    byte three = ((value >> 8) & 0xFF);
    byte two = ((value >> 16) & 0xFF);
    byte one = ((value >> 24) & 0xFF);

    //Write the 4 bytes into the eeprom memory.
    EEPROM.write(address, four);
    EEPROM.write(address + 1, three);
    EEPROM.write(address + 2, two);
    EEPROM.write(address + 3, one);
}

//This function will return a 4 byte (32bit) long from the eeprom
//at the specified address to adress + 3.
long EEPROMReadlong(long address)
{
    //Read the 4 bytes from the eeprom memory.
    long four = EEPROM.read(address);
    long three = EEPROM.read(address + 1);
    long two = EEPROM.read(address + 2);
    long one = EEPROM.read(address + 3);

    //Return the recomposed long by using bitshift.
    return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

bool startPumping()
{
    // Starts pumping if safety interval passed
    if (millis() - lastPumpingTime > PUMPING_SAFETY_INTERVAL) {
        digitalWrite(PIN_RELAY, LOW);
        delay(3000);
        digitalWrite(PIN_RELAY, HIGH);
        lastPumpingTime = millis();
        
        int humidityAnalogValue = testHum - random(150, 200);
        humidityAnalogValue = constrain(humidityAnalogValue, 0, 1023);
        
        testHum = humidityAnalogValue;
        
        return true;
    }
    else {
        return false;
    }
}

void decOut(int x, int y, int dec)
{
    // Displays number on screen at (x; y) point
    lcd.setCursor(x, y);
    lcd.print(dec / 10);
    lcd.print(dec % 10);
}

void displaySettings()
{
    // Displays current status of pumping interval and minimal humidity settings
    decOut(0, 1, hoursInterval);
    lcd.print(":");
    decOut(3, 1, minutesInterval);
    lcd.print(":");
    decOut(6, 1, secondsInterval);
    lcd.setCursor(11, 1);
    lcd.print(humidityPercentage);
    lcd.print("%");
}

void displayCurrentHumidity()
{
    // Displays current humidity from sensor
    lcd.setCursor(0, 3);
    lcd.print("Humidity - ");
    lcd.print(getHumidityPercentage());
    lcd.print("%");
}

int getHumidityPercentage()
{
    // Reads analog pin value and maps it to 0-99% range
    int humidityAnalogValue = analogRead(PIN_HUMIDITY_SENSOR);
    
    // humidityAnalogValue = testHum + random(-5, 5);
    // humidityAnalogValue = constrain(humidityAnalogValue, 0, 1023);
    
    return map(humidityAnalogValue, 0, 1023, 99, 0);
}

void setSelectionArrow(bool type, int pos)
{
    // Sets hollow (type = false) or full (type = true) arrow at setting position
    // Position (1 = hours, 2 = minutes, 3 = seconds, 4 = minimal humidity)
    switch (pos) {
    case 1:
        lcd.setCursor(0, 0);
        break;
    case 2:
        lcd.setCursor(3, 0);
        break;
    case 3:
        lcd.setCursor(6, 0);
        break;
    case 4:
        lcd.setCursor(11, 0);
        break;
    }

    if (!type) {
        lcd.write(1);
        lcd.write(2);
    }
    else {
        lcd.write(3);
        lcd.write(4);
    }
}

void refresh(bool type, int pos)
{
    // Does full refresh of display
    lcd.clear();
    setSelectionArrow(type, pos);
    displaySettings();
    displayCurrentHumidity();
}
