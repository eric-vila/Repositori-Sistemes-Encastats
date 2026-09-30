#include <DHT.h>

#define DHTPIN 8       // Connectat al pin G5
#define DHTTYPE DHT22  // El sensor AM2302 és equivalent a DHT22

DHT dht(DHTPIN, DHTTYPE);

// Variables per al càlcul de fallades (per a l'informe)
int lecturesTotals = 0;
int lecturesFallides = 0;

unsigned long ultimTempsDHT = 0;

void setup() {
  Serial.begin(115200);
  dht.begin();
  Serial.println("--- Inici de lectura del sensor DHT22 / AM2302 (G5) ---");
}

void loop() {
  unsigned long tempsActual = millis();

  // Lectura cada 2 segons (2000 ms) segons l'enunciat
  if (tempsActual - ultimTempsDHT >= 2000) {
    ultimTempsDHT = tempsActual;

    float t = dht.readTemperature(); // Temperatura (°C)
    float h = dht.readHumidity();    // Humitat (%)

    lecturesTotals++;

    // Gestió de lectura fallida (retorna NaN)
    if (isnan(t) || isnan(h)) {
      lecturesFallides++;
      Serial.println("Error: Lectura fallida del sensor DHT22!");
    } else {
      Serial.print("Temps: ");
      Serial.print(tempsActual / 1000);
      Serial.print("s | Temp: ");
      Serial.print(t);
      Serial.print(" °C | Humitat: ");
      Serial.print(h);
      Serial.println(" %");
    }

    // Calcula i mostra la taxa d'error cada 10 lectures per a l'informe
    if (lecturesTotals % 10 == 0) {
      float percentatgeError = ((float)lecturesFallides / lecturesTotals) * 100.0;
      Serial.print("--> Taxa d'errors actual: ");
      Serial.print(percentatgeError);
      Serial.println("%");
    }
  }
}
