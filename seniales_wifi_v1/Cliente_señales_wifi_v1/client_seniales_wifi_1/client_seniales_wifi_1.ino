/*
 * Sistema de Control de Tiras LED - ESP8266-01S (Módulo Cliente)
 * Autor: Sistema LED Control
 * Versión: 3.0 - Con apagado automático
 * 
 * IMPORTANTE: Cambiar MODULE_ID para cada módulo (1-12)
 * 
 * Cambios en v3.0:
 * - Cambiado a GPIO2 para el relevador (más estable que GPIO0)
 * - Agregado apagado automático después de 3 segundos
 * - El módulo se apaga solo sin necesidad de señal del maestro
 * 
 * Funcionalidades:
 * - Conexión automática al Arduino UNO R4 WiFi
 * - Auto-registro con ID único
 * - Control de relevador para tira LED 24V
 * - Apagado automático después de 3 segundos
 * - Servidor HTTP integrado
 * - Sistema de heartbeat automático
 * - Reconexión automática
 * - Interfaz web individual
 * 
 * Conexiones:
 * GPIO2 → Control relevador (CAMBIADO!)
 * GPIO3 → LED indicador estado (RX - opcional)
 * VCC → 3.3V | GND → Tierra
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>

// =============================================================================
// CONFIGURACIÓN DEL MÓDULO - ¡CAMBIAR PARA CADA MÓDULO!
// =============================================================================

#define MODULE_ID 1  // ¡¡¡ CAMBIAR ESTE NÚMERO PARA CADA MÓDULO (1-12) !!!

// =============================================================================
// CONFIGURACIÓN DE RED
// =============================================================================

const char* ssid = "LED_CONTROL_SYSTEM";
const char* password = "12345678";
const char* masterIP = "192.168.4.1";
const int masterPort = 8080;

// =============================================================================
// CONFIGURACIÓN DE HARDWARE
// =============================================================================

// Regresando a GPIO0 - IMPORTANTE: Agregar resistencia pull-up 10K a 3.3V
#define RELAY_PIN 0      // GPIO0 - Control del relevador
#define STATUS_LED_PIN 2 // GPIO2 - LED indicador

// Configuración de lógica del relay
// La mayoría de módulos relay funcionan con lógica invertida
#define RELAY_INVERTED true   // true = LOW enciende, HIGH apaga

// Estados del relay según la lógica
#define RELAY_ON  (RELAY_INVERTED ? LOW : HIGH)
#define RELAY_OFF (RELAY_INVERTED ? HIGH : LOW)

// =============================================================================
// CONFIGURACIÓN DE TIEMPOS
// =============================================================================

#define AUTO_OFF_DELAY 3000         // 3 segundos para apagado automático
#define HEARTBEAT_INTERVAL 45000    // 45 segundos
#define RECONNECT_DELAY 5000        // 5 segundos
#define REGISTRATION_RETRY 10000    // 10 segundos
#define HTTP_TIMEOUT 5000           // 5 segundos

// =============================================================================
// VARIABLES GLOBALES
// =============================================================================

ESP8266WebServer server(80);
WiFiClient wifiClient;
HTTPClient httpClient;

// Estado del sistema
bool relayState = false;
bool isRegistered = false;
bool wifiConnected = false;
unsigned long lastHeartbeat = 0;
unsigned long lastReconnect = 0;
unsigned long lastRegistration = 0;
unsigned long bootTime = 0;

// Variables para apagado automático
bool autoOffActive = false;
unsigned long turnOnTime = 0;

// Estadísticas
int totalCommands = 0;
int failedCommands = 0;
int heartbeatCount = 0;
int reconnectCount = 0;
int autoOffCount = 0;

// =============================================================================
// SETUP - CONFIGURACIÓN INICIAL
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  bootTime = millis();
  
  Serial.println();
  Serial.println("========================================");
  Serial.println("    MODULO LED ESP8266-01S v3.0");
  Serial.println("========================================");
  Serial.println("ID: " + String(MODULE_ID));
  Serial.println("Version: 3.0 - Auto-off 3 segundos");
  Serial.println("Inicializando...");
  
  // Configurar pines ANTES de cualquier otra cosa
  initializePins();
  
  // Mostrar información del chip
  printChipInfo();
  
  // Conectar WiFi
  connectWiFi();
  
  // Configurar servidor web
  setupWebServer();
  
  // Primer intento de registro
  registerWithMaster();
  
  Serial.println("========================================");
  Serial.println("MODULO LISTO - ID: " + String(MODULE_ID));
  Serial.println("IP: " + WiFi.localIP().toString());
  Serial.println("Estado: " + String(isRegistered ? "Registrado" : "Pendiente"));
  Serial.println("Auto-off: ACTIVADO (3 segundos)");
  Serial.println("========================================");
}

// =============================================================================
// LOOP PRINCIPAL
// =============================================================================

void loop() {
  // Manejar servidor web
  server.handleClient();
  
  // Verificar apagado automático
  handleAutoOff();
  
  // Verificar estado WiFi
  if (WiFi.status() != WL_CONNECTED) {
    handleWiFiReconnection();
  } else {
    wifiConnected = true;
    
    // Manejar registro con maestro
    if (!isRegistered) {
      handleRegistration();
    }
    
    // Enviar heartbeat periódico
    if (isRegistered) {
      handleHeartbeat();
    }
  }
  
  // Pausa mínima
  delay(10);
  
  // Yield para mantener watchdog activo
  yield();
}

// =============================================================================
// CONTROL DE APAGADO AUTOMÁTICO
// =============================================================================

void handleAutoOff() {
  // Si el apagado automático está activo y el relay está encendido
  if (autoOffActive && relayState) {
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - turnOnTime;
    
    // Verificar si han pasado 3 segundos
    if (elapsedTime >= AUTO_OFF_DELAY) {
      // Apagar el relevador
      setRelayState(false, false);  // false = no activar timer
      
      // Desactivar el temporizador
      autoOffActive = false;
      autoOffCount++;
      
      Serial.println("⏰ APAGADO AUTOMÁTICO ejecutado (3 segundos cumplidos)");
      Serial.println("   Total apagados automáticos: " + String(autoOffCount));
    }
  }
}

// =============================================================================
// CONFIGURACIÓN DE HARDWARE
// =============================================================================

void initializePins() {
  Serial.println("--- CONFIGURANDO PINES ---");
  Serial.println("⚠️  ADVERTENCIA: Usando GPIO0 para relay");
  Serial.println("   IMPORTANTE: Agregar resistencia pull-up 10K entre GPIO0 y 3.3V");
  Serial.println("   Esto evita problemas de arranque en modo programación");
  
  // CRÍTICO: Establecer GPIO0 en HIGH antes de configurar como OUTPUT
  digitalWrite(RELAY_PIN, RELAY_OFF);  // Asegurar estado OFF
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);  // Confirmar estado OFF
  
  // LED de estado
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  
  relayState = false;
  
  Serial.println("✓ Pines configurados");
  Serial.println("  GPIO0 (Relevador): OUTPUT");
  Serial.println("  GPIO2 (LED Estado): OUTPUT");
  Serial.println("  Lógica relay: " + String(RELAY_INVERTED ? "INVERTIDA" : "NORMAL"));
  Serial.println("  Estado OFF = " + String(RELAY_OFF == HIGH ? "HIGH" : "LOW"));
  Serial.println("  Estado ON = " + String(RELAY_ON == HIGH ? "HIGH" : "LOW"));
  
  // Test del relay con confirmación visual
  Serial.println("\n🧪 Probando relay (3 pulsos)...");
  for (int i = 0; i < 3; i++) {
    Serial.print("  Pulso " + String(i+1) + ": ");
    
    // Encender
    digitalWrite(RELAY_PIN, RELAY_ON);
    digitalWrite(STATUS_LED_PIN, HIGH);
    Serial.print("ON ");
    delay(500);
    
    // Apagar
    digitalWrite(RELAY_PIN, RELAY_OFF);
    digitalWrite(STATUS_LED_PIN, LOW);
    Serial.println("OFF");
    delay(500);
  }
  
  // Asegurar que quede apagado
  digitalWrite(RELAY_PIN, RELAY_OFF);
  digitalWrite(STATUS_LED_PIN, LOW);
  Serial.println("✓ Test completado - Relay en estado OFF\n");
}

void printChipInfo() {
  Serial.println("--- INFORMACIÓN DEL CHIP ---");
  Serial.println("Chip ID: " + String(ESP.getChipId(), HEX));
  Serial.println("Flash Size: " + String(ESP.getFlashChipSize()) + " bytes");
  Serial.println("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("CPU Freq: " + String(ESP.getCpuFreqMHz()) + " MHz");
  Serial.println("MAC: " + WiFi.macAddress());
  
  // Detectar causa del reinicio
  rst_info *resetInfo = ESP.getResetInfoPtr();
  Serial.print("Reset reason: ");
  Serial.println(resetInfo->reason);
}

// =============================================================================
// GESTIÓN WiFi
// =============================================================================

void connectWiFi() {
  Serial.println("--- CONECTANDO WiFi ---");
  Serial.println("SSID: " + String(ssid));
  
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println();
    Serial.println("✓ WiFi Conectado!");
    Serial.println("  IP: " + WiFi.localIP().toString());
    Serial.println("  RSSI: " + String(WiFi.RSSI()) + " dBm");
    Serial.println("  Gateway: " + WiFi.gatewayIP().toString());
  } else {
    wifiConnected = false;
    Serial.println();
    Serial.println("✗ Error conectando WiFi");
  }
}

void handleWiFiReconnection() {
  if (millis() - lastReconnect < RECONNECT_DELAY) return;
  
  lastReconnect = millis();
  reconnectCount++;
  wifiConnected = false;
  isRegistered = false;
  
  Serial.println("⚠ WiFi desconectado. Reintento #" + String(reconnectCount));
  
  // Reiniciar si hay demasiados fallos
  if (reconnectCount > 10) {
    Serial.println("🔄 Reiniciando módulo por exceso de fallos...");
    delay(1000);
    ESP.restart();
  }
  
  // Intentar reconexión
  WiFi.disconnect();
  delay(1000);
  connectWiFi();
}

// =============================================================================
// SERVIDOR WEB
// =============================================================================

void setupWebServer() {
  Serial.println("--- CONFIGURANDO SERVIDOR WEB ---");
  
  // Página principal
  server.on("/", HTTP_GET, handleRoot);
  
  // Controles básicos
  server.on("/on", HTTP_GET, handleOn);
  server.on("/off", HTTP_GET, handleOff);
  server.on("/toggle", HTTP_GET, handleToggle);
  
  // Efectos especiales
  server.on("/blink_fast", HTTP_GET, handleBlinkFast);
  server.on("/blink_slow", HTTP_GET, handleBlinkSlow);
  
  // Información y estado
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/info", HTTP_GET, handleInfo);
  
  // Utilidades
  server.on("/reset", HTTP_GET, handleReset);
  server.on("/test", HTTP_GET, handleTest);
  server.on("/cancel_timer", HTTP_GET, handleCancelTimer);
  
  // 404 handler
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("✓ Servidor web iniciado en puerto 80");
}

// =============================================================================
// MANEJADORES WEB
// =============================================================================

void handleRoot() {
  String html = generateWebInterface();
  server.send(200, "text/html", html);
  Serial.println("📱 Acceso a interfaz web desde: " + server.client().remoteIP().toString());
}

void handleOn() {
  Serial.println("📥 Comando ON recibido");
  setRelayState(true, true);  // true = activar timer
  
  String response = createJsonResponse("on", true);
  server.send(200, "application/json", response);
}

void handleOff() {
  Serial.println("📥 Comando OFF recibido");
  setRelayState(false, false);  // false = no activar timer
  
  // Cancelar temporizador si estaba activo
  if (autoOffActive) {
    autoOffActive = false;
    Serial.println("⏹️ Timer de apagado cancelado");
  }
  
  String response = createJsonResponse("off", false);
  server.send(200, "application/json", response);
}

void handleToggle() {
  bool newState = !relayState;
  setRelayState(newState, newState);  // activar timer solo si se enciende
  String response = createJsonResponse("toggle", relayState);
  server.send(200, "application/json", response);
  Serial.println("🔄 Comando TOGGLE - Estado: " + String(relayState ? "ON" : "OFF"));
}

void handleCancelTimer() {
  if (autoOffActive) {
    autoOffActive = false;
    Serial.println("⏹️ Timer de apagado cancelado manualmente");
  }
  String response = createJsonResponse("cancel_timer", relayState);
  server.send(200, "application/json", response);
}

void handleBlinkFast() {
  executeBlinkPattern(5, 200);
  String response = createJsonResponse("blink_fast", relayState);
  server.send(200, "application/json", response);
  Serial.println("⚡ Comando BLINK FAST");
}

void handleBlinkSlow() {
  executeBlinkPattern(3, 1000);
  String response = createJsonResponse("blink_slow", relayState);
  server.send(200, "application/json", response);
  Serial.println("💫 Comando BLINK SLOW");
}

void handleStatus() {
  String json = generateStatusJson();
  server.send(200, "application/json", json);
}

void handleInfo() {
  String json = generateInfoJson();
  server.send(200, "application/json", json);
}

void handleTest() {
  Serial.println("🧪 Test ejecutado desde: " + server.client().remoteIP().toString());
  
  // Secuencia de test sin timer
  bool timerWasActive = autoOffActive;
  autoOffActive = false;
  
  for (int i = 0; i < 3; i++) {
    digitalWrite(RELAY_PIN, RELAY_ON);
    delay(300);
    digitalWrite(RELAY_PIN, RELAY_OFF);
    delay(300);
  }
  
  autoOffActive = timerWasActive;
  
  String response = createJsonResponse("test", false);
  server.send(200, "application/json", response);
}

void handleReset() {
  String response = createJsonResponse("reset", false);
  server.send(200, "application/json", response);
  Serial.println("🔄 RESET solicitado desde: " + server.client().remoteIP().toString());
  delay(1000);
  ESP.restart();
}

void handleNotFound() {
  String message = "{\"error\":\"Endpoint no encontrado\",\"module_id\":" + String(MODULE_ID) + "}";
  server.send(404, "application/json", message);
  Serial.println("❌ 404 - " + server.uri() + " desde " + server.client().remoteIP().toString());
}

// =============================================================================
// CONTROL DEL RELEVADOR
// =============================================================================

void setRelayState(bool state, bool activateTimer) {
  // Evitar cambios redundantes
  if (relayState == state && !activateTimer) {
    return;
  }
  
  relayState = state;
  
  // Control del relevador según la lógica configurada
  if (state) {
    digitalWrite(RELAY_PIN, RELAY_ON);
    digitalWrite(STATUS_LED_PIN, HIGH);
    Serial.println("🔌 Relevador ACTIVADO");
    Serial.println("   GPIO0 = " + String(RELAY_ON == HIGH ? "HIGH" : "LOW"));
  } else {
    digitalWrite(RELAY_PIN, RELAY_OFF);
    digitalWrite(STATUS_LED_PIN, LOW);
    Serial.println("🔌 Relevador DESACTIVADO");
    Serial.println("   GPIO0 = " + String(RELAY_OFF == HIGH ? "HIGH" : "LOW"));
  }
  
  totalCommands++;
  
  // Manejar temporizador de apagado automático
  if (state && activateTimer) {
    autoOffActive = true;
    turnOnTime = millis();
    Serial.println("⏰ Timer de apagado activado (3 segundos)");
  } else if (!state) {
    autoOffActive = false;
  }
}

void executeBlinkPattern(int cycles, int delayMs) {
  bool originalState = relayState;
  bool timerWasActive = autoOffActive;
  
  // Desactivar timer temporalmente
  autoOffActive = false;
  
  Serial.println("⚡ Ejecutando " + String(cycles) + " parpadeos (" + String(delayMs) + "ms)");
  
  for (int i = 0; i < cycles; i++) {
    digitalWrite(RELAY_PIN, RELAY_ON);
    delay(delayMs);
    digitalWrite(RELAY_PIN, RELAY_OFF);
    delay(delayMs);
  }
  
  // Restaurar estado original
  digitalWrite(RELAY_PIN, originalState ? RELAY_ON : RELAY_OFF);
  relayState = originalState;
  
  // Restaurar timer si estaba activo
  if (timerWasActive && originalState) {
    autoOffActive = true;
  }
  
  Serial.println("✓ Patrón completado, estado restaurado");
}

// =============================================================================
// COMUNICACIÓN CON MAESTRO
// =============================================================================

void handleRegistration() {
  if (millis() - lastRegistration < REGISTRATION_RETRY) return;
  
  lastRegistration = millis();
  registerWithMaster();
}

void registerWithMaster() {
  if (!wifiConnected) return;
  
  Serial.println("📡 Registrando con maestro...");
  
  String url = "http://" + String(masterIP) + ":" + String(masterPort) + "/register?id=" + String(MODULE_ID);
  
  httpClient.begin(wifiClient, url);
  httpClient.setTimeout(HTTP_TIMEOUT);
  
  int httpCode = httpClient.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = httpClient.getString();
    isRegistered = true;
    failedCommands = 0;
    Serial.println("✓ Registro exitoso: " + payload);
  } else if (httpCode > 0) {
    Serial.println("⚠ Error registro - HTTP " + String(httpCode));
    failedCommands++;
  } else {
    Serial.println("⚠ Error conexión maestro: " + httpClient.errorToString(httpCode));
    failedCommands++;
  }
  
  httpClient.end();
}

void handleHeartbeat() {
  if (millis() - lastHeartbeat < HEARTBEAT_INTERVAL) return;
  
  lastHeartbeat = millis();
  sendHeartbeat();
}

void sendHeartbeat() {
  String url = "http://" + String(masterIP) + ":" + String(masterPort) + "/heartbeat";
  url += "?id=" + String(MODULE_ID);
  url += "&state=" + String(relayState ? "1" : "0");
  url += "&uptime=" + String(millis() - bootTime);
  url += "&rssi=" + String(WiFi.RSSI());
  url += "&heap=" + String(ESP.getFreeHeap());
  url += "&autooff=" + String(autoOffActive ? "1" : "0");
  url += "&autooffcount=" + String(autoOffCount);
  
  httpClient.begin(wifiClient, url);
  httpClient.setTimeout(HTTP_TIMEOUT);
  
  int httpCode = httpClient.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    heartbeatCount++;
    failedCommands = 0;
    Serial.println("💓 Heartbeat #" + String(heartbeatCount) + " enviado");
  } else {
    failedCommands++;
    Serial.println("⚠ Error heartbeat - HTTP " + String(httpCode));
    
    // Re-registro si fallan muchos heartbeats
    if (failedCommands > 3) {
      Serial.println("🔄 Demasiados fallos, re-registrando...");
      isRegistered = false;
      failedCommands = 0;
    }
  }
  
  httpClient.end();
}

// =============================================================================
// GENERACIÓN DE RESPUESTAS JSON
// =============================================================================

String createJsonResponse(String command, bool state) {
  String json = "{";
  json += "\"status\":\"ok\",";
  json += "\"module_id\":" + String(MODULE_ID) + ",";
  json += "\"command\":\"" + command + "\",";
  json += "\"relay_state\":" + String(state ? "true" : "false") + ",";
  json += "\"auto_off_active\":" + String(autoOffActive ? "true" : "false") + ",";
  
  if (autoOffActive) {
    unsigned long timeLeft = AUTO_OFF_DELAY - (millis() - turnOnTime);
    json += "\"auto_off_remaining\":" + String(timeLeft) + ",";
  }
  
  json += "\"timestamp\":" + String(millis()) + "";
  json += "}";
  return json;
}

String generateStatusJson() {
  String json = "{";
  json += "\"module_id\":" + String(MODULE_ID) + ",";
  json += "\"relay_state\":" + String(relayState ? "true" : "false") + ",";
  json += "\"auto_off_active\":" + String(autoOffActive ? "true" : "false") + ",";
  
  if (autoOffActive) {
    unsigned long timeLeft = AUTO_OFF_DELAY - (millis() - turnOnTime);
    json += "\"auto_off_remaining\":" + String(timeLeft) + ",";
  }
  
  json += "\"auto_off_count\":" + String(autoOffCount) + ",";
  json += "\"wifi_connected\":" + String(wifiConnected ? "true" : "false") + ",";
  json += "\"registered\":" + String(isRegistered ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis() - bootTime) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"total_commands\":" + String(totalCommands) + ",";
  json += "\"failed_commands\":" + String(failedCommands) + ",";
  json += "\"heartbeats\":" + String(heartbeatCount) + ",";
  json += "\"reconnects\":" + String(reconnectCount) + "";
  json += "}";
  return json;
}

String generateInfoJson() {
  String json = "{";
  json += "\"module\":{";
  json += "\"id\":" + String(MODULE_ID) + ",";
  json += "\"version\":\"3.0\",";
  json += "\"features\":[\"auto-off\",\"3-second-timer\",\"gpio2-relay\"],";
  json += "\"hardware\":\"ESP8266-01S\"";
  json += "},";
  json += "\"network\":{";
  json += "\"ssid\":\"" + String(ssid) + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"mac\":\"" + WiFi.macAddress() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"gateway\":\"" + WiFi.gatewayIP().toString() + "\"";
  json += "},";
  json += "\"system\":{";
  json += "\"uptime\":" + String(millis() - bootTime) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"chip_id\":\"" + String(ESP.getChipId(), HEX) + "\",";
  json += "\"flash_size\":" + String(ESP.getFlashChipSize()) + ",";
  json += "\"cpu_freq\":" + String(ESP.getCpuFreqMHz()) + ",";
  json += "\"auto_off_count\":" + String(autoOffCount) + ",";
  json += "\"relay_pin\":\"GPIO" + String(RELAY_PIN) + "\"";
  json += "}";
  json += "}";
  return json;
}

// =============================================================================
// INTERFAZ WEB
// =============================================================================

String generateWebInterface() {
  String html = "<!DOCTYPE html><html lang='es'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Módulo LED " + String(MODULE_ID) + "</title>";
  html += "<style>";
  html += "body{font-family:-apple-system,BlinkMacSystemFont,Arial,sans-serif;margin:0;padding:20px;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;color:#333}";
  html += ".container{max-width:400px;margin:0 auto;background:rgba(255,255,255,0.95);backdrop-filter:blur(10px);border-radius:20px;padding:30px;box-shadow:0 20px 40px rgba(0,0,0,0.1)}";
  html += ".header{text-align:center;margin-bottom:30px}";
  html += ".module-id{font-size:2.5em;font-weight:bold;background:linear-gradient(45deg,#4CAF50,#2196F3);-webkit-background-clip:text;-webkit-text-fill-color:transparent;margin-bottom:10px}";
  html += ".status{padding:15px;border-radius:15px;margin:20px 0;text-align:center;font-weight:bold;font-size:1.2em;transition:all 0.3s ease}";
  html += ".status.on{background:linear-gradient(45deg,#4CAF50,#45a049);color:white;box-shadow:0 10px 20px rgba(76,175,80,0.3)}";
  html += ".status.off{background:linear-gradient(45deg,#f44336,#d32f2f);color:white;box-shadow:0 10px 20px rgba(244,67,54,0.3)}";
  html += ".timer-info{background:#FFF3E0;padding:15px;border-radius:10px;margin:15px 0;text-align:center;border:2px solid #FF9800;display:none}";
  html += ".timer-info.active{display:block}";
  html += ".countdown{font-size:2em;font-weight:bold;color:#FF6F00;margin:10px 0}";
  html += ".controls{display:grid;grid-template-columns:1fr 1fr;gap:15px;margin:25px 0}";
  html += ".btn{background:linear-gradient(45deg,#2196F3,#1976D2);color:white;border:none;padding:15px;border-radius:15px;cursor:pointer;font-size:1.1em;font-weight:bold;transition:all 0.3s ease;text-decoration:none;display:block;text-align:center}";
  html += ".btn:hover{transform:translateY(-2px);box-shadow:0 10px 20px rgba(33,150,243,0.3)}";
  html += ".btn.success{background:linear-gradient(45deg,#4CAF50,#45a049)}";
  html += ".btn.danger{background:linear-gradient(45deg,#f44336,#d32f2f)}";
  html += ".btn.warning{background:linear-gradient(45deg,#FF9800,#F57C00)}";
  html += ".btn.full{grid-column:1 / -1}";
  html += ".info{background:rgba(33,150,243,0.1);padding:20px;border-radius:15px;margin:25px 0;border-left:4px solid #2196F3}";
  html += ".info-item{display:flex;justify-content:space-between;margin:8px 0;padding:5px 0;border-bottom:1px solid rgba(33,150,243,0.2)}";
  html += ".info-label{font-weight:bold;color:#1976D2}";
  html += ".footer{text-align:center;margin-top:30px;color:#666;font-size:0.9em}";
  html += ".pulse{animation:pulse 2s infinite}";
  html += "@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(255,152,0,0.7)}70%{box-shadow:0 0 0 10px rgba(255,152,0,0)}100%{box-shadow:0 0 0 0 rgba(255,152,0,0)}}";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  
  // Header
  html += "<div class='header'>";
  html += "<div class='module-id'>🔆 Módulo " + String(MODULE_ID) + "</div>";
  html += "<div style='color:#666'>Controlador de Tira LED v3.0</div>";
  html += "</div>";
  
  // Estado actual
  String statusClass = relayState ? "on" : "off";
  String statusText = relayState ? "🟢 ENCENDIDO" : "🔴 APAGADO";
  String statusIcon = relayState ? "💡" : "🌙";
  html += "<div class='status " + statusClass + "'>";
  html += statusIcon + " " + statusText;
  html += "</div>";
  
  // Información del temporizador
  html += "<div class='timer-info" + String(autoOffActive ? " active pulse" : "") + "'>";
  html += "⏰ APAGADO AUTOMÁTICO ACTIVO";
  html += "<div class='countdown' id='countdown'>3 segundos</div>";
  html += "</div>";
  
  // Controles principales
  html += "<div class='controls'>";
  html += "<button class='btn success' onclick='sendCommand(\"/on\")'>🔆 Encender</button>";
  html += "<button class='btn danger' onclick='sendCommand(\"/off\")'>🔅 Apagar</button>";
  html += "<button class='btn' onclick='sendCommand(\"/toggle\")'>🔄 Toggle</button>";
  html += "<button class='btn warning' onclick='sendCommand(\"/blink_fast\")'>⚡ Parpadeo</button>";
  html += "</div>";
  
  // Controles adicionales
  html += "<div class='controls'>";
  html += "<button class='btn' onclick='sendCommand(\"/test\")'>🧪 Test</button>";
  html += "<button class='btn' onclick='location.reload()'>🔄 Actualizar</button>";
  if (autoOffActive) {
    html += "<button class='btn warning full' onclick='sendCommand(\"/cancel_timer\")'>⏹️ Cancelar Timer</button>";
  }
  html += "<button class='btn danger full' onclick='confirmReset()'>🔄 Reiniciar Módulo</button>";
  html += "</div>";
  
  // Información del sistema
  html += "<div class='info'>";
  html += "<h3 style='margin-top:0;color:#1976D2'>📊 Información del Sistema</h3>";
  html += "<div class='info-item'><span class='info-label'>IP:</span><span>" + WiFi.localIP().toString() + "</span></div>";
  html += "<div class='info-item'><span class='info-label'>RSSI:</span><span>" + String(WiFi.RSSI()) + " dBm</span></div>";
  html += "<div class='info-item'><span class='info-label'>Uptime:</span><span>" + String((millis() - bootTime)/1000) + " seg</span></div>";
  html += "<div class='info-item'><span class='info-label'>Registrado:</span><span>" + String(isRegistered ? "✅ Sí" : "❌ No") + "</span></div>";
  html += "<div class='info-item'><span class='info-label'>Comandos:</span><span>" + String(totalCommands) + "</span></div>";
  html += "<div class='info-item'><span class='info-label'>Auto-off:</span><span>" + String(autoOffCount) + " veces</span></div>";
  html += "<div class='info-item'><span class='info-label'>Heartbeats:</span><span>" + String(heartbeatCount) + "</span></div>";
  html += "<div class='info-item'><span class='info-label'>RAM Libre:</span><span>" + String(ESP.getFreeHeap()) + " bytes</span></div>";
  html += "<div class='info-item'><span class='info-label'>Relay Pin:</span><span>GPIO" + String(RELAY_PIN) + "</span></div>";
  html += "</div>";
  
  // Footer
  html += "<div class='footer'>";
  html += "Sistema LED Control v3.0<br>";
  html += "ESP8266-01S • ID: " + String(MODULE_ID) + "<br>";
  html += "⏰ Auto-off: 3 segundos<br>";
  html += "📍 Relay: GPIO2 (estable)";
  html += "</div>";
  
  html += "</div>";
  
  // JavaScript
  html += "<script>";
  html += "var autoOffActive = " + String(autoOffActive ? "true" : "false") + ";";
  html += "var turnOnTime = " + String(turnOnTime) + ";";
  html += "var currentTime = " + String(millis()) + ";";
  html += "var serverOffset = currentTime - Date.now();";
  
  html += "function updateCountdown() {";
  html += "  if (autoOffActive) {";
  html += "    var now = Date.now() + serverOffset;";
  html += "    var elapsed = now - turnOnTime;";
  html += "    var remaining = Math.max(0, 3000 - elapsed);";
  html += "    var seconds = Math.ceil(remaining / 1000);";
  html += "    document.getElementById('countdown').textContent = seconds + ' segundo' + (seconds !== 1 ? 's' : '');";
  html += "    if (remaining <= 0) {";
  html += "      setTimeout(() => location.reload(), 500);";
  html += "    }";
  html += "  }";
  html += "}";
  
  html += "function sendCommand(endpoint){";
  html += "  const btn = event.target;";
  html += "  btn.style.opacity = '0.6';";
  html += "  btn.disabled = true;";
  html += "  fetch(endpoint)";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      console.log('Respuesta:', data);";
  html += "      if(endpoint !== '/reset') {";
  html += "        setTimeout(() => location.reload(), 500);";
  html += "      }";
  html += "    })";
  html += "    .catch(err => {";
  html += "      console.error('Error:', err);";
  html += "      alert('Error ejecutando comando');";
  html += "      btn.style.opacity = '1';";
  html += "      btn.disabled = false;";
  html += "    });";
  html += "}";
  
  html += "function confirmReset(){";
  html += "  if(confirm('¿Reiniciar el módulo " + String(MODULE_ID) + "?')){";
  html += "    sendCommand('/reset');";
  html += "    alert('Módulo reiniciando...');";
  html += "  }";
  html += "}";
  
  html += "if (autoOffActive) {";
  html += "  setInterval(updateCountdown, 100);";
  html += "  updateCountdown();";
  html += "}";
  
  html += "setTimeout(() => location.reload(), 10000);"; // Auto-refresh cada 10 segundos
  html += "</script>";
  
  html += "</body></html>";
  
  return html;
}