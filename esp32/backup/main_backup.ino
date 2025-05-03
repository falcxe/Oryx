#include <Wire.h>
#include <U8g2lib.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

// WiFi и сервер настройки
#define WIFI_SSID "YOUR_WIFI_SSID"      // Имя вашей WiFi сети
#define WIFI_PASSWORD "YOUR_PASSWORD"   // Пароль вашей WiFi сети
#define SYNC_INTERVAL 10000             // Интервал отправки данных (10 секунд)

// Настройки режима точки доступа
#define AP_SSID "Speedometer-Setup"     // Имя точки доступа
#define AP_PASSWORD "12345678"          // Пароль точки доступа (минимум 8 символов)
#define AP_TIMEOUT 300000               // Таймаут режима AP (5 минут)
#define DNS_PORT 53                     // Порт DNS сервера для портала

// Режимы работы устройства
#define MODE_NORMAL 0                   // Обычный режим работы
#define MODE_AP_SETUP 1                 // Режим настройки через точку доступа
#define MODE_PAIRING 2                  // Режим сопряжения с аккаунтом

// Эндпоинты API
#define API_AUTH "auth/"                 // Авторизация устройства
#define API_PAIR "pair/"                 // Запрос на сопряжение
#define API_RECEIVE "receive/"           // Отправка данных
#define API_SETTINGS "settings/"         // Получение настроек
#define API_PAIR_CONFIRM "pair/confirm"   // Подтверждение сопряжения

// Стационарный адрес сервера (без возможности редактирования пользователем)
#define SERVER_URL "http://192.168.1.100:8000/api/"

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
#define WIFI_SETUP_BTN 32  // Новая кнопка для настройки WiFi

// Настройки яркости по умолчанию
#define RGB_DEFAULT_BRIGHTNESS 64  // Значение от 0 до 255

// Constants for speedometer calculation
#define WHEEL_CIRCUMFERENCE 2.10  // Wheel circumference in meters
#define HALL_DEBOUNCE_TIME 20     // Debounce time in ms
#define MAX_SPEED_TIMEOUT 3000    // Time in ms to reset speed
#define EEPROM_SIZE 64            // Size of EEPROM storage

// Добавим переменную для настройки часового пояса
#define TIME_ZONE_OFFSET 3 // UTC+3 для России

// Добавим переменные для настройки яркости светодиода
#define LED_BRIGHTNESS 1  // Значение от 1 до 100, где 1 - минимальная яркость

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

// WiFi и синхронизация
bool wifiConnected = false;
bool serverConnected = false;
unsigned long lastSyncTime = 0;
String deviceID = "ESP32-001";

// Переменные для режима точки доступа и настройки
int deviceMode = MODE_NORMAL;          // Текущий режим работы устройства
bool firstBoot = false;                // Флаг первого запуска
WebServer webServer(80);               // Веб-сервер на порту 80
DNSServer dnsServer;                   // DNS сервер для перенаправления
unsigned long apStartTime = 0;         // Время начала работы в режиме AP
String configWifiSSID = "";            // SSID, введенный пользователем
String configWifiPassword = "";        // Пароль WiFi, введенный пользователем
String serverAddress = "";             // Адрес сервера, введенный пользователем

// Переменные для авторизации и сопряжения
Preferences preferences;
String authToken = "";
bool isPaired = false;
bool pairingMode = false;
String pairingCode = "";
unsigned long pairingStartTime = 0;

// Добавляем улучшенную систему индикации

// Константы для режимов индикации
#define INDICATOR_NONE 0        // Индикация отключена
#define INDICATOR_SENDING 1     // Отправка данных на сервер
#define INDICATOR_RECEIVING 2   // Получение данных с сервера
#define INDICATOR_ERROR 3       // Ошибка соединения
#define INDICATOR_SUCCESS 4     // Успешное соединение/операция
#define INDICATOR_WARNING 5     // Предупреждение

// Переменные для управления индикацией
int indicatorMode = INDICATOR_NONE;
unsigned long indicatorStartTime = 0;
unsigned long indicatorDuration = 0;

// Функция для установки режима индикатора
void setIndicator(int mode, unsigned long duration) {
  indicatorMode = mode;
  indicatorStartTime = millis();
  indicatorDuration = duration;
}

// Функция для обновления RGB-индикатора с учетом текущего режима
void updateRGBEffect() {
  // Если активен эффект радуги
  if (isRGBEffect) {
    unsigned long elapsedTime = millis() - rgbEffectStartTime;
    
    // Run effect for 1 second max
    if (elapsedTime > 1000) {
      isRGBEffect = false;
      return;
    }
    
    int phase = (elapsedTime / 100) % 6;
    
    switch (phase) {
      case 0: setRGBColor(RGB_DEFAULT_BRIGHTNESS, 0, 0); break;     // Red
      case 1: setRGBColor(RGB_DEFAULT_BRIGHTNESS, RGB_DEFAULT_BRIGHTNESS, 0); break;     // Yellow
      case 2: setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0); break;     // Green
      case 3: setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, RGB_DEFAULT_BRIGHTNESS); break;     // Cyan
      case 4: setRGBColor(0, 0, RGB_DEFAULT_BRIGHTNESS); break;     // Blue
      case 5: setRGBColor(RGB_DEFAULT_BRIGHTNESS, 0, RGB_DEFAULT_BRIGHTNESS); break;     // Magenta
    }
    return;
  }

  // Проверяем активный режим индикации
  if (indicatorMode != INDICATOR_NONE) {
    // Если время индикации истекло, сбрасываем режим
    if (millis() - indicatorStartTime > indicatorDuration) {
      indicatorMode = INDICATOR_NONE;
      return;
    }
    
    // Выбираем эффект в зависимости от режима индикации
    switch (indicatorMode) {
      case INDICATOR_SENDING:
        // Пульсирующий синий - отправка данных
        {
          int pulsePeriod = 500; // 500 мс на полный цикл пульсации
          int pulsePhase = (millis() - indicatorStartTime) % pulsePeriod;
          if (pulsePhase < pulsePeriod / 2) {
            setRGBColor(0, 0, RGB_DEFAULT_BRIGHTNESS); // Синий - включен
          } else {
            setRGBColor(0, 0, 0); // Выключен
          }
        }
        break;
        
      case INDICATOR_RECEIVING:
        // Пульсирующий зеленый - получение данных
        {
          int pulsePeriod = 500; // 500 мс на полный цикл пульсации
          int pulsePhase = (millis() - indicatorStartTime) % pulsePeriod;
          if (pulsePhase < pulsePeriod / 2) {
            setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0); // Зеленый - включен
          } else {
            setRGBColor(0, 0, 0); // Выключен
          }
        }
        break;
        
      case INDICATOR_ERROR:
        // Быстрое мигание красным - ошибка
        {
          int errorPeriod = 200; // 200 мс на полный цикл мигания
          int errorPhase = (millis() - indicatorStartTime) % errorPeriod;
          if (errorPhase < errorPeriod / 2) {
            setRGBColor(RGB_DEFAULT_BRIGHTNESS, 0, 0); // Красный - включен
          } else {
            setRGBColor(0, 0, 0); // Выключен
          }
        }
        break;
        
      case INDICATOR_SUCCESS:
        // Постоянный зеленый с коротким затемнением - успех
        {
          int successPeriod = 1000; // 1000 мс на полный цикл
          int successPhase = (millis() - indicatorStartTime) % successPeriod;
          if (successPhase < 900) { // 900 мс светится
            setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0); // Зеленый - включен
          } else {
            setRGBColor(0, 0, 0); // Выключен на 100 мс
          }
        }
        break;
        
      case INDICATOR_WARNING:
        // Пульсирующий желтый - предупреждение
        {
          int warningPeriod = 1000; // 1000 мс на полный цикл пульсации
          int warningPhase = (millis() - indicatorStartTime) % warningPeriod;
          if (warningPhase < warningPeriod / 2) {
            setRGBColor(RGB_DEFAULT_BRIGHTNESS, RGB_DEFAULT_BRIGHTNESS, 0); // Желтый - включен
          } else {
            setRGBColor(0, 0, 0); // Выключен
          }
        }
        break;
    }
  }
}

