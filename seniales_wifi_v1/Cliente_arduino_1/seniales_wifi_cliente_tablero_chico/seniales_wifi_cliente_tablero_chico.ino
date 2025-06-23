/*
 * Sistema de Control de Focos - Arduino UNO R4 WiFi (Cliente Controlador)
 * Autor: Sistema Control de Iluminación
 * Versión: 1.0 - Cliente controlador remoto (CORREGIDO)
 * 
 * Descripción:
 * Este cliente Arduino se conecta al maestro como estación (STA) y permite
 * controlar los grupos de focos mediante 3 botones físicos.
 * 
 * Funcionalidades:
 * - Conexión WiFi al Arduino maestro
 * - 3 botones para activar grupos de módulos
 * - LEDs indicadores para cada botón
 * - Retroalimentación visual del estado
 * - Reconexión automática
 * - Interfaz web de estado
 * 
 * Conexiones:
 * - Botón 1: Pin 2 → Activa grupo 1
 * - Botón 2: Pin 3 → Activa grupo 2
 * - Botón 3: Pin 4 → Activa grupo 3
 * - LED 1: Pin 5
 * - LED 2: Pin 6
 * - LED 3: Pin 7
 */

#include "WiFiS3.h"

// =============================================================================
// CONFIGURACIÓN DE RED
// =============================================================================

const char* ssid = "LED_CONTROL_SYSTEM";
const char* password = "12345678";
const char* masterIP = "192.168.4.1";
const int masterPort = 80;

// =============================================================================
// CONFIGURACIÓN DE HARDWARE
// =============================================================================

// Pines de botones
#define BUTTON1_PIN 2
#define BUTTON2_PIN 3
#define BUTTON3_PIN 4
#define BUTTON_DEBOUNCE 50

// Pines de LEDs indicadores
#define LED1_PIN 5
#define LED2_PIN 6
#define LED3_PIN 7
#define LED_AUTO_OFF_DELAY 3000

// Configuración de lógica invertida para LEDs
#define LED_INVERTED true
#define LED_ON  (LED_INVERTED ? LOW : HIGH)
#define LED_OFF (LED_INVERTED ? HIGH : LOW)

// =============================================================================
// CONFIGURACIÓN DE GRUPOS DE MÓDULOS
// =============================================================================

// Define qué módulos controla cada botón
// Estos deben coincidir con la configuración del maestro
const String BUTTON1_COMMAND = "button_group_1";  // Activará los módulos del grupo 1
const String BUTTON2_COMMAND = "button_group_2";  // Activará los módulos del grupo 2
const String BUTTON3_COMMAND = "button_group_3";  // Activará los módulos del grupo 3

// =============================================================================
// ESTRUCTURAS Y VARIABLES
// =============================================================================

struct ButtonState {
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  bool pressed;
  unsigned long lastActivationTime;
  bool isProcessing;
};

struct LedState {
  bool isOn;
  unsigned long turnOnTime;
};

// Estados de botones y LEDs
ButtonState button1 = {HIGH, HIGH, 0, false, 0, false};
ButtonState button2 = {HIGH, HIGH, 0, false, 0, false};
ButtonState button3 = {HIGH, HIGH, 0, false, 0, false};

LedState led1 = {false, 0};
LedState led2 = {false, 0};
LedState led3 = {false, 0};

// Variables de estado
bool wifiConnected = false;
unsigned long lastReconnectAttempt = 0;
unsigned long bootTime = 0;
int commandsSent = 0;
int commandsSuccess = 0;
int commandsFailed = 0;

// Servidor web local
WiFiServer server(80);

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  bootTime = millis();
  
  Serial.println("\n========================================");
  Serial.println("ARDUINO UNO R4 WIFI - CLIENTE CONTROLADOR");
  Serial.println("========================================");
  Serial.println("Version: 1.0");
  Serial.println("Función: Control remoto de grupos de focos");
  Serial.println("Inicializando...\n");
  
  // Configurar pines
  setupPins();
  
  // Mostrar configuración
  printConfiguration();
  
  // Conectar a WiFi
  connectToMaster();
  
  // Iniciar servidor web
  server.begin();
  Serial.println("\nServidor web iniciado en puerto 80");
  
  Serial.println("\n=== SISTEMA LISTO ===");
  Serial.println("IP Local: " + WiFi.localIP().toString());
  Serial.println("Conectado a: " + String(ssid));
  Serial.println("IP Maestro: " + String(masterIP));
  Serial.println("====================\n");
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================

void loop() {
  // Verificar conexión WiFi
  if (WiFi.status() != WL_CONNECTED) {
    handleWiFiReconnection();
  }
  
  // Manejar botones
  handleButtons();
  
  // Manejar apagado automático de LEDs
  handleLedAutoOff();
  
  // Manejar clientes web
  handleWebClients();
  
  delay(10);
}

