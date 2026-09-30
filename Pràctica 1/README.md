# Pràctica 1: Muntatge de l'entorn de treball

**Equip:** Equip 09
**Components:** Èric Vila

---

## 1. Maquinari utilitzat
* **Gateway:** Raspberry Pi 4 amb targeta microSD de 16 GB.
* **Node de desenvolupament:** Placa M5 (ESP32) amb pantalla OLED I2C.
* **Sensors i perifèrics:**
  * Sensor de temperatura i humitat DHT22.
  * LED vermell de 5 mm amb resistència.
  * Polsador físic.
* **Connectivitat:** Cable USB de dades.

---

## 2. Configuració de la xarxa
* **Adreça IP del Gateway (Raspberry Pi):** `10.11.2.113`
* **Nom d'amfitrió (Hostname):** `admin`
* **Port de Node-RED:** `http://10.11.2.113:1880/`

---

## 3. Com engegar el sistema des de zero

### Pas 1: Engegar el Gateway (Raspberry Pi)
1. Inserir la microSD a la Raspberry Pi amb Raspberry Pi OS Lite.
2. Connectar el cable d'alimentació i el cable de xarxa.
3. El servei de **Node-RED** arrenca automàticament com a servei de systemd.
4. Per accedir al panell de fluxos, obrir el navegador i anar a `http://<IP_DEL_GATEWAY>:1880`.

### Pas 2: Carregar el firmware a l'ESP32
1. Connectar la placa M5 al portàtil mitjançant el cable USB de dades.
2. Obrir el projecte situat a la carpeta `firmware/` amb l'IDE d'Arduino o VS Code.
3. Seleccionar el port sèrie corresponent i la velocitat de transmissió a **115200 bauds**.
4. Compilar i carregar el codi a la placa M5.

### Pas 3: Verificació del funcionament
1. La pantalla de la placa M5 mostrarà la temperatura i humitat del DHT22 en temps real.
2. El polsador permet alternar la velocitat de parpelleig del LED entre **RAPID** i **LENT**.