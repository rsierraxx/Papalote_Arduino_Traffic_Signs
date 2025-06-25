/*
 * Cliente ESP8266-01S v2.0 - Control de Foco LED 10W
 * 
 * CONFIGURACIÓN IMPORTANTE:
 * 1. Cambiar MODULE_ID para cada ESP (1-12)
 * 2. Cambiar ROUTER_SSID y ROUTER_PASS
 * 3. Ajustar calculateBroadcast() según tu red
 * 
 * CARACTERÍSTICAS:
 * - Auto-descubrimiento del maestro
 * - Registro automático
 * - Control de relay con auto-apagado
 * - Heartbeat cada 30 segundos
 * - Reconexión automática
 * 
 * CONEXIONES ESP8266-01S:
 * - GPIO0 → Relay (con resistencia pull-up 10K)
 * - GPIO2 → LED azul integrado (opcional)
 * - VCC → 3.3V (fuente estable 300mA+)
 * - GND → GND
 * - CH_PD → 3.3V
 */

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN - CAMBIAR ESTOS VALORES
// =============================================================================

const int MODULE_ID = 4;                          // CAMBIAR! (1-12) ID único para cada módulo

const char* ROUTER_SSID = "pmn_seniales";       // CAMBIAR! Nombre de tu WiFi
const char* ROUTER_PASS = "2WC456403581";     // CAMBIAR! Contraseña de tu WiFi

// =============================================================================
// CONFIGURACIÓN DEL SISTEMA
// =============================================================================

// Pines
#define RELAY_PIN 0        // GPIO0 del ESP8266-01S
#define LED_PIN 2          // LED azul integrado (invertido)

// Red
#define UDP_PORT 8888
#define API_PORT 8080
#define DISCOVERY_PORT 8888

// Tiempos (ms)
#define AUTO_OFF_DELAY 3000         // 3 segundos auto-apagado
#define HEARTBEAT_INTERVAL 30000    // 30 segundos
#define REGISTER_RETRY 5000         // 5 segundos entre reintentos
#define DISCOVERY_INTERVAL 10000    // 10 segundos entre búsquedas
#define DISCOVERY_TIMEOUT 3000      // 3 segundos timeout por búsqueda

// Estados del sistema
bool registered = false;
bool relayOn = false;
bool masterFound = false;
unsigned long relayOnTime = 0;
unsigned long lastHeartbeat = 0;
unsigned long lastRegister = 0;
unsigned long lastDiscovery = 0;
unsigned long bootTime = 0;

// Información del maestro
WiFiUDP udp;
IPAddress masterIP;
String masterIPString = "";

// Estadísticas
struct Stats {
  unsigned long commandsReceived = 0;
  unsigned long onCount = 0;
  unsigned long discoveryAttempts = 0;
  unsigned long registerAttempts = 0;
} stats;

// =============================================================================
// FUNCIONES AUXILIARES
// =============================================================================

IPAddress calculateBroadcast() {
  // Opción 1: Broadcast específico según tu red
  // Descomenta la línea que corresponda a tu red:
  
  // return IPAddress(192, 168, 1, 255);    // Para red 192.168.1.x
  return IPAddress(192, 168, 0, 255);    // Para red 192.168.0.x
  // return IPAddress(192, 168, 4, 255);    // Para red 192.168.4.x
  // return IPAddress(10, 0, 0, 255);       // Para red 10.0.0.x
  
  // Opción 2: Cálculo automático (puede no funcionar en todas las redes)
  /*
  uint32_t ip = WiFi.localIP();
  uint32_t subnet = WiFi.subnetMask();
  uint32_t broadcast = ip | ~subnet;
  return IPAddress(broadcast);
  */
}

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n");
  
  bootTime = millis();
  
  printHeader();
  
  // Configurar hardware
  setupHardware();
  
  // Conectar al router
  if (connectToRouter()) {
    // Iniciar UDP
    udp.begin(UDP_PORT);
    Serial.println("✅ UDP iniciado en puerto " + String(UDP_PORT));
    
    // Buscar maestro inmediatamente
    delay(2000); // Esperar que todo se estabilice
    discoverMaster();
  }
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Verificar conexión WiFi
  if (!checkWiFiConnection()) {
    return;
  }
  
  // Si no hemos encontrado el maestro, seguir buscando
  if (!masterFound && millis() - lastDiscovery > DISCOVERY_INTERVAL) {
    discoverMaster();
    lastDiscovery = millis();
  }
  
  // Si encontramos maestro pero no estamos registrados
  if (masterFound && !registered && millis() - lastRegister > REGISTER_RETRY) {
    registerWithMaster();
    lastRegister = millis();
  }
  
  // Enviar heartbeat si estamos registrados
  if (registered && millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
  
  // Manejar auto-apagado del relay
  if (relayOn && millis() - relayOnTime >= AUTO_OFF_DELAY) {
    turnOffRelay();
  }
  
  // Escuchar comandos UDP
  handleUDP();
  
  // Mostrar estado periódicamente
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 30000) { // Cada 30 segundos
    printStatus();
    lastStatus = millis();
  }
  
  // Pequeña pausa para estabilidad
  delay(10);
}

