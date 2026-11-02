#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <PubSubClient.h> // Llibreria oficial per a MQTT

// --- CONFIGURACIÓ DEL SENSOR DHT22 ---
#define DHTPIN 8       
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- CONFIGURACIÓ MQTT ---
const char* mqtt_server = "10.11.2.79"; // IP de la teva Raspberry Pi
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqtt(espClient);

// Variables per al Client ID i Temes MQTT
char clientId[32];

// Temes dissenyats segons el P-PRE3 / Exercici 3, 4, 5 i 6
#define BASE_TOPIC "uvic/eric/node01"
const char* T_STATUS     = BASE_TOPIC "/status";
const char* T_STATUS_TX  = BASE_TOPIC "/status/tx";  // [EXERCICI 5] Tema d'estat de transmissió retingut
const char* T_CMD_CONFIG = BASE_TOPIC "/cmd/config";
const char* T_ACK_PERIOD = BASE_TOPIC "/ack/period";
const char* T_TEL        = BASE_TOPIC "/telemetria";

// [AFEGIT EXERCICI 6] Temes per a les alarmes d'esdeveniment
const char* T_ALARM_TEMP = BASE_TOPIC "/alarm/temp";
const char* T_ALARM_HUM  = BASE_TOPIC "/alarm/hum";

// --- UUIDs BLE ---
#define SERVICE_UUID           "9bd46fc4-d6ab-4d64-81ee-1c0d3be13a3d"
#define CHAR_CREDENTIALS_UUID  "1009d074-8913-4371-8313-99958c49c265"
#define CHAR_ESTAT_UUID        "7e93a750-5e54-41a7-9532-8315e7921b63"
#define CHAR_TEMP_UUID         "c481a5a1-79e0-4107-9bb3-12543d2c3be9"

Preferences preferences;

// Variables globals BLE
BLECharacteristic* pCharEstat = nullptr;
BLECharacteristic* pCharTemp = nullptr;
bool deviceConnected = false;

// Definició de la màquina d'estats de connexió
enum State { SENSE_CREDENCIALS, CONNECTANT, OPERATIU };
State currentState = SENSE_CREDENCIALS;

String pendingSSID = "";
String pendingPass = "";
bool newCredentialsReceived = false;
bool feedbackShown = false;

// Variables per al control de temps, sensors i telemetria
unsigned long lastDHTReadTime = 0;
unsigned long lastMqttSendTime = 0;
unsigned long telemetryPeriodMs = 10000; // [EXERCICI 4] Període dinàmic (10s per defecte)
bool isTransmitting = true;              // [EXERCICI 5] Control d'enviament de telemetria

float tempActual = -999.0;
float humActual = -999.0;
float lastSentTemp = -999.0;

// [AFEGIT EXERCICI 6] Llindars de configuració i control d'estat d'alarmes
float tempMin = 10.0;
float tempMax = 35.0;
float humMin  = 20.0;
float humMax  = 80.0;

bool alarmTempActive = false;
bool alarmHumActive  = false;

// Callbacks de connexió BLE
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
      Serial.println("Mòbil connectat per BLE.");
    }

    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      Serial.println("Mòbil desconnectat del BLE. Reprenent anunci...");
      pServer->getAdvertising()->start();
    }
};

// Dibuixar la interfície en pantalla M5
void actualitzaPantalla(const String& estatStr, const String& extraInfo = "") {
  // [AFEGIT EXERCICI 6] Si hi ha qualsevol alarma activa, dibuixar pantalla d'ALERTA amb fons vermell
  if (alarmTempActive || alarmHumActive) {
    M5.Display.fillScreen(RED);
    M5.Display.setTextSize(1.5);
    M5.Display.setTextColor(WHITE, RED);
    M5.Display.setCursor(0, 10);
    M5.Display.println("  ¡¡ ALERTA !!");
    M5.Display.println("----------------");

    if (alarmTempActive) {
      M5.Display.printf("TEMP: %.1f C\n", tempActual);
      M5.Display.printf("Rang: %.0f-%.0f C\n\n", tempMin, tempMax);
    }
    if (alarmHumActive) {
      M5.Display.printf("HUM:  %.1f %%\n", humActual);
      M5.Display.printf("Rang: %.0f-%.0f %%\n", humMin, humMax);
    }
    return;
  }

  // Pantalla normal (fons negre) quan no hi ha cap alarma
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(1.3);
  M5.Display.setCursor(0, 5);

  M5.Display.setTextColor(YELLOW);
  M5.Display.println(" Config Node BLE/MQTT");
  M5.Display.println("----------------");

  M5.Display.setTextColor(WHITE);
  M5.Display.print("Estat: ");
  
  if (currentState == OPERATIU) {
    M5.Display.setTextColor(GREEN);
  } else if (currentState == CONNECTANT) {
    M5.Display.setTextColor(ORANGE);
  } else {
    M5.Display.setTextColor(RED);
  }
  
  M5.Display.println(estatStr);

  if (extraInfo.length() > 0) {
    M5.Display.setTextColor(WHITE);
    M5.Display.print("IP: ");
    M5.Display.setTextColor(CYAN);
    M5.Display.println(extraInfo);
  }

  if ((currentState == OPERATIU || deviceConnected) && tempActual > -100.0) {
    M5.Display.setTextColor(WHITE);
    M5.Display.print("Temp: ");
    M5.Display.setTextColor(GREEN);
    M5.Display.printf("%.1f C\n", tempActual);
  }

  // [EXERCICI 4] Mostrar el període d'enviament actual
  M5.Display.setTextColor(CYAN);
  M5.Display.printf("Període: %lu s\n", telemetryPeriodMs / 1000UL);

  // [EXERCICI 5] Mostrar l'estat de la transmissió (RUNNING / PAUSED)
  M5.Display.setCursor(0, 95);
  if (isTransmitting) {
    M5.Display.setTextColor(GREEN, BLACK);
    M5.Display.println("TX: RUNNING ");
  } else {
    M5.Display.setTextColor(RED, BLACK);
    M5.Display.println("TX: PAUSED  ");
  }
}

