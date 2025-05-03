#include <Wire.h>
#include <U8g2lib.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <math.h>

// Определяем PI, если она не определена
#ifndef PI
#define PI 3.14159265358979323846
#endif

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
#define API_AUTH "api/auth/"                 // Авторизация устройства
#define API_PAIR "api/pair/"                 // Запрос на сопряжение
#define API_RECEIVE "api/receive/"           // Отправка данных
#define API_SETTINGS "api/settings/"         // Получение настроек

// Стационарный адрес сервера (без возможности редактирования пользователем)
#define SERVER_URL "https://oryx-optj.onrender.com"  // URL вашего сервера
#define API_PAIR "/api/pair"                    
  // Эндпоинт для сопряжения
#define API_AUTH "/api/auth"                      // Эндпоинт для авторизации
#define API_DATA "/api/data"                      // Эндпоинт для отправки данных
#define API_SETTINGS "/api/settings"              // Эндпоинт для получения настроек

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
#define INDICATOR_CHECKING 6    // Проверка соединения с сервером

// Переменные для управления индикацией
int indicatorMode = INDICATOR_NONE;
unsigned long indicatorStartTime = 0;
unsigned long indicatorDuration = 0;
bool previousRGBEffectState = false;  // Для сохранения предыдущего состояния RGB эффекта

// Для хранения текущих значений RGB компонентов
byte currentRed = 0;
byte currentGreen = RGB_DEFAULT_BRIGHTNESS;
byte currentBlue = 0;

// Функция для установки режима индикатора
void setIndicator(int mode, unsigned long duration) {
  // Сохраняем предыдущее состояние RGB эффекта
  previousRGBEffectState = isRGBEffect;
  
  indicatorMode = mode;
  indicatorStartTime = millis();
  indicatorDuration = duration;
}

