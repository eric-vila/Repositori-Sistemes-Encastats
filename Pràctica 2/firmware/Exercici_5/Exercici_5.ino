#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <DHT.h>

// --- CONFIGURACIÓ DEL SENSOR DHT22 ---
#define DHTPIN 8       // Pin on està connectat el DHT22
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- UUIDs BLE ---
#define SERVICE_UUID           "9bd46fc4-d6ab-4d64-81ee-1c0d3be13a3d"
#define CHAR_CREDENTIALS_UUID  "1009d074-8913-4371-8313-99958c49c265"
#define CHAR_ESTAT_UUID        "7e93a750-5e54-41a7-9532-8315e7921b63"
#define CHAR_TEMP_UUID         "c481a5a1-79e0-4107-9bb3-12543d2c3be9"

// IP de la teva Raspberry Pi i port de Node-RED
const char* serverUrl = "http://172.20.10.2:1880/api/telemetria";

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
unsigned long lastPostTime = 0;
unsigned int failedTransmissions = 0; // Comptador d'enviaments HTTP fallits

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
  M5.Display.println(" Config Node BLE");
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

  // Només mostra la temperatura si està OPERATIU o amb Bluetooth connectat
  if ((currentState == OPERATIU || deviceConnected) && tempActual > -100.0) {
    M5.Display.setTextColor(WHITE);
    M5.Display.print("Temp: ");
    M5.Display.setTextColor(GREEN);
    M5.Display.printf("%.1f C\n", tempActual);
  }
}

// Pantalla d'avís als 3 segons de premuda (Exercici 3)
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
          
          Serial.print("Rebut SSID: ");
          Serial.print(pendingSSID);
          Serial.print(" | Longitud clau: ");
          Serial.println(pendingPass.length());

          newCredentialsReceived = true;
        } else {
          Serial.println("Error: JSON de credencials invàlid.");
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

// --- EXERCICI 5: ENVIAMENT DE TELEMETRIA REST A NODE-RED ---
void sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (isnan(tempActual) || isnan(humActual) || tempActual < -100.0) {
    Serial.println("Error: Lectura del sensor no vàlida per a l'enviament de telemetria.");
    return;
  }

  // Generació del JSON de telemetria
  JsonDocument doc;
  doc["node_id"] = "ESP32_Eric";
  doc["temperatura"] = tempActual;
  doc["humitat"] = humActual;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;

  String jsonString;
  serializeJson(doc, jsonString);

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(jsonString);

  // Verificació i registre de codis d'estat
  if (httpCode > 0) {
    Serial.printf("HTTP Response code: %d\n", httpCode);
    if (httpCode != 201) { // 201 Created esperat de Node-RED
      failedTransmissions++;
      Serial.printf("Enviament rebutjat. Total fallits: %u\n", failedTransmissions);
    }
  } else {
    failedTransmissions++;
    Serial.printf("Error de connexió HTTP: %s. Total fallits: %u\n", http.errorToString(httpCode).c_str(), failedTransmissions);
  }

  http.end();
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.begin(115200);

  // Inicialització del sensor DHT22
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

  // Si hi ha credencials guardades a la NVS, intentar connectar
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

  // --- EXERCICI 3: RESTABLIMENT DE LA NVS I REINICI DE NODE (POLSACIÓ 5 SEGONS) ---
  if (M5.BtnA.pressedFor(5000)) {
    Serial.println("Polsació de 5s detectada: Esborrant NVS i reiniciant...");
    
    // 1. Esborra les credencials de la NVS
    preferences.clear();
    
    // 2. Desconnecta la Wi-Fi
    WiFi.disconnect(true, true);
    
    // 3. Mostra missatge de confirmació i reinicia el microcontrolador
    M5.Display.fillScreen(RED);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(10, 25);
    M5.Display.println("RESET NVS!");
    delay(1000);
    ESP.restart(); // Reinicia el node per tornar a SENSE CREDENCIALS
  } 
  else if (M5.BtnA.pressedFor(3000)) {
    if (!feedbackShown) {
      feedbackShown = true;
      mostraFeedbackReset(); // Realimentació visual als 3 segons
    }
  } 
  else if (M5.BtnA.wasReleased()) {
    if (feedbackShown) {
      feedbackShown = false;
      String ipStr = (currentState == OPERATIU) ? WiFi.localIP().toString() : "";
      setNodeState(currentState, ipStr);
    }
  }

  // --- EXERCICI 4: LECTURA DEL SENSOR DHT22 I NOTIFICACIÓ BLE ---
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

        // Notificar per BLE si hi ha un central connectat I ha canviat >= 0.1 ºC
        if (deviceConnected && (fabsf(currentFormattedTemp - lastSentTemp) >= 0.1f)) {
          lastSentTemp = currentFormattedTemp;

          pCharTemp->setValue(tempBuffer);
          pCharTemp->notify();

          Serial.printf("Notificat per BLE: %s ºC\n", tempBuffer);
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

  // --- EXERCICI 5: ENVIAMENT DE TELEMETRIA HTTP POST CADA 10 SEGONS (NOMÉS EN OPERATIU) ---
  if (currentState == OPERATIU && (now - lastPostTime >= 10000)) {
    lastPostTime = now;
    sendTelemetry();
  }

  // --- GESTIÓ DE CREDENCIALS REBUDES PER BLE ---
  if (newCredentialsReceived) {
    newCredentialsReceived = false;
    uint32_t startTime = millis();

    if (tryWiFiConnect(pendingSSID, pendingPass)) {
      uint32_t elapsedTime = millis() - startTime;
      Serial.printf("Connexió completada en %u ms\n", elapsedTime);

      preferences.putString("ssid", pendingSSID);
      preferences.putString("pass", pendingPass);

      setNodeState(OPERATIU, WiFi.localIP().toString());
    } else {
      Serial.println("Han fallat els 3 intents. Tornant a SENSE CREDENCIALS.");
      setNodeState(SENSE_CREDENCIALS);
    }
  }

  delay(50);
}