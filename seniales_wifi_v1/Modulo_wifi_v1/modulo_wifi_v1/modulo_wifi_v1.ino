/*
 * Cliente ESP8266 v2.1 - Versión Simple con IP del Maestro
 * 
 * Esta versión no usa auto-discovery, debes configurar la IP del maestro
 * 
 * CONFIGURAR:
 * 1. MODULE_ID único para cada ESP (1-12)
 * 2. ROUTER_SSID y ROUTER_PASS
 * 3. MASTER_IP con la IP real del maestro
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN - CAMBIAR ESTOS VALORES
// =============================================================================

const int MODULE_ID = 1;                        // CAMBIAR! (1-12) ID único

const char* ROUTER_SSID = "sobredosis";     // CAMBIAR!
const char* ROUTER_PASS = "2WC456403581";   // CAMBIAR!

const char* MASTER_IP = "192.168.4.1";        // CAMBIAR! IP del maestro Arduino

// =============================================================================
// CONFIGURACIÓN DEL SISTEMA (no cambiar)
// =============================================================================

#define RELAY_PIN 0
#define LED_PIN 2
#define UDP_PORT 8888
#define API_PORT 8080
#define AUTO_OFF_DELAY 3000
#define HEARTBEAT_INTERVAL 30000
#define REGISTER_RETRY 5000

bool registered = false;
bool relayOn = false;
unsigned long relayOnTime = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastRegister = 0;

WiFiUDP udp;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n========================================");
  Serial.println("ESP8266 v2.1 - Módulo #" + String(MODULE_ID));
  Serial.println("Maestro en: " + String(MASTER_IP));
  Serial.println("========================================");
  
  // Pines
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH);
  
  // Pull-up para el relay
  pinMode(RELAY_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  
  // Conectar WiFi
  connectWiFi();
  
  // UDP
  udp.begin(UDP_PORT);
  Serial.println("UDP puerto: " + String(UDP_PORT));
  
  // Registrar inmediatamente
  delay(1000);
  registerModule();
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Verificar WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi perdido!");
    delay(5000);
    ESP.restart();
  }
  
  // Registrar si no está registrado
  if (!registered && millis() - lastRegister > REGISTER_RETRY) {
    registerModule();
    lastRegister = millis();
  }
  
  // Heartbeat
  if (registered && millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  
  // Auto-apagado
  if (relayOn && millis() - relayOnTime >= AUTO_OFF_DELAY) {
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    relayOn = false;
    Serial.println("💡 Auto-OFF");
  }
  
  // UDP
  handleUDP();
  
  // Estado
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) {
    printStatus();
    lastStatus = millis();
  }
  
  delay(10);
}

// =============================================================================
// CONEXIÓN WIFI
// =============================================================================

void connectWiFi() {
  Serial.print("\nConectando a " + String(ROUTER_SSID));
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ROUTER_SSID, ROUTER_PASS);
  
  // Opcional: IP fija
  /*
  IPAddress ip(192, 168, 1, 100 + MODULE_ID);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(ip, gateway, subnet);
  */
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, attempts % 2);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi conectado!");
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
    digitalWrite(LED_PIN, LOW);
  } else {
    Serial.println("\n✗ Error WiFi!");
    delay(10000);
    ESP.restart();
  }
}

// =============================================================================
// REGISTRO
// =============================================================================

void registerModule() {
  Serial.println("\n📝 Registrando módulo " + String(MODULE_ID) + "...");
  
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(MASTER_IP) + ":" + String(API_PORT) + 
               "/register?id=" + String(MODULE_ID);
  
  http.begin(client, url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    registered = true;
    Serial.println("✓ Registrado!");
    
    // Parpadeo confirmación
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  } else {
    Serial.println("✗ Error registro: " + String(httpCode));
  }
  
  http.end();
}

// =============================================================================
// HEARTBEAT
// =============================================================================

void sendHeartbeat() {
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(MASTER_IP) + ":" + String(API_PORT) + 
               "/heartbeat?id=" + String(MODULE_ID) + 
               "&state=" + String(relayOn ? "1" : "0");
  
  http.begin(client, url);
  http.setTimeout(2000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    Serial.println("♥");
  } else {
    Serial.println("✗ Heartbeat error");
    registered = false;
  }
  
  http.end();
}

// =============================================================================
// COMANDOS UDP
// =============================================================================

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (!packetSize) return;
  
  char buffer[32];
  int len = udp.read(buffer, 31);
  buffer[len] = 0;
  
  String cmd = String(buffer);
  IPAddress senderIP = udp.remoteIP();
  
  // Verificar que sea del maestro
  if (senderIP.toString() == String(MASTER_IP)) {
    Serial.println("CMD: " + cmd);
    
    if (cmd == "ON") {
      digitalWrite(RELAY_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
      relayOn = true;
      relayOnTime = millis();
      Serial.println("💡 ON → OFF en " + String(AUTO_OFF_DELAY/1000) + "s");
    }
    else if (cmd == "OFF") {
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      relayOn = false;
      Serial.println("💡 OFF");
    }
  }
}

// =============================================================================
// ESTADO
// =============================================================================

void printStatus() {
  Serial.println("\n--- Estado M" + String(MODULE_ID) + " ---");
  Serial.println("WiFi: " + String(WiFi.status() == WL_CONNECTED ? "OK" : "NO"));
  Serial.println("IP: " + WiFi.localIP().toString());
  Serial.println("Registrado: " + String(registered ? "SI" : "NO"));
  Serial.println("Relay: " + String(relayOn ? "ON" : "OFF"));
  Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
  Serial.println("Heap: " + String(ESP.getFreeHeap()));
  Serial.println("----------------");
}