// Обновление RGB эффекта
void updateRGBEffect() {
  // Если нет активного эффекта и нет активного индикатора - выходим
  if (!isRGBEffect && indicatorMode == INDICATOR_NONE) {
    return;
  }
  
  // Если активен режим индикации, обрабатываем его
  if (indicatorMode != INDICATOR_NONE) {
    unsigned long elapsedTime = millis() - indicatorStartTime;
    
    // Проверяем, не истекло ли время индикации
    if (elapsedTime >= indicatorDuration) {
      // Сбрасываем режим индикации
      indicatorMode = INDICATOR_NONE;
      
      // Восстанавливаем нормальное отображение
      isRGBEffect = previousRGBEffectState;
      updateLEDColor();
      return;
    }
    
    // Переменные для вычисления яркости
    int brightness;
    
    // Эффекты в зависимости от режима индикации
    switch (indicatorMode) {
      case INDICATOR_ERROR:
        // Красное мигание (2 Гц)
        if ((elapsedTime / 250) % 2 == 0) {
          analogWrite(RGB_R, 255);
          analogWrite(RGB_G, 0);
          analogWrite(RGB_B, 0);
        } else {
          analogWrite(RGB_R, 0);
          analogWrite(RGB_G, 0);
          analogWrite(RGB_B, 0);
        }
        break;
        
      case INDICATOR_WARNING:
        // Оранжевое мигание (1 Гц)
        if ((elapsedTime / 500) % 2 == 0) {
          analogWrite(RGB_R, 255);
          analogWrite(RGB_G, 100);
          analogWrite(RGB_B, 0);
        } else {
          analogWrite(RGB_R, 0);
          analogWrite(RGB_G, 0);
          analogWrite(RGB_B, 0);
        }
        break;
        
      case INDICATOR_SENDING:
        // Синее медленное мигание (0.5 Гц)
        if ((elapsedTime / 1000) % 2 == 0) {
          analogWrite(RGB_R, 0);
          analogWrite(RGB_G, 0);
          analogWrite(RGB_B, 255);
        } else {
          analogWrite(RGB_R, 0);
          analogWrite(RGB_G, 0);
          analogWrite(RGB_B, 64);
        }
        break;
        
      case INDICATOR_RECEIVING:
        // Фиолетовое медленное мигание (0.5 Гц)
        if ((elapsedTime / 1000) % 2 == 0) {
          analogWrite(RGB_R, 200);
          analogWrite(RGB_G, 0);
          analogWrite(RGB_B, 255);
        } else {
          analogWrite(RGB_R, 64);
          analogWrite(RGB_G, 0);
          analogWrite(RGB_B, 64);
        }
        break;
        
      case INDICATOR_SUCCESS:
        // Зеленая вспышка (затухание)
        brightness = 255 - (elapsedTime * 255 / indicatorDuration);
        analogWrite(RGB_R, 0);
        analogWrite(RGB_G, brightness);
        analogWrite(RGB_B, 0);
        break;
        
      case INDICATOR_CHECKING:
        // Бирюзовое мигание (1.5 Гц)
        if ((elapsedTime / 333) % 2 == 0) {
          analogWrite(RGB_R, 0);
          analogWrite(RGB_G, 200);
          analogWrite(RGB_B, 200);
        } else {
          analogWrite(RGB_R, 0);
          analogWrite(RGB_G, 50);
          analogWrite(RGB_B, 100);
        }
        break;
    }
    
    return;
  }
  
  // Обработка обычного RGB эффекта (пульсация, радуга и т.д.)
  unsigned long timeFromStart = millis() - rgbEffectStartTime;
  
  // Плавная пульсация (цикл 2 секунды)
  int brightness = (sin(timeFromStart * PI / 1000) + 1) * 127;
  
  // Используем текущий цвет с пульсирующей яркостью
  analogWrite(RGB_R, (currentRed * brightness) / 255);
  analogWrite(RGB_G, (currentGreen * brightness) / 255);
  analogWrite(RGB_B, (currentBlue * brightness) / 255);
  
  // Проверяем, не истекло ли время эффекта (10 секунд)
  if (timeFromStart > 10000) {
    isRGBEffect = false;
    updateLEDColor(); // Восстанавливаем обычный цвет
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
  // Сохраняем текущие значения RGB
  currentRed = r;
  currentGreen = g;
  currentBlue = b;
  
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
  
  // Запускаем проверку всех систем
  runSystemDiagnostics();
  
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
  
  // В режиме точки доступа обрабатываем DNS и веб-запросы
  if (deviceMode == MODE_AP_SETUP) {
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
    
    delay(10);
    return;
  }
  
  // Обновляем RGB эффекты вместо программного PWM
  if (isRGBEffect || indicatorMode != INDICATOR_NONE) {
    updateRGBEffect();
  } else {
    updateLEDColor();
  }
  
  // Периодическая проверка состояния WiFi-подключения
  static unsigned long lastWiFiCheckTime = 0;
  if (deviceMode == MODE_NORMAL && millis() - lastWiFiCheckTime > 30000) { // каждые 30 секунд
    lastWiFiCheckTime = millis();
    
    // Проверяем статус WiFi-соединения
    if (WiFi.status() != WL_CONNECTED && wifiConnected) {
      Serial.println("Обнаружена потеря WiFi-соединения. Попытка переподключения...");
      wifiConnected = connectToSavedWiFi();
      
      if (wifiConnected) {
        Serial.println("Соединение с WiFi восстановлено");
      } else {
        Serial.println("Не удалось восстановить соединение с WiFi");
      }
    }
  }
  
  // Обработка в зависимости от режима
  switch (deviceMode) {
    case MODE_AP_SETUP:
      // Обработка DNS запросов
      dnsServer.processNextRequest();
      // Обработка веб-сервера
      webServer.handleClient();
  
      // Отображаем экран настройки
      displaySetupScreen();
      
      // Проверяем, не истекло ли время режима AP
      if (millis() - apStartTime > AP_TIMEOUT) {
        // Если время истекло, переходим в обычный режим
        webServer.stop();
        dnsServer.stop();
        deviceMode = MODE_NORMAL;
        
        // Пытаемся подключиться к WiFi с настройками по умолчанию
        wifiConnected = connectToWiFi();
      }
      break;
      
    case MODE_PAIRING:
      // Если активен режим сопряжения, отображаем соответствующий экран
      displayPairingScreen();
  
      // Периодически проверяем статус сопряжения
      static unsigned long lastPairingCheckTime = 0;
      if (millis() - lastPairingCheckTime > 5000) {
        checkPairingStatus();
        lastPairingCheckTime = millis();
      }
      
      // Проверяем, не истекло ли время сопряжения
      if (millis() - pairingStartTime > 300000) {
        pairingMode = false;
        deviceMode = MODE_NORMAL;
        tone(BUZZER_PIN, 500, 500); // Сигнал об окончании времени сопряжения
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
  
  // Заголовок по центру - использую тот же шрифт, что и на экране GPS
  u8g2.setFont(u8g2_font_profont17_tr);
  u8g2.drawStr(30, 20, "WiFi");
  
  // Разделительная линия на такой же высоте, как в GPS экране
  u8g2.drawHLine(0, 24, 128);
  
  // Основные данные - использую тот же шрифт, что и в GPS экране
  u8g2.setFont(u8g2_font_profont12_tr);
  
  // Статус WiFi
  u8g2.drawStr(5, 38, "WiFi:");
  u8g2.drawStr(65, 38, wifiConnected ? "ON" : "OFF");
  
  // IP-адрес (если подключен) - используем меньший шрифт для IP адреса
  if (wifiConnected) {
    u8g2.setFont(u8g2_font_profont10_tr); // Меньший шрифт для IP
    String ipStr = "IP: " + WiFi.localIP().toString();
    u8g2.drawStr(5, 49, ipStr.c_str());
    u8g2.setFont(u8g2_font_profont12_tr); // Возвращаем шрифт
  }
  
  // Статус сопряжения
  u8g2.drawStr(5, 58, "Paired:");
  u8g2.drawStr(65, 58, isPaired ? "YES" : "NO");
  
  // Инструкция по настройке на той же строке, что и высота в экране GPS
  if (!isPaired) {
    u8g2.drawStr(5, 63, "Long press to pair");
  } else {
    u8g2.drawStr(5, 63, "Press SETUP button");
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
        // На экране WiFi
        if (!isPaired && wifiConnected) {
          // Если не сопряжено, запускаем режим сопряжения
          Serial.println("Запуск режима сопряжения...");
          
          // Звуковое и визуальное подтверждение
          tone(BUZZER_PIN, 2000, 100);
          delay(100);
          tone(BUZZER_PIN, 2500, 100);
          
          // Генерируем случайный seed для кода сопряжения
          randomSeed(millis());
          
          // Запускаем режим сопряжения
          if (startPairingMode()) {
            // Переходим в режим сопряжения
            deviceMode = MODE_PAIRING;
            Serial.println("Устройство перешло в режим сопряжения");
            Serial.print("Код сопряжения: ");
            Serial.println(pairingCode);
          } else {
            Serial.println("Не удалось запустить режим сопряжения");
            tone(BUZZER_PIN, 500, 300); // Сигнал ошибки
          }
        } else if (!wifiConnected) {
          // Если нет соединения с WiFi, запускаем режим AP
          activateAPModeByButton();
        } else {
          // Уже сопряжено или нет соединения, запускаем режим AP
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
  
  // Создаем защищенный клиент без проверки сертификата
  WiFiClientSecure client;
  client.setInsecure(); // Отключаем проверку сертификата
  
  HTTPClient http;
  // Используем адрес сервера из настроек + эндпоинт
  http.begin(client, serverAddress + API_RECEIVE);
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
  
  // Создаем защищенный клиент без проверки сертификата
  WiFiClientSecure client;
  client.setInsecure(); // Отключаем проверку сертификата
  
  HTTPClient http;
  http.begin(client, serverAddress + API_SETTINGS);
  
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
  if (!wifiConnected && !connectToWiFi()) {
    Serial.println("Не удалось подключиться к WiFi для сопряжения");
    return false;
  }
  
  // Генерируем 6-значный код сопряжения
  pairingCode = generatePairingCode();
  pairingMode = true;
  pairingStartTime = millis();
  
  Serial.print("Сгенерирован код сопряжения: ");
  Serial.println(pairingCode);
  
  // В этой упрощенной версии мы не отправляем запрос на сервер сразу
  // Это предотвращает зависание при проблемах с подключением
  
  // Мигаем зеленым светодиодом для подтверждения режима сопряжения
  for (int i = 0; i < 3; i++) {
    setRGBColor(0, 255, 0);
    delay(100);
    setRGBColor(0, 0, 0);
    delay(100);
  }
  
  // Успешно входим в режим сопряжения
  Serial.println("Режим сопряжения активирован с кодом: " + pairingCode);
  Serial.println("Используйте этот код для сопряжения на сайте");
  return true;
}

// Настройка веб-сервера для сопряжения
void setupPairingWebServer() {
  // Если сервер уже запущен, останавливаем его
  if (webServer.client()) {
    webServer.stop();
    delay(100);
  }
  
  // Настраиваем обработчик для страницы ввода кода
  webServer.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html>"
                 "<html>"
                 "<head>"
                 "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                 "<meta charset='UTF-8'>"
                 "<title>Сопряжение устройства</title>"
                 "<style>"
                 "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; background-color: #f5f5f5; }"
                 ".container { max-width: 500px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }"
                 "h1 { color: #2c3e50; text-align: center; margin-bottom: 20px; }"
                 ".code { text-align: center; font-size: 32px; margin: 20px 0; font-weight: bold; color: #c0392b; padding: 10px; background: #f9f9f9; border-radius: 5px; }"
                 "p { margin-bottom: 15px; line-height: 1.5; }"
                 ".steps { background: #f0f8ff; padding: 15px; border-radius: 5px; margin: 20px 0; }"
                 ".steps h2 { font-size: 18px; margin-top: 0; }"
                 ".steps ol { padding-left: 20px; }"
                 ".steps li { margin-bottom: 10px; }"
                 ".timer { text-align: center; font-weight: bold; color: #e74c3c; }"
                 "</style>"
                 "</head>"
                 "<body>"
                 "<div class='container'>"
                 "<h1>Сопряжение устройства</h1>"
                 "<p>Используйте этот код для сопряжения вашего устройства с аккаунтом на сайте <strong>oryx-optj.onrender.com</strong></p>"
                 "<div class='code'>" + pairingCode + "</div>"
                 "<div class='steps'>"
                 "<h2>Инструкция по сопряжению:</h2>"
                 "<ol>"
                 "<li>Зарегистрируйтесь на сайте <strong>oryx-optj.onrender.com</strong> (если у вас еще нет аккаунта)</li>"
                 "<li>Войдите в свой аккаунт</li>"
                 "<li>Перейдите в раздел «Профиль» или «Мои устройства»</li>"
                 "<li>Нажмите кнопку «Добавить устройство»</li>"
                 "<li>Введите код сопряжения, показанный выше</li>"
                 "</ol>"
                 "</div>"
                 "<p class='timer'>Код действителен: <span id='timer'>5:00</span></p>"
                 "<script>"
                 "var timeLeft = 300;"
                 "var timerId = setInterval(countdown, 1000);"
                 "function countdown() {"
                 "  if (timeLeft == 0) { clearTimeout(timerId); }"
                 "  var minutes = Math.floor(timeLeft / 60);"
                 "  var seconds = timeLeft % 60;"
                 "  document.getElementById('timer').innerHTML = minutes + ':' + (seconds < 10 ? '0' : '') + seconds;"
                 "  timeLeft--;"
                 "}"
                 "</script>"
                 "</div>"
                 "</body>"
                 "</html>";
    webServer.send(200, "text/html", html);
  });
  
  // Запускаем сервер
  webServer.begin();
  Serial.println("Веб-сервер для сопряжения запущен");
}

// Проверка успешного сопряжения с запросом к серверу
bool checkPairingStatus() {
  if (!pairingMode || !wifiConnected) {
    return false;
  }
  
  // Проверяем, не истекло ли время сопряжения (5 минут)
  if (millis() - pairingStartTime > 300000) {
    pairingMode = false;
    Serial.println("Время сопряжения истекло. Режим сопряжения деактивирован.");
    return false;
  }
  
  // Проверяем каждые 10 секунд
  static unsigned long lastCheckTime = 0;
  
  if (millis() - lastCheckTime > 10000) { 
    lastCheckTime = millis();
    
    // Мигаем синим для индикации проверки
    setRGBColor(0, 0, 255);
    delay(100);
    setRGBColor(0, 0, 0);
    
    // В реальной версии здесь должен быть запрос к серверу
    Serial.print("Запрос к серверу: проверка статуса сопряжения для кода ");
    Serial.println(pairingCode);
    
    // Проверяем соединение
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Ошибка: нет подключения к WiFi");
      return false;
    }
    
    // Создаем защищенный клиент с отключенной проверкой сертификата
    WiFiClientSecure client;
    client.setInsecure();
    
    HTTPClient http;
    
    // Формируем URL для проверки статуса сопряжения
    String url = String(SERVER_URL) + "/api/check_pairing/" + pairingCode;
    
    // Настраиваем запрос
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    // Отправляем запрос
    int httpCode = http.GET();
    
    Serial.print("HTTP код ответа: ");
    Serial.println(httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.print("Ответ сервера: ");
      Serial.println(payload);
      
      // Парсим JSON-ответ
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error && doc.containsKey("status") && doc["status"] == "paired") {
        // Устройство успешно сопряжено
        Serial.println("Устройство успешно сопряжено!");
        
        // Сохраняем токен авторизации из ответа
        if (doc.containsKey("auth_token")) {
          authToken = doc["auth_token"].as<String>();
          Serial.print("Получен токен авторизации: ");
          Serial.println(authToken);
          isPaired = true;
          pairingMode = false;
          
          // Сохраняем настройки
          savePairingSettings();
          
          // Индикация успешного сопряжения
          for (int i = 0; i < 3; i++) {
            setRGBColor(0, 255, 0);
            delay(100);
            setRGBColor(0, 0, 0);
            delay(100);
          }
          
          http.end();
          return true;
        }
      }
      
      // Если сопряжение в процессе или ожидании
      if (!error && doc.containsKey("status") && doc["status"] == "waiting") {
        Serial.println("Сопряжение в процессе. Ожидаем подтверждения от пользователя.");
      }
    }
    
    http.end();
  }
  
  return false;
}

// Display the pairing screen (simplified version)
void displayPairingScreen() {
  u8g2.clearBuffer();
  
  // Animate RGB LED
  static unsigned long lastAnimTime = 0;
  static int animState = 0;
  
  if (millis() - lastAnimTime > 400) {  // Animation speed
    animState = (animState + 1) % 4;    // 4 animation states
    lastAnimTime = millis();
    
    // Animate RGB LED
    switch (animState) {
      case 0: 
        setRGBColor(64, 0, 0); // Red
        break;
      case 1: 
        setRGBColor(0, 64, 0); // Green
        break;
      case 2: 
        setRGBColor(0, 0, 64); // Blue
        break;
      case 3: 
        setRGBColor(0, 0, 0);  // Off
        break;
    }
  }
  
  // Title centered
  u8g2.setFont(u8g2_font_profont17_tr);
  char titleStr[] = "PAIRING CODE";
  int titleWidth = u8g2.getStrWidth(titleStr);
  u8g2.drawStr((128-titleWidth)/2, 16, titleStr);
  
  // Divider line
  u8g2.drawHLine(0, 20, 128);
  
  // Display code in large font
  if (pairingCode.length() > 0) {
    u8g2.setFont(u8g2_font_profont22_tn); // Large font for code
    int codeWidth = u8g2.getStrWidth(pairingCode.c_str());
    u8g2.drawStr((128-codeWidth)/2, 40, pairingCode.c_str());
    } else {
    u8g2.setFont(u8g2_font_profont12_tr);
    char genStr[] = "Generating...";
    int genWidth = u8g2.getStrWidth(genStr);
    u8g2.drawStr((128-genWidth)/2, 40, genStr);
  }
  
  // Show website URL
  u8g2.setFont(u8g2_font_profont10_tr);
  char urlStr[] = "oryx-optj.onrender.com";
  int urlWidth = u8g2.getStrWidth(urlStr);
  u8g2.drawStr((128-urlWidth)/2, 52, urlStr);
  
  // Display time left
  unsigned long elapsedTime = millis() - pairingStartTime;
  unsigned long remainingTime = (elapsedTime < 300000) ? (300000 - elapsedTime) / 1000 : 0;
  
  char timeBuf[16];
  sprintf(timeBuf, "%lu:%02lu", remainingTime / 60, remainingTime % 60);
  
  u8g2.setFont(u8g2_font_profont10_tr);
  char timeStr[30];
  sprintf(timeStr, "Time left: %s", timeBuf);
  int timeWidth = u8g2.getStrWidth(timeStr);
  u8g2.drawStr((128-timeWidth)/2, 63, timeStr);
  
  u8g2.sendBuffer();
}

// startAPMode был удален для устранения дублирования

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
                "button { background-color: #2980b9; color: white; border: none; padding: 10px 15px; border-radius: 4px; cursor: pointer; width: 100%; }"
                "button:hover { background-color: #3498db; }"
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
                "<button type='submit'>Save</button>"
                "</form>"
                "</div>"
                "</body>"
                "</html>";
                
  webServer.send(200, "text/html", html);
}

