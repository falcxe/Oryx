#include <Wire.h>
#include <U8g2lib.h>
#include <Preferences.h>
#include <TinyGPS++.h>
#include <math.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_task_wdt.h>  // Добавляем watchdog таймер

#ifndef PI
#define PI 3.14159265358979323846
#endif

// Pin definitions
#define OLED_SDA 21
#define OLED_SCL 22
#define POWER_SWITCH 2
#define FUNC_SWITCH 27
#define HALL_SENSOR 4
#define GPS_POWER_PIN 26
#define BUZZER_PIN 25 // Пин для писчалки

// Constants for speedometer calculation
#define WHEEL_CIRCUMFERENCE 0.44  // длина окружности, которую проходит магнит (2 * π * 0.07)
#define WHEEL_DIAMETER 27.5       // диаметр колеса в дюймах
#define WHEEL_RATIO 3.14159 * WHEEL_DIAMETER * 0.0254 / WHEEL_CIRCUMFERENCE // соотношение длины окружности колеса к длине окружности магнита
#define HALL_DEBOUNCE_TIME 100    // увеличенное время защиты от дребезга (было 80)
#define MAX_SPEED_TIMEOUT 2000     // если сигнала нет больше 2 секунды, считаем скорость равной 0
#define SPEED_FILTER_COEFF 0.7     // коэффициент фильтрации для сглаживания показаний скорости (0-1)
#define WDT_TIMEOUT_MS 8000        // таймаут watchdog в миллисекундах

// Display modes
#define DISPLAY_MAIN 0
#define DISPLAY_STATS 1

// Координаты целевой точки (55°58'56.7"N 37°11'21.8"E)
#define TARGET_LAT 55.982417
#define TARGET_LON 37.189389

// Инициализация объектов для работы с памятью, дисплеем, GPS и UART
Preferences preferences; // Работа с энергонезависимой памятью ESP32
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE); // OLED-дисплей
TinyGPSPlus gps; // GPS-парсер
HardwareSerial GPSSerial(1); // Аппаратный UART1 для GPS-модуля

// Переменные для отслеживания состояния кнопок
#define DEBOUNCE_DELAY 50  // Задержка для подавления дребезга кнопок (мс)

// Переменные для хранения состояния устройства
bool isPowerOn = false; // Включено ли питание устройства
int currentDisplay = DISPLAY_MAIN; // Текущий режим отображения (основной/статистика)

// Статистика текущей сессии
unsigned long sessionStartTime = 0; // Время старта сессии (мс)
float sessionDistance = 0.0;        // Пройденное расстояние за сессию (км)
float averageSpeed = 0.0;           // Средняя скорость за сессию (км/ч)
int speedReadingsCount = 0;         // Количество замеров скорости
float distanceToTarget = 0.0;       // Расстояние до целевой точки (км)

// Переменные для расчёта скорости по датчику Холла (точно как в алгоритме AlexGyver)
volatile unsigned long lastHallPulseTime = 0; // Время последнего импульса (мс)
volatile float currentSpeed = 0.0;            // Текущая скорость (км/ч)
volatile float filteredSpeed = 0.0;           // Отфильтрованная скорость (км/ч)
volatile float tripDistance = 0.0;            // Пробег за сессию (км)
volatile bool hallTriggered = false;          // Флаг срабатывания датчика Холла
volatile int hallPulseCount = 0;              // Счетчик импульсов для фильтрации

// Statistics variables
unsigned long startTime = 0;
float odometer = 0.0;
float maxRecordedSpeed = 0.0;

// UUID'ы для BLE сервиса и характеристик
#define BLE_SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define BLE_CHAR_SPEED_UUID     "12345678-1234-1234-1234-1234567890ac"
#define BLE_CHAR_DIST_UUID      "12345678-1234-1234-1234-1234567890ad"
#define BLE_CHAR_COORD_UUID     "12345678-1234-1234-1234-1234567890ae"

BLECharacteristic *pSpeedCharacteristic;
BLECharacteristic *pDistCharacteristic;
BLECharacteristic *pCoordCharacteristic;

