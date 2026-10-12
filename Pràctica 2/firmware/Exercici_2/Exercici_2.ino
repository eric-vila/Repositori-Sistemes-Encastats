#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUIDs BLE per a l'Exercici 1/2
#define SERVICE_UUID           "9bd46fc4-d6ab-4d64-81ee-1c0d3be13a3d"
#define CHAR_CREDENTIALS_UUID  "1009d074-8913-4371-8313-99958c49c265"
#define CHAR_ESTAT_UUID        "7e93a750-5e54-41a7-9532-8315e7921b63"

// Memòria NVS
Preferences preferences;

// Característiques BLE
BLECharacteristic* pCharEstat = nullptr;

// Màquina d'estats
enum State { SENSE_CREDENCIALS, CONNECTANT, OPERATIU };
State currentState = SENSE_CREDENCIALS;

String pendingSSID = "";
String pendingPass = "";
bool newCredentialsReceived = false;

// Funció per actualitzar la pantalla fent servir M5Unified de forma segura
void actualitzaPantalla(const String& estatStr, const String& extraInfo = "") {
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(1.5);
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
    M5.Display.println();
    M5.Display.print("IP: ");
    M5.Display.setTextColor(CYAN);
    M5.Display.println(extraInfo);
  }
}

// Canvia l'estat global, notifica per BLE i actualitza la pantalla
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

  // Notificar per BLE
  if (pCharEstat) {
    pCharEstat->setValue(stateStr.c_str());
    pCharEstat->notify();
  }

  // Refrescar interfície gràfica
  actualitzaPantalla(stateStr, extraInfo);
}

// Callback BLE per rebre les credencials
class CredentialsCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, rxValue);

        if (!error && doc.containsKey("ssid") && doc.containsKey("pass")) {
          pendingSSID = doc["ssid"].as<String>();
          pendingPass = doc["pass"].as<String>();
          
          // Imprimir només SSID i longitud de la clau per complir els requeriments de seguretat
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

// Funció d'intent de connexió Wi-Fi amb límit d'intents
bool tryWiFiConnect(const String& ssid, const String& pass) {
  setNodeState(CONNECTANT);
  
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Intent de connexió Wi-Fi (%d/3)...\n", attempt);
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

void setup() {
  // Inicialització correcta del hardware M5Core2 / M5Stick
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.begin(115200);

  // Lectura de la NVS
  preferences.begin("wifi_config", false);
  String savedSSID = preferences.getString("ssid", "");
  String savedPass = preferences.getString("pass", "");

  // Inicialització BLE
  BLEDevice::init("ESP32_Eric");
  BLEServer* pServer = BLEDevice::createServer();
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

  pService->start();

  // Si hi ha credencials a la NVS, intentem connectar
  if (savedSSID.length() > 0) {
    if (tryWiFiConnect(savedSSID, savedPass)) {
      setNodeState(OPERATIU, WiFi.localIP().toString());
      BLEDevice::deinit(true); // Aturar BLE en estar en OPERATIU
      return;
    }
  }

  // Si no hi ha credencials o la connexió falla
  setNodeState(SENSE_CREDENCIALS);
  
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
}

void loop() {
  M5.update();

  if (newCredentialsReceived) {
    newCredentialsReceived = false;

    uint32_t startTime = millis();

    if (tryWiFiConnect(pendingSSID, pendingPass)) {
      uint32_t elapsedTime = millis() - startTime;
      Serial.printf("Connexió completada en %u ms\n", elapsedTime);

      // Guardar a la NVS
      preferences.putString("ssid", pendingSSID);
      preferences.putString("pass", pendingPass);

      setNodeState(OPERATIU, WiFi.localIP().toString());

      // Aturar el servei BLE per estalviar recursos/energia
      BLEDevice::deinit(true);
    } else {
      Serial.println("Han fallat els 3 intents. Tornant a SENSE CREDENCIALS.");
      setNodeState(SENSE_CREDENCIALS);
    }
  }

  delay(100);
}