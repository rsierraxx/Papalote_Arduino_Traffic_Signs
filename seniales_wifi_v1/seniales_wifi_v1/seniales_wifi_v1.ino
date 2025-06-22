/*
 * Sistema de Control de 12 Focos - Arduino UNO R4 WiFi (Maestro)
 * Autor: Sistema Control de Iluminación
 * Versión: 4.0 - Con servidor de configuración
 * 
 * Cambios en v4.0:
 * - Agregado endpoint /config para servir configuración a clientes
 * - AUTO_OFF_DELAY configurable desde el maestro
 * - Los clientes leen la configuración al conectarse
 * - Posibilidad de cambiar dinámicamente el tiempo de apagado
 * 
 * Funcionalidades:
 * - Punto de acceso WiFi autónomo
 * - Control de 12 módulos ESP8266-01S (focos de 10W)
 * - Servidor de configuración centralizada
 * - Sistema de registro automático de módulos
 * - Efectos predefinidos y personalizables
 * - Monitoreo de estado en tiempo real
 * - Servidor web para interfaz de control
 * - Sistema de heartbeat y reconexión
 */

#include "WiFiS3.h"

// =============================================================================
// CONFIGURACIÓN DEL SISTEMA
// =============================================================================

// Configuración WiFi
const char* ssid = "LED_CONTROL_SYSTEM";
const char* password = "12345678";
const IPAddress local_IP(192, 168, 4, 1);
const IPAddress gateway(192, 168, 4, 1);
const IPAddress subnet(255, 255, 255, 0);

// Configuración del sistema
#define MAX_MODULES 12
#define HEARTBEAT_INTERVAL 60000  // 60 segundos
#define MODULE_TIMEOUT 120000     // 2 minutos para considerar módulo offline
#define EFFECT_DELAY_DEFAULT 500  // Delay por defecto para efectos (ms)

// CONFIGURACIÓN CENTRALIZADA - Cambiar aquí afecta a todos los clientes
unsigned long AUTO_OFF_DELAY = 3000;  // Tiempo de apagado automático en ms (3 segundos por defecto)

// Configuración de los botones físicos
#define BUTTON1_PIN 2             // Pin digital 2 para botón 1 (Módulo 1)
#define BUTTON2_PIN 3             // Pin digital 3 para botón 2 (Módulo 2)
#define BUTTON3_PIN 4             // Pin digital 4 para botón 3 (Módulo 3)
#define BUTTON_DEBOUNCE 50        // 50ms debounce
#define HTTP_TIMEOUT 1000         // 1 segundo timeout para HTTP (más rápido)

// Puertos
WiFiServer server(80);
WiFiServer apiServer(8080);

// =============================================================================
// ESTRUCTURA DE DATOS
// =============================================================================

struct ModuleInfo {
  uint8_t id;
  IPAddress ip;
  bool isOnline;
  bool isOn;
  unsigned long lastHeartbeat;
  String status;
  String version;  // Nueva: versión del cliente
  unsigned long configuredDelay;  // Nueva: delay configurado en el cliente
};

ModuleInfo modules[MAX_MODULES];
uint8_t registeredModules = 0;

// =============================================================================
// VARIABLES GLOBALES
// =============================================================================

bool autoMode = false;
bool effectRunning = false;
String currentEffect = "none";
unsigned long lastHeartbeatCheck = 0;
unsigned long lastEffectUpdate = 0;
int effectDelay = EFFECT_DELAY_DEFAULT;
int effectStep = 0;

// Variables para los botones físicos
struct ButtonState {
  bool lastState;
  bool currentState;
  unsigned long lastDebounceTime;
  bool pressed;
  unsigned long lastActivationTime;
  bool isProcessing;
};

ButtonState button1 = {HIGH, HIGH, 0, false, 0, false};
ButtonState button2 = {HIGH, HIGH, 0, false, 0, false};
ButtonState button3 = {HIGH, HIGH, 0, false, 0, false};

// Tiempo mínimo entre activaciones (ms) - ahora dinámico basado en AUTO_OFF_DELAY
unsigned long getMinActivationInterval() {
  return AUTO_OFF_DELAY + 500;  // AUTO_OFF_DELAY + 500ms de margen
}

// =============================================================================
// CONFIGURACIÓN INICIAL
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== SISTEMA DE CONTROL DE FOCOS v4.0 ===");
  Serial.println("Inicializando Arduino UNO R4 WiFi como Punto de Acceso...");
  Serial.println("IMPORTANTE: Configuración centralizada:");
  Serial.println("  - AUTO_OFF_DELAY: " + String(AUTO_OFF_DELAY) + "ms (" + String(AUTO_OFF_DELAY/1000.0) + " segundos)");
  Serial.println("  - Los clientes leerán esta configuración al conectarse");
  Serial.println("  - Endpoint de configuración: /config (puerto 8080)");
  
  // Configurar pines de los botones
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  // Leer estados iniciales
  button1.lastState = digitalRead(BUTTON1_PIN);
  button1.currentState = button1.lastState;
  button1.lastActivationTime = 0;
  button1.isProcessing = false;
  
  button2.lastState = digitalRead(BUTTON2_PIN);
  button2.currentState = button2.lastState;
  button2.lastActivationTime = 0;
  button2.isProcessing = false;
  
  button3.lastState = digitalRead(BUTTON3_PIN);
  button3.currentState = button3.lastState;
  button3.lastActivationTime = 0;
  button3.isProcessing = false;
  
  Serial.println("\nBotones físicos configurados:");
  Serial.println("  Botón 1 (Pin " + String(BUTTON1_PIN) + ") → Módulo 1");
  Serial.println("  Botón 2 (Pin " + String(BUTTON2_PIN) + ") → Módulo 2");
  Serial.println("  Botón 3 (Pin " + String(BUTTON3_PIN) + ") → Módulo 3");
  
  // Inicializar estructura de módulos
  initializeModules();
  
  // Configurar punto de acceso WiFi
  if (!setupAccessPoint()) {
    Serial.println("ERROR: No se pudo crear el punto de acceso");
    while(1) delay(1000);
  }
  
  // Iniciar servidores
  server.begin();
  apiServer.begin();
  
  Serial.println("\n=== SISTEMA LISTO ===");
  printSystemInfo();
  printCommands();
}

