#include <M5Unified.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// UUIDs BLE de la teva activitat
#define SERVICE_UUID           "9bd46fc4-d6ab-4d64-81ee-1c0d3be13a3d"
#define CHAR_CREDENTIALS_UUID  "1009d074-8913-4371-8313-99958c49c265"
#define CHAR_ESTAT_UUID        "7e93a750-5e54-41a7-9532-8315e7921b63"

Preferences preferences;

BLECharacteristic* pCharEstat = nullptr;

enum State { SENSE_CREDENCIALS, CONNECTANT, OPERATIU };
State currentState = SENSE_CREDENCIALS;

String pendingSSID = "";
String pendingPass = "";
bool newCredentialsReceived = false;

// Variables per al control de la polsació llarga del botó
bool feedbackShown = false;

// Funció per dibuixar la pantalla segons l'estat actual
void actualitzaPantalla(const String& estatStr, const String& extraInfo = "") {
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextSize(1.2);
  M5.Display.setCursor(0, 5);

  M5.Display.setTextColor(YELLOW);
  M5.Display.println(" Config Node BLE");
  M5.Display.println("----------------");

  M5.Display.setTextColor(WHITE);
  M5.Display.print("Estat:\n");
  
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

// Pantalla d'avís de realimentació als 3 segons de polsació
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

// Canvia l'estat del node, notifica per BLE i actualitza la pantalla
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

  // Notificar canvi d'estat per BLE
  if (pCharEstat) {
    pCharEstat->setValue(stateStr.c_str());
    pCharEstat->notify();
  }

  actualitzaPantalla(stateStr, extraInfo);
}

// Callback BLE per a les credencials
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

// Funció d'intent de connexió Wi-Fi
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

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.begin(115200);

  // Obre l'espai de noms de la NVS
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

  // Si hi ha credencials guardades a la NVS, intentar connexió directament
  if (savedSSID.length() > 0) {
    if (tryWiFiConnect(savedSSID, savedPass)) {
      setNodeState(OPERATIU, WiFi.localIP().toString());
      BLEDevice::deinit(true); // Atura la ràdio BLE per estalviar energia
      return;
    }
  }

  // En cas de no tenir credencials o fallar la connexió
  setNodeState(SENSE_CREDENCIALS);

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
}

void loop() {
  M5.update(); // Actualitza l'estat del botó integrat M5.BtnA

  // --- LÒGICA DE RECONEIXEMENT DE POLSACIÓ DE L'EXERCICI 3 ---
  
  // 1. Detecta si es manté premut durant 5 segons o més (5000 ms)
  if (M5.BtnA.pressedFor(5000)) {
    Serial.println("Polsació de 5s detectada: Esborrant NVS i unint Restart...");
    
    // Esborra tot l'espai de noms de la NVS
    preferences.clear();
    
    // Mostra missatge de confirmació
    M5.Display.fillScreen(RED);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(10, 25);
    M5.Display.println("RESET NVS!");
    delay(1000);

    // Reinicia el microcontrolador
    ESP.restart();
  } 
  // 2. Realimentació visible a partir dels 3 segons (3000 ms)
  else if (M5.BtnA.pressedFor(3000)) {
    if (!feedbackShown) {
      feedbackShown = true;
      mostraFeedbackReset();
    }
  } 
  // 3. Si l'usuari deixa de premer abans dels 5 segons, es restaura la pantalla
  else if (M5.BtnA.wasReleased()) {
    if (feedbackShown) {
      feedbackShown = false;
      // Restaura la interfície d'estat normal
      String ipStr = (currentState == OPERATIU) ? WiFi.localIP().toString() : "";
      setNodeState(currentState, ipStr);
    }
  }

  // --- GESTIÓ DE CREDENCIALS REBUDES PER BLE ---
  if (newCredentialsReceived) {
    newCredentialsReceived = false;
    uint32_t startTime = millis();

    if (tryWiFiConnect(pendingSSID, pendingPass)) {
      uint32_t elapsedTime = millis() - startTime;
      Serial.printf("Connexió completada en %u ms\n", elapsedTime);

      // Desar credencials a la NVS
      preferences.putString("ssid", pendingSSID);
      preferences.putString("pass", pendingPass);

      setNodeState(OPERATIU, WiFi.localIP().toString());

      // Aturar el servei BLE en estar operatiu
      BLEDevice::deinit(true);
    } else {
      Serial.println("Han fallat els 3 intents. Tornant a SENSE CREDENCIALS.");
      setNodeState(SENSE_CREDENCIALS);
    }
  }

  delay(50);
}
