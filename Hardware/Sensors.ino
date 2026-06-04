#include "DHT.h"
#include <WiFi.h>
#include <PubSubClient.h>

//Pinos 
#define typeDHT DHT22   
const int pinDHT 34;
const int pinLDR 35;
const int pinSOIL 36;
DHT dht(pinDHT, typeDHT);

//Valores para conversao lux
const float V_IN 3.3;
const float resistor 10000;

//Conexao com servidor 
const char* ssid = "PlantCare";
const char* password = "Plant1234";
const char* mqtt_server = "1710d2c005d94ca7902c7327ae6b95fd.s1.eu.hivemq.cloud.hivemq.cloud";
const int mqtt_port = 8883;
// Credenciais MQTT
const char* mqtt_user = "plantcare";
const char* mqtt_password = "zcY7a@7&ONNhQdmE";

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  //Iniciando Wifi e servidor 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi conectado");

  client.setServer(mqtt_server, mqtt_port);
  while (!client.connected()) {
    Serial.println("Conectando ao broker MQTT...");
    if (client.connect("ESP32Client", mqtt_user,mqtt_password)) {
      Serial.println("Conectado!");
    } else {
      Serial.print("Falha. Código: ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void loop() {
  delay(2000);

  // Leitura dos sensores 
  float umid = dht.readHumidity();
  float temp = dht.readTemperature();
  int adcBruto = analogRead(pinLDR);
  int solo = analogRead(pinSOIL);
  //Repete tentativa de leitura no caso de falha 
  if (isnan(umid) || isnan(temp)||isnan(adcBruto)|| isnan(solo)) {
    Serial.println(F("Falha de leitura"));
    return;
  }
  
  // Leitura do valor analógico do sensor LDR(0 a 4095) e conversao para lux
  if (adcBruto == 0) adcBruto = 1; //correcao do valor para divisao 
  float tensao = (adcBruto / 4095.0) * V_IN;
  float resistenciaLDR = resistor * ((V_IN / tensao) - 1.0);//calcula resistencia a partir de tensao
  float lux = pow(500000 / resistenciaLDR, 1.4); //calculo de ohms para lux (apenas aceitar)

  //======== sensor do solo ========
  int porcenSolo = map(solo, 4095, 0, 0, 100);
  
  if (!client.connected()) {
        reconnect();
  }
  client.loop();
  client.publish("planta/temperatura", String(temp).c_str());
  client.publish("planta/solo", String(porcenSolo).c_str());
  client.publish("planta/umidade", String(umid).c_str());
  client.publish("planta/luz", String(lux).c_str());
    delay(5000);
    
  Serial.print(F("Humidity: "));
  Serial.print(umid);

  Serial.print(F("%  Temperature: "));
  Serial.print(temp);

  Serial.print(F("°C   Heat index: "));
  Serial.print(hic);

  Serial.print("°C    Lux: ");
  Serial.print(lux);

  Serial.print("    Umidade do solo: ");
  Serial.print(porcenSolo);
  Serial.println("% ");
}