// =============================================================================
// BUCLE PRINCIPAL
// =============================================================================

void loop() {
  // Procesar comandos serie
  handleSerialCommands();
  
  // Manejar botones físicos
  handlePhysicalButtons();
  
  // Manejar conexiones web
  handleWebClients();
  
  // Manejar API REST
  handleApiClients();
  
  // Verificar heartbeat de módulos
  checkModulesHeartbeat();
  
  // Ejecutar efectos automáticos
  handleEffects();
  
  delay(10); // Pequeña pausa para estabilidad
}

// =============================================================================
// CONFIGURACIÓN DEL PUNTO DE ACCESO
// =============================================================================

bool setupAccessPoint() {
  // Configurar como punto de acceso
  WiFi.config(local_IP, gateway, subnet);
  
  int status = WiFi.beginAP(ssid, password);
  if (status != WL_AP_LISTENING) {
    Serial.println("Error al crear punto de acceso");
    return false;
  }
  
  // Esperar a que se configure
  delay(2000);
  
  Serial.println("Punto de acceso creado exitosamente");
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Password: ");
  Serial.println(password);
  
  return true;
}

// =============================================================================
// INICIALIZACIÓN DE MÓDULOS
// =============================================================================

void initializeModules() {
  for (int i = 0; i < MAX_MODULES; i++) {
    modules[i].id = i + 1;
    modules[i].ip = IPAddress(0, 0, 0, 0);
    modules[i].isOnline = false;
    modules[i].isOn = false;
    modules[i].lastHeartbeat = 0;
    modules[i].status = "offline";
    modules[i].version = "unknown";
    modules[i].configuredDelay = 0;
  }
  registeredModules = 0;
  Serial.println("Estructura de módulos inicializada");
}

// =============================================================================
// MANEJO DE LOS BOTONES FÍSICOS
// =============================================================================

void handlePhysicalButtons() {
  // Manejar cada botón individualmente
  handleSingleButton(BUTTON1_PIN, &button1, 1); // Botón 1 → Módulo 1
  handleSingleButton(BUTTON2_PIN, &button2, 2); // Botón 2 → Módulo 2
  handleSingleButton(BUTTON3_PIN, &button3, 3); // Botón 3 → Módulo 3
}

void handleSingleButton(int pin, ButtonState* buttonState, int moduleId) {
  // Leer estado actual del botón
  int reading = digitalRead(pin);
  
  // Verificar si el estado cambió (para debounce)
  if (reading != buttonState->lastState) {
    buttonState->lastDebounceTime = millis();
  }
  
  // Si ha pasado el tiempo de debounce
  if ((millis() - buttonState->lastDebounceTime) > BUTTON_DEBOUNCE) {
    
    // Si el estado del botón realmente cambió
    if (reading != buttonState->currentState) {
      buttonState->currentState = reading;
      
      // Botón presionado (LOW porque usamos pull-up)
      if (buttonState->currentState == LOW && !buttonState->isProcessing) {
        buttonState->pressed = true;
        
        // Verificar si ha pasado suficiente tiempo desde la última activación
        unsigned long currentTime = millis();
        unsigned long timeSinceLastActivation = currentTime - buttonState->lastActivationTime;
        unsigned long minInterval = getMinActivationInterval();
        
        if (timeSinceLastActivation < minInterval) {
          unsigned long timeToWait = (minInterval - timeSinceLastActivation) / 1000;
          Serial.println("⏳ Botón " + String(moduleId) + " - Espera " + String(timeToWait) + "s más");
          Serial.println("   (El módulo necesita completar su ciclo de " + String(AUTO_OFF_DELAY/1000.0) + "s)");
          return;
        }
        
        Serial.println("🔘 Botón " + String(moduleId) + " presionado → Enviando señal de encendido");
        
        // Verificar que el módulo esté online
        if (!modules[moduleId - 1].isOnline) {
          Serial.println("❌ Módulo " + String(moduleId) + " no está online");
          return;
        }
        
        // Marcar como procesando para evitar múltiples envíos
        buttonState->isProcessing = true;
        buttonState->lastActivationTime = currentTime;
        
        // Enviar señal de encendido
        bool success = controlModuleFast(moduleId, true);
        
        if (success) {
          Serial.println("✅ Módulo " + String(moduleId) + " encendido");
          Serial.println("⏰ El módulo se apagará automáticamente en " + String(AUTO_OFF_DELAY/1000.0) + " segundos");
        } else {
          Serial.println("❌ Error al encender módulo " + String(moduleId));
          // Si falla, permitir reintento más rápido
          buttonState->lastActivationTime = currentTime - (minInterval / 2);
        }
        
        // Liberar el procesamiento después de un pequeño delay
        buttonState->isProcessing = false;
      }
      
      // Botón liberado
      else if (buttonState->pressed && buttonState->currentState == HIGH) {
        buttonState->pressed = false;
        buttonState->isProcessing = false;
        Serial.println("🔘 Botón " + String(moduleId) + " liberado");
      }
    }
  }
  
  buttonState->lastState = reading;
}