void mostraFeedbackReset() {
  M5.Display.fillScreen(RED);
  M5.Display.setTextSize(1.2);
  M5.Display.setTextColor(WHITE);
  M5.Display.setCursor(0, 10);
  M5.Display.println("  ATENCIO!");
  M5.Display.println("--------------");
  M5.Display.println("Mantingues 2s");
  M5.Display.println("mes per a");
  M5.Display.println("RESET NVS...");
}

void setNodeState(State newState, String extraInfo = "") {
  currentState = newState;
  String stateStr = "";

  switch (currentState) {
    case SENSE_CREDENCIALS:
      stateStr = "SENSE CREDENCIALS";
      break;
    case CONNECTANT:
      stateStr = "CONNECTANT";
      break;
    case OPERATIU:
      stateStr = "OPERATIU";
      break;
  }

  if (pCharEstat && deviceConnected) {
    pCharEstat->setValue(stateStr.c_str());
    pCharEstat->notify();
  }

  actualitzaPantalla(stateStr, extraInfo);
}

class CredentialsCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, rxValue);

        if (!error && doc.containsKey("ssid") && doc.containsKey("pass")) {
          pendingSSID = doc["ssid"].as<String>();
          pendingPass = doc["pass"].as<String>();
          newCredentialsReceived = true;
        }
      }
    }
};

bool tryWiFiConnect(const String& ssid, const String& pass) {
  setNodeState(CONNECTANT);
  
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Attempting Wi-Fi connection (%d/3)...\n", attempt);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 10000)) {
      delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
  }
  return false;
}

// --- GENERAR ID ÚNIC I GESTIÓ MQTT ---

void generateClientId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(clientId, sizeof(clientId), "ESP32_%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("MQTT Client ID Únic: ");
  Serial.println(clientId);
}

// [EXERCICI 6] Funció per comparar les lectures del DHT22 amb els llindars i gestionar alarmes
void comprovaLlindars() {
  if (tempActual < -100.0 || humActual < -100.0) return;

  bool estatAnteriorTemp = alarmTempActive;
  bool estatAnteriorHum  = alarmHumActive;

  // 1. Control d'alarma de Temperatura
  if (tempActual < tempMin || tempActual > tempMax) {
    alarmTempActive = true;
    if (!estatAnteriorTemp) {
      // Esdeveniment d'INICI d'alarma de temperatura
      JsonDocument doc;
      doc["status"] = "ALARM";
      doc["value"]  = tempActual;
      doc["min"]    = tempMin;
      doc["max"]    = tempMax;
      doc["msg"]    = "Temperatura fora de rang!";

      String payload;
      serializeJson(doc, payload);
      mqtt.publish(T_ALARM_TEMP, payload.c_str(), false);
      Serial.println("ALERTA ACTIVADA: Temperatura fora de rang!");
    }
  } else {
    alarmTempActive = false;
    if (estatAnteriorTemp) {
      // Esdeveniment de FI d'alarma de temperatura
      JsonDocument doc;
      doc["status"] = "OK";
      doc["value"]  = tempActual;
      doc["msg"]    = "Temperatura tornada al rang normal.";

      String payload;
      serializeJson(doc, payload);
      mqtt.publish(T_ALARM_TEMP, payload.c_str(), false);
      Serial.println("ALERTA DESACTIVADA: Temperatura normalitzada.");
    }
  }

  // 2. Control d'alarma d'Humitat
  if (humActual < humMin || humActual > humMax) {
    alarmHumActive = true;
    if (!estatAnteriorHum) {
      // Esdeveniment d'INICI d'alarma d'humitat
      JsonDocument doc;
      doc["status"] = "ALARM";
      doc["value"]  = humActual;
      doc["min"]    = humMin;
      doc["max"]    = humMax;
      doc["msg"]    = "Humitat fora de rang!";

      String payload;
      serializeJson(doc, payload);
      mqtt.publish(T_ALARM_HUM, payload.c_str(), false);
      Serial.println("ALERTA ACTIVADA: Humitat fora de rang!");
    }
  } else {
    alarmHumActive = false;
    if (estatAnteriorHum) {
      // Esdeveniment de FI d'alarma d'humitat
      JsonDocument doc;
      doc["status"] = "OK";
      doc["value"]  = humActual;
      doc["msg"]    = "Humitat tornada al rang normal.";

      String payload;
      serializeJson(doc, payload);
      mqtt.publish(T_ALARM_HUM, payload.c_str(), false);
      Serial.println("ALERTA DESACTIVADA: Humitat normalitzada.");
    }
  }

  // Refrescar pantalla si ha canviat l'estat d'alguna alerta
  if (estatAnteriorTemp != alarmTempActive || estatAnteriorHum != alarmHumActive) {
    String ipStr = (currentState == OPERATIU) ? WiFi.localIP().toString() : "";
    actualitzaPantalla("OPERATIU", ipStr);
  }
}

