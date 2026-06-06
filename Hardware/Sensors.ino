#include "DHT.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// Pinos
#define typeDHT DHT22
const int pinDHT = 17;
const int pinLDR = 35;
const int pinSOIL = 32;
const int pinBomba = 21;
DHT dht(pinDHT, typeDHT);

// Valores para conversao lux
const float V_IN = 3.3;
const float resistor = 10000;

// Conexao com servidor
const char* ssid = "PlantCare";
const char* password = "Plant1234";
const char* mqtt_server = "1710d2c005d94ca7902c7327ae6b95fd.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;

// Credenciais MQTT
const char* mqtt_user = "plantcare";
const char* mqtt_password = "zcY7a@7&ONNhQdmE";

WiFiClientSecure espClient;
PubSubClient client(espClient);

//Agendamento de Rega Automatica
struct Intervalo {
  int onHour, onMin;   // hora de ligar
  int offHour, offMin; // hora de desligar
};

const int MAX_INTERVALOS = 10;
Intervalo agenda[MAX_INTERVALOS];
int totalIntervalos = 0;
bool agendaRecebida = false;

// Configuracao da Planta (recebida via MQTT)
struct ConfigPlanta {
  char nome[64];
  int umidadeMax; // acima disto, rega fixa nao ocorre
  int umidadeMin; // abaixo disto, rega de emergencia e ativada
};

ConfigPlanta configPlanta = {"", 80, 20};
bool configRecebida = false;

// Estado da bomba
bool bombaLigada = false;
bool bombaEmergencia = false;

// NTP - após sincronizar com NTP, o ESP32 mantém o tempo internamente
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3 * 3600; //ajuste do fuso
const int   daylightOffset_sec = 0;

bool ntpSincronizado = false;

void sincronizarNTP() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.print("Sincronizando NTP");
  struct tm timeinfo;
  for (int i = 0; i < 20; i++) {       // tenta por ~10 s
    if (getLocalTime(&timeinfo)) {
      ntpSincronizado = true;
      Serial.println(" OK");
      Serial.printf("Hora atual: %02d:%02d:%02d\n",
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      return;
    }
    Serial.print(".");
    delay(500);
  }
  Serial.println(" FALHA — bomba usará agenda se recebida, sem garantia de hora correta");
}

// Retorna true se conseguiu ler a hora local
bool getHoraAtual(int &hora, int &minuto) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;
  hora   = timeinfo.tm_hour;
  minuto = timeinfo.tm_min;
  return true;
}

// Formato esperado no tópico planta/config
// {"nome":"Samambaia","umidade_max":70,"umidade_min":25}
void parseConfigPlanta(const String& json) {
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("Erro ao parsear config da planta: ");
    Serial.println(err.c_str());
    return;
  }

  const char* nome = doc["nome"] | "";
  int uMax = doc["umidade_max"] | -1;
  int uMin = doc["umidade_min"] | -1;

  if (strlen(nome) == 0 || uMax < 0 || uMin < 0) {
    Serial.println("Config invalida: campos ausentes ou incorretos");
    return;
  }

  if (uMin >= uMax) {
    Serial.println("Config invalida: umidade_min deve ser menor que umidade_max");
    return;
  }

  strlcpy(configPlanta.nome, nome, sizeof(configPlanta.nome));
  configPlanta.umidadeMax = uMax;
  configPlanta.umidadeMin = uMin;
  configRecebida = true;

  Serial.printf("Config da planta recebida:\n");
  Serial.printf("  Nome       : %s\n",  configPlanta.nome);
  Serial.printf("  Umid. Max  : %d%%\n", configPlanta.umidadeMax);
  Serial.printf("  Umid. Min  : %d%%\n", configPlanta.umidadeMin);
}

// Formato esperado no tópico planta/bomba/horarios
void parseAgenda(const String& json) {
  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("Erro ao parsear agenda: ");
    Serial.println(err.c_str());
    return;
  }

  JsonArray arr = doc.as<JsonArray>();
  totalIntervalos = 0;

  for (JsonObject obj : arr) {
    if (totalIntervalos >= MAX_INTERVALOS) break;
    const char* on_str  = obj["on"];
    const char* off_str = obj["off"];
    if (!on_str || !off_str) continue;

    // Parse "HH:MM"
    int onH, onM, offH, offM;
    if (sscanf(on_str,  "%d:%d", &onH,  &onM)  != 2) continue;
    if (sscanf(off_str, "%d:%d", &offH, &offM) != 2) continue;

    agenda[totalIntervalos] = {onH, onM, offH, offM};
    totalIntervalos++;
    Serial.printf("  Intervalo %d: ligar %02d:%02d | desligar %02d:%02d\n",
                  totalIntervalos, onH, onM, offH, offM);
  }

  agendaRecebida = (totalIntervalos > 0);
  Serial.printf("Agenda carregada: %d intervalo(s)\n", totalIntervalos);
}