// =============================================================================
// MANEJO DE COMANDOS SERIE
// =============================================================================

void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    processCommand(command);
  }
}

void processCommand(String cmd) {
  Serial.println("Comando recibido: " + cmd);
  
  if (cmd.startsWith("on ")) {
    int moduleId = cmd.substring(3).toInt();
    if (moduleId >= 1 && moduleId <= MAX_MODULES) {
      controlModule(moduleId, true);
    } else {
      Serial.println("ID de módulo inválido (1-12)");
    }
  }
  else if (cmd.startsWith("off ")) {
    int moduleId = cmd.substring(4).toInt();
    if (moduleId >= 1 && moduleId <= MAX_MODULES) {
      controlModule(moduleId, false);
    } else {
      Serial.println("ID de módulo inválido (1-12)");
    }
  }
  else if (cmd.startsWith("setdelay ")) {
    unsigned long newDelay = cmd.substring(9).toInt();
    if (newDelay >= 1000 && newDelay <= 60000) {
      AUTO_OFF_DELAY = newDelay;
      Serial.println("⏰ AUTO_OFF_DELAY cambiado a: " + String(AUTO_OFF_DELAY) + "ms (" + String(AUTO_OFF_DELAY/1000.0) + " segundos)");
      Serial.println("   Los clientes aplicarán este cambio en su próxima conexión");
    } else {
      Serial.println("Delay inválido (1000-60000ms)");
    }
  }
  else if (cmd == "all_on") {
    controlAllModules(true);
  }
  else if (cmd == "all_off") {
    controlAllModules(false);
    stopEffect();
  }
  else if (cmd == "wave") {
    startEffect("wave");
  }
  else if (cmd == "chase") {
    startEffect("chase");
  }
  else if (cmd == "blink") {
    startEffect("blink");
  }
  else if (cmd == "random") {
    startEffect("random");
  }
  else if (cmd == "auto_on") {
    autoMode = true;
    startEffect("auto");
    Serial.println("Modo automático activado");
  }
  else if (cmd == "auto_off") {
    autoMode = false;
    stopEffect();
    Serial.println("Modo automático desactivado");
  }
  else if (cmd == "status") {
    printSystemStatus();
  }
  else if (cmd == "modules") {
    printModulesStatus();
  }
  else if (cmd == "config") {
    printConfiguration();
  }
  else if (cmd == "reset") {
    resetSystem();
  }
  else if (cmd == "help") {
    printCommands();
  }
  else if (cmd.startsWith("speed ")) {
    int speed = cmd.substring(6).toInt();
    if (speed >= 100 && speed <= 5000) {
      effectDelay = speed;
      Serial.println("Velocidad de efecto ajustada a: " + String(speed) + "ms");
    } else {
      Serial.println("Velocidad inválida (100-5000ms)");
    }
  }
  else {
    Serial.println("Comando no reconocido. Usa 'help' para ver comandos disponibles");
  }
}

// Función rápida para control de módulos con mejor manejo de errores
bool controlModuleFast(int moduleId, bool state) {
  if (moduleId < 1 || moduleId > MAX_MODULES) {
    Serial.println("❌ ID fuera de rango: " + String(moduleId));
    return false;
  }
  
  int index = moduleId - 1;
  if (!modules[index].isOnline) {
    Serial.println("❌ Módulo " + String(moduleId) + " offline");
    return false;
  }
  
  // Solo enviar comando ON desde botones (el OFF lo maneja el cliente)
  if (!state && buttonPressed(moduleId)) {
    Serial.println("ℹ️ Módulo " + String(moduleId) + " - Apagado manejado por cliente");
    return true;
  }
  
  WiFiClient client;
  client.setTimeout(500);  // Timeout corto de 500ms
  
  Serial.println("🔌 Conectando con " + modules[index].ip.toString() + ":80");
  
  if (!client.connect(modules[index].ip, 80)) {
    Serial.println("❌ No se pudo establecer conexión TCP");
    Serial.println("   Verificar que el módulo esté encendido");
    Serial.println("   Verificar conexión de red");
    return false;
  }
  
  // Conexión exitosa, enviar comando
  String httpRequest = "GET ";
  httpRequest += (state ? "/on" : "/off");
  httpRequest += " HTTP/1.1\r\n";
  httpRequest += "Host: " + modules[index].ip.toString() + "\r\n";
  httpRequest += "Connection: close\r\n\r\n";
  
  client.print(httpRequest);
  
  // Pequeño delay para asegurar envío
  delay(50);
  
  // Cerrar conexión inmediatamente
  client.stop();
  
  // Actualizar estado local
  modules[index].isOn = state;
  
  Serial.println("✅ Comando " + String(state ? "ON" : "OFF") + " enviado");
  return true;
}

// Función auxiliar para verificar si el comando viene de un botón
bool buttonPressed(int moduleId) {
  // Esta función se llama solo desde controlModuleFast
  // Asumimos que si es módulo 1, 2 o 3 y se llama OFF, no es desde botón
  return (moduleId >= 1 && moduleId <= 3);
}

// =============================================================================
// CONTROL DE MÓDULOS
// =============================================================================