// Обработчик прерывания датчика Холла (реализует точно такой же алгоритм как в коде AlexGyver)
void IRAM_ATTR hallSensorISR() {
  // Защита от случайных измерений с увеличенным временем дебаунса
  unsigned long currentTime = millis();
  if (currentTime - lastHallPulseTime > HALL_DEBOUNCE_TIME) {
    // Расчет скорости, км/ч
    float newSpeed = WHEEL_CIRCUMFERENCE * WHEEL_RATIO / ((float)(currentTime - lastHallPulseTime) / 1000.0) * 3.6;

    // Проверка на реалистичность скорости (не более 100 км/ч для велосипеда)
    if (newSpeed < 100.0) {
      // Фильтрация скорости для устранения резких скачков
      if (currentSpeed == 0) {
        // Если это первое измерение после остановки
        currentSpeed = newSpeed;
        filteredSpeed = newSpeed;
      } else {
        // Применяем фильтр низких частот для сглаживания
        currentSpeed = newSpeed;
        filteredSpeed = SPEED_FILTER_COEFF * filteredSpeed + (1 - SPEED_FILTER_COEFF) * newSpeed;
      }

      // Запоминаем время последнего оборота
      lastHallPulseTime = currentTime;

      // Прибавляем длину окружности к дистанции при каждом обороте
      // Также учитываем соотношение между длиной окружности колеса и длиной окружности, по которой движется магнит
      tripDistance += WHEEL_CIRCUMFERENCE * WHEEL_RATIO / 1000.0;
      sessionDistance += WHEEL_CIRCUMFERENCE * WHEEL_RATIO / 1000.0;

      hallTriggered = true;
      hallPulseCount++;
    }
  }
}

#ifdef ESP32
  #define HAS_TONE 1
#else
  #define HAS_TONE 0
#endif

void beep(int duration = 100, int times = 1, int pause = 80) {
#if HAS_TONE
  for (int i = 0; i < times; i++) {
    tone(BUZZER_PIN, 2000, duration); // 2 кГц — стандартный писк
    delay(duration);
    noTone(BUZZER_PIN);
    if (i < times - 1) delay(pause);
  }
#else
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(duration);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(pause);
  }
#endif
}

