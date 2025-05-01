#include <Wire.h>
#include <U8g2lib.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <EEPROM.h>

// Pin definitions
#define OLED_SDA 21
#define OLED_SCL 22
#define BUZZER_PIN 25
#define RGB_R 14
#define RGB_G 12
#define RGB_B 13
#define POWER_SWITCH 2
#define FUNC_SWITCH 27
#define HALL_SENSOR 4
#define GPS_POWER_PIN 26

// Constants for speedometer calculation
#define WHEEL_CIRCUMFERENCE 2.10  // Wheel circumference in meters
#define HALL_DEBOUNCE_TIME 20     // Debounce time in ms
#define MAX_SPEED_TIMEOUT 3000    // Time in ms to reset speed
#define EEPROM_SIZE 64            // Size of EEPROM storage

// Object initialization
// For 1.3" OLED with SH1106 controller
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// If display is inverted try U8G2_R2 instead of U8G2_R0

TinyGPSPlus gps;
HardwareSerial GPSSerial(1); // UART1

// Variables for switch state monitoring
int lastPowerState = HIGH;
int lastFuncState = HIGH;
unsigned long lastPowerDebounceTime = 0;
unsigned long lastFuncDebounceTime = 0;
unsigned long debounceDelay = 50;

// Variable to track current screen and settings
int currentScreen = 0;     // 0 = Main Screen, 1 = GPS Screen
bool isPowerOn = false;    // Состояние устройства (индикация через RGB)
bool isRGBEffect = false;
unsigned long rgbEffectStartTime = 0;

// Variables for Hall sensor speed calculation
volatile unsigned long lastHallPulseTime = 0;
volatile unsigned long currentHallPulseTime = 0;
volatile unsigned long hallPulseCount = 0;
volatile unsigned long revolutionTime = 0;
volatile float currentSpeed = 0.0;
volatile float maxSpeed = 0.0;
volatile float tripDistance = 0.0;
volatile bool hallTriggered = false;

// Statistics variables
unsigned long startTime = 0;
float odometer = 0.0;
float maxRecordedSpeed = 0.0;

// Добавляем переменные для отслеживания состояния GPS
bool lastGPSvalid = false;
int lastSatelliteCount = 0;
bool gpsStatusSoundEnabled = true;
unsigned long lastGPSstatusTime = 0;
#define GPS_STATUS_SOUND_INTERVAL 2000 // Минимальный интервал между звуковыми сигналами GPS (мс)

// Interrupt service routine for Hall sensor
void IRAM_ATTR hallSensorISR() {
  unsigned long currentTime = millis();
  
  // Debounce
  if (currentTime - lastHallPulseTime > HALL_DEBOUNCE_TIME) {
    currentHallPulseTime = currentTime;
    revolutionTime = currentHallPulseTime - lastHallPulseTime;
    lastHallPulseTime = currentHallPulseTime;
    hallPulseCount++;
    hallTriggered = true;
    
    // Calculate current speed
    if (revolutionTime > 0) {
      currentSpeed = (WHEEL_CIRCUMFERENCE * 3600.0) / (revolutionTime / 1000.0);
      
      // Update maximum speed
      if (currentSpeed > maxSpeed) {
        maxSpeed = currentSpeed;
      }
      
      // Update distance
      tripDistance += WHEEL_CIRCUMFERENCE / 1000.0; // Convert to km
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Smart Speedometer Starting");
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Load saved data
  loadDataFromEEPROM();
  
  // Initialize pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(POWER_SWITCH, INPUT_PULLUP);
  pinMode(FUNC_SWITCH, INPUT_PULLUP);
  pinMode(HALL_SENSOR, INPUT_PULLUP);
  pinMode(GPS_POWER_PIN, OUTPUT);
  
  // Заменяем настройку PWM на прямую настройку пинов
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
  
  // Выключаем все светодиоды
  digitalWrite(RGB_R, LOW);
  digitalWrite(RGB_G, LOW);
  digitalWrite(RGB_B, LOW);
  
  // Подаем питание на GPS
  digitalWrite(GPS_POWER_PIN, HIGH);
  
  // Initialize I2C
  Wire.begin(OLED_SDA, OLED_SCL);
  
  // Initialize OLED
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tf);
  
  // Initialize GPS
  GPSSerial.begin(9600, SERIAL_8N1, 16, 17);
  
  // Attach interrupt for Hall sensor
  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR), hallSensorISR, FALLING);
  
  // Record start time
  startTime = millis();
  
  // Welcome message
  displayWelcomeScreen();
  delay(2000);
  
  // Начальная индикация - устройство включено (синий)
  isPowerOn = true;
  setRGBColor(0, 0, 1);
}