bool controlModule(int moduleId, bool state) {
  if (moduleId < 1 || moduleId > MAX_MODULES) {
    Serial.println("ID de módulo fuera de rango");
    return false;
  }
  
  int index = moduleId - 1;
  if (!modules[index].isOnline) {
    Serial.println("Módulo " + String(moduleId) + " no está online");
    return false;
  }
  
  WiFiClient client;
  
  // Configurar timeout más corto para conexión
  client.setTimeout(HTTP_TIMEOUT);
  
  if (client.connect(modules[index].ip, 80)) {
    String httpRequest = "GET ";
    httpRequest += (state ? "/on" : "/off");
    httpRequest += " HTTP/1.1";
    client.println(httpRequest);
    client.println("Host: " + modules[index].ip.toString());
    client.println("Connection: close");
    client.println();
    
    // Timeout más corto para respuesta
    unsigned long timeout = millis() + HTTP_TIMEOUT;
    while (client.available() == 0 && millis() < timeout) {
      delay(10);
    }
    
    bool success = false;
    if (client.available()) {
      String response = client.readString();
      success = response.indexOf("200 OK") != -1;
    } else {
      // Si no hay respuesta, asumir éxito para módulos que no responden rápido
      success = true;
    }
    
    client.stop();
    
    if (success) {
      modules[index].isOn = state;
      Serial.println("Módulo " + String(moduleId) + " " + (state ? "encendido" : "apagado"));
      if (state) {
        Serial.println("⏰ Nota: El módulo se apagará automáticamente en " + String(AUTO_OFF_DELAY/1000.0) + " segundos");
      }
      return true;
    } else {
      Serial.println("Error al controlar módulo " + String(moduleId));
      return false;
    }
  } else {
    Serial.println("No se pudo conectar al módulo " + String(moduleId));
    return false;
  }
}

void controlAllModules(bool state) {
  Serial.println(state ? "Encendiendo todos los focos..." : "Apagando todos los focos...");
  
  int successCount = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline) {
      if (controlModule(i + 1, state)) {
        successCount++;
      }
      delay(100); // Pequeña pausa entre comandos
    }
  }
  
  Serial.println("Comando ejecutado en " + String(successCount) + " focos");
  if (state) {
    Serial.println("⏰ Todos los focos se apagarán automáticamente en " + String(AUTO_OFF_DELAY/1000.0) + " segundos");
  }
}

// =============================================================================
// SISTEMA DE EFECTOS
// =============================================================================

void startEffect(String effectName) {
  currentEffect = effectName;
  effectRunning = true;
  effectStep = 0;
  lastEffectUpdate = millis();
  
  Serial.println("Iniciando efecto: " + effectName);
  
  if (effectName == "auto") {
    // En modo automático, comenzar con efecto wave
    currentEffect = "wave";
  }
}

void stopEffect() {
  effectRunning = false;
  currentEffect = "none";
  Serial.println("Efecto detenido");
}

void handleEffects() {
  if (!effectRunning) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastEffectUpdate < effectDelay) return;
  
  lastEffectUpdate = currentTime;
  
  if (currentEffect == "wave") {
    executeWaveEffect();
  }
  else if (currentEffect == "chase") {
    executeChaseEffect();
  }
  else if (currentEffect == "blink") {
    executeBlinkEffect();
  }
  else if (currentEffect == "random") {
    executeRandomEffect();
  }
  else if (currentEffect == "auto") {
    executeAutoMode();
  }
}

void executeWaveEffect() {
  // Apagar todos los módulos
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline && modules[i].isOn) {
      controlModule(i + 1, false);
    }
  }
  
  delay(50);
  
  // Encender módulo actual
  if (effectStep < MAX_MODULES && modules[effectStep].isOnline) {
    controlModule(effectStep + 1, true);
  }
  
  effectStep++;
  if (effectStep >= MAX_MODULES) {
    effectStep = 0;
  }
}

void executeChaseEffect() {
  static bool direction = true; // true = adelante, false = atrás
  
  // Apagar todos
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline && modules[i].isOn) {
      controlModule(i + 1, false);
    }
  }
  
  delay(50);
  
  // Encender 3 módulos consecutivos
  for (int i = 0; i < 3; i++) {
    int moduleIndex = (effectStep + i) % MAX_MODULES;
    if (modules[moduleIndex].isOnline) {
      controlModule(moduleIndex + 1, true);
    }
  }
  
  if (direction) {
    effectStep++;
    if (effectStep >= MAX_MODULES) {
      effectStep = MAX_MODULES - 1;
      direction = false;
    }
  } else {
    effectStep--;
    if (effectStep < 0) {
      effectStep = 0;
      direction = true;
    }
  }
}

void executeBlinkEffect() {
  bool state = (effectStep % 2 == 0);
  
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline) {
      controlModule(i + 1, state);
    }
  }
  
  effectStep++;
  if (effectStep >= 10) { // 5 parpadeos
    effectStep = 0;
  }
}

void executeRandomEffect() {
  // Encender/apagar módulos aleatoriamente
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline) {
      bool state = random(0, 2) == 1;
      controlModule(i + 1, state);
    }
  }
}

void executeAutoMode() {
  static unsigned long lastModeChange = 0;
  static int currentAutoEffect = 0;
  static String autoEffects[] = {"wave", "chase", "blink", "random"};
  static int numAutoEffects = 4;
  
  // Cambiar efecto cada 30 segundos
  if (millis() - lastModeChange > 30000) {
    currentAutoEffect = (currentAutoEffect + 1) % numAutoEffects;
    currentEffect = autoEffects[currentAutoEffect];
    lastModeChange = millis();
    effectStep = 0;
    Serial.println("Auto mode: Cambiando a efecto " + currentEffect);
  }
  
  // Ejecutar efecto actual
  if (currentEffect == "wave") executeWaveEffect();
  else if (currentEffect == "chase") executeChaseEffect();
  else if (currentEffect == "blink") executeBlinkEffect();
  else if (currentEffect == "random") executeRandomEffect();
}

