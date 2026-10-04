#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  
  // Configurem la Wi-Fi en mode Estació (STA) i ens desconnectem si estava connectat
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("Iniciant l'escaneig Wi-Fi...");
}

void loop() {
  Serial.println("Escanejant xarxes...");

  // WiFi.scanNetworks retorna el número de xarxes trobades
  int n = WiFi.scanNetworks();
  Serial.println("Escaneig completat.");

  if (n == 0) {
    Serial.println("No s'han trobat xarxes Wi-Fi.");
  } else {
    Serial.print(n);
    Serial.println(" xarxes trobades:");
    Serial.println("--------------------------------------------------");
    
    for (int i = 0; i < n; ++i) {
      // Imprimim SSID, Canal i RSSI (potència del senyal) de cada xarxa
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" | Canal: ");
      Serial.print(WiFi.channel(i));
      Serial.print(" | RSSI: ");
      Serial.print(WiFi.RSSI(i));
      Serial.println(" dBm");
      delay(10);
    }
  }
  Serial.println("--------------------------------------------------\n");

  // Esperem 10 segons abans de fer el següent escaneig
  delay(10000);
}