// Функция для сохранения настроек WiFi
void saveWiFiSettings() {
  preferences.begin("wifi", false);
  
  // Сохраняем настройки и явно устанавливаем флаг configured
  preferences.putString("ssid", configWifiSSID);
  preferences.putString("password", configWifiPassword);
  preferences.putString("server", SERVER_URL); // Используем константу вместо пользовательского ввода
  preferences.putBool("configured", true);
  
  // Явно завершаем работу с preferences
  preferences.end();
  
  // Двойной звуковой сигнал для подтверждения сохранения настроек
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  tone(BUZZER_PIN, 2500, 100);
  
  Serial.println("Настройки WiFi сохранены:");
  Serial.print("SSID: ");
  Serial.println(configWifiSSID);
  Serial.print("Password length: ");
  Serial.println(configWifiPassword.length());
  Serial.print("Server: ");
  Serial.println(SERVER_URL);
  Serial.println("Флаг configured установлен в TRUE");
}

// Функция для загрузки настроек WiFi
bool loadWiFiSettings() {
  Serial.println("Загрузка настроек WiFi...");
  
  preferences.begin("wifi", true); // true - только для чтения
  
  bool isConfigured = preferences.getBool("configured", false);
  
  Serial.print("Флаг configured: ");
  Serial.println(isConfigured ? "TRUE" : "FALSE");
  
  // Если это первый запуск или настройки не были сохранены
  if (!isConfigured) {
    preferences.end();
    Serial.println("WiFi не настроен. Запуск режима точки доступа.");
    firstBoot = true;
    return false;
  }
  
  // Загружаем сохранённые настройки
  configWifiSSID = preferences.getString("ssid", "");
  configWifiPassword = preferences.getString("password", "");
  serverAddress = SERVER_URL; // Всегда используем константу
  
  // Явно завершаем работу с preferences
  preferences.end();
  
  Serial.println("Загружены настройки WiFi:");
  Serial.print("SSID: '");
  Serial.print(configWifiSSID);
  Serial.println("'");
  Serial.print("Password length: ");
  Serial.println(configWifiPassword.length());
  Serial.print("Server: '");
  Serial.print(serverAddress);
  Serial.println("'");
  
  // Проверяем, что данные загрузились корректно
  if (configWifiSSID.length() == 0) {
    Serial.println("ОШИБКА: SSID пустой после загрузки!");
    firstBoot = true;
    return false;
  }
  
  return true;
}

// Функция для имитации печати текста в терминале с сохранением предыдущих надписей
void typeText(const char* text, int x, int y, int delayMs) {
  int len = strlen(text);
  char buffer[64] = {0};
  
  for (int i = 0; i < len; i++) {
    buffer[i] = text[i];
    buffer[i+1] = '\0';
    // Не очищаем буфер, а просто перерисовываем строку
    u8g2.drawStr(x, y, buffer);
    u8g2.sendBuffer();
    delay(delayMs);
  }
}

// Диагностика в стиле терминала с постоянным тоном звука
void runSystemDiagnostics() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont10_tr); // Маленький моноширинный шрифт для терминала
  u8g2.sendBuffer();
  
  // Имитация запуска терминала
  typeText("> SYSTEM BOOT", 0, 10, 30);
  delay(300);
  
  // Проверка дисплея
  typeText("> OLED DISPLAY....OK", 0, 20, 15);
  tone(BUZZER_PIN, 1000, 20);
  delay(200);
  
  // Проверка RGB
  typeText("> RGB LED TEST....", 0, 30, 15);
  
  // Последовательная проверка RGB
  digitalWrite(RGB_R, HIGH);
  delay(100);
  digitalWrite(RGB_R, LOW);
  digitalWrite(RGB_G, HIGH);
  delay(100);
  digitalWrite(RGB_G, LOW);
  digitalWrite(RGB_B, HIGH);
  delay(100);
  digitalWrite(RGB_B, LOW);
  
  // Дописываем "OK" к существующей строке
  u8g2.drawStr(0, 30, "> RGB LED TEST....OK");
  u8g2.sendBuffer();
  tone(BUZZER_PIN, 1000, 20);
  delay(200);
  
  // Проверка GPS
  typeText("> GPS MODULE......OK", 0, 40, 15);
  tone(BUZZER_PIN, 1000, 20);
  delay(200);
  
  // Проверка Hall сенсора
  typeText("> HALL SENSOR.....OK", 0, 50, 15);
  tone(BUZZER_PIN, 1000, 20);
  delay(200);
  
  // Проверка EEPROM
  typeText("> EEPROM..........OK", 0, 60, 15);
  tone(BUZZER_PIN, 1000, 20);
  delay(300);
  
  // Финальное сообщение
  u8g2.clearBuffer();
  typeText("> SYSTEM LOADED", 0, 20, 20);
  typeText("> ALL CHECKS PASSED", 0, 35, 20);
  typeText("> READY", 0, 50, 20);
  
  // Звуковой сигнал завершения - одинаковый тон
  tone(BUZZER_PIN, 1000, 50);
  delay(50);
  tone(BUZZER_PIN, 1000, 50); 
  delay(50);
  tone(BUZZER_PIN, 1000, 50);
  delay(300);
}

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

// Обычная установка цвета светодиода (для прямого управления пинами)
void setRGBColor(byte r, byte g, byte b) {
  // Используем analogWrite вместо специфичных ESP32 функций
  analogWrite(RGB_R, r);
  analogWrite(RGB_G, g);
  analogWrite(RGB_B, b);
}

// Функция для применения текущего цвета светодиода - обновлена для использования значений 0-255
void updateLEDColor() {
  if (!isPowerOn) {
    setRGBColor(0, 0, 0);
    return;
  }
  
  // Устанавливаем постоянный зеленый цвет с заданной яркостью
  setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0);
}

// Функция для обновления RGB на основе статуса GPS - обновлена для 0-255
void updateGPSLedStatus() {
  if (!gps.satellites.isValid()) {
    // Нет данных о спутниках - красный
    setRGBColor(RGB_DEFAULT_BRIGHTNESS, 0, 0);
  } else if (gps.satellites.value() > 0 && !gps.location.isValid()) {
    // Есть спутники, но нет фикса - желтый
    setRGBColor(RGB_DEFAULT_BRIGHTNESS, RGB_DEFAULT_BRIGHTNESS/2, 0);
  } else if (gps.location.isValid()) {
    // Есть фикс местоположения - зеленый
    setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0);
  } else {
    // По умолчанию - красный
    setRGBColor(RGB_DEFAULT_BRIGHTNESS, 0, 0);
  }
}

