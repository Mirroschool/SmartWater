/**@file WaterPump.ino */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <buttonMinim.h>

#define PIN_RELAY 3 ///< Номер піну реле

#define PIN_LEFT_BTN 7 ///< Номер піну лівої кнопки
#define PIN_SELECT_BTN 6 ///< Номер піну кнопки вибору
#define PIN_RIGHT_BTN 5 ///< Номер піну правої кнопки

#define PIN_HUMIDITY_SENSOR A0 ///< Аналоговий пін 0 для датчика вологості ґрунту

#define SECONDS_OFFSET 10 ///< Зміщення адреси EEPROM для зберігання таймера поливу
#define PUMPING_SAFETY_INTERVAL 10000 ///< Мінімальний безпечний інтервал між перекачуванням води

///@{
buttonMinim btnLeft(PIN_LEFT_BTN); ///< Об'єкт лівої кнопки
buttonMinim btnSelect(PIN_SELECT_BTN); ///< Об'єкт кнопки вибору
buttonMinim btnRight(PIN_RIGHT_BTN); ///< Об'єкт правої кнопки
///@}

int8_t selectionPosition = 1; ///< Поточна позиція стрілки. Пізніше використовується всередині setSelectionArrow()
bool selectionType = false; ///< Поточний тип виділення, впливає на піктограму зі стрілкою (false = порожня, true = повна)

/** @name Символи стрілки
 *  Використовується для малювання порожніх та заповнених стрілок для інтерфейсу користувача
 	(hollow = режим вибору, full = режим редагування)
 */
///@{
uint8_t hollowLeftArrow[8] = { 0b11111, 0b01001, 0b00101, 0b00011, 0b00001, 0b00000, 0b00000, 0b00000 };
uint8_t hollowRightArrow[8] = { 0b11111, 0b10010, 0b10100, 0b11000, 0b10000, 0b00000, 0b00000, 0b00000 };
uint8_t fullLeftArrow[8] = { 0b11111, 0b01111, 0b00111, 0b00011, 0b00001, 0b00000, 0b00000, 0b00000 };
uint8_t fullRightArrow[8] = { 0b11111, 0b11110, 0b11100, 0b11000, 0b10000, 0b00000, 0b00000, 0b00000 };
///@}


/** @name Налаштування поливу
 *  Години, хвилини, секунди та мінімальний відсоток вологості, які відображаються на екрані та синхронізуються з EEPROM.
    Використані зміщення у EEPROM: 0 = години, 1 = хвилини, 2 = секунди, 3 = вологість
 */
///@{
int8_t hoursInterval, minutesInterval, secondsInterval, humidityPercentage;
///@}


/** @name Таймери
 *  Таймери, що використовуються в loop(), для повторного відображення інтерфейсу користувача, запису та перевірки часу eepprom та збереження останнього часу поливу
 */
///@{
long renderTimer; ///< Таймер рендерингу інтерфейсу користувача
long eepromSecondsTimer; ///< Таймер перевірки та запису інтервалу поливу в EEPROM
long lastPumpingTime; ///< Таймер збереження часу останнього поливу (для перевірки інтервалу безпечності) 
///@}


LiquidCrystal_I2C lcd(0x27, 20, 4); ///< Екранний об'єкт LCD2004

void setup()
{
    Serial.begin(9600);
    randomSeed(analogRead(A0));

    // Встановити пін реле на вихід, та його значення на HIGH (HIGH означає, що реле закрито)
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, HIGH);

    lcd.init(); // Ініціалізувати підключення I2C до РК-екрану
    lcd.backlight(); // Увімкнути підсвічування дисплея

    // Створити символи-стрілки для використання пізніше
    lcd.createChar(1, hollowLeftArrow);
    lcd.createChar(2, hollowRightArrow);
    lcd.createChar(3, fullLeftArrow);
    lcd.createChar(4, fullRightArrow);

    // Інтервал поливу з EEPROM
    hoursInterval = EEPROM.read(0);
    minutesInterval = EEPROM.read(1);
    secondsInterval = EEPROM.read(2);
    humidityPercentage = EEPROM.read(3);

    refresh(selectionType, selectionPosition);
}