void setup() {
  // Initialize serial port
  Serial.begin(9600);

  // Настройка watchdog таймера
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_MS,
    .idle_core_mask = 0, // Не отслеживать idle задачи
    .trigger_panic = true // Вызывать панику при срабатывании WDT
  };
  esp_task_wdt_init(&wdt_config); // Инициализация с новым API
  esp_task_wdt_add(NULL); // Добавляем текущую задачу в watchdog

  // Отключение RGB-светодиода
  pinMode(14, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);

  digitalWrite(14, LOW); // или HIGH, если светодиод включается по LOW
  digitalWrite(12, LOW);
  digitalWrite(13, LOW);

  // Initialize GPS power pin
  pinMode(GPS_POWER_PIN, OUTPUT);
  digitalWrite(GPS_POWER_PIN, HIGH);  // Turn on GPS module
  delay(1000);  // Wait for GPS to power up

  // Initialize GPS serial
  GPSSerial.begin(9600, SERIAL_8N1, 16, 17);  // RX=16, TX=17

  // Send GPS module configuration commands
  GPSSerial.println("$PMTK220,1000*1F");  // Update rate: 1Hz
  GPSSerial.println("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28");  // Enable GGA and RMC

  // Set initial states
  isPowerOn = true;
  currentDisplay = DISPLAY_MAIN;  // Explicitly set initial display mode

  // Initialize OLED display
  u8g2.begin();
  u8g2.clear();

  // Initialize pins
  pinMode(HALL_SENSOR, INPUT_PULLUP);
  pinMode(FUNC_SWITCH, INPUT_PULLUP);
  pinMode(POWER_SWITCH, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Настройка прерывания с дополнительной фильтрацией
  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR), hallSensorISR, FALLING);

  // Display welcome message
  displayWelcomeScreen();
  delay(2000);  // Show welcome screen for 2 seconds

  beep(80, 2, 100); // Двойной сигнал при включении

  Serial.println("Initialization completed");

  // BLE инициализация
  BLEDevice::init("OryxSpeedometer");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

  pSpeedCharacteristic = pService->createCharacteristic(
    BLE_CHAR_SPEED_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pDistCharacteristic = pService->createCharacteristic(
    BLE_CHAR_DIST_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCoordCharacteristic = pService->createCharacteristic(
    BLE_CHAR_COORD_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  pSpeedCharacteristic->addDescriptor(new BLE2902());
  pDistCharacteristic->addDescriptor(new BLE2902());
  pCoordCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEDevice::startAdvertising();
  Serial.println("BLE server started!");
}

void loop() {
  // Сброс watchdog таймера
  esp_task_wdt_reset();

  // Оптимизация памяти
  optimizeMemory();

  // Check switches
  checkSwitches();

  // If power is off, don't perform other operations
  if (!isPowerOn) {
    displayPowerOffScreen();
    delay(50);
    return;
  }

  // Read GPS data
  readGPS();

  // Проверка на обнуление скорости (аналог кода AlexGyver)
  if ((millis() - lastHallPulseTime) > MAX_SPEED_TIMEOUT) {
    currentSpeed = 0.0;
    filteredSpeed = 0.0;
  }

  // Update speed calculations
  updateSpeed();

  // Update display - main screen
  displayMainScreen();

  // Save data every minute
  static unsigned long lastSaveTime = 0;
  if (millis() - lastSaveTime > 60000) {
    lastSaveTime = millis();
    // Здесь можно сохранять данные в память, как в алгоритме AlexGyver
  }

  // --- BLE обновление характеристик ---
  static unsigned long lastBLEUpdate = 0;
  if (millis() - lastBLEUpdate > 500) { // Обновляем BLE не чаще чем раз в 500 мс
    lastBLEUpdate = millis();

    char speedStr[8], distStr[8], coordStr[32];
    dtostrf(filteredSpeed, 4, 1, speedStr); // Используем отфильтрованную скорость
    dtostrf(tripDistance, 4, 2, distStr);
    if (gps.location.isValid()) {
      sprintf(coordStr, "%0.6f,%0.6f", gps.location.lat(), gps.location.lng());
    } else {
      strcpy(coordStr, "--,--");
    }
    pSpeedCharacteristic->setValue(speedStr);
    pSpeedCharacteristic->notify();
    pDistCharacteristic->setValue(distStr);
    pDistCharacteristic->notify();
    pCoordCharacteristic->setValue(coordStr);
    pCoordCharacteristic->notify();
  }

  delay(50);
}

void checkSwitches() {
  checkPowerSwitch();
  checkFunctionSwitch();
}

void checkPowerSwitch() {
  int powerReading = digitalRead(POWER_SWITCH);
  bool newPowerState = (powerReading == LOW);

  if (newPowerState != isPowerOn) {
    isPowerOn = newPowerState;
    if (isPowerOn) {
      // Turn on GPS when device powers on
      digitalWrite(GPS_POWER_PIN, HIGH);
      delay(1000);  // Wait for GPS to power up

      // Reset session statistics
      sessionStartTime = millis();
      sessionDistance = 0.0;
      speedReadingsCount = 0;
      averageSpeed = 0.0;
      maxRecordedSpeed = 0.0;
      tripDistance = 0.0;
    } else {
      // Turn off GPS when device powers off
      digitalWrite(GPS_POWER_PIN, LOW);
    }
  }
}

void checkFunctionSwitch() {
  static bool lastTumblerState = HIGH;

  int reading = digitalRead(FUNC_SWITCH);

  // Detect state change of tumbler
  if (reading != lastTumblerState) {
    delay(10); // Simple debounce
    reading = digitalRead(FUNC_SWITCH); // Read again after debounce

    if (reading != lastTumblerState) {
      // Tumbler position determines display mode
      if (reading == LOW) {
        currentDisplay = DISPLAY_STATS;  // Tumbler down = stats screen
      } else {
        currentDisplay = DISPLAY_MAIN;   // Tumbler up = main screen
      }

      Serial.print("Tumbler switched! Display changed to: ");
      Serial.println(currentDisplay);
      lastTumblerState = reading;
    }
  }
}

void updateSpeed() {
  // Update max speed record
  if (filteredSpeed > maxRecordedSpeed) {
    maxRecordedSpeed = filteredSpeed;
    // Один длинный сигнал при новой максимальной скорости
  }

  // Update average speed
  if (filteredSpeed > 0) {
    averageSpeed = ((averageSpeed * speedReadingsCount) + filteredSpeed) / (speedReadingsCount + 1);
    speedReadingsCount++;
  }
}

void displayMainScreen() {
  Serial.print("Current display mode: ");
  Serial.println(currentDisplay);

  if (currentDisplay == DISPLAY_MAIN) {
    displayMainSpeedScreen();
  } else {
    displayStatsScreen();
  }
}

void displayMainSpeedScreen() {
  Serial.println("Displaying MAIN screen");

  u8g2.clearBuffer();

  // Larger font for speed
  u8g2.setFont(u8g2_font_profont29_tn);
  char speedBuf[10];
  dtostrf(filteredSpeed, 2, 1, speedBuf); // Используем отфильтрованную скорость

  // Center speed
  int textWidth = u8g2.getStrWidth(speedBuf);
  u8g2.drawStr((128-textWidth)/2, 30, speedBuf);

  // Divider line
  u8g2.drawHLine(0, 35, 128);

  // Total distance (Trip)
  u8g2.setFont(u8g2_font_profont15_tr);
  char tripBuf[10];
  dtostrf(tripDistance, 2, 2, tripBuf);
  u8g2.drawStr(2, 47, "Dist:");
  u8g2.drawStr(45, 47, tripBuf);
  u8g2.drawStr(75, 47, "km");

  // Current time UTC+3 from GPS and satellites count
  u8g2.setFont(u8g2_font_profont15_tr);
  char timeBuf[10];
  char satBuf[5];
  if (gps.time.isValid()) {
    int hours = (gps.time.hour() + 3) % 24; // UTC+3
    sprintf(timeBuf, "%02d:%02d", hours, gps.time.minute());
  } else {
    strcpy(timeBuf, "--:--");
  }
  sprintf(satBuf, "%d", gps.satellites.value());

  u8g2.drawStr(2, 60, "Time:");
  u8g2.drawStr(45, 60, timeBuf);

  // Hall sensor indicator
  if (hallTriggered) {
    u8g2.drawDisc(120, 45, 3);
    hallTriggered = false;
  }

  u8g2.sendBuffer();
}

void displayStatsScreen() {
  Serial.println("Displaying STATS screen");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont12_tr);

  // Maximum speed
  char maxBuf[10];
  dtostrf(maxRecordedSpeed, 2, 1, maxBuf);
  u8g2.drawStr(2, 15, "Max speed:");
  u8g2.drawStr(70, 15, maxBuf);
  u8g2.drawStr(105, 15, "km/h");

  // Average speed
  char avgBuf[10];
  dtostrf(averageSpeed, 2, 1, avgBuf);
  u8g2.drawStr(2, 30, "Avg speed:");
  u8g2.drawStr(70, 30, avgBuf);
  u8g2.drawStr(105, 30, "km/h");

  // Время текущей сессии
  unsigned long sessionMillis = millis() - sessionStartTime;
  unsigned long sessionSec = sessionMillis / 1000;
  unsigned int sessionMin = sessionSec / 60;
  unsigned int sessionRemSec = sessionSec % 60;
  char sessionTimeBuf[10];
  sprintf(sessionTimeBuf, "%02u:%02u", sessionMin, sessionRemSec);
  u8g2.drawStr(2, 45, "Session:");
  u8g2.drawStr(70, 45, sessionTimeBuf);

  // Distance to target coordinates
  if (gps.location.isValid()) {
    // Calculate distance to target
    float distToTarget = calculateDistance(
      gps.location.lat(), gps.location.lng(),
      TARGET_LAT, TARGET_LON
    );
    char distBuf[15];
    if (distToTarget >= 1.0) {
      dtostrf(distToTarget, 3, 1, distBuf);
      u8g2.drawStr(2, 60, "Distance:");
      u8g2.drawStr(60, 60, distBuf);
      u8g2.drawStr(100, 60, "km");
    } else {
      // Show in meters if less than 1 km
      int distanceMeters = (int)(distToTarget * 1000);
      sprintf(distBuf, "%d", distanceMeters);
      u8g2.drawStr(2, 60, "Distance:");
      u8g2.drawStr(60, 60, distBuf);
      u8g2.drawStr(95, 60, "m");
    }

  } else {
    u8g2.drawStr(2, 60, "GPS: Searching...");
  }

  u8g2.sendBuffer();
}

float calculateDistance(float lat1, float lon1, float lat2, float lon2) {
  // Convert degrees to radians
  float lat1Rad = lat1 * PI / 180.0;
  float lon1Rad = lon1 * PI / 180.0;
  float lat2Rad = lat2 * PI / 180.0;
  float lon2Rad = lon2 * PI / 180.0;

  // Haversine formula
  float dLat = lat2Rad - lat1Rad;
  float dLon = lon2Rad - lon1Rad;
  float a = sin(dLat/2) * sin(dLat/2) +
            cos(lat1Rad) * cos(lat2Rad) *
            sin(dLon/2) * sin(dLon/2);
  float c = 2 * atan2(sqrt(a), sqrt(1-a));
  float distance = 6371.0 * c; // Earth's radius in km

  return distance;
}

void readGPS() {
  static unsigned long lastGPSDebug = 0;
  static unsigned long lastGPSRead = 0;

  // Ограничиваем частоту чтения GPS данных
  if (millis() - lastGPSRead < 100) { // Читаем не чаще 10 раз в секунду
    return;
  }
  lastGPSRead = millis();

  // Ограничиваем количество считываемых байт за один раз
  int bytesRead = 0;
  const int maxBytesToRead = 64; // Максимальное количество байт для чтения за один вызов

  while (GPSSerial.available() > 0 && bytesRead < maxBytesToRead) {
    if (gps.encode(GPSSerial.read())) {
      bytesRead++;
      // Update distance to target if we have a valid position
      if (gps.location.isValid()) {
        distanceToTarget = calculateDistance(
          gps.location.lat(), gps.location.lng(),
          TARGET_LAT, TARGET_LON
        );
      }
    }
  }

  // Debug GPS info every 5 seconds
  if (millis() - lastGPSDebug > 5000) {
    Serial.print("GPS Stats - Satellites: ");
    Serial.print(gps.satellites.value());
    Serial.print(" Valid: ");
    Serial.print(gps.location.isValid());
    Serial.print(" Quality: ");
    Serial.println(gps.hdop.value());

    // Вывод информации о свободной памяти
    Serial.print("Free Heap: ");
    Serial.println(ESP.getFreeHeap());

    lastGPSDebug = millis();
  }

  // Check for GPS timeout
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println("No GPS detected");
  }
}

// Функция для освобождения неиспользуемой памяти
void optimizeMemory() {
  static unsigned long lastMemoryOptimize = 0;

  // Выполняем оптимизацию памяти каждые 30 секунд
  if (millis() - lastMemoryOptimize > 30000) {
    ESP.getFreeHeap(); // Вызов этой функции может помочь в освобождении фрагментированной памяти
    lastMemoryOptimize = millis();
  }
}

void displayPowerOffScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(25, 38, "protok0l");
  u8g2.sendBuffer();
}

void displayWelcomeScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(25, 30, "protok0l");
  u8g2.setFont(u8g2_font_profont12_tr);
  u8g2.drawStr(25, 50, "SPEEDOMETER");
  u8g2.setFont(u8g2_font_profont10_tr);
  u8g2.drawStr(50, 62, "v2.0");
  u8g2.sendBuffer();
}
