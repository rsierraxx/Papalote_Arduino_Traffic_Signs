/*
 * Cliente ESP8266-01S v3.0 - Con IP ESTÁTICA
 * 
 * VENTAJAS:
 * - Arranque más rápido
 * - Sin necesidad de registro
 * - Conexión directa con maestro
 * 
 * CONFIGURACIÓN:
 * 1. Cambiar MODULE_ID para cada ESP (1-12)
 * 2. La IP se calcula automáticamente: 192.168.0.(100 + MODULE_ID)
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN - CAMBIAR ESTOS VALORES
// =============================================================================

const int MODULE_ID = 12;                          // CAMBIAR! (1-12) ID único

const char* ROUTER_SSID = "pmn_seniales";         // Nombre WiFi
const char* ROUTER_PASS = "2WC456403581";        // Contraseña

// CONFIGURACIÓN DE RED - AJUSTAR SEGÚN TU RED
IPAddress local_IP(192, 168, 0, 100 + MODULE_ID); // .101, .102, etc.
IPAddress gateway(192, 168, 0, 1);                // IP del router
IPAddress subnet(255, 255, 255, 0);               // Máscara de red
IPAddress master_IP(192, 168, 0, 100);            // IP FIJA del maestro

// =============================================================================
// CONFIGURACIÓN DEL SISTEMA
// =============================================================================

#define RELAY_PIN 0
#define LED_PIN 2
#define UDP_PORT 8888
#define AUTO_OFF_DELAY 3000
#define HEARTBEAT_INTERVAL 30000

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
  Serial.println(F("\n\nESP8266 LED v3.0 - IP Estatica"));
  Serial.print(F("Modulo ID: "));
  Serial.println(MODULE_ID);
  Serial.print(F("IP asignada: "));
  Serial.println(local_IP);
  
  bootTime = millis();
  
  // Configurar pines
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH);
  
  // Conectar con IP estática
  connectWiFi();
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Verificar WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi perdido!"));
    delay(5000);
    ESP.restart();
  }
  
  // Auto-apagar relay
  if (relayOn && millis() - relayOnTime >= AUTO_OFF_DELAY) {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, HIGH);
    relayOn = false;
    Serial.println(F("Auto OFF"));
  }
  
  // Heartbeat simple
  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  
  // Escuchar UDP
  handleUDP();
  
  // Estado cada minuto
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 60000) {
    printStatus();
    lastStatus = millis();
  }
  
  delay(10);
}

// =============================================================================
// FUNCIONES
// =============================================================================

void connectWiFi() {
  Serial.println(F("Configurando IP estatica..."));
  
  // Configurar IP estática ANTES de WiFi.begin()
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
    digitalWrite(LED_PIN, attempts % 2);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n✓ Conectado!"));
    Serial.print(F("IP real: "));
    Serial.println(WiFi.localIP());
    Serial.print(F("RSSI: "));
    Serial.print(WiFi.RSSI());
    Serial.println(F(" dBm"));
    
    // LED indica conexión OK
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }
    
    // Iniciar UDP
    udp.begin(UDP_PORT);
    Serial.println(F("UDP listo"));
    
    // Anunciar presencia
    announcePresence();
    
  } else {
    Serial.println(F("\nError WiFi!"));
    delay(5000);
    ESP.restart();
  }
}

void announcePresence() {
  // Enviar anuncio directo al maestro
  Serial.print(F("Anunciando al maestro "));
  Serial.print(master_IP);
  Serial.println(F("..."));
  
  for (int i = 0; i < 3; i++) {
    udp.beginPacket(master_IP, UDP_PORT);
    udp.print(F("HELLO:"));
    udp.print(MODULE_ID);
    udp.print(F(":"));
    udp.print(local_IP);
    udp.endPacket();
    delay(100);
  }
}

void sendHeartbeat() {
  // Heartbeat directo por UDP (sin HTTP)
  udp.beginPacket(master_IP, UDP_PORT);
  udp.print(F("HB:"));
  udp.print(MODULE_ID);
  udp.print(F(":"));
  udp.print(relayOn ? 1 : 0);
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
      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(LED_PIN, LOW);
      relayOn = true;
      relayOnTime = millis();
      Serial.println(F("\nON"));
    }
    else if (strcmp(buffer, "OFF") == 0) {
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_PIN, HIGH);
      relayOn = false;
      Serial.println(F("\nOFF"));
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
  Serial.print(F("IP: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("WiFi: "));
  Serial.print(WiFi.RSSI());
  Serial.println(F(" dBm"));
  Serial.print(F("Relay: "));
  Serial.println(relayOn ? F("ON") : F("OFF"));
  Serial.print(F("Uptime: "));
  Serial.print((millis() - bootTime) / 60000);
  Serial.println(F(" min"));
  Serial.print(F("Heap: "));
  Serial.print(ESP.getFreeHeap());
  Serial.println(F(" bytes"));
}