// =============================================================================
// SERVIDOR WEB Y API
// =============================================================================

void handleWebClients() {
  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();
    
    // Procesar comandos de la interfaz web
    if (request.indexOf("/api/toggle") != -1) {
      handleToggleRequest(client, request);
      return;
    }
    else if (request.indexOf("/api/command") != -1) {
      handleCommandRequest(client, request);
      return;
    }
    else if (request.indexOf("/api/status") != -1) {
      handleApiStatusRequest(client);
      return;
    }
    else if (request.indexOf("/api/modules") != -1) {
      handleApiModulesRequest(client);
      return;
    }
    else if (request.indexOf("/api/setdelay") != -1) {
      handleSetDelayRequest(client, request);
      return;
    }
    
    // Generar respuesta HTML para la página principal
    String html = generateWebInterface();
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html; charset=UTF-8");
    client.println("Connection: close");
    client.println();
    client.println(html);
    
    client.stop();
  }
}

void handleApiClients() {
  WiFiClient client = apiServer.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.flush();
    
    // Procesar registro de módulo
    if (request.indexOf("/register") != -1) {
      handleModuleRegistration(client, request);
    }
    // Procesar heartbeat
    else if (request.indexOf("/heartbeat") != -1) {
      handleHeartbeat(client, request);
    }
    // NUEVO: Servir configuración a clientes
    else if (request.indexOf("/config") != -1) {
      handleConfigRequest(client);
    }
    
    client.stop();
  }
}

// NUEVO: Manejar solicitud de configuración
void handleConfigRequest(WiFiClient& client) {
  String json = "{";
  json += "\"auto_off_delay\":" + String(AUTO_OFF_DELAY) + ",";
  json += "\"version\":\"4.0\",";
  json += "\"max_modules\":" + String(MAX_MODULES) + ",";
  json += "\"effect_delay\":" + String(effectDelay);
  json += "}";
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
  
  Serial.println("📤 Configuración enviada a cliente desde " + client.remoteIP().toString());
}

// NUEVO: Manejar cambio de delay desde web
void handleSetDelayRequest(WiFiClient& client, String request) {
  // Extraer nuevo delay
  int delayStart = request.indexOf("delay=") + 6;
  int delayEnd = request.indexOf(" ", delayStart);
  if (delayEnd == -1) delayEnd = request.indexOf("&", delayStart);
  if (delayEnd == -1) delayEnd = request.length();
  
  unsigned long newDelay = request.substring(delayStart, delayEnd).toInt();
  
  if (newDelay >= 1000 && newDelay <= 60000) {
    AUTO_OFF_DELAY = newDelay;
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"status\":\"ok\",\"new_delay\":" + String(AUTO_OFF_DELAY) + "}");
    
    Serial.println("⏰ AUTO_OFF_DELAY cambiado a " + String(AUTO_OFF_DELAY) + "ms desde interfaz web");
  } else {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"Invalid delay value\"}");
  }
}

void handleToggleRequest(WiFiClient& client, String request) {
  // Extraer ID del módulo
  int idStart = request.indexOf("id=") + 3;
  int idEnd = request.indexOf(" ", idStart);
  if (idEnd == -1) idEnd = request.indexOf("&", idStart);
  if (idEnd == -1) idEnd = request.length();
  
  int moduleId = request.substring(idStart, idEnd).toInt();
  
  if (moduleId >= 1 && moduleId <= MAX_MODULES) {
    int index = moduleId - 1;
    bool newState = !modules[index].isOn;
    bool success = controlModule(moduleId, newState);
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"status\":\"" + String(success ? "ok" : "error") + "\",\"module\":" + String(moduleId) + ",\"state\":" + String(newState ? "true" : "false") + "}");
  } else {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"Invalid module ID\"}");
  }
}

void handleCommandRequest(WiFiClient& client, String request) {
  // Extraer comando
  int cmdStart = request.indexOf("cmd=") + 4;
  int cmdEnd = request.indexOf(" ", cmdStart);
  if (cmdEnd == -1) cmdEnd = request.indexOf("&", cmdStart);
  if (cmdEnd == -1) cmdEnd = request.length();
  
  String command = request.substring(cmdStart, cmdEnd);
  command.toLowerCase();
  
  // Procesar comando
  processCommand(command);
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"status\":\"ok\",\"command\":\"" + command + "\"}");
}

void handleApiStatusRequest(WiFiClient& client) {
  String response = generateStatusJson();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(response);
}

void handleApiModulesRequest(WiFiClient& client) {
  String response = generateModulesJson();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(response);
}

void handleModuleRegistration(WiFiClient& client, String request) {
  // Extraer ID del módulo de la URL
  int idStart = request.indexOf("id=") + 3;
  int idEnd = request.indexOf(" ", idStart);
  if (idEnd == -1) idEnd = request.indexOf("&", idStart);
  if (idEnd == -1) idEnd = request.length();
  
  int moduleId = request.substring(idStart, idEnd).toInt();
  
  // Extraer versión si está presente
  String version = "unknown";
  int versionStart = request.indexOf("version=") + 8;
  if (versionStart > 8) {
    int versionEnd = request.indexOf("&", versionStart);
    if (versionEnd == -1) versionEnd = request.indexOf(" ", versionStart);
    if (versionEnd == -1) versionEnd = request.length();
    version = request.substring(versionStart, versionEnd);
  }
  
  if (moduleId >= 1 && moduleId <= MAX_MODULES) {
    int index = moduleId - 1;
    modules[index].ip = client.remoteIP();
    modules[index].isOnline = true;
    modules[index].lastHeartbeat = millis();
    modules[index].status = "online";
    modules[index].version = version;
    
    // Incrementar contador si es un módulo nuevo
    bool isNewModule = true;
    for (int i = 0; i < registeredModules; i++) {
      if (modules[i].id == moduleId && modules[i].ip == client.remoteIP()) {
        isNewModule = false;
        break;
      }
    }
    
    if (isNewModule && registeredModules < MAX_MODULES) {
      registeredModules++;
    }
    
    Serial.println("Módulo " + String(moduleId) + " registrado desde IP: " + client.remoteIP().toString() + " (v" + version + ")");
    
    // Respuesta de éxito
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"status\":\"registered\",\"id\":" + String(moduleId) + "}");
  } else {
    // ID inválido
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"Invalid module ID\"}");
  }
}

