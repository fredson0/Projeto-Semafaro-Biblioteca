/*
 * PROJETO: SEMÁFORO DE RUÍDO IoT (Versão com Microfone Digital I2S)
 * Autor: (Seu Nome)
 *
 * Componentes:
 * - ESP32
 * - Microfone I2S (INMP441 ou similar)
 * - 3 LEDs (Verde, Amarelo, Vermelho)
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include "driver/i2s.h" // <-- Biblioteca para o microfone I2S

// --- CONFIGURAÇÕES DO USUÁRIO (TUDO PREENCHIDO) ---
const char* ssid = "detector-conversa";
const char* password = "12345678";
String thingSpeakApiKey = "XXHKUFADAP5IFK1S"; 
String discordWebhookUrl = "https://discord.com/api/webhooks/1438165252041609307/AvdU1fdwQuqXlI4cK3lZdtA4lJpJv9EO92lcKXUR7RpmR0fgC3IQdxtr9ACM6WVyzWQp"; 

// --- CONFIGURAÇÃO DO HARDWARE (PINOS) ---
// *** PINOS DOS LEDS MUDARAM! ***
const int LED_VERDE_PIN = 18;
const int LED_AMARELO_PIN = 19;
const int LED_VERMELHO_PIN = 21;

// --- CONFIGURAÇÃO DO MICROFONE I2S ---
const i2s_port_t I2S_PORT = I2S_NUM_0;
const int I2S_SD_PIN = 34; // SD (Dados)
const int I2S_WS_PIN = 25; // WS (Word Select)
const int I2S_SCK_PIN = 26; // SCK (Clock)
const int I2S_BUFFER_SIZE = 512; // Tamanho do buffer de leitura

// --- CALIBRAÇÃO (PARA AJUSTAR AMANHÃ) ---
// Os valores de I2S são diferentes do analógico. Teremos que ajustar!
int LIMITE_AMARELO = 5000;   // Chute inicial (0 a 30.000+)
int LIMITE_VERMELHO = 15000; // Chute inicial


// --- VARIÁVEIS DE CONTROLE DE TEMPO ---
long intervaloThingSpeak = 60000; 
unsigned long tempoAnteriorThingSpeak = 0;
long intervaloAlertaRuido = 30000; 
unsigned long tempoInicioLedVermelho = 0;
bool alertaEnviado = false; 

// ==================
// FUNÇÃO DE SETUP
// ==================
void setup() {
  Serial.begin(115200); 
  
  // Define os pinos dos LEDs como SAÍDA
  pinMode(LED_VERDE_PIN, OUTPUT);
  pinMode(LED_AMARELO_PIN, OUTPUT);
  pinMode(LED_VERMELHO_PIN, OUTPUT);

  // Acende o LED verde para mostrar que ligou
  digitalWrite(LED_VERDE_PIN, HIGH);
  digitalWrite(LED_AMARELO_PIN, LOW);
  digitalWrite(LED_VERMELHO_PIN, LOW);
  
  // Inicia o microfone I2S
  iniciarMicrofoneI2S();

  // Inicia a conexão Wi-Fi
  conectarWiFi();
}

// ==================
// FUNÇÃO DE LOOP
// ==================
void loop() {
  // 1. Ler o nível de ruído do microfone I2S
  int nivelRuido = lerMicrofone();

  // 2. Controlar os LEDs com base no ruído
  controlarLEDs(nivelRuido);

  // 3. Enviar dados para o ThingSpeak
  unsigned long agora = millis();
  if (agora - tempoAnteriorThingSpeak >= intervaloThingSpeak) {
    enviarDadosThingSpeak(nivelRuido);
    tempoAnteriorThingSpeak = agora; 
  }

  // 4. Checar se o alerta do Discord deve ser enviado
  checarAlertaDiscord();
}


// ==================
// FUNÇÕES AUXILIARES
// ==================

// *** NOVA FUNÇÃO PARA INICIAR O I2S ***
void iniciarMicrofoneI2S() {
  Serial.println("Configurando Microfone I2S...");
  
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = 0,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK_PIN, // SCK
    .ws_io_num = I2S_WS_PIN,  // WS
    .data_out_num = I2S_PIN_NO_CHANGE, // Não usado (só estamos recebendo)
    .data_in_num = I2S_SD_PIN     // SD
  };

  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_set_clk(I2S_PORT, 44100, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_STEREO);
}


// *** FUNÇÃO ATUALIZADA PARA LER O I2S ***
// Lê o microfone e calcula o volume (RMS)
int lerMicrofone() {
  int32_t amostras[I2S_BUFFER_SIZE];
  size_t bytesLidos = 0;

  // Lê um bloco de dados do microfone
  i2s_read(I2S_PORT, &amostras, sizeof(amostras), &bytesLidos, portMAX_DELAY);

  if (bytesLidos == 0) {
    return 0;
  }
  
  double somaQuadrados = 0;
  int numeroAmostras = bytesLidos / sizeof(int32_t);

  // Calcula o RMS (Root Mean Square) para achar o "volume"
  for (int i = 0; i < numeroAmostras; i++) {
    // Os dados vêm em 32 bits, mas o som real está nos 24 bits mais altos
    // (Por isso o shift >> 8)
    int32_t amostraLimpa = amostras[i] >> 8; 
    
    // Ignora amostras estranhas (às vezes acontece)
    if (amostraLimpa > 2000000 || amostraLimpa < -2000000) {
      continue;
    }
    
    somaQuadrados += (double)amostraLimpa * (double)amostraLimpa;
  }

  double mediaQuadrados = somaQuadrados / numeroAmostras;
  double rms = sqrt(mediaQuadrados);
  
  int nivelRuido = (int)rms; // Converte para um inteiro

  // IMPORTANTE: Imprime o valor no Monitor Serial
  // É ASSIM que você vai calibrar (ver os números amanhã)
  Serial.print("Nível de Ruído (RMS): ");
  Serial.println(nivelRuido);
  
  delay(50); 
  return nivelRuido;
}

// Controla qual LED deve acender (lógica idêntica, só os pinos mudaram)
void controlarLEDs(int nivelRuído) {
  if (nivelRuído >= LIMITE_VERMELHO) {
    // Nível ALTO (Vermelho)
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, HIGH);
    
    if (tempoInicioLedVermelho == 0) {
      tempoInicioLedVermelho = millis();
    }
    alertaEnviado = false; 

  } else if (nivelRuído >= LIMITE_AMARELO) {
    // Nível MÉDIO (Amarelo)
    digitalWrite(LED_VERDE_PIN, LOW);
    digitalWrite(LED_AMARELO_PIN, HIGH);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; 
    
  } else {
    // Nível BAIXO (Verde)
    digitalWrite(LED_VERDE_PIN, HIGH);
    digitalWrite(LED_AMARELO_PIN, LOW);
    digitalWrite(LED_VERMELHO_PIN, LOW);
    tempoInicioLedVermelho = 0; 
  }
}

// Verifica se o LED vermelho está aceso por tempo suficiente
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

// Envia os dados de ruído para o ThingSpeak
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

// Dispara o alerta para o Discord
void dispararAlertaDiscord() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(discordWebhookUrl); 
    http.addHeader("Content-Type", "application/json"); 
    String mensagem = "{\"content\":\"ALERTA: Barulho excessivo detectado na biblioteca!\"}";
    int httpCode = http.POST(mensagem); 
    
    if (httpCode > 0) {
      Serial.printf("[Discord] Alerta enviado com sucesso, código: %d\n", httpCode);
    } else {
      Serial.printf("[Discord] Falha no envio do alerta, erro: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); 
  } else {
    Serial.println("WiFi desconectado. Não foi possível enviar alerta Discord.");
  }
}

// Conecta ao Wi-Fi
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
