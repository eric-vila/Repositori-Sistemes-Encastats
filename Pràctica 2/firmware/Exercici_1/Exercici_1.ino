#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 1. Assignació de UUIDs de 128 bits generats aleatòriament
#define SERVICE_UUID           "9bd46fc4-d6ab-4d64-81ee-1c0d3be13a3d"
#define CHAR_CREDENTIALS_UUID  "1009d074-8913-4371-8313-99958c49c265"
#define CHAR_ESTAT_UUID        "7e93a750-5e54-41a7-9532-8315e7921b63"

BLEServer* pServer = nullptr;
BLECharacteristic* pCharCredentials = nullptr;
BLECharacteristic* pCharEstat = nullptr;

bool deviceConnected = false;

// Callback per gestionar connexions/desconnexions
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) override {
      deviceConnected = false;
      // Reiniciar l'anunci si es desconnecta en estat SENSE CREDENCIALS
      BLEDevice::startAdvertising();
    }
};

// Callback per rebre les credencials per escriptura
class CredentialsCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
      String rxValue = pCharacteristic->getValue().c_str();
      if (rxValue.length() > 0) {
        Serial.print("Dades rebudes a credencials (longitud): ");
        Serial.println(rxValue.length());
        
        // Aquí en l'Exercici 2 tractaràs el JSON i canviaràs d'estat
      }
    }
};

void setup() {
  Serial.begin(115200);

  // Inicialització del dispositiu BLE
  BLEDevice::init("ESP32_Config_Node");

  // Creació del servidor GATT
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Creació del Servei de Configuració
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Característica 'credencials' (Només Escriptura)
  pCharCredentials = pService->createCharacteristic(
                        CHAR_CREDENTIALS_UUID,
                        BLECharacteristic::PROPERTY_WRITE
                      );
  pCharCredentials->setCallbacks(new CredentialsCallbacks());

  // Característica 'estat' (Lectura i Notificació)
  pCharEstat = pService->createCharacteristic(
                  CHAR_ESTAT_UUID,
                  BLECharacteristic::PROPERTY_READ |
                  BLECharacteristic::PROPERTY_NOTIFY
                );
  // Descriptor 2902 necessari per a habilitar Notificacions al client (mòbil)
  pCharEstat->addDescriptor(new BLE2902());
  
  // Valor inicial de l'estat
  pCharEstat->setValue("SENSE CREDENCIALS");

  // Iniciar el servei
  pService->start();

  // Configurar i iniciar l'anunci (Advertising)
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Funcionalitat per millorar compatibilitat amb iOS
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("Servidor GATT actiu i anunciant...");
}

void loop() {
  // El control d'estats es gestionarà en els següents exercicis
  delay(1000);
}