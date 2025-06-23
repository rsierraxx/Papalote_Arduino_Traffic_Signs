/*
 * Sistema de Control de Focos - Cliente Control Remoto
 * Versión: 4.0 - Ultra simplificado
 * 
 * Características:
 * - Solo envía comandos UDP, sin esperar respuesta
 * - Sin heartbeat ni configuración dinámica
 * - Máxima velocidad y confiabilidad
 * - LEDs simples de confirmación
 */

#include "WiFiS3.h"
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN
// =============================================================================

const char* ssid = "LED_CONTROL_SYSTEM";
const char* password = "12345678";
const char* masterIP = "192.168.4.1";
const int udpPort = 8888;

// Pines
#define BUTTON1_PIN 2
#define BUTTON2_PIN 3
#define BUTTON3_PIN 4
#define LED1_PIN 5
#define LED2_PIN 6
#define LED3_PIN 7

// Tiempos
#define DEBOUNCE_TIME 50
#define MIN_PRESS_INTERVAL 3500  // 3.5 segundos entre pulsaciones
#define LED_ON_TIME 200         // LED encendido 200ms

// Comandos
const char* CMD_GROUP_1 = "button_group_1";
const char* CMD_GROUP_2 = "button_group_2";
const char* CMD_GROUP_3 = "button_group_3";

// =============================================================================
// VARIABLES
// =============================================================================

WiFiUDP udp;
bool wifiConnected = false;

struct Button {
  bool lastState = HIGH;
  bool currentState = HIGH;
  unsigned long lastDebounce = 0;
  unsigned long lastPress = 0;
};

Button buttons[3];
unsigned long ledOffTime[3] = {0, 0, 0};

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== CONTROL REMOTO v4.0 ===");
  Serial.println("Modo: ULTRA SIMPLE");
  
  // Configurar pines
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  // LEDs apagados
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  
  // Conectar WiFi
  connectWiFi();
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Verificar WiFi
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      wifiConnected = false;
      Serial.println("WiFi perdido!");
    }
    // Intentar reconectar cada 5 segundos
    static unsigned long lastReconnect = 0;
    if (millis() - lastReconnect > 5000) {
      lastReconnect = millis();
      connectWiFi();
    }
  }
  
  // Leer botones
  bool states[3] = {
    digitalRead(BUTTON1_PIN),
    digitalRead(BUTTON2_PIN),
    digitalRead(BUTTON3_PIN)
  };
  
  // Procesar cada botón
  for (int i = 0; i < 3; i++) {
    // Debounce
    if (states[i] != buttons[i].lastState) {
      buttons[i].lastDebounce = millis();
    }
    
    if ((millis() - buttons[i].lastDebounce) > DEBOUNCE_TIME) {
      if (states[i] != buttons[i].currentState) {
        buttons[i].currentState = states[i];
        
        // Botón presionado (LOW)
        if (buttons[i].currentState == LOW) {
          // Verificar intervalo mínimo
          if (millis() - buttons[i].lastPress >= MIN_PRESS_INTERVAL) {
            processButton(i);
            buttons[i].lastPress = millis();
          } else {
            // Parpadeo rápido = esperar
            for (int j = 0; j < 3; j++) {
              digitalWrite(LED1_PIN + i, HIGH);
              delay(50);
              digitalWrite(LED1_PIN + i, LOW);
              delay(50);
            }
          }
        }
      }
    }
    
    buttons[i].lastState = states[i];
  }
  
  // Apagar LEDs automáticamente
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    if (ledOffTime[i] > 0 && now >= ledOffTime[i]) {
      digitalWrite(LED1_PIN + i, LOW);
      ledOffTime[i] = 0;
    }
  }
  
  delay(10);
}

// =============================================================================
// FUNCIONES
// =============================================================================

void connectWiFi() {
  Serial.print("Conectando a WiFi");
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    udp.begin(udpPort);
    
    Serial.println("\nWiFi conectado!");
    Serial.println("IP: " + WiFi.localIP().toString());
    
    // Confirmación visual
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED1_PIN, HIGH);
      digitalWrite(LED2_PIN, HIGH);
      digitalWrite(LED3_PIN, HIGH);
      delay(100);
      digitalWrite(LED1_PIN, LOW);
      digitalWrite(LED2_PIN, LOW);
      digitalWrite(LED3_PIN, LOW);
      delay(100);
    }
  } else {
    Serial.println("\nError WiFi!");
  }
}

void processButton(int button) {
  Serial.print("Botón " + String(button + 1) + " → ");
  
  // Verificar WiFi
  if (!wifiConnected) {
    Serial.println("Sin WiFi!");
    // LED rojo rápido
    digitalWrite(LED1_PIN + button, HIGH);
    delay(50);
    digitalWrite(LED1_PIN + button, LOW);
    return;
  }
  
  // Encender LED
  digitalWrite(LED1_PIN + button, HIGH);
  ledOffTime[button] = millis() + LED_ON_TIME;
  
  // Enviar comando
  const char* command = "";
  switch(button) {
    case 0: command = CMD_GROUP_1; break;
    case 1: command = CMD_GROUP_2; break;
    case 2: command = CMD_GROUP_3; break;
  }
  
  udp.beginPacket(masterIP, udpPort);
  udp.print(command);
  udp.endPacket();
  
  Serial.println(command);
}