// =============================================================================
// CONFIGURACIÓN DE HARDWARE
// =============================================================================

void setupPins() {
  Serial.println("Configurando hardware...");
  
  // Configurar botones
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  // Configurar LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  // Asegurar LEDs apagados
  digitalWrite(LED1_PIN, LED_OFF);
  digitalWrite(LED2_PIN, LED_OFF);
  digitalWrite(LED3_PIN, LED_OFF);
  
  // Leer estados iniciales de botones
  button1.lastState = digitalRead(BUTTON1_PIN);
  button1.currentState = button1.lastState;
  button2.lastState = digitalRead(BUTTON2_PIN);
  button2.currentState = button2.lastState;
  button3.lastState = digitalRead(BUTTON3_PIN);
  button3.currentState = button3.lastState;
  
  Serial.println("✓ Hardware configurado");
  
  // Test de LEDs
  Serial.println("\nTest de LEDs...");
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED1_PIN + i, LED_ON);
    delay(200);
    digitalWrite(LED1_PIN + i, LED_OFF);
    delay(100);
  }
  Serial.println("✓ Test completado\n");
}

// =============================================================================
// CONEXIÓN WIFI
// =============================================================================

void connectToMaster() {
  Serial.println("Conectando a red del maestro...");
  Serial.println("SSID: " + String(ssid));
  
  // Arduino UNO R4 WiFi no necesita WiFi.mode()
  WiFi.begin(ssid, password);
  
  // Parpadeo mientras conecta
  bool ledState = false;
  int attempts = 0;
  
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    ledState = !ledState;
    digitalWrite(LED1_PIN, ledState ? LED_ON : LED_OFF);
    digitalWrite(LED2_PIN, ledState ? LED_ON : LED_OFF);
    digitalWrite(LED3_PIN, ledState ? LED_ON : LED_OFF);
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  // Apagar LEDs
  digitalWrite(LED1_PIN, LED_OFF);
  digitalWrite(LED2_PIN, LED_OFF);
  digitalWrite(LED3_PIN, LED_OFF);
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✓ Conectado exitosamente!");
    Serial.println("IP asignada: " + WiFi.localIP().toString());
    Serial.println("Gateway (Maestro): " + WiFi.gatewayIP().toString());
    Serial.println("Intensidad señal: " + String(WiFi.RSSI()) + " dBm");
    
    // Celebración con LEDs
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED1_PIN, LED_ON);
      digitalWrite(LED2_PIN, LED_ON);
      digitalWrite(LED3_PIN, LED_ON);
      delay(100);
      digitalWrite(LED1_PIN, LED_OFF);
      digitalWrite(LED2_PIN, LED_OFF);
      digitalWrite(LED3_PIN, LED_OFF);
      delay(100);
    }
  } else {
    Serial.println("\n✗ Error al conectar");
    wifiConnected = false;
  }
}

void handleWiFiReconnection() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastReconnectAttempt < 5000) return;
  
  lastReconnectAttempt = currentTime;
  wifiConnected = false;
  
  Serial.println("\n⚠ WiFi desconectado. Intentando reconectar...");
  
  // Indicación visual de desconexión
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED1_PIN, LED_ON);
    digitalWrite(LED2_PIN, LED_ON);
    digitalWrite(LED3_PIN, LED_ON);
    delay(200);
    digitalWrite(LED1_PIN, LED_OFF);
    digitalWrite(LED2_PIN, LED_OFF);
    digitalWrite(LED3_PIN, LED_OFF);
    delay(200);
  }
  
  WiFi.disconnect();
  delay(1000);
  connectToMaster();
}

// =============================================================================
// MANEJO DE BOTONES
// =============================================================================

void handleButtons() {
  handleSingleButton(BUTTON1_PIN, &button1, &led1, LED1_PIN, 1, BUTTON1_COMMAND);
  handleSingleButton(BUTTON2_PIN, &button2, &led2, LED2_PIN, 2, BUTTON2_COMMAND);
  handleSingleButton(BUTTON3_PIN, &button3, &led3, LED3_PIN, 3, BUTTON3_COMMAND);
}