// [EXERCICI 4, 5 & 6] Callback per gestionar ordres i comandes de configuració
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.printf("Comanda rebuda al tema [%s]: %s\n", topic, message.c_str());

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);

  if (!error) {
    // 1. Configurar el període d'enviament (Exercici 4)
    if (doc.containsKey("param") && doc["param"] == "period" && doc.containsKey("val")) {
      if (doc["val"].is<int>()) {
        int nouPeriode = doc["val"].as<int>();
        if (nouPeriode >= 1 && nouPeriode <= 60) {
          telemetryPeriodMs = nouPeriode * 1000UL;
          mqtt.publish(T_ACK_PERIOD, "{\"status\":\"OK\"}", false);
        }
      }
    }

    // 2. Control de transmissió (Exercici 5)
    if (doc.containsKey("param") && doc["param"] == "tx_state" && doc.containsKey("val")) {
      String newState = doc["val"].as<String>();
      if (newState == "paused") {
        isTransmitting = false;
        mqtt.publish(T_STATUS_TX, "paused", true);
      } else if (newState == "running") {
        isTransmitting = true;
        mqtt.publish(T_STATUS_TX, "running", true);
      }
    }

    // 3. [EXERCICI 6] Configuració dels llindars d'alerta des de Node-RED
    if (doc.containsKey("param") && doc.containsKey("val")) {
      String param = doc["param"].as<String>();
      float val = doc["val"].as<float>();

      if (param == "temp_min") {
        tempMin = val;
        Serial.printf("Nou llindar Temp Min: %.1f C\n", tempMin);
      } else if (param == "temp_max") {
        tempMax = val;
        Serial.printf("Nou llindar Temp Max: %.1f C\n", tempMax);
      } else if (param == "hum_min") {
        humMin = val;
        Serial.printf("Nou llindar Hum Min: %.1f %%\n", humMin);
      } else if (param == "hum_max") {
        humMax = val;
        Serial.printf("Nou llindar Hum Max: %.1f %%\n", humMax);
      }

      // Re-comprovar immediatament els llindars amb el nou valor rebut
      comprovaLlindars();
    }

    String ipStr = (currentState == OPERATIU) ? WiFi.localIP().toString() : "";
    actualitzaPantalla("OPERATIU", ipStr);
  }
}

bool mqttConnect() {
  bool ok = mqtt.connect(clientId, NULL, NULL, T_STATUS, 1, true, "offline"); 
  
  if (ok) {
    Serial.println("Connectat al broker Mosquitto!");
    mqtt.publish(T_STATUS, "online", true); 
    mqtt.publish(T_STATUS_TX, isTransmitting ? "running" : "paused", true);

    // Subscriure's al tema de configuració amb QoS 1
    mqtt.subscribe(T_CMD_CONFIG, 1); 
  } else {
    Serial.print("Error de connexió MQTT, codi=");
    Serial.println(mqtt.state());
  }
  return ok;
}

void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    static uint32_t nextTry = 0;
    if (millis() >= nextTry) {
      if (mqttConnect()) {
        nextTry = 0;
      } else {
        nextTry = millis() + 5000;
      }
    }
  } else {
    mqtt.loop();
  }
}

