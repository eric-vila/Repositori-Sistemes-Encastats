#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>
#include <PubSubClient.h> // Llibreria oficial per a MQTT[cite: 4]

// --- CONFIGURACIÓ DEL SENSOR DHT22 ---
#define DHTPIN 8       
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- CONFIGURACIÓ MQTT ---[cite: 5]
const char* mqtt_server = "10.11.3.145"; // IP de la teva Raspberry Pi
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient mqtt(espClient);

// Variables per al Client ID i Temes MQTT
char clientId[32];

// Temes dissenyats segons el P-PRE3 / Apunts M7[cite: 2, 7]
#define BASE_TOPIC "uvic/eric/node01"
const char* T_STATUS = BASE_TOPIC "/status";
const char* T_CMD    = BASE_TOPIC "/cmd/#";
const char* T_TEL    = BASE_TOPIC "/telemetria";

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

// Definició de la màquina d'estats
enum State { SENSE_CREDENCIALS, CONNECTANT, OPERATIU };
State currentState = SENSE_CREDENCIALS;

String pendingSSID = "";
String pendingPass = "";
bool newCredentialsReceived = false;
bool feedbackShown = false;

// Variables per al control de temps i sensors
unsigned long lastDHTReadTime = 0;
unsigned long lastMqttSendTime = 0;

float tempActual = -999.0;
float humActual = -999.0;
float lastSentTemp = -999.0;

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

// --- GENERAR ID ÚNIC I GESTIÓ MQTT ---[cite: 2, 7]

// 1. Funció per agafar la MAC de la placa i crear un ID estable i únic
void generateClientId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(clientId, sizeof(clientId), "ESP32_%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("MQTT Client ID Únic: ");
  Serial.println(clientId);
}

// 2. Funció de connexió inicial / reconnexió al broker MQTT amb Last Will (LWT)
bool mqttConnect() {
  // Configuració de Last Will: si el node cau, el broker publica "offline" (retain = true)[cite: 7]
  bool ok = mqtt.connect(clientId, NULL, NULL, T_STATUS, 1, true, "offline"); 
  
  if (ok) {
    Serial.println("Connectat al broker Mosquitto!");
    // Publicar "online" com a estat retingut[cite: 7]
    mqtt.publish(T_STATUS, "online", true); 
    // Subscriure's a les ordres amb QoS 1[cite: 7]
    mqtt.subscribe(T_CMD, 1); 
  } else {
    Serial.print("Error de connexió MQTT, codi=");
    Serial.println(mqtt.state());
  }
  return ok;
}

// 3. Manteniment de connexió MQTT no bloquejant amb backoff[cite: 7]
void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    static uint32_t nextTry = 0;
    if (millis() >= nextTry) {
      if (mqttConnect()) {
        nextTry = 0;
      } else {
        nextTry = millis() + 5000; // Reintentar d'aquí a 5 segons[cite: 7]
      }
    }
  } else {
    mqtt.loop(); // Processar el manteniment de canal i missatges entrants[cite: 7]
  }
}

// 4. Publicar la telemetria en format JSON per MQTT[cite: 2, 7]
void sendTelemetryMQTT() {
  if (!mqtt.connected()) return;

  if (isnan(tempActual) || isnan(humActual) || tempActual < -100.0) {
    Serial.println("Error: Lectura del sensor no vàlida per a la telemetria.");
    return;
  }

  // Creació del payload JSON
  JsonDocument doc;
  doc["t"] = tempActual;             // Temperatura (ºC)
  doc["h"] = humActual;             // Humitat (%)
  doc["uptime"] = millis() / 1000;  // Segons en marxa

  String jsonString;
  serializeJson(doc, jsonString);

  // La telemetria mai no es reté (retain = false)[cite: 7]
  mqtt.publish(T_TEL, jsonString.c_str(), false);

  Serial.print("Telemetria enviada per MQTT: ");
  Serial.println(jsonString);
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

  // Configurar adreça del broker i generar Client ID[cite: 7]
  generateClientId(); 
  mqtt.setServer(mqtt_server, mqtt_port);

  // Intents de connexió Wi-Fi inicials
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

  // Manteniment MQTT quan s'està en estat OPERATIU[cite: 7]
  if (currentState == OPERATIU) {
    mqttLoop();
  }

  // Restabliment de la NVS amb polsació de 5 segons del botó A
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

  // Enviament de telemetria per MQTT cada 10 segons (només en estat OPERATIU)[cite: 2]
  if (currentState == OPERATIU && (now - lastMqttSendTime >= 10000)) {
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