void handleSingleButton(int pin, ButtonState* buttonState, LedState* ledState, 
                       int ledPin, int buttonId, String command) {
  int reading = digitalRead(pin);
  
  // Debounce
  if (reading != buttonState->lastState) {
    buttonState->lastDebounceTime = millis();
  }
  
  if ((millis() - buttonState->lastDebounceTime) > BUTTON_DEBOUNCE) {
    if (reading != buttonState->currentState) {
      buttonState->currentState = reading;
      
      // Botón presionado
      if (buttonState->currentState == LOW && !buttonState->isProcessing) {
        buttonState->pressed = true;
        
        // Encender LED
        digitalWrite(ledPin, LED_ON);
        ledState->isOn = true;
        ledState->turnOnTime = millis();
        
        Serial.println("\n🔘 Botón " + String(buttonId) + " presionado");
        Serial.println("💡 LED " + String(buttonId) + " encendido");
        
        // Verificar conexión WiFi
        if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
          Serial.println("❌ No hay conexión WiFi");
          // Parpadeo rápido para indicar error
          for (int i = 0; i < 5; i++) {
            digitalWrite(ledPin, LED_OFF);
            delay(50);
            digitalWrite(ledPin, LED_ON);
            delay(50);
          }
          digitalWrite(ledPin, LED_OFF);
          ledState->isOn = false;
          return;
        }
        
        // Verificar tiempo mínimo entre activaciones
        unsigned long currentTime = millis();
        unsigned long timeSinceLastActivation = currentTime - buttonState->lastActivationTime;
        
        if (timeSinceLastActivation < 4000) { // 4 segundos mínimo
          unsigned long timeToWait = (4000 - timeSinceLastActivation) / 1000;
          Serial.println("⏳ Espera " + String(timeToWait) + "s más");
          digitalWrite(ledPin, LED_OFF);
          ledState->isOn = false;
          return;
        }
        
        buttonState->isProcessing = true;
        buttonState->lastActivationTime = currentTime;
        
        // Enviar comando al maestro
        bool success = sendCommandToMaster(command);
        
        if (success) {
          Serial.println("✅ Comando enviado exitosamente");
          commandsSuccess++;
        } else {
          Serial.println("❌ Error al enviar comando");
          commandsFailed++;
          // Indicar error
          digitalWrite(ledPin, LED_OFF);
          ledState->isOn = false;
        }
        
        buttonState->isProcessing = false;
      }
      // Botón liberado
      else if (buttonState->pressed && buttonState->currentState == HIGH) {
        buttonState->pressed = false;
        buttonState->isProcessing = false;
        Serial.println("🔘 Botón " + String(buttonId) + " liberado");
      }
    }
  }
  
  buttonState->lastState = reading;
}

// =============================================================================
// COMUNICACIÓN CON MAESTRO
// =============================================================================

bool sendCommandToMaster(String command) {
  commandsSent++;
  
  WiFiClient client;
  
  Serial.println("📡 Conectando con maestro en " + String(masterIP) + ":" + String(masterPort));
  
  if (!client.connect(masterIP, masterPort)) {
    Serial.println("❌ No se pudo conectar con el maestro");
    return false;
  }
  
  // Construir petición HTTP
  String httpRequest = "GET /api/command?cmd=" + command + " HTTP/1.1\r\n";
  httpRequest += "Host: " + String(masterIP) + "\r\n";
  httpRequest += "Connection: close\r\n";
  httpRequest += "User-Agent: ArduinoClient/1.0\r\n\r\n";
  
  Serial.println("📤 Enviando: " + command);
  client.print(httpRequest);
  
  // Esperar respuesta
  unsigned long timeout = millis() + 2000;
  while (client.available() == 0) {
    if (millis() > timeout) {
      Serial.println("⏱️ Timeout esperando respuesta");
      client.stop();
      return false;
    }
    delay(10);
  }
  
  // Leer respuesta
  String response = "";
  bool responseStarted = false;
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      responseStarted = true;
    } else if (responseStarted) {
      response += line;
    }
  }
  
  client.stop();
  
  Serial.println("📥 Respuesta: " + response);
  
  // Verificar si fue exitoso
  return response.indexOf("\"status\":\"ok\"") != -1;
}

// =============================================================================
// MANEJO DE LEDs
// =============================================================================

void handleLedAutoOff() {
  unsigned long currentTime = millis();
  
  // LED 1
  if (led1.isOn && (currentTime - led1.turnOnTime >= LED_AUTO_OFF_DELAY)) {
    digitalWrite(LED1_PIN, LED_OFF);
    led1.isOn = false;
    Serial.println("💡 LED 1 apagado automáticamente");
  }
  
  // LED 2
  if (led2.isOn && (currentTime - led2.turnOnTime >= LED_AUTO_OFF_DELAY)) {
    digitalWrite(LED2_PIN, LED_OFF);
    led2.isOn = false;
    Serial.println("💡 LED 2 apagado automáticamente");
  }
  
  // LED 3
  if (led3.isOn && (currentTime - led3.turnOnTime >= LED_AUTO_OFF_DELAY)) {
    digitalWrite(LED3_PIN, LED_OFF);
    led3.isOn = false;
    Serial.println("💡 LED 3 apagado automáticamente");
  }
}

// =============================================================================
// SERVIDOR WEB
// =============================================================================

