/*
 * Sistema de Control de Focos - Arduino UNO R4 WiFi (Cliente Controlador)
 * Versión: 2.0 - Simplificado
 * 
 * Descripción:
 * Cliente simplificado que se conecta al maestro y envía comandos
 * sin esperar respuesta, similar a los módulos ESP8266
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
// CONFIGURACIÓN
// =============================================================================

const char* ssid = "LED_CONTROL_SYSTEM";
const char* password = "12345678";
const char* masterIP = "192.168.4.1";
const int masterPort = 80;

// Pines
#define BUTTON1_PIN 2
#define BUTTON2_PIN 3
#define BUTTON3_PIN 4
#define LED1_PIN 5
#define LED2_PIN 6
#define LED3_PIN 7

// Configuración
#define BUTTON_DEBOUNCE 50
#define MIN_ACTIVATION_INTERVAL 3500  // 3.5 segundos entre activaciones
#define RECONNECT_DELAY 5000

// Comandos
const String BUTTON1_COMMAND = "button_group_1";
const String BUTTON2_COMMAND = "button_group_2";
const String BUTTON3_COMMAND = "button_group_3";

// =============================================================================
// VARIABLES
// =============================================================================

struct ButtonState {
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  unsigned long lastActivationTime;
};

ButtonState button1 = {HIGH, HIGH, 0, 0};
ButtonState button2 = {HIGH, HIGH, 0, 0};
ButtonState button3 = {HIGH, HIGH, 0, 0};

bool wifiConnected = false;
unsigned long lastReconnect = 0;
unsigned long bootTime = 0;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  bootTime = millis();
  
  Serial.println("\n========================================");
  Serial.println("CONTROLADOR REMOTO v2.0 - SIMPLIFICADO");
  Serial.println("========================================");
  
  // Configurar pines
  setupPins();
  
  // Conectar WiFi
  connectWiFi();
  
  Serial.println("\n=== SISTEMA LISTO ===");
  Serial.println("IP: " + WiFi.localIP().toString());
  Serial.println("Maestro: " + String(masterIP));
  Serial.println("===================\n");
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Verificar conexión WiFi
  if (WiFi.status() != WL_CONNECTED) {
    handleWiFiReconnection();
  }
  
  // Manejar botones
  handleButtons();
  
  delay(10);
}

// =============================================================================
// CONFIGURACIÓN DE PINES
// =============================================================================

void setupPins() {
  Serial.println("Configurando hardware...");
  
  // Botones
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  // LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  // Apagar LEDs
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  
  // Estados iniciales
  button1.lastState = digitalRead(BUTTON1_PIN);
  button2.lastState = digitalRead(BUTTON2_PIN);
  button3.lastState = digitalRead(BUTTON3_PIN);
  
  Serial.println("✓ Hardware configurado");
  
  // Test rápido de LEDs
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED1_PIN + i, HIGH);
    delay(100);
    digitalWrite(LED1_PIN + i, LOW);
    delay(50);
  }
}

// =============================================================================
// CONEXIÓN WIFI
// =============================================================================

void connectWiFi() {
  Serial.println("Conectando a WiFi...");
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✓ WiFi Conectado!");
    Serial.println("IP: " + WiFi.localIP().toString());
    
    // Parpadeo de confirmación
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED1_PIN, HIGH);
      digitalWrite(LED2_PIN, HIGH);
      digitalWrite(LED3_PIN, HIGH);
      delay(50);
      digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, LOW);
      digitalWrite(LED3_PIN, LOW);
      delay(50);
    }
  } else {
    Serial.println("\n✗ Error conectando WiFi");
    wifiConnected = false;
  }
}

void handleWiFiReconnection() {
  if (millis() - lastReconnect < RECONNECT_DELAY) return;
  
  lastReconnect = millis();
  wifiConnected = false;
  
  Serial.println("⚠ WiFi desconectado. Reconectando...");
  
  WiFi.disconnect();
  delay(1000);
  connectWiFi();
}

// =============================================================================
// MANEJO DE BOTONES
// =============================================================================

void handleButtons() {
  handleSingleButton(BUTTON1_PIN, &button1, LED1_PIN, 1, BUTTON1_COMMAND);
  handleSingleButton(BUTTON2_PIN, &button2, LED2_PIN, 2, BUTTON2_COMMAND);
  handleSingleButton(BUTTON3_PIN, &button3, LED3_PIN, 3, BUTTON3_COMMAND);
}

void handleSingleButton(int pin, ButtonState* state, int ledPin, int id, String command) {
  int reading = digitalRead(pin);
  
  // Debounce
  if (reading != state->lastState) {
    state->lastDebounceTime = millis();
  }
  
  if ((millis() - state->lastDebounceTime) > BUTTON_DEBOUNCE) {
    if (reading != state->currentState) {
      state->currentState = reading;
      
      // Botón presionado (LOW = presionado con pull-up)
      if (state->currentState == LOW) {
        
        // Verificar tiempo mínimo entre activaciones
        unsigned long currentTime = millis();
        if (currentTime - state->lastActivationTime < MIN_ACTIVATION_INTERVAL) {
          Serial.println("⏳ Botón " + String(id) + " - Espera más tiempo");
          return;
        }
        
        Serial.println("\n🔘 Botón " + String(id) + " presionado");
        
        // Encender LED brevemente
        digitalWrite(ledPin, HIGH);
        
        // Verificar WiFi
        if (WiFi.status() != WL_CONNECTED) {
          Serial.println("❌ Sin conexión WiFi");
          // Parpadeo de error
          for (int i = 0; i < 3; i++) {
            digitalWrite(ledPin, LOW);
            delay(100);
            digitalWrite(ledPin, HIGH);
            delay(100);
          }
          digitalWrite(ledPin, LOW);
          return;
        }
        
        // Enviar comando sin esperar respuesta
        sendCommand(command);
        state->lastActivationTime = currentTime;
        
        // Apagar LED después de 200ms
        delay(200);
        digitalWrite(ledPin, LOW);
      }
    }
  }
  
  state->lastState = reading;
}

// =============================================================================
// ENVÍO DE COMANDOS (Sin esperar respuesta)
// =============================================================================

void sendCommand(String command) {
  WiFiClient client;
  client.setTimeout(500);  // Timeout corto
  
  Serial.println("📤 Enviando: " + command);
  
  if (client.connect(masterIP, masterPort)) {
    // Enviar petición HTTP
    String request = "GET /api/command?cmd=" + command + " HTTP/1.1\r\n";
    request += "Host: " + String(masterIP) + "\r\n";
    request += "Connection: close\r\n\r\n";
    
    client.print(request);
    
    // Pequeño delay para asegurar envío
    delay(50);
    
    // Cerrar inmediatamente sin esperar respuesta
    client.stop();
    
    Serial.println("✅ Comando enviado");
  } else {
    Serial.println("❌ Error de conexión");
  }
}