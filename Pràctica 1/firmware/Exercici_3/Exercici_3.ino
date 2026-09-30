#define LED 39
#define Pulsador 38

bool cadenciaRapida = false;
unsigned long ultimTempsLED = 0;
unsigned long ultimTempsPolsador = 0;

void setup() {
  Serial.begin(115200); // 115200 bauds
  pinMode(LED, OUTPUT);
  pinMode(Pulsador, INPUT_PULLUP);
}

void loop() {
  unsigned long tempsActual = millis();

  // 1. LECTURA DEL POLSADOR AMB ANTI-REBOT SIMPLE (200 ms)
  if (digitalRead(Pulsador) == LOW) { 
    if (tempsActual - ultimTempsPolsador > 200) { // Evita el rebot
      cadenciaRapida = !cadenciaRapida;            // Canvia entre lent i ràpid
      ultimTempsPolsador = tempsActual;

      // Informació per port sèrie
      Serial.print("Temps activitat: ");
      Serial.print(tempsActual / 1000);
      Serial.print(" s | Cadència: ");
      Serial.println(cadenciaRapida ? "RÀPIDA" : "LENTA");
    }
  }

  // 2. PARPELLEIG DEL LED SENSE DORMIR EL SYSTEMA
  int tempsEspera = cadenciaRapida ? 200 : 1000; // 200 ms (ràpid) o 1000 ms (lent)

  if (tempsActual - ultimTempsLED >= tempsEspera) {
    ultimTempsLED = tempsActual;
    
    // Invertim l'estat del LED
    digitalWrite(LED, !digitalRead(LED)); 

    // Muestra por el puerto serie
    Serial.print("Temps activitat (ms): ");
    Serial.print(tempsActual);
    Serial.print(" | LED: ");
    Serial.println(digitalRead(LED) ? "ON" : "OFF");
  }
}