void sendTelemetryMQTT() {
  if (!mqtt.connected()) return;

  if (isnan(tempActual) || isnan(humActual) || tempActual < -100.0) {
    Serial.println("Error: Lectura del sensor no vàlida per a la telemetria.");
    return;
  }

  JsonDocument doc;
  doc["t"] = tempActual;             
  doc["h"] = humActual;             
  doc["uptime"] = millis() / 1000;  

  String jsonString;
  serializeJson(doc, jsonString);

  mqtt.publish(T_TEL, jsonString.c_str(), false);

  Serial.printf("Telemetria enviada (%lus): %s\n", telemetryPeriodMs / 1000UL, jsonString.c_str());
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.begin(115200);

  dht.begin();

  preferences.begin("wifi_config", false);
  String savedSSID = preferences.getString("ssid", "");
  String savedPass = preferences.getString("pass", "");

  // Inicialització BLE
  BLEDevice::init("ESP32_Eric");
  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic* pCharCredentials = pService->createCharacteristic(
                                          CHAR_CREDENTIALS_UUID,
                                          BLECharacteristic::PROPERTY_WRITE
                                        );
  pCharCredentials->setCallbacks(new CredentialsCallbacks());

  pCharEstat = pService->createCharacteristic(
                 CHAR_ESTAT_UUID,
                 BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
               );
  pCharEstat->addDescriptor(new BLE2902());

  pCharTemp = pService->createCharacteristic(
                CHAR_TEMP_UUID,
                BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
              );
  pCharTemp->addDescriptor(new BLE2902());
  pCharTemp->setValue("0.0");

  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  // Configurar adreça del broker, callback i Client ID
  generateClientId(); 
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);

  if (savedSSID.length() > 0) {
    if (tryWiFiConnect(savedSSID, savedPass)) {
      setNodeState(OPERATIU, WiFi.localIP().toString());
      return;
    }
  }

  setNodeState(SENSE_CREDENCIALS);
}

void loop() {
  M5.update();
  unsigned long now = millis();

  if (currentState == OPERATIU) {
    mqttLoop();
  }

  // Restabliment de la NVS amb polsació de 5 segons
  if (M5.BtnA.pressedFor(5000)) {
    preferences.clear();
    WiFi.disconnect(true, true);
    
    M5.Display.fillScreen(RED);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(10, 25);
    M5.Display.println("RESET NVS!");
    delay(1000);
    ESP.restart();
  } 
  else if (M5.BtnA.pressedFor(3000)) {
    if (!feedbackShown) {
      feedbackShown = true;
      mostraFeedbackReset();
    }
  } 
  else if (M5.BtnA.wasReleased()) {
    if (feedbackShown) {
      feedbackShown = false;
      String ipStr = (currentState == OPERATIU) ? WiFi.localIP().toString() : "";
      setNodeState(currentState, ipStr);
    }
  }

  // Lectura del sensor DHT22 cada 2 segons
  if (now - lastDHTReadTime >= 2000) {
    lastDHTReadTime = now;

    if (currentState == OPERATIU || deviceConnected) {
      float t = dht.readTemperature();
      float h = dht.readHumidity();

      if (!isnan(t) && !isnan(h)) {
        tempActual = t;
        humActual = h;

        // [EXERCICI 6] Comprovar llindars a cada nova lectura del DHT22
        comprovaLlindars();

        char tempBuffer[10];
        snprintf(tempBuffer, sizeof(tempBuffer), "%.1f", tempActual);
        float currentFormattedTemp = atof(tempBuffer);

        if (deviceConnected && (fabsf(currentFormattedTemp - lastSentTemp) >= 0.1f)) {
          lastSentTemp = currentFormattedTemp;
          pCharTemp->setValue(tempBuffer);
          pCharTemp->notify();
        }

        String ipStr = (currentState == OPERATIU) ? WiFi.localIP().toString() : "";
        actualitzaPantalla((currentState == OPERATIU) ? "OPERATIU" : "SENSE CREDENCIALS", ipStr);
      }
    } else {
      tempActual = -999.0;
      humActual = -999.0;
      String ipStr = (currentState == OPERATIU) ? WiFi.localIP().toString() : "";
      actualitzaPantalla((currentState == OPERATIU) ? "OPERATIU" : "SENSE CREDENCIALS", ipStr);
    }
  }

  // Enviament de telemetria per MQTT quan isTransmitting és true
  if (currentState == OPERATIU && isTransmitting && (now - lastMqttSendTime >= telemetryPeriodMs)) {
    lastMqttSendTime = now;
    sendTelemetryMQTT();
  }

  // Gestió de credencials noves rebudes via BLE
  if (newCredentialsReceived) {
    newCredentialsReceived = false;

    if (tryWiFiConnect(pendingSSID, pendingPass)) {
      preferences.putString("ssid", pendingSSID);
      preferences.putString("pass", pendingPass);
      setNodeState(OPERATIU, WiFi.localIP().toString());
    } else {
      setNodeState(SENSE_CREDENCIALS);
    }
  }

  delay(10);
}