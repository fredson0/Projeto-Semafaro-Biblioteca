/*
 * PROJETO: EcoLibrary - Semáforo de Ruído IoT
 * DESCRIÇÃO: Monitoramento de ruído analógico (MAX4466) com filtro de Média Móvel.
 * ENVIOS: Telemetria (ThingSpeak) e Alertas (Discord).
 */

#include <WiFi.h>
#include <HTTPClient.h>

// --- CONFIGURAÇÕES DE CONEXÃO ---
const char* ssid = "Fred";
const char* password = "sophia24";
String thingSpeakApiKey = "XXHKUFADAP5IFK1S"; 
String discordWebhookUrl = "https://discord.com/api/webhooks/1438165252041609307/AvdU1fdwQuqXlI4cK3lZdtA4lJpJv9EO92lcKXUR7RpmR0fgC3IQdxtr9ACM6WVyzWQp";

// --- CONFIGURAÇÃO DE HARDWARE ---
const int LED_AMARELO_PIN = 19; 
const int LED_VERMELHO_PIN = 21; 
const int MIC_PIN = 34; // Pino ADC para o microfone
const int sampleWindow = 50; // Janela de amostragem em ms

// --- CALIBRAÇÃO DE LIMITES ---
int LIMITE_AMARELO = 400; // Início da 'Conversa Suave'
int LIMITE_VERMELHO = 1000; // Início da 'Conversa Atrapalhando'

// --- VARIÁVEIS DE TEMPORIZAÇÃO E CONTROLE ---
long intervaloThingSpeak = 60000; // Intervalo de envio (1 minuto)
unsigned long tempoAnteriorThingSpeak = 0;
long intervaloAlertaRuido = 5000; // Tempo contínuo para alerta (5s para teste)
unsigned long tempoInicioLedVermelho = 0; // Timer para contagem
bool alertaEnviado = false;

// --- VARIÁVEIS DA MÉDIA MÓVEL ---
const int NUM_AMOSTRAS = 20; // 20 amostras = 1 segundo de média
int amostras[NUM_AMOSTRAS];
int indiceAmostra = 0;
long totalAmostras = 0;
int mediaRuido = 0;

// ======================================
//   PROTÓTIPOS DAS FUNÇÕES 
// ======================================
int getSoundAmplitude();
void controlarLEDs(int nivelRuído);
void checarAlertaDiscord();
void enviarDadosThingSpeak(int nivelRuido);
void enviarMensagemDiscord(String mensagemConteudo); 
void dispararAlertaDiscord();
void conectarWiFi();


// ==================
// FUNÇÃO DE SETUP
// ==================
void setup() {
  Serial.begin(115200);

  // Configura pinos de saída
  pinMode(LED_AMARELO_PIN, OUTPUT);
  pinMode(LED_VERMELHO_PIN, OUTPUT);
  digitalWrite(LED_AMARELO_PIN, LOW);
  digitalWrite(LED_VERMELHO_PIN, LOW);

  // Inicializa o array da média móvel
  for (int i = 0; i < NUM_AMOSTRAS; i++) {
    amostras[i] = 0;
  }
  
  // 1. Conecta ao WiFi
  conectarWiFi();
  
  // 2. MANDA MENSAGEM DE "ONLINE" PARA O DISCORD
  if (WiFi.status() == WL_CONNECTED) {
    enviarMensagemDiscord("✅ O Monitor de Ruído está ONLINE e funcionando.");
  }
}