// =============================================================================
// CONFIGURACIÓN DE HARDWARE
// =============================================================================

void printHeader() {
  Serial.println("╔════════════════════════════════════════════════╗");
  Serial.println("║         ESP8266 CLIENTE v2.0                   ║");
  Serial.println("║         Módulo LED #" + String(MODULE_ID) + "                         ║");
  Serial.println("╚════════════════════════════════════════════════╝");
}

void setupHardware() {
  Serial.println("\n📌 Configurando hardware...");
  
  // Configurar pines
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Estado inicial (relay apagado)
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH);  // LED apagado (invertido)
  
  // Agregar pull-up interno para estabilidad del relay
  pinMode(RELAY_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(RELAY_PIN, OUTPUT);
  
  Serial.println("   Pin Relay: GPIO" + String(RELAY_PIN));
  Serial.println("   Pin LED: GPIO" + String(LED_PIN));
  Serial.println("✅ Hardware configurado");
  
  // Test visual del LED
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, LOW);   // Encender
    delay(100);
    digitalWrite(LED_PIN, HIGH);  // Apagar
    delay(100);
  }
}

// =============================================================================
// CONEXIÓN WIFI
// =============================================================================

bool connectToRouter() {
  Serial.println("\n🌐 Conectando al router...");
  Serial.println("   SSID: " + String(ROUTER_SSID));
  Serial.print("   ");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ROUTER_SSID, ROUTER_PASS);
  
  // Opcional: Configurar IP estática para evitar conflictos DHCP
  // Descomenta y ajusta si tienes problemas de conexión:
  /*
  IPAddress ip(192, 168, 1, 100 + MODULE_ID);  // .101, .102, etc
  IPAddress gateway(192, 168, 1, 1);           // IP de tu router
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(ip, gateway, subnet);
  */
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    
    // Parpadeo del LED mientras conecta
    digitalWrite(LED_PIN, attempts % 2 ? HIGH : LOW);
    
    if (attempts % 20 == 19) {
      Serial.print("\n   ");
    }
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n\n✅ CONECTADO AL ROUTER!");
    Serial.println("   IP Local: " + WiFi.localIP().toString());
    Serial.println("   Gateway: " + WiFi.gatewayIP().toString());
    Serial.println("   Subnet: " + WiFi.subnetMask().toString());
    Serial.println("   MAC: " + WiFi.macAddress());
    Serial.println("   RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("   Broadcast: " + calculateBroadcast().toString());
    
    // LED fijo = conectado
    digitalWrite(LED_PIN, LOW);  // Encendido
    delay(1000);
    digitalWrite(LED_PIN, HIGH); // Apagado
    
    return true;
  } else {
    Serial.println("\n\n❌ ERROR: No se pudo conectar al router!");
    Serial.println("   Estado WiFi: " + String(WiFi.status()));
    Serial.println("   Verifica SSID y contraseña");
    Serial.println("   Reiniciando en 10 segundos...");
    
    // Parpadeo rápido = error
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }
    
    delay(8000);
    ESP.restart();
    return false;
  }
}

bool checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠️ WiFi perdido! Estado: " + String(WiFi.status()));
    registered = false;
    masterFound = false;
    
    // Intentar reconectar
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = millis();
      Serial.println("Reiniciando para reconectar...");
      delay(1000);
      ESP.restart();
    }
    return false;
  }
  return true;
}

// =============================================================================
// DESCUBRIMIENTO DEL MAESTRO
// =============================================================================