void loop()
{
    // Обробка взаємодії з меню
    if (btnLeft.clicked() || btnLeft.holding()) {
        // Ліва кнопка
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

    // Кнопка вибору
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

    // Права кнопка
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
        // Повторна відмальовка екрану щосекунди
        refresh(selectionType, selectionPosition);
        renderTimer = millis();
    }

    if (millis() - eepromSecondsTimer > 60000) {
        // Кожні 60 секунд зчитувати збережений часу з EEPROM в секундах. Потім порівняння його із заданим інтервалом поливу
        // Якщо цей час перевищує інтервал поливу, тоді починається перекачування води та таймер перевірки інтервалу накачування обнуляється
        // В іншому випадку додається 60 секунд
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

/// @brief Запис long int в EEPROM
/// Ця функція запише 4-байтне (32-бітне) число в EEPROM
/// за вказаною адресою для адреси + 3.
void EEPROMWritelong(int address, long value)
{
    // Розкладання з long на 4 байти за допомогою бітового зсуву.
    // one = Найважливіший -> four = Найменший значущий байт
    byte four = (value & 0xFF);
    byte three = ((value >> 8) & 0xFF);
    byte two = ((value >> 16) & 0xFF);
    byte one = ((value >> 24) & 0xFF);

    // Записати 4 байти в пам’ять EEPROM.
    EEPROM.write(address, four);
    EEPROM.write(address + 1, three);
    EEPROM.write(address + 2, two);
    EEPROM.write(address + 3, one);
}

/// @brief Прочитайте long int з EEPROM
/// Ця функція поверне 4-байтне (32-бітне) число з EEPROM
/// за вказаною адресою для адреси + 3.
long EEPROMReadlong(long address)
{
    // Зчитати 4 байти з пам'яті EEPROM.
    long four = EEPROM.read(address);
    long three = EEPROM.read(address + 1);
    long two = EEPROM.read(address + 2);
    long one = EEPROM.read(address + 3);

    // Повернути перекомпонований long int за допомогою бітового зміщення.
    return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

/// @brief Починає накачувати воду, якщо це безпечно
/// \returns Статус відкачки 
bool startPumping()
{
    // Починає перекачування, якщо пройшов інтервал безпеки від попереднього процесу поливу
    if (millis() - lastPumpingTime > PUMPING_SAFETY_INTERVAL) {
        digitalWrite(PIN_RELAY, LOW);
        delay(3000);
        digitalWrite(PIN_RELAY, HIGH);
        lastPumpingTime = millis();        
        return true;
    }
    else {
        return false;
    }
}

/// @brief Відображає число на екрані
/// @param x Координата X
/// @param y Y координата
/// @param dec 1-2-значне ціле число, щоб відобразити
void decOut(int x, int y, int dec)
{
    lcd.setCursor(x, y);
    lcd.print(dec / 10);
    lcd.print(dec % 10);
}

/// @brief Відображає поточні налаштування інтервалу поливу та мінімальної вологості ґрунту
void displaySettings()
{
    decOut(0, 1, hoursInterval);
    lcd.print(":");
    decOut(3, 1, minutesInterval);
    lcd.print(":");
    decOut(6, 1, secondsInterval);
    lcd.setCursor(11, 1);
    lcd.print(humidityPercentage);
    lcd.print("%");
}

/// @brief Відображає поточну вологість ґрунту на екрані
void displayCurrentHumidity()
{    
    lcd.setCursor(0, 3);
    lcd.print("Humidity - ");
    lcd.print(getHumidityPercentage());
    lcd.print("%");
}

/// @brief Отримує поточний відсоток вологості ґрунту
/// \returns Значення вологи від датчика, від 0 до 99 відсотків
int getHumidityPercentage()
{
    // Зчитати аналогове значення і перевести його в діапазон 0-99%
    int humidityAnalogValue = analogRead(PIN_HUMIDITY_SENSOR);

    return map(humidityAnalogValue, 0, 1023, 99, 0);
}

/// @brief Відмалювати стрілку налаштувань
/// @param type Визначає порожнистий (type = false) або повний (type = true) тип стрілки
/// @param pos Положення стрілки (1 = години, 2 = хвилини, 3 = секунди, 4 = мінімальна вологість)
void setSelectionArrow(bool type, int pos)
{
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

/// @brief Повторно перемалювати весь екран
void refresh(bool selType, int selPos)
{
    lcd.clear();
    setSelectionArrow(selType, selPos);
    displaySettings();
    displayCurrentHumidity();
}