void handleHeartbeat(WiFiClient& client, String request) {
  // Extraer parámetros del heartbeat
  int idStart = request.indexOf("id=") + 3;
  int idEnd = request.indexOf("&", idStart);
  if (idEnd == -1) idEnd = request.indexOf(" ", idStart);
  if (idEnd == -1) idEnd = request.length();
  
  int moduleId = request.substring(idStart, idEnd).toInt();
  
  // Extraer estado del relay
  int stateStart = request.indexOf("state=") + 6;
  if (stateStart > 6) {
    int stateEnd = request.indexOf("&", stateStart);
    if (stateEnd == -1) stateEnd = request.indexOf(" ", stateStart);
    if (stateEnd == -1) stateEnd = request.length();
    String stateStr = request.substring(stateStart, stateEnd);
    bool relayState = (stateStr == "1");
    
    // Actualizar estado del módulo
    if (moduleId >= 1 && moduleId <= MAX_MODULES) {
      modules[moduleId - 1].isOn = relayState;
    }
  }
  
  // Extraer delay configurado si está presente
  int delayStart = request.indexOf("configured_delay=") + 17;
  if (delayStart > 17) {
    int delayEnd = request.indexOf("&", delayStart);
    if (delayEnd == -1) delayEnd = request.indexOf(" ", delayStart);
    if (delayEnd == -1) delayEnd = request.length();
    unsigned long configuredDelay = request.substring(delayStart, delayEnd).toInt();
    
    if (moduleId >= 1 && moduleId <= MAX_MODULES) {
      modules[moduleId - 1].configuredDelay = configuredDelay;
    }
  }
  
  // Extraer información de auto-off si está presente
  int autoOffStart = request.indexOf("autooffcount=") + 13;
  if (autoOffStart > 13) {
    int autoOffEnd = request.indexOf("&", autoOffStart);
    if (autoOffEnd == -1) autoOffEnd = request.indexOf(" ", autoOffStart);
    if (autoOffEnd == -1) autoOffEnd = request.length();
    String autoOffCount = request.substring(autoOffStart, autoOffEnd);
    // Podemos usar esta información para estadísticas si queremos
  }
  
  if (moduleId >= 1 && moduleId <= MAX_MODULES) {
    int index = moduleId - 1;
    modules[index].lastHeartbeat = millis();
    modules[index].isOnline = true;
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"status\":\"ok\"}");
  } else {
    client.println("HTTP/1.1 400 Bad Request");
    client.println("Connection: close");
    client.println();
    client.println("{\"error\":\"Invalid module ID\"}");
  }
}

// =============================================================================
// GENERACIÓN DE INTERFACES
// =============================================================================