void loop() {
  // Проверка статуса переключателей
  checkSwitches();
  
  // Проверка длительного нажатия
  checkLongPress();
  
  if (isPowerOn) {
    // Чтение GPS данных
    readGPSData();
    
    // Обновление расчетов скорости
    updateSpeed();
    
    // Обработка RGB эффекта
    updateRGBEffect();
    
    // Отображение соответствующего экрана
    if (currentScreen == 0) {
      displayMainScreen();
    } else {
      displayGPSScreen();
    }
    
    // Сохранение данных раз в минуту
    static unsigned long lastSaveTime = 0;
    if (millis() - lastSaveTime > 60000) {
      saveDataToEEPROM();
      lastSaveTime = millis();
    }
  } else {
    // Выключенное состояние
    displayPowerOffScreen();
  }
  
  delay(100);
}

// Упрощенная функция проверки переключателей
void checkSwitches() {
  int powerReading = digitalRead(POWER_SWITCH);
  int funcReading = digitalRead(FUNC_SWITCH);
  
  // ===== POWER SWITCH =====
  bool newPowerState = (powerReading == LOW);
  
  // Если изменилось состояние питания
  if (newPowerState != isPowerOn) {
    // Если выключаемся
    if (isPowerOn && !newPowerState) {
      // Отображаем сообщение выключения
      tone(BUZZER_PIN, 800, 100);
      setRGBColor(1, 0, 0); // Красный - выключаем
      saveDataToEEPROM();
      displayPowerOffScreen();
      delay(2000); // Даем время увидеть сообщение
    }
    
    isPowerOn = newPowerState;
    
    if (isPowerOn) {
      // Power ON
      tone(BUZZER_PIN, 1500, 100);
      setRGBColor(0, 0, 1); // Синий - включено
      displayWelcomeScreen();
      delay(500);
    }
  }
  
  // ===== FUNCTION SWITCH для переключения экранов =====
  bool newScreenState = (funcReading == LOW);
  
  if (isPowerOn && newScreenState != (currentScreen == 1)) {
    currentScreen = newScreenState ? 1 : 0;
    tone(BUZZER_PIN, 1000, 30);
  }
  
  lastPowerState = powerReading;
  lastFuncState = funcReading;
}

void readGPSData() {
  bool dataUpdated = false;
  
  while (GPSSerial.available() > 0) {
    if (gps.encode(GPSSerial.read())) {
      dataUpdated = true;
    }
  }
  
  // Проверяем статус GPS если получили новые данные
  if (dataUpdated) {
    checkGPSStatus();
  }
}

void updateSpeed() {
  // Reset speed if no updates for MAX_SPEED_TIMEOUT
  if (millis() - lastHallPulseTime > MAX_SPEED_TIMEOUT && currentSpeed > 0) {
    currentSpeed = 0;
    hallTriggered = false;
  }
  
  // Update RGB color based on speed (only if no effect active)
  if (!isRGBEffect) {
    if (currentSpeed < 5) {
      setRGBColor(0, 0, 1); // Blue at idle/very slow
    } else if (currentSpeed < 20) {
      setRGBColor(0, 1, 0); // Green for slow speed
    } else if (currentSpeed < 40) {
      setRGBColor(1, 1, 0); // Yellow-orange for medium
    } else {
      setRGBColor(1, 0, 0); // Red for high speed
    }
  }
  
  // Update max speed record
  if (currentSpeed > maxRecordedSpeed) {
    maxRecordedSpeed = currentSpeed;
    tone(BUZZER_PIN, 2000, 50);
  }
}

void updateRGBEffect() {
  if (isRGBEffect) {
    unsigned long elapsedTime = millis() - rgbEffectStartTime;
    
    // Run effect for 1 second max
    if (elapsedTime > 1000) {
      isRGBEffect = false;
      updateSpeed(); // Restore color based on speed
      return;
    }
    
    int phase = (elapsedTime / 100) % 6;
    
    switch (phase) {
      case 0: setRGBColor(1, 0, 0); break;     // Red
      case 1: setRGBColor(1, 1, 0); break;     // Yellow
      case 2: setRGBColor(0, 1, 0); break;     // Green
      case 3: setRGBColor(0, 1, 1); break;     // Cyan
      case 4: setRGBColor(0, 0, 1); break;     // Blue
      case 5: setRGBColor(1, 0, 1); break;     // Magenta
    }
  }
}

