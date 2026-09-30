#include <M5Unified.h>
#include <DHT.h>

#define LED 39
#define Pulsador 38
#define DHTPIN 8       
#define DHTTYPE DHT22  

// Creem l'objecte "dht" passant-li el pin físic (8) i el model de sensor (DHT22)
DHT dht(DHTPIN, DHTTYPE);

// Control de velocitat del LED: false = lent (1000 ms), true = ràpid (200 ms)
bool cadenciaRapida = false;

// Guardem la marca de temps (en ms) de l'últim cop que el LED o el polsador van canviar
unsigned long ultimTempsLED = 0;
unsigned long ultimTempsPolsador = 0;

// Guardem la marca de temps de l'última lectura del DHT22
unsigned long ultimTempsDHT = 0;

// Variables globals per conservar l'última temperatura i humitat vàlides
float tempActual = 0.0;
float humActual = 0.0;

// Funció personalitzada per dibuixar tota la interfície a la pantalla
void actualitzaPantalla() {
  // Pinta tota la pantalla de color negre per esborrar el contingut anterior abans de redibuixar
  M5.Display.fillScreen(BLACK); 

  // Escala del text: '1' significa la mida base original de la font (8x8 píxels per caràcter)
  M5.Display.setTextSize(1.5);

  // Situa el cursor d'escriptura a les coordenades x=0 (esquerra) i y=5 (5 píxels des de dalt)
  M5.Display.setCursor(0, 5);

  // Canvia el color del text a groc per al títol
  M5.Display.setTextColor(YELLOW);
  M5.Display.println(" Control Dades");
  M5.Display.println("--------------");

  // Canvia el color del text a blanc per a l'etiqueta "Temp:"
  M5.Display.setTextColor(WHITE);
  M5.Display.print("Temp: ");

  // Fixa el color verd per al valor numèric de la temperatura
  M5.Display.setTextColor(GREEN);

  // El ', 1' indica que volem imprimir el nombre flotant (float) formatat amb exactament 1 decimal
  M5.Display.print(tempActual, 1); 
  M5.Display.println(" C");

  M5.Display.setTextColor(WHITE);
  M5.Display.print("Hum:  ");

  // Color cian (blau clar) per al valor de la humitat
  M5.Display.setTextColor(CYAN);

  // Dibuixa la humitat imprimint també un sol decimal (p. ex. 45.2)
  M5.Display.print(humActual, 1); 
  M5.Display.println(" %");

  M5.Display.setTextColor(WHITE);
  M5.Display.print("LED:  ");

  // Llegeix l'estat físic del pin del LED (HIGH/LOW) per saber si actualment està encès
  if (digitalRead(LED) == HIGH) {
    M5.Display.setTextColor(RED);
    M5.Display.println("ON");
  } else {
    // Utilitza un color gris fosc per indicar que el LED està apagat
    M5.Display.setTextColor(DARKGREY); 
    M5.Display.println("OFF");
  }

  M5.Display.setTextColor(WHITE);
  M5.Display.print("Mode: ");

  // Color taronja per ressaltar el mode de funcionament seleccionat
  M5.Display.setTextColor(ORANGE);

  // Operador ternari (condició ? opció_cert : opció_fals): si cadenciaRapida és true escriu "RAPID", sinó "LENT"
  M5.Display.println(cadenciaRapida ? "RAPID" : "LENT");
}

void setup() {
  // Crea una estructura de configuració per defecte de la llibreria M5Unified
  auto cfg = M5.config();

  // Inicialitza la pantalla, el bus I2C, els botons intern i el gestor de potència del dispositiu
  M5.begin(cfg); 
  
  Serial.begin(115200);
  
  pinMode(LED, OUTPUT);

  // Activa la resistència interna de PULL-UP (el pin estarà a HIGH per defecte i passarà a LOW en prémer)
  pinMode(Pulsador, INPUT_PULLUP); 

  // Inicialitza el bus de comunicació d'un sol fil (One-Wire) del sensor DHT22
  dht.begin();

  // Gira l'orientació de la pantalla: 1 = horitzontal (landscapе rotat 90º en sentit horari)
  M5.Display.setRotation(1); 

  // Crida inicial per pintar el disseny per primer cop en arrencar
  actualitzaPantalla();
}

void loop() {
  // Llegeix l'estat dels botons i perifèrics de la placa M5Unified a cada cicle
  M5.update();

  // Obté el temps d'execució actual del microcontrolador des de l'arrencada (en mil·lisisegons)
  unsigned long tempsActual = millis();

  // --- 1. LECTURA DEL POLSADOR AMB ANTI-REBOT (DEBOUNCE) ---
  // Comprova si el polsador s'ha premut (LOW degut al circuit INPUT_PULLUP)
  if (digitalRead(Pulsador) == LOW) { 

    // Comprova que hagin passat com a mínim 200 ms des de la darrera pulsació per ignorar el rebot mecànic
    if (tempsActual - ultimTempsPolsador > 200) { 

      // Inverteix el valor booleà: si era false (lent) passa a true (ràpid) i viceversa
      cadenciaRapida = !cadenciaRapida;

      // Desa el temps actual per reiniciar el temporitzador de l'anti-rebot
      ultimTempsPolsador = tempsActual;

      // Refresca la pantalla per mostrar immediatament el nou mode ("RAPID" o "LENT")
      actualitzaPantalla(); 
    }
  }

  // --- 2. TEMPORITZADOR DEL LED SENSE BLOCAR (MILLIS) ---
  // Defineix l'interval d'espera fent servir l'operador ternari: 200 ms si és ràpid o 1000 ms si és lent
  int tempsEspera = cadenciaRapida ? 200 : 1000;

  // Comprova si ha transcorregut el temps corresponent per fer parpellejar el LED
  if (tempsActual - ultimTempsLED >= tempsEspera) {

    // Actualitza la marca de temps de l'últim canvi del LED
    ultimTempsLED = tempsActual;

    // Inverteix l'estat del pin del LED: si estava a HIGH el posa a LOW, i si estava a LOW el posa a HIGH
    digitalWrite(LED, !digitalRead(LED)); 

    // Refresca la pantalla per actualitzar el text "ON" / "OFF" del LED en temps real
    actualitzaPantalla(); 
  }

  // --- 3. LECTURA PERIODICA DEL SENSOR DHT22 ---
  // Comprova si han passat 2000 ms (2 segons), ja que el DHT22 no pot llegir-se a més velocitat
  if (tempsActual - ultimTempsDHT >= 2000) {

    // Actualitza la marca de temps de l'última lectura del sensor
    ultimTempsDHT = tempsActual;

    // Executa la lectura de la temperatura i la humitat del sensor
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    // Comprova que la lectura no hagi retornat "NaN" (Not a Number / lectura fallida)
    if (!isnan(t) && !isnan(h)) {

      // Guarda els valors vàlids a les variables globals
      tempActual = t;
      humActual = h;

      // Actualitza la pantalla només quan s'obtenen noves dades vàlides
      actualitzaPantalla(); 
    }
  }
}