String generateWebInterface() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Control de Focos</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body{font-family:Arial;margin:20px;background:#f0f0f0}";
  html += ".container{max-width:800px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1)}";
  html += ".module{display:inline-block;width:80px;height:80px;margin:10px;text-align:center;border-radius:10px;cursor:pointer;transition:all 0.3s}";
  html += ".module.online{background:#4CAF50;color:white}";
  html += ".module.offline{background:#f44336;color:white}";
  html += ".module.on{box-shadow:0 0 20px #4CAF50}";
  html += ".controls{margin:20px 0}";
  html += "button{background:#2196F3;color:white;border:none;padding:10px 20px;margin:5px;border-radius:5px;cursor:pointer}";
  html += "button:hover{background:#1976D2}";
  html += ".status{background:#e7f3ff;padding:15px;border-radius:5px;margin:20px 0}";
  html += ".note{background:#fffbe6;padding:10px;border-radius:5px;margin:10px 0;border-left:4px solid #ff9800}";
  html += ".config{background:#f0f0f0;padding:15px;border-radius:5px;margin:20px 0}";
  html += "input[type='number']{padding:5px;margin:0 10px;width:80px}";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>💡 Sistema de Control de Focos</h1>";
  
  // Configuración de tiempo
  html += "<div class='config'>";
  html += "<h3>⚙️ Configuración del Sistema</h3>";
  html += "<p>Tiempo de apagado automático: ";
  html += "<input type='number' id='delayInput' min='1000' max='60000' step='1000' value='" + String(AUTO_OFF_DELAY) + "'>";
  html += "ms (" + String(AUTO_OFF_DELAY/1000.0) + " segundos)";
  html += "<button onclick='updateDelay()'>Actualizar</button></p>";
  html += "</div>";
  
  // Nota sobre el comportamiento
  html += "<div class='note'>";
  html += "⏰ <strong>Apagado automático:</strong> Los focos se apagan automáticamente después de " + String(AUTO_OFF_DELAY/1000.0) + " segundos<br>";
  html += "💡 <strong>Hardware:</strong> Control de focos de 10W mediante relevadores<br>";
  html += "🔄 <strong>Configuración:</strong> Los cambios se aplicarán cuando los módulos se reconecten";
  html += "</div>";
  
  // Estado del sistema
  html += "<div class='status'>";
  html += "<h3>Estado del Sistema</h3>";
  html += "<p><strong>Módulos Online:</strong> " + String(getOnlineModulesCount()) + "/" + String(MAX_MODULES) + "</p>";
  html += "<p><strong>Efecto Actual:</strong> " + currentEffect + "</p>";
  html += "<p><strong>Modo Automático:</strong> " + String(autoMode ? "Activado" : "Desactivado") + "</p>";
  html += "</div>";
  
  // Módulos individuales
  html += "<h3>Control Individual</h3>";
  html += "<div class='modules'>";
  for (int i = 0; i < MAX_MODULES; i++) {
    String moduleClass = "module ";
    moduleClass += modules[i].isOnline ? "online" : "offline";
    if (modules[i].isOn) moduleClass += " on";
    
    html += "<div class='" + moduleClass + "' onclick='toggleModule(" + String(i+1) + ")'>";
    html += "<div>" + String(i+1) + "</div>";
    String statusText = modules[i].isOnline ? "ON" : "OFF";
    html += "<div style='font-size:12px'>" + statusText + "</div>";
    if (modules[i].isOnline && modules[i].configuredDelay > 0) {
      html += "<div style='font-size:10px'>" + String(modules[i].configuredDelay/1000) + "s</div>";
    }
    html += "</div>";
  }
  html += "</div>";
  
  // Controles globales
  html += "<div class='controls'>";
  html += "<h3>Controles Globales</h3>";
  html += "<button onclick='sendCommand(\"all_on\")'>🔆 Todas ON</button>";
  html += "<button onclick='sendCommand(\"all_off\")'>🔅 Todas OFF</button>";
  html += "<br>";
  html += "<button onclick='sendCommand(\"wave\")'>🌊 Onda</button>";
  html += "<button onclick='sendCommand(\"chase\")'>🏃 Persecución</button>";
  html += "<button onclick='sendCommand(\"blink\")'>⚡ Parpadeo</button>";
  html += "<button onclick='sendCommand(\"random\")'>🎲 Aleatorio</button>";
  html += "<br>";
  html += "<button onclick='sendCommand(\"auto_on\")'>🤖 Auto ON</button>";
  html += "<button onclick='sendCommand(\"auto_off\")'>⏹️ Auto OFF</button>";
  html += "</div>";
  
  html += "</div>";
  
  // JavaScript
  html += "<script>";
  html += "function toggleModule(id){";
  html += "  fetch('/api/toggle?id='+id).then(()=>location.reload());";
  html += "}";
  html += "function sendCommand(cmd){";
  html += "  fetch('/api/command?cmd='+cmd).then(()=>location.reload());";
  html += "}";
  html += "function updateDelay(){";
  html += "  var delay = document.getElementById('delayInput').value;";
  html += "  fetch('/api/setdelay?delay='+delay).then(()=>location.reload());";
  html += "}";
  html += "setTimeout(()=>location.reload(), 5000);"; // Auto-refresh cada 5 segundos
  html += "</script>";
  
  html += "</body></html>";
  
  return html;
}

String generateStatusJson() {
  String json = "{";
  json += "\"systemStatus\":\"" + String(effectRunning ? "running" : "idle") + "\",";
  json += "\"currentEffect\":\"" + currentEffect + "\",";
  json += "\"autoMode\":" + String(autoMode ? "true" : "false") + ",";
  json += "\"onlineModules\":" + String(getOnlineModulesCount()) + ",";
  json += "\"totalModules\":" + String(MAX_MODULES) + ",";
  json += "\"autoOffDelay\":" + String(AUTO_OFF_DELAY) + ",";
  json += "\"uptime\":" + String(millis()) + "";
  json += "}";
  return json;
}

String generateModulesJson() {
  String json = "{\"modules\":[";
  for (int i = 0; i < MAX_MODULES; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"id\":" + String(modules[i].id) + ",";
    json += "\"ip\":\"" + modules[i].ip.toString() + "\",";
    json += "\"online\":" + String(modules[i].isOnline ? "true" : "false") + ",";
    json += "\"state\":" + String(modules[i].isOn ? "true" : "false") + ",";
    json += "\"version\":\"" + modules[i].version + "\",";
    json += "\"configuredDelay\":" + String(modules[i].configuredDelay) + ",";
    json += "\"lastHeartbeat\":" + String(modules[i].lastHeartbeat) + "";
    json += "}";
  }
  json += "]}";
  return json;
}

// =============================================================================
// MONITOREO Y DIAGNÓSTICO
// =============================================================================

void checkModulesHeartbeat() {
  unsigned long currentTime = millis();
  
  if (currentTime - lastHeartbeatCheck < 10000) return; // Verificar cada 10 segundos
  lastHeartbeatCheck = currentTime;
  
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline) {
      if (currentTime - modules[i].lastHeartbeat > MODULE_TIMEOUT) {
        modules[i].isOnline = false;
        modules[i].status = "timeout";
        Serial.println("Módulo " + String(i+1) + " desconectado (timeout)");
      }
    }
  }
}

int getOnlineModulesCount() {
  int count = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline) count++;
  }
  return count;
}