void setup() {
  // Инициализируем последовательный порт
  Serial.begin(9600);
  
  // Инициализация EEPROM
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("Ошибка инициализации EEPROM!");
  }
  
  // Инициализируем GPS
  GPSSerial.begin(9600, SERIAL_8N1, 16, 17);
  
  // Инициализируем OLED дисплей
  u8g2.begin();
  u8g2.clear();
  
  // Инициализируем выводы
  pinMode(HALL_SENSOR, INPUT_PULLUP);
  pinMode(POWER_SWITCH, INPUT_PULLUP);
  pinMode(FUNC_SWITCH, INPUT_PULLUP);
  pinMode(WIFI_SETUP_BTN, INPUT_PULLUP); // Инициализация новой кнопки
  pinMode(BUZZER_PIN, OUTPUT);
  
  // Инициализация PWM для RGB-светодиодов
  setupRGBPWM();
  
  pinMode(GPS_POWER_PIN, OUTPUT);
  
  // Включаем питание GPS модуля
  digitalWrite(GPS_POWER_PIN, HIGH);
  
  // Настройка прерывания от датчика Холла
  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR), hallSensorISR, FALLING);
  
  // Инициализация RGB и пьезоэлемента
  setRGBColor(0, 0, 0);
  tone(BUZZER_PIN, 1000, 100);
  
  // Отображаем приветственное сообщение - используем существующий displayWelcomeScreen
  displayWelcomeScreen();
  
  // Загружаем одометр и другие данные из EEPROM - используем существующий loadDataFromEEPROM
  loadDataFromEEPROM();
  
  // Загружаем настройки WiFi из Preferences
  if (!loadWiFiSettings()) {
    // Если настройки не загружены или это первый запуск
    prepareAPMode();
  } else {
    // Если настройки WiFi успешно загружены, пытаемся подключиться
    Serial.println("Попытка подключения к WiFi с загруженными настройками...");
    wifiConnected = connectToSavedWiFi();
    
    if (wifiConnected) {
      Serial.println("Подключение успешно установлено!");
      // Устанавливаем индикацию успешного подключения
      setIndicator(INDICATOR_SUCCESS, 1000);
      
      // Проверяем соединение с сервером после успешного подключения
      if (isPaired) {
        Serial.println("Устройство сопряжено, проверка связи с сервером...");
        // Отправляем тестовый запрос
        bool serverOk = getSettingsFromServer();
        serverConnected = serverOk;
        
        if (serverOk) {
          Serial.println("Связь с сервером установлена!");
          setIndicator(INDICATOR_SUCCESS, 1000);
        } else {
          Serial.println("Не удалось подключиться к серверу");
          setIndicator(INDICATOR_WARNING, 1000);
        }
      }
    } else {
      Serial.println("Не удалось подключиться к WiFi");
      setIndicator(INDICATOR_ERROR, 1000);
    }
  }
  
  // Включаем устройство
  isPowerOn = true;
  
  Serial.println("Инициализация завершена");
}

// Инициализация PWM для RGB-светодиодов
void setupRGBPWM() {
  // Настраиваем пины как выходы
  pinMode(RGB_R, OUTPUT);
  pinMode(RGB_G, OUTPUT);
  pinMode(RGB_B, OUTPUT);
  
  // Устанавливаем все цвета в 0 (выключено)
  analogWrite(RGB_R, 0);
  analogWrite(RGB_G, 0);
  analogWrite(RGB_B, 0);
  
  Serial.println("Инициализация RGB PWM завершена");
}