void handleWebClients() {
  WiFiClient client = server.available();
  if (!client) return;
  
  String request = client.readStringUntil('\r');
  client.flush();
  
  // Generar página HTML
  String html = generateWebInterface();
  
  // Enviar respuesta
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  client.println(html);
  
  delay(1);
  client.stop();
}

String generateWebInterface() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Controlador Remoto</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>";
  html += "body{font-family:Arial;margin:20px;background:#f0f0f0}";
  html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
  html += ".status{background:#e7f3ff;padding:15px;border-radius:5px;margin:20px 0}";
  html += ".stats{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:20px 0}";
  html += ".stat{background:#f5f5f5;padding:10px;border-radius:5px;text-align:center}";
  html += ".button-info{background:#fff3cd;padding:10px;border-radius:5px;margin:10px 0}";
  html += "h1{color:#333;text-align:center}";
  html += ".value{font-size:24px;font-weight:bold;color:#2196F3}";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>🎮 Controlador Remoto de Focos</h1>";
  
  // Estado de conexión
  html += "<div class='status'>";
  html += "<h3>Estado del Sistema</h3>";
  html += "<p><strong>Conexión WiFi:</strong> " + String(WiFi.status() == WL_CONNECTED ? "✅ Conectado" : "❌ Desconectado") + "</p>";
  html += "<p><strong>IP Local:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>IP Maestro:</strong> " + String(masterIP) + "</p>";
  html += "<p><strong>Intensidad Señal:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "<p><strong>Tiempo Activo:</strong> " + String((millis() - bootTime) / 1000) + " segundos</p>";
  html += "</div>";
  
  // Información de botones
  html += "<div class='button-info'>";
  html += "<h3>🔘 Configuración de Botones</h3>";
  html += "<p><strong>Botón 1:</strong> Activa Grupo 1</p>";
  html += "<p><strong>Botón 2:</strong> Activa Grupo 2</p>";
  html += "<p><strong>Botón 3:</strong> Activa Grupo 3</p>";
  html += "<p><em>Nota: Los LEDs se apagan automáticamente después de 3 segundos</em></p>";
  html += "</div>";
  
  // Estadísticas
  html += "<h3>📊 Estadísticas</h3>";
  html += "<div class='stats'>";
  html += "<div class='stat'>Comandos Enviados<br><span class='value'>" + String(commandsSent) + "</span></div>";
  html += "<div class='stat'>Exitosos<br><span class='value'>" + String(commandsSuccess) + "</span></div>";
  html += "<div class='stat'>Fallidos<br><span class='value'>" + String(commandsFailed) + "</span></div>";
  html += "<div class='stat'>Tasa de Éxito<br><span class='value'>" + String(commandsSent > 0 ? (commandsSuccess * 100 / commandsSent) : 0) + "%</span></div>";
  html += "</div>";
  
  // Información técnica
  html += "<div class='status'>";
  html += "<h3>🔧 Información Técnica</h3>";
  html += "<p><strong>Versión:</strong> 1.0</p>";
  html += "<p><strong>Hardware:</strong> Arduino UNO R4 WiFi</p>";
  html += "<p><strong>Modo:</strong> Cliente (STA)</p>";
  
  // MAC Address corregido para Arduino UNO R4 WiFi
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String macStr = "";
  for (int i = 0; i < 6; i++) {
    if (i > 0) macStr += ":";
    if (mac[i] < 16) macStr += "0";
    macStr += String(mac[i], HEX);
  }
  html += "<p><strong>MAC:</strong> " + macStr + "</p>";
  html += "</div>";
  
  html += "</div>";
  
  // Auto-refresh
  html += "<script>setTimeout(()=>location.reload(),5000);</script>";
  
  html += "</body></html>";
  
  return html;
}

// =============================================================================
// UTILIDADES
// =============================================================================

void printConfiguration() {
  Serial.println("=== CONFIGURACIÓN ===");
  Serial.println("Red WiFi: " + String(ssid));
  Serial.println("IP Maestro: " + String(masterIP));
  Serial.println("\nBotones:");
  Serial.println("  Botón 1 (Pin " + String(BUTTON1_PIN) + ") → " + BUTTON1_COMMAND);
  Serial.println("  Botón 2 (Pin " + String(BUTTON2_PIN) + ") → " + BUTTON2_COMMAND);
  Serial.println("  Botón 3 (Pin " + String(BUTTON3_PIN) + ") → " + BUTTON3_COMMAND);
  Serial.println("\nLEDs indicadores:");
  Serial.println("  LED 1: Pin " + String(LED1_PIN));
  Serial.println("  LED 2: Pin " + String(LED2_PIN));
  Serial.println("  LED 3: Pin " + String(LED3_PIN));
  Serial.println("  Auto-apagado: " + String(LED_AUTO_OFF_DELAY/1000) + " segundos");
  Serial.println("===================\n");
}