// ==================
// FUNÇÃO DE LOOP
// ==================
void loop() {
  int nivelRuido = getSoundAmplitude(); 

  // --- CÁLCULO DA MÉDIA MÓVEL (Filtro de Suavização) ---
  totalAmostras = totalAmostras - amostras[indiceAmostra]; // Remove a leitura mais antiga
  amostras[indiceAmostra] = nivelRuido;                    // Salva a nova leitura
  totalAmostras = totalAmostras + nivelRuido;              // Adiciona a nova leitura
  indiceAmostra = indiceAmostra + 1;
  if (indiceAmostra >= NUM_AMOSTRAS) {
    indiceAmostra = 0; // Reinicia o índice
  }
  mediaRuido = totalAmostras / NUM_AMOSTRAS; // Calcula a média

  // Debug: Imprime os valores instantâneos e médios
  Serial.print("Ruído Instantâneo: ");
  Serial.print(nivelRuido);
  Serial.print("  |  Ruído MÉDIO: ");
  Serial.println(mediaRuido);

  
  // 2. Controlar os LEDs usando a MÉDIA
  controlarLEDs(mediaRuido); 

  // 3. Enviar dados para o ThingSpeak (Checa o intervalo de tempo)
  unsigned long agora = millis();
  if (agora - tempoAnteriorThingSpeak >= intervaloThingSpeak) {
    enviarDadosThingSpeak(mediaRuido);
    tempoAnteriorThingSpeak = agora;
  }

  // 4. Checar o alerta do Discord
  checarAlertaDiscord();
}


// ==================
// FUNÇÕES AUXILIARES
// ==================

// Lê o sensor analógico e retorna a amplitude (Pico a Pico)
int getSoundAmplitude() {
  unsigned long startTime = millis();
  unsigned int signalMax = 0;
  unsigned int signalMin = 4095;

  while (millis() - startTime < sampleWindow) {
    int sample = analogRead(MIC_PIN); 
    if (sample < 4095) { 
      if (sample > signalMax) {
        signalMax = sample;
      } else if (sample < signalMin) {
        signalMin = sample;
      }
    }
  }
  return signalMax - signalMin; // Amplitude Pico a Pico
}

// Controla a lógica do semáforo de LEDs
void controlarLEDs(int nivelRuído) {
  if (nivelRuído >= LIMITE_VERMELHO) { 
    // Nível ALTO (Vermelho)
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, HIGH);
    
    if (tempoInicioLedVermelho == 0) {
      tempoInicioLedVermelho = millis(); // Inicia o contador
    }
    alertaEnviado = false;

  } else if (nivelRuído >= LIMITE_AMARELO) {
    // Nível MÉDIO (Amarelo)
    digitalWrite(LED_AMARELO_PIN, HIGH);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; // Zera o contador de alerta
    
  } else {
    // Nível BAIXO (Silêncio)
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; // Zera o contador de alerta
  }
}

// Checa se o tempo de alerta foi atingido
void checarAlertaDiscord() {
  if (tempoInicioLedVermelho != 0 && !alertaEnviado) {
    if (millis() - tempoInicioLedVermelho >= intervaloAlertaRuido) {
      Serial.println(">>> TEMPO DE ALERTA ATINGIDO! Enviando notificação para o Discord...");
      dispararAlertaDiscord(); 
      alertaEnviado = true; 
      tempoInicioLedVermelho = 0;
    }
  }
}

// Envia a MÉDIA de ruído para o Field 2 do ThingSpeak
void enviarDadosThingSpeak(int nivelRuido) { 
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.thingspeak.com/update?api_key=" + thingSpeakApiKey + "&field2=" + String(nivelRuido); // Envia para Field 2
    
    http.begin(url);
    http.GET(); // Apenas executa a requisição
    http.end();
  }
}

// Função genérica para enviar qualquer mensagem ao Discord via Webhook
void enviarMensagemDiscord(String mensagemConteudo) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(discordWebhookUrl);
    http.addHeader("Content-Type", "application/json");
    
    String mensagem = "{\"content\":\"" + mensagemConteudo + "\"}";
    
    http.POST(mensagem);
    http.end();
  }
}

// Dispara o alerta de barulho
void dispararAlertaDiscord() {
  enviarMensagemDiscord("ALERTA: Barulho excessivo detectado na biblioteca!");
}

// Conecta o ESP32 ao WiFi
void conectarWiFi() {
  Serial.print("Conectando ao WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("\nWiFi Conectado!");
    Serial.print("Endereço IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFalha ao conectar no WiFi. Verifique a senha.");
  }
}