void loop() {
  // Проверяем нажатие кнопки настройки WiFi
  checkWifiSetupButton();
  
  // Обрабатываем переключатели - используем существующую функцию checkSwitches
  checkSwitches();
  
  // Если питание выключено, не выполняем остальные операции
  if (!isPowerOn) {
    delay(50);
    return;
  }
  
  // Обработка в зависимости от режима устройства
  switch (deviceMode) {
    case MODE_AP_SETUP:
      // В режиме точки доступа обрабатываем DNS и веб-запросы
      dnsServer.processNextRequest();
      webServer.handleClient();
      
      // Отображаем экран настройки
      displayAPScreen();
      
      // Проверяем таймаут режима AP
      if (millis() - apStartTime > AP_TIMEOUT) {
        Serial.println("Таймаут режима AP. Возврат к нормальному режиму.");
        exitAPMode();
      }
      
      // Обрабатываем RGB индикацию
      handleAPModeIndication();
      break;
    
    case MODE_PAIRING:
      // В режиме сопряжения отображаем экран с кодом и проверяем статус сопряжения
      displayPairingScreen();
      
      // Проверяем успешность сопряжения каждые 5 секунд
      static unsigned long lastPairingCheck = 0;
      if (millis() - lastPairingCheck > 5000) {
        if (checkPairingStatus()) {
          // Если сопряжение успешно - переходим в обычный режим
          tone(BUZZER_PIN, 2000, 100);
          delay(100);
          tone(BUZZER_PIN, 2500, 100);
          deviceMode = MODE_NORMAL;
        }
        lastPairingCheck = millis();
      }
      
      // Проверяем таймаут режима сопряжения (5 минут)
      if (millis() - pairingStartTime > 300000) {
        Serial.println("Таймаут режима сопряжения. Возврат к нормальному режиму.");
        pairingMode = false;
        deviceMode = MODE_NORMAL;
        tone(BUZZER_PIN, 500, 300); // Звуковой сигнал таймаута
      }
      
      // RGB-индикация режима сопряжения - мигающий синий
      if (millis() % 1000 < 500) {
        setRGBColor(0, 0, RGB_DEFAULT_BRIGHTNESS);
      } else {
        setRGBColor(0, 0, 0);
      }
      break;
      
    case MODE_NORMAL:
    default:
      // Проверка статуса переключателей
      checkSwitches();
      
      if (isPowerOn) {
        // Стандартное поведение
        // Проверка длительного нажатия
        checkLongPress();
        
        // Чтение GPS данных
        readGPSData();
        
        // Обновление расчетов скорости
        updateSpeed();
        
        // Обновление RGB для экрана GPS
        if (currentScreen == 1) {
          updateGPSLedStatus();
        }
        
        // Отображение соответствующего экрана
        if (currentScreen == 0) {
          displayMainScreen();
        } else if (currentScreen == 1) {
          displayGPSScreen();
        } else if (currentScreen == 2) {
          displayWiFiScreen();
        }
        
        // Синхронизация данных с сервером, только если устройство сопряжено
        if (isPaired && wifiConnected && millis() - lastSyncTime > SYNC_INTERVAL) {
          bool success = sendDataToServer();
          if (success) {
            // При успешной отправке, получаем настройки с сервера
            getSettingsFromServer();
          }
          lastSyncTime = millis();
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
      break;
  }
  
  delay(50);
}

// Обновленная функция проверки переключателей с полным отключением
void checkSwitches() {
  int powerReading = digitalRead(POWER_SWITCH);
  int funcReading = digitalRead(FUNC_SWITCH);
  
  // ===== POWER SWITCH =====
  // Инвертируем логику - теперь HIGH (разомкнуто) означает включение
  bool newPowerState = (powerReading == HIGH);
  
  // Если изменилось состояние питания
  if (newPowerState != isPowerOn) {
    // Если выключаемся
    if (isPowerOn && !newPowerState) {
      // Отображаем сообщение выключения
      tone(BUZZER_PIN, 800, 100);
      setRGBColor(RGB_DEFAULT_BRIGHTNESS, 0, 0); // Красный - выключаем
      saveDataToEEPROM();
      displayPowerOffScreen();
      
      // Показываем сообщение "POWER OFF" в течение 1 секунды вместо 3
      delay(3000);
      
      // Полное отключение всех систем
      u8g2.clear();          // Очистка дисплея
      u8g2.setPowerSave(1);  // Отключение дисплея
      
      // Выключаем RGB светодиоды
      setRGBColor(0, 0, 0);
      
      digitalWrite(GPS_POWER_PIN, LOW); // Отключение GPS модуля
    }
    
    isPowerOn = newPowerState;
    
    if (isPowerOn) {
      // Power ON
      u8g2.setPowerSave(0);  // Включение дисплея
      digitalWrite(GPS_POWER_PIN, HIGH); // Включение GPS модуля
      tone(BUZZER_PIN, 1500, 100);
      setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0); // Зеленый вместо синего при включении
      displayWelcomeScreen();
      delay(3000);
    }
  }
  
  // ===== FUNCTION SWITCH для переключения экранов =====
  // Возвращаем исходную логику - LOW (нажато/замкнуто) активирует второй экран
  bool newScreenState = (funcReading == LOW);
  
  if (isPowerOn && newScreenState != (lastFuncState == LOW)) {
    // При нажатии переключаем экраны циклически: 0 -> 1 -> 2 -> 0
    if (newScreenState) {
      currentScreen = (currentScreen + 1) % 3;
      tone(BUZZER_PIN, 1000, 30);
    }
  }
  
  // На экране GPS меняем цвет RGB в зависимости от статуса GPS
  if (isPowerOn) {
    switch (currentScreen) {
      case 0:
        // На главном экране - постоянный зеленый цвет
        setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0);
        break;
      case 1:
        // На экране GPS - цвет зависит от статуса
        updateGPSLedStatus();
        break;
      case 2:
        // На экране WiFi - цвет зависит от подключения
        if (wifiConnected && serverConnected) {
          setRGBColor(0, RGB_DEFAULT_BRIGHTNESS, 0); // Зеленый - всё подключено
        } else if (wifiConnected) {
          setRGBColor(RGB_DEFAULT_BRIGHTNESS, RGB_DEFAULT_BRIGHTNESS, 0); // Желтый - Wi-Fi подключен, но сервер нет
        } else {
          setRGBColor(RGB_DEFAULT_BRIGHTNESS, 0, 0); // Красный - нет подключения к Wi-Fi
        }
        break;
    }
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
  
  // Update max speed record
  if (currentSpeed > maxRecordedSpeed) {
    maxRecordedSpeed = currentSpeed;
    tone(BUZZER_PIN, 2000, 50);
  }
}

void displayWelcomeScreen() {
      u8g2.clearBuffer();
  
  // Логотип
  u8g2.setFont(u8g2_font_profont17_tr); // Меньший моноширинный шрифт
  u8g2.drawStr(25, 30, "protok0l");
  
  u8g2.setFont(u8g2_font_profont12_tr); // Меньший моноширинный шрифт
  u8g2.drawStr(25, 50, "SPEEDOMETER");
  
  // Версия
  u8g2.setFont(u8g2_font_profont10_tr); // Меньший моноширинный шрифт
  u8g2.drawStr(50, 62, "v1.1");
  
      u8g2.sendBuffer();
}

void displayPowerOffScreen() {
  u8g2.clearBuffer();
  
  // Изменяем надпись о выключении
  u8g2.setFont(u8g2_font_profont17_tr); // Больший моноширинный шрифт
  u8g2.drawStr(25, 38, "protok0l");
  

  
  u8g2.sendBuffer();
}

void displayMainScreen() {
  u8g2.clearBuffer();
  
  // Увеличенный шрифт для скорости
  u8g2.setFont(u8g2_font_profont29_tn); // Больший моноширинный шрифт
  char speedBuf[10];
  dtostrf(currentSpeed, 2, 1, speedBuf);
  
  // Центрирование скорости
  int textWidth = u8g2.getStrWidth(speedBuf);
  u8g2.drawStr((128-textWidth)/2, 35, speedBuf);
  
  // Разделительная линия
  u8g2.drawHLine(0, 40, 128);
  
  // Информация внизу
  u8g2.setFont(u8g2_font_profont12_tr); // Меньший моноширинный шрифт
  
  // Общее расстояние (Trip)
  char tripBuf[10];
  dtostrf(tripDistance, 2, 2, tripBuf);
  u8g2.drawStr(2, 52, "Distance:");
  u8g2.drawStr(60, 52, tripBuf);
  u8g2.drawStr(100, 52, "km");
  
  // Местное время (UTC+3)
  char timeBuf[20];
  if (gps.time.isValid()) {
    // Конвертируем в локальное время UTC+3
    int localHour = (gps.time.hour() + TIME_ZONE_OFFSET) % 24;
    sprintf(timeBuf, "%02d:%02d:%02d", 
      localHour, 
      gps.time.minute(), 
      gps.time.second());
  } else {
    // Если GPS время недоступно, показываем прочерки
    sprintf(timeBuf, "--:--:--");
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
  u8g2.setFont(u8g2_font_profont17_tr); // Меньший моноширинный шрифт
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
  u8g2.setFont(u8g2_font_profont12_tr); // Меньший моноширинный шрифт
  
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

// Экран с информацией о WiFi и синхронизации с сервером
void displayWiFiScreen() {
  u8g2.clearBuffer();
  
  // Заголовок по центру - используем одинаковый шрифт для всех экранов
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(30, 20, "WiFi");
  
  // Разделительная линия
  u8g2.drawHLine(0, 24, 128);
  
  // Основные данные с корректным позиционированием
  u8g2.setFont(u8g2_font_profont12_tr);
  
  // Статус WiFi
  u8g2.drawStr(5, 38, "WiFi:");
  u8g2.drawStr(65, 38, wifiConnected ? "ON" : "OFF");
  
  // IP-адрес (если подключен) или причина отсутствия подключения
  if (wifiConnected) {
    char ipBuf[20];
    sprintf(ipBuf, "IP: %s", WiFi.localIP().toString().c_str());
    u8g2.drawStr(5, 48, ipBuf);
  } else {
    u8g2.drawStr(5, 48, "Press SETUP to config");
  }
  
  // Статус сопряжения
  u8g2.drawStr(5, 58, "Paired:");
  u8g2.drawStr(65, 58, isPaired ? "YES" : "NO");
  
  // Инструкция в зависимости от статуса
  if (isPaired) {
    u8g2.drawStr(5, 63, "Connected to server");
  } else if (wifiConnected) {
    u8g2.drawStr(5, 63, "Long press to pair");
  }
  
  u8g2.sendBuffer();
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
      } 
      else if (currentScreen == 1) {
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
      else if (currentScreen == 2) {
        // На экране WiFi длительное нажатие запускает режим сопряжения или AP
        if (wifiConnected && !isPaired) {
          // Если WiFi подключен, но устройство не сопряжено, запускаем сопряжение
          startPairingMode();
        } else if (!wifiConnected) {
          // Если WiFi не подключен, запускаем настройку
          activateAPModeByButton();
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

// Функция подключения к WiFi
bool connectToWiFi() {
  // Если устройство настроено - используем сохраненные настройки
  if (configWifiSSID.length() > 0) {
    return connectToSavedWiFi();
  }
  
  // Если устройство не настроено - используем настройки по умолчанию
  Serial.print("Подключение к WiFi...");
  
  // Настройка WiFi в режиме станции
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Ожидаем подключения с таймаутом 10 секунд
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nПодключено к WiFi!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  
  Serial.println("\nНе удалось подключиться к WiFi");
  return false;
}

// Функция для отображения экрана WiFi-настройки
void displaySetupScreen() {
  u8g2.clearBuffer();
  
  // Заголовок
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(10, 20, "WiFi SETUP");
  
  // Разделительная линия
  u8g2.drawHLine(0, 24, 128);
  
  // Информация о точке доступа - уменьшим шрифт и оптимизируем текст
  u8g2.setFont(u8g2_font_profont10_tr); // Используем меньший шрифт
  u8g2.drawStr(5, 36, "Connect to WiFi:");
  u8g2.drawStr(5, 48, AP_SSID);
  
  u8g2.drawStr(5, 60, "Open:");
  u8g2.drawStr(35, 60, "192.168.4.1");
  
  u8g2.sendBuffer();
}

// Функция для подключения к WiFi с использованием сохраненных настроек
bool connectToSavedWiFi() {
  Serial.print("Подключение к WiFi с сохраненными настройками...");
  
  // Проверяем, что настройки загружены
  if (configWifiSSID.length() == 0) {
    Serial.println("Ошибка: SSID не задан");
    return false;
  }
  
  // Настройка WiFi в режиме станции
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Отключаемся от предыдущих подключений
  delay(100);
  
  Serial.print("Подключение к сети '");
  Serial.print(configWifiSSID);
  Serial.println("'");
  
  // Максимальное количество попыток подключения
  const int MAX_CONNECTION_ATTEMPTS = 3;
  
  for (int attempt = 1; attempt <= MAX_CONNECTION_ATTEMPTS; attempt++) {
    Serial.printf("Попытка %d из %d\n", attempt, MAX_CONNECTION_ATTEMPTS);
    
    // Начинаем подключение
    WiFi.begin(configWifiSSID.c_str(), configWifiPassword.c_str());
    
    // Ожидаем подключения с таймаутом 10 секунд на каждую попытку
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
      // Индикация процесса подключения
      setIndicator(INDICATOR_WARNING, 300);
    }
    
    // Проверяем результат подключения
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nПодключено к WiFi!");
      Serial.print("IP адрес: ");
      Serial.println(WiFi.localIP());
      
      // Индикация успешного подключения
      setIndicator(INDICATOR_SUCCESS, 1000);
      
      // Проверяем устойчивость подключения (небольшая задержка)
      delay(500);
      
      // Если подключение стабильно, возвращаем успех
      if (WiFi.status() == WL_CONNECTED) {
        return true;
      } else {
        Serial.println("Подключение нестабильно, повторная попытка...");
      }
    } else {
      Serial.printf("\nПопытка %d не удалась. Код ошибки: %d\n", attempt, WiFi.status());
      
      // Если не последняя попытка, делаем небольшую паузу перед следующей
      if (attempt < MAX_CONNECTION_ATTEMPTS) {
        Serial.println("Небольшая пауза перед следующей попыткой...");
        delay(1000);
        WiFi.disconnect();
        delay(500);
      }
    }
  }
  
  Serial.println("Все попытки подключения к WiFi не удались");
  Serial.print("Последний код ошибки: ");
  Serial.println(WiFi.status());
  
  // Индикация ошибки
  setIndicator(INDICATOR_ERROR, 1000);
  
  return false;
}

// Функция для отправки данных на сервер (обновленная с индикацией)
bool sendDataToServer() {
  // Включаем индикацию отправки данных
  setIndicator(INDICATOR_SENDING, 3000); // Максимум 3 секунды
  
  // Проверяем подключение
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi не подключен. Попытка переподключения...");
    wifiConnected = connectToSavedWiFi();
    if (!wifiConnected) {
      setIndicator(INDICATOR_ERROR, 1000); // Индикация ошибки на 1 секунду
      return false;
    }
  }
  
  // Проверяем, сопряжено ли устройство
  if (!isPaired) {
    Serial.println("Устройство не сопряжено. Невозможно отправить данные.");
    setIndicator(INDICATOR_WARNING, 1000); // Индикация предупреждения на 1 секунду
    return false;
  }
  
  // Проверяем, настроен ли адрес сервера
  if (serverAddress.length() == 0) {
    Serial.println("Не указан адрес сервера");
    setIndicator(INDICATOR_ERROR, 1000); // Индикация ошибки на 1 секунду
    return false;
  }
  
  Serial.println("Отправка данных на сервер...");
  
  HTTPClient http;
  // Используем адрес сервера из настроек + эндпоинт
  http.begin(serverAddress + API_RECEIVE);
  http.addHeader("Content-Type", "application/json");
  
  // Добавляем токен авторизации
  http.addHeader("Authorization", "Bearer " + authToken);
  
  // Создаем JSON документ с данными
  StaticJsonDocument<512> doc;
  doc["device_id"] = deviceID;
  doc["speed"] = currentSpeed;
  doc["max_speed"] = maxSpeed;
  doc["distance"] = tripDistance;
  
  // Добавляем GPS данные если они доступны
    if (gps.location.isValid()) {
    doc["latitude"] = gps.location.lat();
    doc["longitude"] = gps.location.lng();
    doc["altitude"] = gps.altitude.meters();
  }
  
  // Добавляем состояние переключателей и сенсоров
  doc["hall_sensor"] = hallTriggered;
  doc["switch1"] = (digitalRead(POWER_SWITCH) == HIGH);
  doc["switch2"] = (digitalRead(FUNC_SWITCH) == LOW);
  
  // Добавляем время, если GPS активен
  if (gps.time.isValid()) {
    int localHour = (gps.time.hour() + TIME_ZONE_OFFSET) % 24;
    char timeStr[20];
    sprintf(timeStr, "%04d-%02d-%02dT%02d:%02d:%02dZ", 
      gps.date.year(),
      gps.date.month(),
      gps.date.day(),
      localHour,
      gps.time.minute(),
      gps.time.second());
    doc["gps_time"] = timeStr;
  }
  
  // Сериализуем JSON в строку
  String jsonData;
  serializeJson(doc, jsonData);
  
  // Отправляем запрос
  int httpCode = http.POST(jsonData);
  
  // Проверяем результат
  if (httpCode > 0) {
    Serial.printf("HTTP код ответа: %d\n", httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Ответ сервера:");
      Serial.println(payload);
      serverConnected = true;
      
      // Сначала закрываем соединение
      http.end();
      
      // Индикация успешной отправки
      setIndicator(INDICATOR_SUCCESS, 500); // Успех на 0.5 секунды
      
      // Проверяем подключение к WiFi после отправки
      if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        // Повторная попытка подключения
        wifiConnected = connectToSavedWiFi();
      }
      
      return true;
    } else if (httpCode == HTTP_CODE_UNAUTHORIZED) {
      // Если сервер вернул 401, значит токен недействителен
      Serial.println("Токен авторизации недействителен. Сбрасываем сопряжение.");
      isPaired = false;
      authToken = "";
      savePairingSettings();
      
      // Закрываем соединение
      http.end();
      
      // Индикация ошибки
      setIndicator(INDICATOR_ERROR, 1000); // Ошибка на 1 секунду
      
      // Проверяем подключение к WiFi после ошибки
      if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        // Повторная попытка подключения
        wifiConnected = connectToSavedWiFi();
      }
      return false;
    }
  } else {
    Serial.printf("Ошибка запроса: %s\n", http.errorToString(httpCode).c_str());
    serverConnected = false;
    
    // Закрываем соединение
    http.end();
    
    // Индикация ошибки
    setIndicator(INDICATOR_ERROR, 1000); // Ошибка на 1 секунду
    
    // Проверяем подключение к WiFi после ошибки
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      // Повторная попытка подключения
      wifiConnected = connectToSavedWiFi();
    }
  }
  
  return false;
}

