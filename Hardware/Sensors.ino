#include "DHT.h"
#include <WiFi.h>
#include <PubSubClient.h>

//Pinos 
#define typeDHT DHT22   
const int pinDHT = 17;
const int pinLDR = 35;
const int pinSOIL = 32;
const int pinBomba = 21;
DHT dht(pinDHT, typeDHT);

//Valores para conversao lux
const float V_IN = 3.3;
const float resistor = 10000;

//Conexao com servidor 
const char* ssid = "PlantCare";
const char* password = "Plant1234";
const char* mqtt_server = "1710d2c005d94ca7902c7327ae6b95fd.s1.eu.hivemq.cloud.hivemq.cloud";
const int mqtt_port = 8883;
// Credenciais MQTT
const char* mqtt_user = "plantcare";
const char* mqtt_password = "zcY7a@7&ONNhQdmE";

WiFiClientSecure espClient;
PubSubClient client(espClient);
//Bomba: comando e status atual
bool comandoBomba = false;
bool bombaLigada = false;

//Conexao ao servidor MQTT
void reconnect()
{
  while (!client.connected()) {
    Serial.println("Conectando ao broker MQTT...");
    if (client.connect("ESP32Client", mqtt_user,mqtt_password)) {
      Serial.println("Conectado!");
      client.subscribe("planta/bomba");
    } else {
      Serial.print("Falha. Código: ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

//Callback do MQTT
void callback(char* topic, byte* payload, unsigned int length) 
{
    String msg = "";
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    if (String(topic) == "planta/bomba"){
      if (msg == "ON")
          comandoBomba = true; 
      if (msg == "OFF")
          comandoBomba = false; 
    }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  pinMode(pinBomba, OUTPUT);
  digitalWrite(pinBomba, LOW);
  //Iniciando Wifi e servidor 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi conectado");

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()){
    reconnect();
  }

  // Leitura dos sensores 
  float umid = dht.readHumidity();
  float temp = dht.readTemperature();
  int adcBruto = analogRead(pinLDR);
  int solo = analogRead(pinSOIL);
  //Repete tentativa de leitura no caso de falha 
  if (isnan(umid) || isnan(temp)) {
    Serial.println(F("Falha de leitura"));
    return;
  }
  
  // Leitura do valor analógico do sensor LDR(0 a 4095) e conversao para lux
  if (adcBruto == 0) adcBruto = 1; //correcao do valor para divisao 
  float tensao = (adcBruto / 4095.0) * V_IN;
  float resistenciaLDR = resistor * ((V_IN / tensao) - 1.0);//calcula resistencia a partir de tensao
  float lux = pow(500000 / resistenciaLDR, 1.4); //calculo de ohms para lux (apenas aceitar)

  //Sensor do solo 
  int porcenSolo = map(solo, 4095, 0, 0, 100);
  
  if (!client.connected()) {
        reconnect();
  }
  //Sensores -> MQTT
  client.loop();
  static unsigned long ultimoEnvio = 0;

  if (millis() - ultimoEnvio > 5000){
    ultimoEnvio = millis();
    client.publish("planta/temperatura", String(temp).c_str());
    client.publish("planta/solo", String(porcenSolo).c_str());
    client.publish("planta/umidade", String(umid).c_str());
    client.publish("planta/luz", String(lux).c_str());
  }
    
  Serial.print(F("Humidity: "));
  Serial.print(umid);

  Serial.print(F("%  Temperature: "));
  Serial.print(temp);

  Serial.print("°C    Lux: ");
  Serial.print(lux);

  Serial.print("    Umidade do solo: ");
  Serial.print(porcenSolo);
  Serial.println("% ");
  
  //MQTT -> Atuador 
  if (comandoBomba && !bombaLigada){
    digitalWrite(pinBomba, HIGH);
    bombaLigada = true;
    Serial.println("Bomba Ligada");
  }
  else if (!comandoBomba && bombaLigada){
    digitalWrite(pinBomba, LOW);
    bombaLigada = false; 
    Serial.println("Bomba desligada");
  }
}