void discoverMaster() {
  stats.discoveryAttempts++;
  Serial.println("\n🔍 Buscando maestro en la red... (intento #" + String(stats.discoveryAttempts) + ")");
  
  // Calcular IP de broadcast
  IPAddress broadcastIP = calculateBroadcast();
  
  // Enviar múltiples paquetes para mayor confiabilidad
  for (int i = 0; i < 3; i++) {
    udp.beginPacket(broadcastIP, DISCOVERY_PORT);
    udp.print("DISCOVER");
    udp.endPacket();
    delay(50);
  }
  
  Serial.println("   Broadcast enviado a: " + broadcastIP.toString());
  Serial.println("   Esperando respuesta...");
  
  // Esperar respuesta
  unsigned long startTime = millis();
  while (millis() - startTime < DISCOVERY_TIMEOUT) {
    int packetSize = udp.parsePacket();
    if (packetSize) {
      char buffer[64];
      int len = udp.read(buffer, 63);
      buffer[len] = 0;
      
      String response = String(buffer);
      Serial.println("   Respuesta recibida: " + response);
      
      // Formato esperado: "MASTER:192.168.1.100"
      if (response.startsWith("MASTER:")) {
        masterIPString = response.substring(7);
        masterIPString.trim();
        masterIP.fromString(masterIPString);
        masterFound = true;
        
        Serial.println("\n✅ MAESTRO ENCONTRADO!");
        Serial.println("   IP del Maestro: " + masterIPString);
        Serial.println("   Desde: " + udp.remoteIP().toString());
        
        // Confirmación visual
        for (int i = 0; i < 5; i++) {
          digitalWrite(LED_PIN, LOW);   // Encender
          delay(100);
          digitalWrite(LED_PIN, HIGH);  // Apagar
          delay(100);
        }
        
        // Registrarse inmediatamente
        delay(random(100, 1000));  // Delay aleatorio para evitar colisiones
        registerWithMaster();
        
        return;
      }
    }
    
    delay(10);
    yield(); // Importante para ESP8266
  }
  
  Serial.println("   ❌ Maestro no encontrado");
  Serial.println("   Reintentando en " + String(DISCOVERY_INTERVAL/1000) + " segundos...");
  
  // Si han pasado muchos intentos, sugerir verificaciones
  if (stats.discoveryAttempts > 5 && stats.discoveryAttempts % 5 == 0) {
    Serial.println("\n⚠️ SUGERENCIAS:");
    Serial.println("   1. Verifica que el maestro esté encendido");
    Serial.println("   2. Confirma que estén en la misma red");
    Serial.println("   3. Revisa la configuración de broadcast");
    Serial.println("   4. Algunos routers bloquean broadcast UDP");
  }
}

// =============================================================================
// REGISTRO CON EL MAESTRO
// =============================================================================

void registerWithMaster() {
  if (!masterFound) {
    Serial.println("❌ No se puede registrar sin maestro");
    return;
  }
  
  stats.registerAttempts++;
  Serial.println("\n📝 Registrando módulo " + String(MODULE_ID) + " con maestro...");
  Serial.println("   Intento #" + String(stats.registerAttempts));
  
  WiFiClient client;
  HTTPClient http;
  
  // Construir URL de registro
  String url = "http://" + masterIPString + ":" + String(API_PORT) + 
               "/register?id=" + String(MODULE_ID) + 
               "&version=2.0";
  
  Serial.println("   URL: " + url);
  
  // Configurar timeout
  http.setTimeout(5000);
  http.begin(client, url);
  
  // Realizar petición GET
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    Serial.println("   Código HTTP: " + String(httpCode));
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("   Respuesta: " + payload);
      
      registered = true;
      stats.registerAttempts = 0; // Reset counter
      
      Serial.println("\n✅ REGISTRO EXITOSO!");
      Serial.println("   Módulo #" + String(MODULE_ID) + " registrado");
      Serial.println("   Heartbeat cada " + String(HEARTBEAT_INTERVAL/1000) + " segundos");
      
      // Confirmación visual (3 parpadeos lentos)
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, LOW);   // Encender
        delay(300);
        digitalWrite(LED_PIN, HIGH);  // Apagar
        delay(300);
      }
      
    } else {
      Serial.println("   ❌ Error HTTP: " + String(httpCode));
      Serial.println("   Reintentando en " + String(REGISTER_RETRY/1000) + " segundos...");
    }
  } else {
    Serial.println("   ❌ Error de conexión: " + http.errorToString(httpCode));
    
    // Si falla la conexión HTTP, el maestro podría haberse movido
    if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED || 
        httpCode == HTTPC_ERROR_SEND_HEADER_FAILED ||
        httpCode == HTTPC_ERROR_SEND_PAYLOAD_FAILED) {
      Serial.println("   Maestro no responde, buscando de nuevo...");
      masterFound = false;
      registered = false;
    }
  }
  
  http.end();
}

// =============================================================================
// HEARTBEAT
// =============================================================================