// Функция для получения настроек с сервера (обновленная с индикацией)
bool getSettingsFromServer() {
  // Включаем индикацию получения данных
  setIndicator(INDICATOR_RECEIVING, 3000); // Максимум 3 секунды
  
  // Проверяем подключение
  if (WiFi.status() != WL_CONNECTED) {
    setIndicator(INDICATOR_ERROR, 1000); // Индикация ошибки на 1 секунду
    return false;
  }
  
  // Проверяем, настроен ли адрес сервера
  if (serverAddress.length() == 0) {
    Serial.println("Не указан адрес сервера");
    setIndicator(INDICATOR_ERROR, 1000); // Индикация ошибки на 1 секунду
    return false;
  }
  
  HTTPClient http;
  http.begin(serverAddress + API_SETTINGS);
  
  // Добавляем заголовок авторизации, если есть токен
  if (authToken.length() > 0) {
    http.addHeader("Authorization", "Bearer " + authToken);
  }
  
  // Отправляем запрос
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      
      // Парсим полученные настройки
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        // Применяем полученные настройки
        if (doc.containsKey("gps_sound_enabled")) {
          gpsStatusSoundEnabled = doc["gps_sound_enabled"];
        }
  
        // Добавьте обработку других настроек по необходимости
        
        http.end();
        
        // Индикация успешного получения
        setIndicator(INDICATOR_SUCCESS, 500); // Успех на 0.5 секунды
        return true;
      }
    }
  }
  
  http.end();
  
  // Индикация ошибки
  setIndicator(INDICATOR_ERROR, 1000); // Ошибка на 1 секунду
  return false;
}