void displayWelcomeScreen() {
  u8g2.clearBuffer();
  
  // Логотип
  u8g2.setFont(u8g2_font_inb19_mf);
  u8g2.drawStr(15, 30, "BIKE");
  
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(22, 50, "SPEEDOMETER");
  
  // Версия
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(50, 62, "v1.1");
  
  u8g2.sendBuffer();
}

void displayPowerOffScreen() {
  u8g2.clearBuffer();
  
  // Большая надпись POWER OFF
  u8g2.setFont(u8g2_font_inb16_mf);
  u8g2.drawStr(10, 30, "POWER");
  u8g2.drawStr(30, 55, "OFF");
  
  // Дополнительные эффекты
  u8g2.drawFrame(0, 0, 128, 64);
  
  u8g2.sendBuffer();
}

void displayMainScreen() {
  u8g2.clearBuffer();
  
  // Большой дисплей скорости
  u8g2.setFont(u8g2_font_logisoso32_tn);
  char speedBuf[10];
  dtostrf(currentSpeed, 2, 1, speedBuf);
  
  // Центрирование скорости
  int textWidth = u8g2.getStrWidth(speedBuf);
  u8g2.drawStr((128-textWidth)/2, 35, speedBuf);
  
  // Единицы измерения в правом верхнем углу
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(90, 16, "km/h");
  
  // Разделительная линия
  u8g2.drawHLine(0, 40, 128);
  
  // Информация внизу
  u8g2.setFont(u8g2_font_6x10_tf);
  
  // Общее расстояние (Trip)
  char tripBuf[10];
  dtostrf(tripDistance, 2, 2, tripBuf);
  u8g2.drawStr(2, 52, "Distance:");
  u8g2.drawStr(60, 52, tripBuf);
  u8g2.drawStr(100, 52, "km");
  
  // Время
  char timeBuf[20];
  if (gps.time.isValid()) {
    sprintf(timeBuf, "%02d:%02d:%02d", 
      gps.time.hour(), 
      gps.time.minute(), 
      gps.time.second());
  } else {
    // Если GPS время недоступно, показываем время работы
    unsigned long runTime = millis() / 1000; // Время в секундах
    int hours = runTime / 3600;
    int minutes = (runTime % 3600) / 60;
    int seconds = runTime % 60;
    sprintf(timeBuf, "%02d:%02d:%02d", hours, minutes, seconds);
  }
  u8g2.drawStr(2, 62, "Time:");
  u8g2.drawStr(60, 62, timeBuf);
  
  // Индикатор датчика холла
  if (hallTriggered) {
    u8g2.drawDisc(120, 45, 3); // Маленький индикатор справа
    hallTriggered = false;
  }
  
  u8g2.sendBuffer();
}

void displayGPSScreen() {
  u8g2.clearBuffer();
  
  // Заголовок
  u8g2.setFont(u8g2_font_inb16_mf);
  u8g2.drawStr(30, 20, "GPS");
  
  // Индикатор звука GPS в углу
  if (gpsStatusSoundEnabled) {
    u8g2.drawDisc(120, 10, 3);
  } else {
    u8g2.drawCircle(120, 10, 3);
  }
  
  // Разделительная линия
  u8g2.drawHLine(0, 24, 128);
  
  // Основные данные
  u8g2.setFont(u8g2_font_7x13B_tf);
  
  // GPS Статус
  u8g2.drawStr(5, 38, "Status:");
  u8g2.drawStr(65, 38, gps.location.isValid() ? "FIXED" : "NO FIX");
  
  // Количество спутников
  u8g2.drawStr(5, 52, "Satellites:");
  char satBuf[5];
  sprintf(satBuf, "%d", gps.satellites.value());
  u8g2.drawStr(90, 52, satBuf);
  
  // Высота
  u8g2.drawStr(5, 63, "Altitude:");
  char altBuf[10];
  dtostrf(gps.altitude.meters(), 4, 0, altBuf);
  u8g2.drawStr(70, 63, altBuf);
  u8g2.drawStr(100, 63, "m");
  
  u8g2.sendBuffer();
}

void setRGBColor(byte r, byte g, byte b) {
  digitalWrite(RGB_R, r > 0 ? 1 : LOW);
  digitalWrite(RGB_G, g > 0 ? 1 : LOW);
  digitalWrite(RGB_B, b > 0 ? 1 : LOW);
}

void saveDataToEEPROM() {
  // Save odometer and max speed
  EEPROM.writeFloat(0, tripDistance);  // Save current trip
  EEPROM.writeFloat(4, maxRecordedSpeed);
  EEPROM.commit();
  Serial.println("Data saved to EEPROM");
}

