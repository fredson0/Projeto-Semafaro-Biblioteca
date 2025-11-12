/*
 * PROJETO: SEMÁFORO DE RUÍDO IoT (Versão com Discord)
 * Autor: (Seu Nome)
 *
 * Componentes:
 * - ESP32
 * - Microfone (Módulo KY-037 ou similar)
 * - 3 LEDs (Verde, Amarelo, Vermelho)
 */

// Bibliotecas para Wi-Fi e para "chamar" os links (URLs)
#include <WiFi.h>
#include <HTTPClient.h>

// --- CONFIGURAÇÕES DO USUÁRIO (TUDO PREENCHIDO) ---

// Credenciais do Wi-Fi (JÁ PREENCHIDAS)
const char* ssid = "detector-conversa";
const char* password = "12345678";

// Chave que você pegou no ThingSpeak (JÁ PREENCHIDA)
String thingSpeakApiKey = "XXHKUFADAP5IFK1S"; 

// Link do Webhook do Discord (JÁ PREENCHIDO)
String discordWebhookUrl = "https://discord.com/api/webhooks/1438165252041609307/AvdU1fdwQuqXlI4cK3lZdtA4lJpJv9EO92lcKXUR7RpmR0fgC3IQdxtr9ACM6WVyzWQp"; 
// --- FIM DAS CONFIGURAÇÕES ---


// --- CONFIGURAÇÃO DO HARDWARE (PINOS) ---
// Amanhã, verifique se são esses pinos que você vai usar
const int MIC_PIN = 34;         // Pino ANALÓGICO para o microfone
const int LED_VERDE_PIN = 25;
const int LED_AMARELO_PIN = 26;
const int LED_VERMELHO_PIN = 27;


// --- CALIBRAÇÃO (PARA AJUSTAR AMANHÃ) ---
// Estes valores são CHUTES! Você VAI PRECISAR mudar eles amanhã.
// O ESP32 lê valores analógicos de 0 a 4095.
int LIMITE_AMARELO = 1500;  // Acima deste valor, acende o amarelo
int LIMITE_VERMELHO = 2500; // Acima deste valor, acende o vermelho


// --- VARIÁVEIS DE CONTROLE DE TEMPO ---
// Usamos millis() para não travar o código com 'delay()'

// Intervalo para enviar dados para o ThingSpeak (ex: 60 segundos)
long intervaloThingSpeak = 60000; 
unsigned long tempoAnteriorThingSpeak = 0;

// Tempo que o LED vermelho precisa ficar aceso para disparar o alerta (ex: 30 segundos)
long intervaloAlertaRuido = 30000; 
unsigned long tempoInicioLedVermelho = 0;
bool alertaEnviado = false; // Controle para não enviar 1000 alertas


// ==================
// FUNÇÃO DE SETUP (Roda uma vez quando liga)
// ==================
void setup() {
  Serial.begin(115200); // Inicia o monitor serial (para ver os valores do microfone)
  
  // Define os pinos dos LEDs como SAÍDA
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_AMARELO_PIN, OUTPUT);
  pinMode(LED_VERMELHO_PIN, OUTPUT);

  // Define o pino do Microfone como ENTRADA
  pinMode(MIC_PIN, INPUT);

  // Acende o LED verde para mostrar que ligou
  digitalWrite(LED_VERDE_PIN, HIGH);
  digitalWrite(LED_AMARELO_PIN, LOW);
  digitalWrite(LED_VERMELHO_PIN, LOW);

  // Inicia a conexão Wi-Fi
  conectarWiFi();
}


// ==================
// FUNÇÃO DE LOOP (Roda para sempre)
// ==================
void loop() {
  // 1. Ler o nível de ruído do microfone
  int nivelRuido = lerMicrofone();

  // 2. Controlar os LEDs com base no ruído
  controlarLEDs(nivelRuido);

  // 3. Enviar dados para o ThingSpeak (sem travar)
  unsigned long agora = millis();
  if (agora - tempoAnteriorThingSpeak >= intervaloThingSpeak) {
    enviarDadosThingSpeak(nivelRuido);
    tempoAnteriorThingSpeak = agora; // Reseta o contador
  }

  // 4. Checar se o alerta do Discord deve ser enviado
  checarAlertaDiscord();
}


// ==================
// FUNÇÕES AUXILIARES
// ==================

// Função para ler o microfone de forma mais estável
int lerMicrofone() {
  // Lê o valor analógico do pino do microfone
  // Isso será um número entre 0 (silêncio) e 4095 (máximo)
  int valor = analogRead(MIC_PIN);

  // IMPORTANTE: Imprime o valor no Monitor Serial
  // É ASSIM que você vai calibrar (ver os números amanhã)
  Serial.print("Nível de Ruído: ");
  Serial.println(valor);
  
  delay(50); // Pequeno delay para estabilizar a leitura
  return valor;
}

// Controla qual LED deve acender
void controlarLEDs(int nivelRuído) {
  if (nivelRuído >= LIMITE_VERMELHO) {
    // Nível ALTO (Vermelho)
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, HIGH);
    
    // Se o LED vermelho acabou de acender, marca o tempo
    if (tempoInicioLedVermelho == 0) {
      tempoInicioLedVermelho = millis();
    }
    alertaEnviado = false; // Reseta o status do alerta

  } else if (nivelRuído >= LIMITE_AMARELO) {
    // Nível MÉDIO (Amarelo)
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_AMARELO_PIN, HIGH);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; // Reseta o timer do alerta
    
  } else {
    // Nível BAIXO (Verde)
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; // Reseta o timer do alerta
  }
}

// Verifica se o LED vermelho está aceso por tempo suficiente
void checarAlertaDiscord() {
  // Se o timer do LED vermelho foi iniciado E o alerta ainda não foi enviado
  if (tempoInicioLedVermelho != 0 && !alertaEnviado) {