// Загрузка настроек сопряжения из памяти
void loadPairingSettings() {
  preferences.begin("speedometer", false); // false - для чтения/записи
  
  // Загружаем сохраненный токен авторизации
  authToken = preferences.getString("auth_token", "");
  isPaired = (authToken.length() > 0);
  
  // Загружаем Device ID
  String savedDeviceID = preferences.getString("device_id", "");
  if (savedDeviceID.length() > 0) {
    deviceID = savedDeviceID;
  }
  
  preferences.end();
  
  Serial.println("Загружены настройки сопряжения:");
  Serial.print("Device ID: ");
  Serial.println(deviceID);
  Serial.print("Paired: ");
  Serial.println(isPaired ? "Yes" : "No");
}

// Сохранение настроек сопряжения в память
void savePairingSettings() {
  preferences.begin("speedometer", false);
  
  // Сохраняем токен авторизации
  preferences.putString("auth_token", authToken);
  
  // Сохраняем Device ID
  preferences.putString("device_id", deviceID);
  
  preferences.end();
  
  Serial.println("Настройки сопряжения сохранены");
}

// Генерация кода сопряжения
String generatePairingCode() {
  // Генерируем случайный 6-значный код
  String code = "";
  for (int i = 0; i < 6; i++) {
    code += String(random(0, 10));
}
  return code;
}

// Запуск режима сопряжения
bool startPairingMode() {
  // Проверяем подключение к WiFi
  if (!wifiConnected) {
    Serial.println("Попытка подключения к WiFi для сопряжения...");
    wifiConnected = connectToSavedWiFi();
    
    if (!wifiConnected) {
      Serial.println("Не удалось подключиться к WiFi для сопряжения");
      tone(BUZZER_PIN, 500, 100); // Звуковой сигнал ошибки
      return false;
    }
  }
  
  // Генерируем код сопряжения
  pairingCode = generatePairingCode();
  pairingMode = true;
  deviceMode = MODE_PAIRING; // Переключаемся в режим сопряжения
  pairingStartTime = millis();
  
  Serial.print("Код сопряжения: ");
  Serial.println(pairingCode);
  
  // Звуковой сигнал успешного начала сопряжения
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  tone(BUZZER_PIN, 2500, 100);
  
  // Отправляем запрос на сервер для начала процесса сопряжения
  HTTPClient http;
  http.begin(serverAddress + API_PAIR);
  http.addHeader("Content-Type", "application/json");
  
  // Создаем JSON с данными устройства
  StaticJsonDocument<256> doc;
  doc["device_id"] = deviceID;
  doc["pairing_code"] = pairingCode;
  
  String jsonData;
  serializeJson(doc, jsonData);
  
  int httpCode = http.POST(jsonData);
  
  if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Ответ сервера: " + payload);
    http.end();
    
    // Переходим сразу к отображению экрана сопряжения
    displayPairingScreen();
    return true;
  }
  
  Serial.println("Ошибка запроса на сопряжение: " + String(httpCode));
  http.end();
  
  // Возвращаемся в обычный режим при ошибке
  pairingMode = false;
  deviceMode = MODE_NORMAL;
  return false;
}

// Проверка успешного сопряжения
bool checkPairingStatus() {
  if (!pairingMode || !wifiConnected) {
    return false;
  }
  
  // Проверяем, не истекло ли время сопряжения (5 минут)
  if (millis() - pairingStartTime > 300000) {
    pairingMode = false;
    return false;
  }
  
  // Запрашиваем статус сопряжения с сервера
  HTTPClient http;
  http.begin(String(SERVER_URL) + API_AUTH);
  http.addHeader("Content-Type", "application/json");
  
  // Отправляем ID устройства
  StaticJsonDocument<128> doc;
  doc["device_id"] = deviceID;
  
  String jsonData;
  serializeJson(doc, jsonData);
  
  int httpCode = http.POST(jsonData);
  
  if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Парсим ответ
    StaticJsonDocument<256> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, payload);
    
    if (!error && responseDoc["success"]) {
      // Сопряжение успешно, сохраняем токен
      authToken = responseDoc["token"].as<String>();
      isPaired = true;
      pairingMode = false;
      
      savePairingSettings();
      
      Serial.println("Устройство успешно сопряжено!");
      http.end();
      return true;
    }
  }
  
  http.end();
  return false;
}

// Отображение экрана сопряжения
void displayPairingScreen() {
  u8g2.clearBuffer();
  
  // Заголовок - используем тот же шрифт, что и в других экранах
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(15, 20, "PAIRING");
  
  // Разделительная линия
  u8g2.drawHLine(0, 24, 128);
  
  // Инструкции и код
  u8g2.setFont(u8g2_font_profont12_tr);
  u8g2.drawStr(5, 38, "Enter code:");
  
  // Отображаем код большим шрифтом по центру
  u8g2.setFont(u8g2_font_profont22_tn);
  int codeWidth = u8g2.getStrWidth(pairingCode.c_str());
  u8g2.drawStr((128-codeWidth)/2, 58, pairingCode.c_str());
  
  // Отображаем время до конца сопряжения
  unsigned long remainingTime = (300000 - (millis() - pairingStartTime)) / 1000;
  char timeBuf[10];
  sprintf(timeBuf, "%lu sec", remainingTime);
  
  u8g2.setFont(u8g2_font_profont10_tr);
  u8g2.drawStr(5, 63, "Time left:");
  u8g2.drawStr(60, 63, timeBuf);
  
  u8g2.sendBuffer();
}