void loadDataFromEEPROM() {
  // Load trip and max speed
  tripDistance = EEPROM.readFloat(0);
  maxRecordedSpeed = EEPROM.readFloat(4);
  
  // Set initial max speed
  maxSpeed = 0;
  
  Serial.println("Data loaded from EEPROM");
  Serial.print("Trip: ");
  Serial.println(tripDistance);
}

// Добавляем функцию для проверки состояния GPS
void checkGPSStatus() {
  // Проверяем состояние только если звуковые сигналы GPS включены и прошло достаточно времени
  if (!gpsStatusSoundEnabled || (millis() - lastGPSstatusTime < GPS_STATUS_SOUND_INTERVAL)) {
    return;
  }
  
  bool currentGPSvalid = gps.location.isValid();
  int currentSatelliteCount = gps.satellites.value();
  
  // Обнаружение изменения статуса фиксации GPS
  if (currentGPSvalid != lastGPSvalid) {
    if (currentGPSvalid) {
      // GPS фиксация получена - двойной высокий сигнал
      tone(BUZZER_PIN, 2200, 50);
      delay(80);
      tone(BUZZER_PIN, 2500, 50);
      
      // Кратковременная вспышка зеленым светодиодом
      bool prevState = isRGBEffect;
      isRGBEffect = true;
      setRGBColor(0, 1, 0); // Зеленый - успешная фиксация
      delay(200);
      isRGBEffect = prevState;
      updateSpeed(); // Восстановление цвета
    } else {
      // GPS фиксация потеряна - низкий сигнал
      tone(BUZZER_PIN, 800, 150);
      
      // Кратковременная вспышка красным светодиодом
      bool prevState = isRGBEffect;
      isRGBEffect = true;
      setRGBColor(1, 0, 0); // Красный - потеря сигнала
      delay(200);
      isRGBEffect = prevState;
      updateSpeed(); // Восстановление цвета
    }
    
    lastGPSstatusTime = millis();
  } 
  // Обнаружение значительного изменения количества спутников
  else if (currentGPSvalid && abs(currentSatelliteCount - lastSatelliteCount) >= 2) {
    // Звуковая индикация только при значительном изменении
    if (currentSatelliteCount > lastSatelliteCount) {
      // Увеличение числа спутников - растущий тон
      tone(BUZZER_PIN, 1500 + currentSatelliteCount * 50, 30);
    } else {
      // Уменьшение числа спутников - понижающийся тон
      tone(BUZZER_PIN, 1500 + currentSatelliteCount * 50, 30);
    }
    
    lastGPSstatusTime = millis();
  }
  
  // Обновление предыдущих значений
  lastGPSvalid = currentGPSvalid;
  lastSatelliteCount = currentSatelliteCount;
}

// Добавим функцию для длительного нажатия, которую будем вызывать из loop()
void checkLongPress() {
  static unsigned long longPressStart = 0;
  static bool longPressActive = false;
  
  // Проверяем только когда включено питание
  if (!isPowerOn) return;
  
  int funcReading = digitalRead(FUNC_SWITCH);
  
  // Отслеживаем начало нажатия
  if (funcReading == LOW && lastFuncState == HIGH) {
    longPressStart = millis();
    longPressActive = true;
  }
  
  // Проверяем длительное нажатие
  if (longPressActive && funcReading == LOW) {
    if (millis() - longPressStart > 2000) {
      if (currentScreen == 0) {
        // Сброс счетчика поездки на главном экране
        tripDistance = 0;
        maxSpeed = 0;
        
        // Звуковое подтверждение
        tone(BUZZER_PIN, 1500, 50);
        delay(100);
        tone(BUZZER_PIN, 2000, 50);
      } else {
        // Переключение звуков GPS на экране GPS
        gpsStatusSoundEnabled = !gpsStatusSoundEnabled;
        
        if (gpsStatusSoundEnabled) {
          tone(BUZZER_PIN, 1000, 50);
          delay(50);
          tone(BUZZER_PIN, 2000, 50);
        } else {
          tone(BUZZER_PIN, 2000, 50);
          delay(50);
          tone(BUZZER_PIN, 1000, 50);
        }
      }
      
      // Предотвращение повторных срабатываний
      longPressActive = false;
      
      // RGB эффект
      isRGBEffect = true;
      rgbEffectStartTime = millis();
    }
  }
  
  // Сброс при отпускании
  if (funcReading == HIGH && lastFuncState == LOW) {
    longPressActive = false;
  }
}