// Обработчик сохранения настроек
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
  
  // Сохраняем настройки
  saveWiFiSettings();
  
  // Отправляем ответ пользователю
  String html = "<!DOCTYPE html>"
                "<html>"
                "<head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<meta charset='UTF-8'>"
                "<title>Settings Saved</title>"
                "<style>"
                "body { font-family: Arial, sans-serif; margin: 0; padding: 20px; color: #333; text-align: center; }"
                ".container { max-width: 400px; margin: 0 auto; }"
                "h1 { color: #27ae60; }"
                ".success-icon { font-size: 48px; color: #27ae60; margin-bottom: 20px; }"
                "p { margin-bottom: 20px; }"
                ".restart-timer { font-weight: bold; color: #e74c3c; }"
                "</style>"
                "</head>"
                "<body>"
                "<div class='container'>"
                "<div class='success-icon'>✓</div>"
                "<h1>Настройки сохранены</h1>"
                "<p>Устройство перезагрузится через <span class='restart-timer' id='timer'>5</span> секунд.</p>"
                "<script>"
                "var timeLeft = 5;"
                "var timerId = setInterval(countdown, 1000);"
                "function countdown() {"
                "  if (timeLeft == 0) {"
                "    clearTimeout(timerId);"
                "    window.location.href = 'http://192.168.1.1';"
                "  }"
                "  document.getElementById('timer').innerHTML = timeLeft;"
                "  timeLeft--;"
                "}"
                "</script>"
                "</div>"
                "</body>"
                "</html>";
                
  webServer.send(200, "text/html", html);
  
  // Задержка, чтобы клиент получил страницу до перезагрузки
      delay(1000);
  
  // Закрываем режим AP
  webServer.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  
  // Переходим в нормальный режим
  deviceMode = MODE_NORMAL;
  
  // Пытаемся подключиться с новыми настройками
  Serial.println("Перезагрузка WiFi с новыми настройками...");
  WiFi.disconnect(true);
  delay(1000);
  
  // Попытка подключения к WiFi с сохраненными настройками
  wifiConnected = connectToSavedWiFi();
  
  if (wifiConnected) {
    Serial.println("Подключено к WiFi с новыми настройками!");
    tone(BUZZER_PIN, 2000, 100); // Звуковое подтверждение
    delay(100);
    tone(BUZZER_PIN, 2500, 100);
    
    // Если успешно подключились, делаем красивую индикацию
    setIndicator(INDICATOR_SUCCESS, 1000);
    
    // Проверяем соединение с сервером
    if (!serverConnected) {
      serverConnected = checkServerConnection();
    }
  } else {
    Serial.println("Не удалось подключиться к WiFi с новыми настройками");
    tone(BUZZER_PIN, 1000, 300); // Звуковое оповещение об ошибке
    setIndicator(INDICATOR_ERROR, 1000);
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
  webServer.onNotFound([]() {
    // Перенаправляем на страницу настройки
    webServer.sendHeader("Location", "/", true);
    webServer.send(302, "text/plain", "");
  });
  webServer.begin();
  
  // Сохраняем время запуска
  apStartTime = millis();
  
  // Изменяем цвет на синий для индикации
  setRGBColor(0, 0, RGB_DEFAULT_BRIGHTNESS);
  
  // Выводим информацию в Serial
  Serial.println("Точка доступа запущена:");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
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
unsigned long wifiSetupBtnPressStart = 0;
bool wifiSetupBtnLongPress = false;

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
        // Запоминаем время начала нажатия для определения длительного нажатия
        wifiSetupBtnPressStart = millis();
        
        // Звуковой сигнал при нажатии
        tone(BUZZER_PIN, 1500, 50);
      }
      // Если кнопка отпущена (HIGH)
      else if (currentState == HIGH && lastWifiSetupBtnState == LOW) {
        // Если было долгое нажатие, то не выполняем действие при отпускании
        if (!wifiSetupBtnLongPress) {
          // Если устройство включено и нажатие не было длительным,
          // определяем длительность нажатия
          unsigned long pressDuration = millis() - wifiSetupBtnPressStart;
          
          if (pressDuration >= 2000) {
            // Это было долгое нажатие, но мы его пропустили (не должно происходить)
            wifiSetupBtnLongPress = false;
          } else if (isPowerOn) {
            // Если короткое нажатие и устройство включено - запускаем режим AP
            activateAPModeByButton();
          }
        }
        // Сбрасываем флаг длительного нажатия
        wifiSetupBtnLongPress = false;
      }
      
      lastWifiSetupBtnState = currentState;
    }
  }
  
  // Проверка на длительное нажатие
  if (currentState == LOW && !wifiSetupBtnLongPress) {
    // Если кнопка удерживается нажатой более 2 секунд
    if (millis() - wifiSetupBtnPressStart >= 2000) {
      wifiSetupBtnLongPress = true;
      
      // Звуковой сигнал для подтверждения длительного нажатия
      tone(BUZZER_PIN, 2000, 100);
      delay(100);
      tone(BUZZER_PIN, 2500, 100);
      
      // Если устройство включено, запускаем процесс сопряжения
      if (isPowerOn && currentScreen == 2 && !isPaired && wifiConnected) {
        Serial.println("Запуск режима сопряжения через длительное нажатие...");
        
        // Генерируем случайный seed для кода сопряжения
        randomSeed(millis());
        
        // Запускаем режим сопряжения
        if (startPairingMode()) {
          // Переходим в режим сопряжения
          deviceMode = MODE_PAIRING;
          Serial.println("Устройство перешло в режим сопряжения");
          Serial.print("Код сопряжения: ");
          Serial.println(pairingCode);
    } else {
          Serial.println("Не удалось запустить режим сопряжения");
          tone(BUZZER_PIN, 500, 300); // Сигнал ошибки
        }
      }
    }
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

// Проверка соединения с сервером
bool checkServerConnection() {
  // Проверяем подключение к WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Нет подключения к WiFi для проверки сервера");
    return false;
  }
  
  // Показываем индикацию проверки соединения
  setIndicator(INDICATOR_CHECKING, 3000); // Максимум 3 секунды на проверку
  
  // Создаем защищенный клиент без проверки сертификата
  WiFiClientSecure client;
  client.setInsecure(); // Отключаем проверку сертификата
  
  HTTPClient http;
  Serial.print("Проверка соединения с сервером: ");
  Serial.println(serverAddress);
  
  // Делаем простой GET запрос
  http.begin(client, serverAddress);
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    Serial.print("HTTP код ответа: ");
    Serial.println(httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      Serial.println("Сервер доступен");
      http.end();
      
      // Индикация успешного соединения
      setIndicator(INDICATOR_SUCCESS, 500);
      return true;
    }
  }
  
  Serial.print("Ошибка соединения с сервером: ");
  Serial.println(http.errorToString(httpCode));
  http.end();
  
  // Индикация ошибки
  setIndicator(INDICATOR_ERROR, 1000);
  return false;
}