// Обработчик корневой страницы - форма настройки
void handleRoot() {
  String html = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<meta charset='UTF-8'>"
                "<title>WiFi Setup</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; }"
                ".container { max-width: 400px; margin: 0 auto; }"
                "h1 { color: #2c3e50; text-align: center; }"
                "label { display: block; margin-bottom: 5px; font-weight: bold; }"
                "input[type='text'], input[type='password'] { width: 100%; padding: 8px; margin-bottom: 15px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }"
                "button { background-color: #2980b9; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; width: 100%; margin-bottom: 15px; }"
                "button:hover { background-color: #3498db; }"
                ".pairing { margin-top: 40px; border-top: 1px solid #eee; padding-top: 20px; }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container'>"
                "<h1>WiFi Setup</h1>"
                "<form action='/save' method='post'>"
                "<label for='ssid'>WiFi Name:</label>"
                "<input type='text' id='ssid' name='ssid' required>"
                "<label for='password'>WiFi Password:</label>"
                "<input type='password' id='password' name='password'>"
                "<button type='submit'>Save WiFi Settings</button>"
                "</form>"
                
                "<div class='pairing'>"
                "<h2>Device Pairing</h2>"
                "<p>If you see a pairing code on your device, enter it here:</p>"
                "<form action='/pair' method='post'>"
                "<label for='pairingCode'>Pairing Code:</label>"
                "<input type='text' id='pairingCode' name='pairingCode' placeholder='6-digit code' minlength='6' maxlength='6' pattern='[0-9]{6}'>"
                "<button type='submit'>Pair Device</button>"
                "</form>"
                "</div>"
                "</div>"
                "</body>"
                "</html>";
                
  webServer.send(200, "text/html", html);
}