void printSystemStatus() {
  Serial.println("\n=== ESTADO DEL SISTEMA ===");
  Serial.println("Punto de Acceso: " + String(ssid));
  Serial.println("IP: " + WiFi.localIP().toString());
  Serial.println("Módulos online: " + String(getOnlineModulesCount()) + "/" + String(MAX_MODULES));
  Serial.println("Efecto actual: " + currentEffect);
  Serial.println("Modo automático: " + String(autoMode ? "ON" : "OFF"));
  Serial.println("AUTO_OFF_DELAY: " + String(AUTO_OFF_DELAY) + "ms (" + String(AUTO_OFF_DELAY/1000.0) + " segundos)");
  Serial.println("Uptime: " + String(millis()/1000) + " segundos");
  Serial.println("=========================\n");
}

void printModulesStatus() {
  Serial.println("\n=== ESTADO DE MÓDULOS ===");
  int onlineCount = 0;
  int onCount = 0;
  
  for (int i = 0; i < MAX_MODULES; i++) {
    Serial.print("Módulo " + String(i+1) + ": ");
    if (modules[i].isOnline) {
      onlineCount++;
      if (modules[i].isOn) onCount++;
      
      Serial.print("ONLINE (" + modules[i].ip.toString() + ") ");
      Serial.print(modules[i].isOn ? "[ON]" : "[OFF]");
      Serial.print(" v" + modules[i].version);
      Serial.print(" Delay:" + String(modules[i].configuredDelay) + "ms");
      Serial.println(" - Último heartbeat: " + String((millis() - modules[i].lastHeartbeat)/1000) + "s");
    } else {
      Serial.println("OFFLINE");
    }
  }
  
  Serial.println("\nResumen:");
  Serial.println("  Módulos online: " + String(onlineCount) + "/" + String(MAX_MODULES));
  Serial.println("  Módulos encendidos: " + String(onCount));
  Serial.println("========================\n");
}

void printConfiguration() {
  Serial.println("\n=== CONFIGURACIÓN ACTUAL ===");
  Serial.println("AUTO_OFF_DELAY: " + String(AUTO_OFF_DELAY) + "ms (" + String(AUTO_OFF_DELAY/1000.0) + " segundos)");
  Serial.println("EFFECT_DELAY: " + String(effectDelay) + "ms");
  Serial.println("HEARTBEAT_INTERVAL: " + String(HEARTBEAT_INTERVAL/1000) + " segundos");
  Serial.println("MODULE_TIMEOUT: " + String(MODULE_TIMEOUT/1000) + " segundos");
  Serial.println("MIN_ACTIVATION_INTERVAL: " + String(getMinActivationInterval()) + "ms");
  Serial.println("===========================\n");
}

void printSystemInfo() {
  Serial.println("\n=== INFORMACIÓN DEL SISTEMA ===");
  Serial.println("SSID: " + String(ssid));
  Serial.println("Password: " + String(password));
  Serial.println("IP Gateway: " + WiFi.localIP().toString());
  Serial.println("Puerto Web: 80");
  Serial.println("Puerto API: 8080");
  Serial.println("Máximo módulos: " + String(MAX_MODULES));
  Serial.println("\nBotones físicos:");
  Serial.println("  Botón 1: Pin " + String(BUTTON1_PIN) + " → Módulo 1");
  Serial.println("  Botón 2: Pin " + String(BUTTON2_PIN) + " → Módulo 2");
  Serial.println("  Botón 3: Pin " + String(BUTTON3_PIN) + " → Módulo 3");
  Serial.println("\nConfiguración de clientes ESP8266:");
  Serial.println("  Pin de relay: GPIO0 (con resistencia pull-up 10K)");
  Serial.println("  Control de focos: 10W por módulo");
  Serial.println("  Apagado automático: " + String(AUTO_OFF_DELAY/1000.0) + " segundos (configurable)");
  Serial.println("  Endpoint de configuración: http://" + WiFi.localIP().toString() + ":8080/config");
  Serial.println("==============================\n");
}

void printCommands() {
  Serial.println("\n=== COMANDOS DISPONIBLES ===");
  Serial.println("COMANDOS SERIE:");
  Serial.println("on [1-12]     - Encender foco específico");
  Serial.println("off [1-12]    - Apagar foco específico");
  Serial.println("all_on        - Encender todos los focos");
  Serial.println("all_off       - Apagar todos los focos");
  Serial.println("wave          - Efecto onda secuencial");
  Serial.println("chase         - Efecto persecución");
  Serial.println("blink         - Efecto parpadeo");
  Serial.println("random        - Efecto aleatorio");
  Serial.println("auto_on       - Activar modo automático");
  Serial.println("auto_off      - Desactivar modo automático");
  Serial.println("speed [ms]    - Ajustar velocidad efecto");
  Serial.println("setdelay [ms] - Cambiar tiempo de apagado (1000-60000)");
  Serial.println("status        - Estado del sistema");
  Serial.println("modules       - Estado de módulos");
  Serial.println("config        - Mostrar configuración actual");
  Serial.println("reset         - Reiniciar sistema");
  Serial.println("help          - Mostrar esta ayuda");
  Serial.println("");
  Serial.println("BOTONES FÍSICOS:");
  Serial.println("Botón 1 (Pin " + String(BUTTON1_PIN) + ") → Módulo 1");
  Serial.println("Botón 2 (Pin " + String(BUTTON2_PIN) + ") → Módulo 2");
  Serial.println("Botón 3 (Pin " + String(BUTTON3_PIN) + ") → Módulo 3");
  Serial.println("");
  Serial.println("CONFIGURACIÓN:");
  Serial.println("Los módulos se apagarán automáticamente después de " + String(AUTO_OFF_DELAY/1000.0) + " segundos");
  Serial.println("============================\n");
}

void resetSystem() {
  Serial.println("Reiniciando sistema...");
  initializeModules();
  stopEffect();
  autoMode = false;
  Serial.println("Sistema reiniciado");
}