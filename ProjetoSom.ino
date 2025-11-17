/*
 * PROJETO: SEMÁFORO DE RUÍDO IoT (Versão com Microfone ANALÓGICO MAX4466)
 *
 * *** VERSÃO FINAL (COM MENSAGEM DE "ONLINE" NO DISCORD AO INICIAR) ***
 *
 * Componentes:
 * - ESP32
 * - Microfone Analógico (MAX4466)
 * - 2 LEDs (Amarelo, Vermelho)
 */

#include <WiFi.h>
#include <HTTPClient.h>

// --- CONFIGURAÇÕES DO USUÁRIO ---
const char* ssid = "Fred";
const char* password = "sophia24";
String thingSpeakApiKey = "XXHKUFADAP5IFK1S"; 
String discordWebhookUrl = "https://discord.com/api/webhooks/1438165252041609307/AvdU1fdwQuqXlI4cK3lZdtA4lJpJv9EO92lcKXUR7RpmR0fgC3IQdxtr9ACM6WVyzWQp";

// --- CONFIGURAÇÃO DO HARDWARE (PINOS) ---
const int LED_AMARELO_PIN = 19; // "Conversa suave"
const int LED_VERMELHO_PIN = 21; // "Conversa atrapalhando"

// --- CONFIGURAÇÃO DO MICROFONE ANALÓGICO (MAX4466) ---
const int MIC_PIN = 34; // Tem que ser um pino ADC!
const int sampleWindow = 50; // ms

// --- CALIBRAÇÃO (AJUSTADA) ---
int LIMITE_AMARELO = 400; // Nível para "conversa suave"
int LIMITE_VERMELHO = 1000; // Nível para "conversa atrapalhando" (Mais tolerante)


// --- VARIÁVEIS DE CONTROLE DE TEMPO ---
long intervaloThingSpeak = 60000; // 1 minuto
unsigned long tempoAnteriorThingSpeak = 0;
long intervaloAlertaRuido = 30000; // 30 segundos
unsigned long tempoInicioLedVermelho = 0;
bool alertaEnviado = false;


// ======================================
//   PROTÓTIPOS DAS FUNÇÕES 
// ======================================
int getSoundAmplitude();
void controlarLEDs(int nivelRuído);
void checarAlertaDiscord();
void enviarDadosThingSpeak(int nivelRuido);
void enviarMensagemDiscord(String mensagemConteudo); // <-- NOVA FUNÇÃO
void dispararAlertaDiscord();
void conectarWiFi();


// ==================
// FUNÇÃO DE SETUP
// ==================
void setup() {
  Serial.begin(115200);

  pinMode(LED_AMARELO_PIN, OUTPUT);
  pinMode(LED_VERMELHO_PIN, OUTPUT);
  digitalWrite(LED_AMARELO_PIN, LOW);
  digitalWrite(LED_VERMELHO_PIN, LOW);

  // 1. Conecta ao WiFi
  conectarWiFi();
  
  // 2. *** MANDA MENSAGEM DE "ONLINE" PARA O DISCORD ***
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Enviando mensagem de 'Online' para o Discord...");
    enviarMensagemDiscord("✅ O Monitor de Ruído está ONLINE e funcionando.");
  }
  
  Serial.println("Setup concluído. Iniciando loop principal...");
}

// ==================
// FUNÇÃO DE LOOP
// ==================
void loop() {
  int nivelRuido = getSoundAmplitude(); 
  controlarLEDs(nivelRuido); 

  unsigned long agora = millis();
  if (agora - tempoAnteriorThingSpeak >= intervaloThingSpeak) {
    enviarDadosThingSpeak(nivelRuido);
    tempoAnteriorThingSpeak = agora;
  }

  checarAlertaDiscord();
}


// ==================
// FUNÇÕES AUXILIARES
// ==================

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
  unsigned int peakToPeak = signalMax - signalMin;

  Serial.print("Amplitude (Nível Ruído): ");
  Serial.println(peakToPeak);

  return peakToPeak;
}

void controlarLEDs(int nivelRuído) {
  if (nivelRuído >= LIMITE_VERMELHO) { 
    // Nível ALTO (Vermelho)
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, HIGH);
    
    if (tempoInicioLedVermelho == 0) {
      tempoInicioLedVermelho = millis();
    }
    alertaEnviado = false;

  } else if (nivelRuído >= LIMITE_AMARELO) {
    // Nível MÉDIO (Amarelo)
    digitalWrite(LED_AMARELO_PIN, HIGH);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; 
    
  } else {
    // Nível BAIXO (Silêncio)
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; 
  }
}

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

void enviarDadosThingSpeak(int nivelRuido) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://api.thingspeak.com/update?api_key=" + thingSpeakApiKey + "&field1=" + String(nivelRuido);
    
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      Serial.printf("[ThingSpeak] Enviado com sucesso, código: %d\n", httpCode);
    } else {
      Serial.printf("[ThingSpeak] Falha no envio, erro: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println("WiFi desconectado. Não foi possível enviar para o ThingSpeak.");
  }
}

// *** FUNÇÃO GENÉRICA PARA ENVIAR QUALQUER MENSAGEM ***
void enviarMensagemDiscord(String mensagemConteudo) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(discordWebhookUrl);
    http.addHeader("Content-Type", "application/json");
    
    String mensagem = "{\"content\":\"" + mensagemConteudo + "\"}";
    
    int httpCode = http.POST(mensagem);
    
    if (httpCode > 0) {
      Serial.printf("[Discord] Mensagem enviada com sucesso, código: %d\n", httpCode);
    } else {
      Serial.printf("[Discord] Falha no envio da mensagem, erro: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println("WiFi desconectado. Não foi possível enviar mensagem Discord.");
  }
}

void dispararAlertaDiscord() {
  enviarMensagemDiscord("ALERTA: Barulho excessivo detectado na biblioteca!");
}

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
