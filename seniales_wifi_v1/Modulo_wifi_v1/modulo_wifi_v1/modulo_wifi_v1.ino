/*
 * Cliente ESP8266-01S v3.2 - LÓGICA INVERTIDA
 * 
 * SOLUCIÓN: El relay ahora funciona al revés
 * - GPIO0 = HIGH → Foco APAGADO (estado normal)
 * - GPIO0 = LOW → Foco ENCENDIDO
 * 
 * Esto evita que el foco se encienda al iniciar
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN - CAMBIAR ESTOS VALORES
// =============================================================================

const int MODULE_ID = 12;                          // CAMBIAR! (1-12) ID único

const char* ROUTER_SSID = "pmn_senviales";        // Nombre WiFi
const char* ROUTER_PASS = "2WC456403581";         // Contraseña

// CONFIGURACIÓN DE RED
IPAddress local_IP(192, 168, 0, 100 + MODULE_ID);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress master_IP(192, 168, 0, 100);

// =============================================================================
// CONFIGURACIÓN DEL SISTEMA
// =============================================================================

#define RELAY_PIN 0
#define LED_PIN 2
#define UDP_PORT 8888
#define AUTO_OFF_DELAY 3000
#define HEARTBEAT_INTERVAL 15000

// LÓGICA INVERTIDA PARA EL RELAY
#define RELAY_ON LOW     // LOW enciende el foco
#define RELAY_OFF HIGH   // HIGH apaga el foco (estado seguro)

// Variables globales
WiFiUDP udp;
bool relayOn = false;
unsigned long relayOnTime = 0;
unsigned long lastHeartbeat = 0;
unsigned long bootTime = 0;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n\nESP8266 v3.2 - Logica Invertida"));
  Serial.print(F("Modulo ID: "));
  Serial.println(MODULE_ID);
  
  bootTime = millis();
  
  // IMPORTANTE: Configurar relay ANTES de cualquier otra cosa
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);  // Asegurar que empiece APAGADO
  
  // LED indicador
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);  // LED apagado
  
  Serial.println(F("Relay configurado en modo INVERTIDO"));
  Serial.println(F("HIGH = Foco APAGADO (seguro)"));
  Serial.println(F("LOW = Foco ENCENDIDO"));
  
  // Pequeña pausa para estabilizar
  delay(100);
  
  // Conectar WiFi
  connectWiFi();
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Verificar WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi perdido!"));
    digitalWrite(RELAY_PIN, RELAY_OFF);  // Seguridad: apagar relay
    delay(5000);
    ESP.restart();
  }
  
  // Auto-apagar relay
  if (relayOn && millis() - relayOnTime >= AUTO_OFF_DELAY) {
    turnOffRelay();
    Serial.println(F("Auto OFF"));
  }
  
  // Heartbeat
  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    digitalWrite(LED_PIN, LOW);   // LED on durante HB
    sendHeartbeat();
    lastHeartbeat = millis();
    delay(50);
    digitalWrite(LED_PIN, HIGH);  // LED off
  }
  
  // Escuchar UDP
  handleUDP();
  
  // Estado cada 30 segundos
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    printStatus();
    lastStatus = millis();
  }
  
  // Comandos serie
  if (Serial.available()) {
    char cmd = Serial.read();
    switch(cmd) {
      case '1': turnOnRelay(); break;
      case '0': turnOffRelay(); break;
      case 's': printStatus(); break;
      case 'h': sendHeartbeat(); break;
    }
  }
  
  delay(10);
}

// =============================================================================
// FUNCIONES DE CONTROL DEL RELAY
// =============================================================================

void turnOnRelay() {
  digitalWrite(RELAY_PIN, RELAY_ON);   // LOW = Encender
  digitalWrite(LED_PIN, LOW);          // LED encendido
  relayOn = true;
  relayOnTime = millis();
  Serial.println(F(">>> RELAY ON (GPIO0 = LOW) <<<"));
}

void turnOffRelay() {
  digitalWrite(RELAY_PIN, RELAY_OFF);  // HIGH = Apagar
  digitalWrite(LED_PIN, HIGH);         // LED apagado
  relayOn = false;
  Serial.println(F(">>> RELAY OFF (GPIO0 = HIGH) <<<"));
}

// =============================================================================
// FUNCIONES WiFi
// =============================================================================

void connectWiFi() {
  Serial.println(F("\nConfigurando IP estatica..."));
  
  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println(F("Error config IP!"));
  }
  
  Serial.print(F("Conectando WiFi"));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ROUTER_SSID, ROUTER_PASS);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(F("."));
    
    // Asegurar que relay siga apagado durante conexión
    digitalWrite(RELAY_PIN, RELAY_OFF);
    
    digitalWrite(LED_PIN, attempts % 2);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nConectado!"));
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
    
    // Parpadeo de confirmación
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }
    
    udp.begin(UDP_PORT);
    announcePresence();
    
  } else {
    Serial.println(F("\nError WiFi!"));
    delay(5000);
    ESP.restart();
  }
}

void announcePresence() {
  Serial.print(F("Anunciando al maestro..."));
  
  for (int i = 0; i < 3; i++) {
    udp.beginPacket(master_IP, UDP_PORT);
    udp.print(F("HELLO:"));
    udp.print(MODULE_ID);
    udp.print(F(":"));
    udp.print(local_IP);
    udp.endPacket();
    delay(100);
  }
  Serial.println(F(" OK"));
}

void sendHeartbeat() {
  udp.beginPacket(master_IP, UDP_PORT);
  udp.print(F("HB:"));
  udp.print(MODULE_ID);
  udp.print(F(":"));
  udp.print(relayOn ? 1 : 0);
  udp.print(F(":v3.2"));
  udp.endPacket();
  
  Serial.print(F("♥"));
}

void handleUDP() {
  if (!udp.parsePacket()) return;
  
  char buffer[32];
  int len = udp.read(buffer, 31);
  buffer[len] = 0;
  
  IPAddress senderIP = udp.remoteIP();
  
  // Solo aceptar del maestro
  if (senderIP == master_IP) {
    if (strcmp(buffer, "ON") == 0) {
      turnOnRelay();
    }
    else if (strcmp(buffer, "OFF") == 0) {
      turnOffRelay();
    }
    else if (strcmp(buffer, "PING") == 0) {
      udp.beginPacket(senderIP, UDP_PORT);
      udp.print(F("PONG:"));
      udp.print(MODULE_ID);
      udp.endPacket();
      Serial.print(F("O"));
    }
  }
}

void printStatus() {
  Serial.println(F("\n=== STATUS ==="));
  Serial.print(F("Modulo: "));
  Serial.println(MODULE_ID);
  Serial.print(F("Relay: "));
  Serial.print(relayOn ? F("ON") : F("OFF"));
  Serial.print(F(" (GPIO0="));
  Serial.print(digitalRead(RELAY_PIN) ? F("HIGH)") : F("LOW)"));
  Serial.print(F(" WiFi: "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
  Serial.println(F("Comandos: 1=ON, 0=OFF, s=status"));
}