// Обработчик для длительного нажатия на экране WiFi
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
      } 
      else if (currentScreen == 1) {
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
      else if (currentScreen == 2) {
        // На экране WiFi длительное нажатие запускает режим сопряжения или AP
        if (wifiConnected && !isPaired) {
          // Если WiFi подключен, но устройство не сопряжено, запускаем сопряжение
          startPairingMode();
        } else if (!wifiConnected) {
          // Если WiFi не подключен, запускаем настройку
          activateAPModeByButton();
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

// Функция для активации режима AP по нажатию кнопки
void activateAPModeByButton() {
  // Звуковая индикация начала режима настройки
  tone(BUZZER_PIN, 2000, 100);
  delay(100);
  tone(BUZZER_PIN, 2500, 100);
  
  // Показываем сообщение о переходе в режим настройки
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(0, 30, "WiFi SETUP");
  u8g2.setFont(u8g2_font_profont12_tr);
  u8g2.drawStr(0, 50, "Starting AP...");
  u8g2.sendBuffer();
  
  // Переходим в режим точки доступа
  prepareAPMode();
}

// Функция подготовки режима точки доступа без блокировки
void prepareAPMode() {
  // Переключаемся в режим AP
  deviceMode = MODE_AP_SETUP;
  
  // Отключаем WiFi, если был подключен
  WiFi.disconnect();
  delay(100);
  
  // Настраиваем точку доступа
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  
  // Настраиваем DNS-сервер
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  
  // Настраиваем веб-сервер
  webServer.on("/", handleRoot);
  webServer.on("/save", HTTP_POST, handleSaveConfig);
  webServer.on("/pair", HTTP_POST, handlePairDevice);
  webServer.onNotFound([]() {
    // Перенаправляем на страницу настройки
    webServer.sendHeader("Location", "/", true);
    webServer.send(302, "text/plain", "");
  });
  webServer.begin();
  
  // Сохраняем время запуска
  apStartTime = millis();
  
  Serial.println("Точка доступа запущена");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP().toString());
}

// Заглушка для обратной совместимости
void startAPMode() {
  prepareAPMode();
}

// Добавим переменную для отслеживания длительного нажатия на экране WiFi
unsigned long wifiScreenLongPressStart = 0;
bool wifiScreenLongPressActive = false;

// Переменные для определения состояния кнопки настройки WiFi
int lastWifiSetupBtnState = HIGH;
unsigned long lastWifiSetupBtnTime = 0;
bool wifiSetupBtnPressed = false;

// Добавляем функцию для проверки состояния кнопки настройки WiFi
void checkWifiSetupButton() {
  // Чтение текущего состояния кнопки
  int currentState = digitalRead(WIFI_SETUP_BTN);
  
  // Проверка на дребезг
  if ((millis() - lastWifiSetupBtnTime) > debounceDelay) {
    // Если состояние изменилось
    if (currentState != lastWifiSetupBtnState) {
      lastWifiSetupBtnTime = millis();
      
      // Если кнопка нажата (LOW)
      if (currentState == LOW && lastWifiSetupBtnState == HIGH) {
        wifiSetupBtnPressed = true;
        
        // Звуковой сигнал при нажатии
        tone(BUZZER_PIN, 1500, 50);
        
        // Если устройство включено, запускаем режим настройки WiFi
        if (isPowerOn) {
          // Начинаем процесс настройки WiFi
          activateAPModeByButton();
        }
      }
      
      lastWifiSetupBtnState = currentState;
    }
  }
  
  // Сбрасываем флаг, если кнопка отпущена
  if (currentState == HIGH && wifiSetupBtnPressed) {
    wifiSetupBtnPressed = false;
  }
}

// Функция для отображения экрана в режиме AP
void displayAPScreen() {
  static unsigned long lastUpdateTime = 0;
  
  // Обновляем экран не чаще чем раз в секунду
  if (millis() - lastUpdateTime < 1000) {
    return;
  }
  
  lastUpdateTime = millis();
  
  u8g2.clearBuffer();
  
  // Использую тот же шрифт, что и на других экранах
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(5, 20, "WiFi SETUP");
  
  u8g2.drawHLine(0, 24, 128);
  
  u8g2.setFont(u8g2_font_profont12_tr);
  u8g2.drawStr(5, 38, "Connect to WiFi:");
  u8g2.drawStr(5, 52, AP_SSID);
  
  u8g2.drawStr(5, 63, "Open: 192.168.4.1");
  
  u8g2.sendBuffer();
}

// Функция для обработки индикации в режиме AP - обновлена для 0-255
void handleAPModeIndication() {
  // Мигаем синим светодиодом для индикации режима AP
  if (millis() % 1000 < 500) {
    setRGBColor(0, 0, RGB_DEFAULT_BRIGHTNESS); // Синий
  } else {
    setRGBColor(0, 0, 0); // Выключен
  }
}

// Функция для выхода из режима AP
void exitAPMode() {
  // Останавливаем серверы
  webServer.stop();
  dnsServer.stop();
  
  // Переключаемся в обычный режим
  deviceMode = MODE_NORMAL;
  
  // Восстанавливаем индикацию
  setRGBColor(0, 1, 0);
}

// Функция для обработки сохранения настроек WiFi
void handleSaveConfig() {
  Serial.println("Получены настройки WiFi:");
  
  configWifiSSID = webServer.arg("ssid");
  configWifiPassword = webServer.arg("password");
  serverAddress = SERVER_URL; // Используем фиксированный адрес сервера
  
  Serial.print("SSID: '");
  Serial.print(configWifiSSID);
  Serial.println("'");
  Serial.print("Password length: ");
  Serial.println(configWifiPassword.length());
  Serial.print("Server: '");
  Serial.print(serverAddress);
  Serial.println("'");
  
  // Проверяем, что SSID не пустой
  if (configWifiSSID.length() == 0) {
    String errorHtml = "<!DOCTYPE html>"
                  "<html>"
                  "<head>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                  "<meta charset='UTF-8'>"
                  "<title>Error</title>"
                  "<style>"
                  "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; text-align: center; }"
                  ".container { max-width: 400px; margin: 0 auto; }"
                  "h1 { color: #e74c3c; }"
                  "p { margin-bottom: 20px; }"
                  ".icon { font-size: 48px; color: #e74c3c; margin-bottom: 20px; }"
                  "button { background-color: #3498db; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; }"
                  "</style>"
                  "</head>"
                  "<body>"
                  "<div class='container'>"
                  "<div class='icon'>✗</div>"
                  "<h1>Error</h1>"
                  "<p>WiFi name cannot be empty.</p>"
                  "<a href='/'><button>Back</button></a>"
                  "</div>"
                  "</body>"
                  "</html>";
    
    webServer.send(400, "text/html", errorHtml);
    return;
  }
  
  // Проверяем минимальную длину пароля
  if (configWifiPassword.length() > 0 && configWifiPassword.length() < 8) {
    String errorHtml = "<!DOCTYPE html>"
                  "<html>"
                  "<head>"
                  "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                  "<meta charset='UTF-8'>"
                  "<title>Error</title>"
                  "<style>"
                  "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; text-align: center; }"
                  ".container { max-width: 400px; margin: 0 auto; }"
                  "h1 { color: #e74c3c; }"
                  "p { margin-bottom: 20px; }"
                  ".icon { font-size: 48px; color: #e74c3c; margin-bottom: 20px; }"
                  "button { background-color: #3498db; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; }"
                  "</style>"
                  "</head>"
                  "<body>"
                  "<div class='container'>"
                  "<div class='icon'>✗</div>"
                  "<h1>Error</h1>"
                  "<p>Password must be at least 8 characters.</p>"
                  "<a href='/'><button>Back</button></a>"
                  "</div>"
                  "</body>"
                  "</html>";
    
    webServer.send(400, "text/html", errorHtml);
    return;
  }
  
  // Сохраняем настройки
  saveWiFiSettings();
  
  // Отправляем страницу успешного сохранения с поддержкой UTF-8
  String html = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<meta charset='UTF-8'>"
                "<title>Success</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; text-align: center; }"
                ".container { max-width: 400px; margin: 0 auto; }"
                "h1 { color: #27ae60; }"
                "p { margin-bottom: 20px; }"
                ".icon { font-size: 48px; color: #27ae60; margin-bottom: 20px; }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container'>"
                "<div class='icon'>✓</div>"
                "<h1>Success!</h1>"
                "<p>Settings saved. Device will restart.</p>"
                "</div>"
                "</body>"
                "</html>";
                
  webServer.send(200, "text/html", html);
  
  // Даем время браузеру получить страницу
  delay(2000);
  
  // Останавливаем серверы и перезагружаем устройство
  webServer.stop();
  dnsServer.stop();
  
  deviceMode = MODE_NORMAL;
  
  // Для надежности еще раз сохраняем настройки перед перезагрузкой
  saveWiFiSettings();
  
  // Перезагружаем устройство
  ESP.restart();
}

// Обработчик отправки кода сопряжения
void handlePairDevice() {
  String pairingCodeInput = webServer.arg("pairingCode");
  
  // Проверяем формат кода (6 цифр)
  if (pairingCodeInput.length() != 6 || !isNumeric(pairingCodeInput)) {
    String errorHtml = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<meta charset='UTF-8'>"
                "<title>Error</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; text-align: center; }"
                ".container { max-width: 400px; margin: 0 auto; }"
                "h1 { color: #e74c3c; }"
                "p { margin-bottom: 20px; }"
                ".icon { font-size: 48px; color: #e74c3c; margin-bottom: 20px; }"
                "button { background-color: #3498db; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container'>"
                "<div class='icon'>✗</div>"
                "<h1>Error</h1>"
                "<p>Invalid pairing code. Must be 6 digits.</p>"
                "<a href='/'><button>Back</button></a>"
                "</div>"
                "</body>"
                "</html>";
    
    webServer.send(400, "text/html", errorHtml);
    return;
  }
  
  // Отправляем информацию о сопряжении на сервер
  HTTPClient http;
  String serverUrl = String(SERVER_URL) + String(API_PAIR_CONFIRM);
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  
  // Формируем JSON с данными для подтверждения сопряжения
  StaticJsonDocument<256> doc;
  doc["device_id"] = deviceID;
  doc["pairing_code"] = pairingCodeInput;
  
  String jsonData;
  serializeJson(doc, jsonData);
  
  int httpCode = http.POST(jsonData);
  bool success = false;
  String authTokenReceived = "";
  
  if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Ответ сервера при сопряжении: " + payload);
    
    // Парсим ответ
    StaticJsonDocument<256> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, payload);
    
    if (!error && responseDoc["success"]) {
      authTokenReceived = responseDoc["token"].as<String>();
      success = true;
    }
  }
  
  http.end();
  
  // Отображаем результат
  if (success) {
    // Сохраняем полученный токен
    authToken = authTokenReceived;
    isPaired = true;
    savePairingSettings();
    
    String successHtml = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<meta charset='UTF-8'>"
                "<title>Success</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; text-align: center; }"
                ".container { max-width: 400px; margin: 0 auto; }"
                "h1 { color: #27ae60; }"
                "p { margin-bottom: 20px; }"
                ".icon { font-size: 48px; color: #27ae60; margin-bottom: 20px; }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container'>"
                "<div class='icon'>✓</div>"
                "<h1>Success!</h1>"
                "<p>Device successfully paired with your account! You can now close this page.</p>"
                "</div>"
                "</body>"
                "</html>";
    
    webServer.send(200, "text/html", successHtml);
  } else {
    // Сопряжение не удалось
    String errorHtml = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<meta charset='UTF-8'>"
                "<title>Error</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; text-align: center; }"
                ".container { max-width: 400px; margin: 0 auto; }"
                "h1 { color: #e74c3c; }"
                "p { margin-bottom: 20px; }"
                ".icon { font-size: 48px; color: #e74c3c; margin-bottom: 20px; }"
                "button { background-color: #3498db; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container'>"
                "<div class='icon'>✗</div>"
                "<h1>Error</h1>"
                "<p>Pairing failed. Invalid code or connection error.</p>"
                "<a href='/'><button>Try Again</button></a>"
                "</div>"
                "</body>"
                "</html>";
    
    webServer.send(400, "text/html", errorHtml);
  }
}

// Проверка, является ли строка числовой
bool isNumeric(String str) {
  for (unsigned int i = 0; i < str.length(); i++) {
    if (!isDigit(str.charAt(i))) {
      return false;
    }
  }
  return true;
}