void sendHeartbeat() {
  if (!registered || !masterFound) return;
  
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + masterIPString + ":" + String(API_PORT) + 
               "/heartbeat?id=" + String(MODULE_ID) + 
               "&state=" + String(relayOn ? "1" : "0") +
               "&uptime=" + String((millis() - bootTime) / 1000) +
               "&commands=" + String(stats.commandsReceived) +
               "&on_count=" + String(stats.onCount);
  
  http.setTimeout(3000);
  http.begin(client, url);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    // Heartbeat silencioso exitoso
    Serial.print("♥");
  } else {
    Serial.println("\n⚠️ Heartbeat falló: " + String(httpCode));
    
    // Si falla el heartbeat, intentar re-registrar
    if (httpCode < 0) {
      Serial.println("   Conexión perdida con maestro");
      registered = false;
      masterFound = false;
    }
  }
  
  http.end();
}

// =============================================================================
// MANEJO DE COMANDOS UDP
// =============================================================================

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (!packetSize) return;
  
  char buffer[64];
  int len = udp.read(buffer, 63);
  if (len <= 0) return;
  
  buffer[len] = 0;
  String command = String(buffer);
  command.trim();
  
  IPAddress senderIP = udp.remoteIP();
  
  // Log del comando
  Serial.println("\n📨 Comando UDP: '" + command + "' desde " + senderIP.toString());
  stats.commandsReceived++;
  
  // Verificar que el comando sea del maestro
  if (masterFound && senderIP == masterIP) {
    if (command == "ON") {
      turnOnRelay();
    }
    else if (command == "OFF") {
      turnOffRelay();
    }
    else if (command == "PING") {
      // Responder al ping
      udp.beginPacket(senderIP, UDP_PORT);
      udp.print("PONG:" + String(MODULE_ID));
      udp.endPacket();
      Serial.println("   → PONG enviado");
    }
    else if (command == "REGISTER") {
      // El maestro solicita re-registro
      Serial.println("   → Maestro solicita re-registro");
      registered = false;
      delay(random(100, 1000)); // Delay aleatorio
      registerWithMaster();
    }
    else {
      Serial.println("   ⚠️ Comando no reconocido");
    }
  }
  else if (command.startsWith("MASTER:")) {
    // Actualizar IP del maestro si cambió
    String newMasterIP = command.substring(7);
    newMasterIP.trim();
    
    if (newMasterIP != masterIPString) {
      Serial.println("   → Maestro cambió de IP: " + newMasterIP);
      masterIPString = newMasterIP;
      masterIP.fromString(masterIPString);
      masterFound = true;
      registered = false;
      
      // Re-registrarse con el nuevo maestro
      delay(random(100, 1000));
      registerWithMaster();
    }
  }
  else {
    Serial.println("   ⚠️ Comando de IP no autorizada");
  }
}

// =============================================================================
// CONTROL DEL RELAY
// =============================================================================

void turnOnRelay() {
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, LOW);   // LED encendido
  relayOn = true;
  relayOnTime = millis();
  stats.onCount++;
  
  Serial.println("💡 RELAY ON");
  Serial.println("   Auto-apagado en " + String(AUTO_OFF_DELAY/1000.0) + " segundos");
}

void turnOffRelay() {
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, HIGH);  // LED apagado
  relayOn = false;
  
  if (millis() - relayOnTime < AUTO_OFF_DELAY) {
    Serial.println("💡 RELAY OFF (manual)");
  } else {
    Serial.println("💡 RELAY OFF (auto)");
  }
}

// =============================================================================
// ESTADO DEL SISTEMA
// =============================================================================

void printStatus() {
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║             ESTADO MÓDULO #" + String(MODULE_ID) + "                   ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ WiFi: " + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado") + 
                 " (" + WiFi.localIP().toString() + ")");
  Serial.println("║ Maestro: " + (masterFound ? masterIPString : "No encontrado"));
  Serial.println("║ Registrado: " + String(registered ? "Sí" : "No"));
  Serial.println("║ Relay: " + String(relayOn ? "ON" : "OFF"));
  Serial.println("║ RSSI: " + String(WiFi.RSSI()) + " dBm");
  Serial.println("║ Comandos recibidos: " + String(stats.commandsReceived));
  Serial.println("║ Veces encendido: " + String(stats.onCount));
  Serial.println("║ Uptime: " + formatUptime(millis() - bootTime));
  Serial.println("║ Heap libre: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("╚════════════════════════════════════════════════╝");
}

String formatUptime(unsigned long ms) {
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  if (hours > 0) {
    return String(hours) + "h " + String(minutes % 60) + "m";
  } else {
    return String(minutes) + "m " + String(seconds % 60) + "s";
  }
}