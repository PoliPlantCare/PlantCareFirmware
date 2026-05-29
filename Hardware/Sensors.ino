#include "DHT.h"

#define typeDHT DHT22   
const int pinDHT 17;
const int pinLDR 34;
const int pinSOIL 32;

const float lux_ppfd 0.0185;
const float V_IN 3.3;
const float resistor 10000;

DHT dht(pinDHT, typeDHT);

void setup() {
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");
  
  dht.begin();
}

void loop() {
  delay(2000);

  // ========= sensor DHT de umidade e temperatura ==========
  float umid = dht.readHumidity();
  float temp = dht.readTemperature();
  //Repete tentativa de leitura no caso de falha 
  if (isnan(umid) || isnan(temp)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
  //Calcula sensacao termica 
  float hic = dht.computeHeatIndex(temp, umid, false);


  // Leitura do valor analógico do sensor LDR(0 a 4095 ) e conversao para PPFD (medida de fotossintese) -> impreciso
  int adcBruto = analogRead(pinLDR);
   if (adcBruto == 0) adcBruto = 1; //correcao do valor para divisao 
  
  float tensao = (adcBruto / 4095.0) * V_IN;
  float resistenciaLDR = resistor * ((V_IN / tensao) - 1.0);//calcula resistencia a partir de tensao

  float lux = pow(500000 / resistenciaLDR, 1.4); //calculo de ohms para lux (apenas aceitar)
  float ppfd = lux * lux_ppfd;

  //======== sensor do solo ========
  int solo = analogRead(pinSOIL);
  int porcenSolo = map(solo, 4095, 0, 0, 100);
  

  Serial.print(F("Humidity: "));
  Serial.print(umid);

  Serial.print(F("%  Temperature: "));
  Serial.print(temp);
  Serial.print(F("°C "));

  Serial.print(F("Heat index: "));
  Serial.print(hic);
  Serial.print(F("°C "));

  Serial.print("PPFD: ");
  Serial.print(ppfd);
  Serial.print(" umol/m2/s");

  Serial.print("Umidade do solo: ");
  Serial.print(porcenSolo);
  Serial.print(" % ");
}