//Logica da bomba: agenda + limites de umidade do solo
void atualizarBomba(int porcenSolo) {
  // --- Rega de emergencia: solo abaixo do minimo, fora da agenda ---
  if (configRecebida && porcenSolo < configPlanta.umidadeMin) {
    if (!bombaLigada) {
      digitalWrite(pinBomba, HIGH);
      bombaLigada = true;
      bombaEmergencia = true;
      Serial.printf("Bomba LIGADA (emergencia: solo %d%% < min %d%%)\n",
                    porcenSolo, configPlanta.umidadeMin);
    }
    return;
  }

  // Se estava em emergencia e solo subiu, desliga
  if (bombaEmergencia && bombaLigada) {
    digitalWrite(pinBomba, LOW);
    bombaLigada = false;
    bombaEmergencia = false;
    Serial.println("Bomba DESLIGADA (solo recuperado apos emergencia)");
  }

  // --- Rega por agenda: apenas se solo abaixo do maximo ---
  if (!agendaRecebida || !ntpSincronizado) return;

  int hora, minuto;
  if (!getHoraAtual(hora, minuto)) return;

  int agora = hora * 60 + minuto;
  bool deveEstarLigada = false;

  for (int i = 0; i < totalIntervalos; i++) {
    int inicio = agenda[i].onHour  * 60 + agenda[i].onMin;
    int fim    = agenda[i].offHour * 60 + agenda[i].offMin;
    if (agora >= inicio && agora < fim) {
      deveEstarLigada = true;
      break;
    }
  }

  // Bloqueia rega agendada se solo ja esta acima do maximo
  if (deveEstarLigada && configRecebida && porcenSolo >= configPlanta.umidadeMax) {
    deveEstarLigada = false;
    Serial.printf("Rega agendada BLOQUEADA (solo %d%% >= max %d%%)\n",
                  porcenSolo, configPlanta.umidadeMax);
  }

  if (deveEstarLigada && !bombaLigada) {
    digitalWrite(pinBomba, HIGH);
    bombaLigada = true;
    Serial.println("Bomba LIGADA (agenda)");
  } else if (!deveEstarLigada && bombaLigada) {
    digitalWrite(pinBomba, LOW);
    bombaLigada = false;
    Serial.println("Bomba DESLIGADA (agenda)");
  }
}

//Callback MQTT 
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  if (String(topic) == "planta/config") {
    Serial.println("Nova config de planta recebida:");
    Serial.println(msg);
    parseConfigPlanta(msg);
  }
  else if (String(topic) == "planta/bomba/horarios") {
    Serial.println("Nova agenda recebida:");
    Serial.println(msg);
    parseAgenda(msg);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Conectando ao broker MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("Conectado!");
      // Recebe a agenda com retain=true — o broker reenvia o último valor ao conectar
      client.subscribe("planta/config");
      client.subscribe("planta/bomba/horarios");
    } else {
      Serial.print("Falha. Código: ");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(pinBomba, OUTPUT);
  digitalWrite(pinBomba, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nWiFi conectado");

  sincronizarNTP(); 

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  // Mantém conexão MQTT para receber atualizações de agenda e publicar sensores.
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    reconnect();
  }
  if (client.connected()) {
    client.loop();
  }

  // Leitura dos sensores
  float umid = dht.readHumidity();
  float temp = dht.readTemperature();
  int adcBruto = analogRead(pinLDR);
  int solo = analogRead(pinSOIL);

  if (isnan(umid) || isnan(temp)) {
    Serial.println(F("Falha de leitura DHT"));
    delay(2000);
    return;
  }

  if (adcBruto == 0) adcBruto = 1;
  float tensao = (adcBruto / 4095.0) * V_IN;
  float resistenciaLDR = resistor * ((V_IN / tensao) - 1.0);
  float lux = pow(500000 / resistenciaLDR, 1.4);
  int porcenSolo = map(solo, 4095, 0, 0, 100);

  // Publicacao de sensores 
  static unsigned long ultimoEnvio = 0;
  if (client.connected() && millis() - ultimoEnvio > 600000) { //10 minutos
    ultimoEnvio = millis();
    client.publish("planta/temperatura", String(temp).c_str());
    client.publish("planta/solo",        String(porcenSolo).c_str());
    client.publish("planta/umidade",     String(umid).c_str());
    client.publish("planta/luz",         String(lux).c_str());
  }

  Serial.printf("Temp: %.1f°C  Umid: %.1f%%  Lux: %.1f  Solo: %d%%\n",
                temp, umid, lux, porcenSolo);

  // Controle da bomba por agenda local + limites de umidade
  atualizarBomba(porcenSolo);

  delay(1000);
}
