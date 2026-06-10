# PlantCareFirmware

Este repositório contém o firmware desenvolvido em C++ (plataforma Arduino) para o microcontrolador ESP32. Ele é a peça de hardware do **PlantCare**, responsável por coletar dados da planta e executar a lógica de controle da bomba de irrigação.

Este projeto atua como o cliente IoT e deve ser utilizado em conjunto com o nosso backend, disponível em: [PlantCareServer](https://github.com/PoliPlantCare/).

## Funcionalidades

* **Monitoramento Ambiental:** Leitura de temperatura e umidade do ar, luminosidade em lux (LDR com cálculo de resistência) e umidade do solo.
* **Conectividade e Segurança:** Conexão Wi-Fi e comunicação com broker MQTT (HiveMQ Cloud) utilizando protocolo seguro TLS/SSL.
* **Sincronização de Tempo (NTP):** O ESP32 sincroniza seu relógio interno via internet (fuso GMT-3) para garantir que a agenda de irrigação seja executada na hora exata, sem depender do envio constante de gatilhos do servidor.
* **Irrigação Inteligente (Lógica Mista):**
  * **Agendamento:** Executa intervalos de rega (`on` / `off`) programados pelo usuário e recebidos via MQTT.
  * **Prevenção de Afogamento:** Cancela automaticamente uma rega agendada se os sensores detectarem que a umidade do solo já ultrapassou o limite máximo (`umidade_max`).
  * **Rega de Emergência:** Aciona a bomba imediatamente se o solo atingir níveis críticos de ressecamento (`umidade_min`), independentemente da agenda.

## Hardware e Pinos (Pinout)

A montagem do circuito no ESP32 segue o mapeamento abaixo:

* **Sensor DHT11** (Temperatura/Umidade do Ar): Pino `17`
* **Sensor LDR** (Luminosidade): Pino `35` (Analógico)
* **Sensor de Umidade do Solo**: Pino `32` (Analógico)
* **Bomba de Água / Módulo Relé**: Pino `21` (Digital - OUTPUT)

## Dependências

Para compilar este código na Arduino IDE, instale as seguintes bibliotecas através do Library Manager:

* `DHT sensor library` (por Adafruit)
* `PubSubClient` (por Nick O'Leary) - Gerenciamento das mensagens MQTT.
* `ArduinoJson` (por Benoit Blanchon) - Serialização e desserialização de payloads.
* *Bibliotecas nativas do ESP32 já inclusas no core:* `WiFi.h`, `time.h`, `WiFiClientSecure.h`.

## Comunicação MQTT

O dispositivo gerencia dados e ações assinando e publicando nos seguintes tópicos do broker:

### Tópicos Publicados (Sensores)
O ESP32 envia telemetria a cada 10 minutos (`600000` ms).

* `planta/temperatura` - Temperatura atual em °C
* `planta/umidade` - Umidade relativa do ar em %
* `planta/luz` - Luminosidade calculada em Lux
* `planta/solo` - Umidade do solo percentual (mapeado do ADC bruto para 0-100%)

### Tópicos Inscritos (Configurações)
O ESP32 recebe atualizações em tempo real baseadas no que foi configurado no servidor.

* `planta/config` - Parâmetros limite da planta atual.
  * **Payload esperado (JSON):**
    ```json
    {
      "nome": "Samambaia",
      "umidade_max": 70,
      "umidade_min": 25
    }
    ```

* `planta/bomba/horarios` - Agenda diária de irrigação (suporta até 10 intervalos).
  * **Payload esperado (JSON Array):**
    ```json
    [
      { "on": "08:00", "off": "08:05" },
      { "on": "18:00", "off": "18:10" }
    ]
    ```

## Como Instalar e Executar

1. Clone este repositório para sua máquina local.
2. Abra o arquivo principal (ex: `PlantCareFirmware.ino`) na Arduino IDE.
3. Edite o bloco de constantes no topo do arquivo com as suas credenciais:
   * `ssid` e `password` da sua rede Wi-Fi.
   * `mqtt_server`, `mqtt_user` e `mqtt_password` da sua instância HiveMQ.
4. Conecte o ESP32 ao computador e selecione a placa/porta correta na IDE.
5. Compile e faça o Upload do código.
6. Abra o Serial Monitor (Baud rate: `115200`) para acompanhar os logs de inicialização, conexão e leituras.

## Licença

Este software é **Proprietário**. Todos os direitos reservados aos autores. A cópia, modificação ou distribuição não autorizada é estritamente proibida.

