/*
 * Sistema de Control de 12 Tiras LED - Arduino UNO R4 WiFi (Maestro)
 * Autor: Sistema LED Control
 * Versión: 2.2 - Corregido
 * 
 * Funcionalidades:
 * - Punto de acceso WiFi autónomo
 * - Control de 12 módulos ESP8266-01S
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

// Configuración de los botones físicos
#define BUTTON1_PIN 2             // Pin digital 2 para botón 1 (Módulo 1)
#define BUTTON2_PIN 3             // Pin digital 3 para botón 2 (Módulo 2)
#define BUTTON3_PIN 4             // Pin digital 4 para botón 3 (Módulo 3)
#define BUTTON_DEBOUNCE 50        // 50ms debounce
#define AUTO_OFF_DELAY 2000       // 2 segundos para apagado automático
#define HTTP_TIMEOUT 1000        // 1 segundo timeout para HTTP (más rápido)

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
};

ButtonState button1 = {HIGH, HIGH, 0, false};
ButtonState button2 = {HIGH, HIGH, 0, false};
ButtonState button3 = {HIGH, HIGH, 0, false};

// Variables para control temporal de módulos
struct ModuleTimer {
  bool autoOffActive;
  unsigned long turnOnTime;
  int moduleId;
};

ModuleTimer moduleTimers[3] = {
  {false, 0, 1},  // Botón 1 → Módulo 1
  {false, 0, 2},  // Botón 2 → Módulo 2  
  {false, 0, 3}   // Botón 3 → Módulo 3
};

// =============================================================================
// CONFIGURACIÓN INICIAL
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== SISTEMA DE CONTROL DE TIRAS LED ===");
  Serial.println("Inicializando Arduino UNO R4 WiFi como Punto de Acceso...");
  
  // Configurar pines de los botones
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  // Leer estados iniciales
  button1.lastState = digitalRead(BUTTON1_PIN);
  button1.currentState = button1.lastState;
  button2.lastState = digitalRead(BUTTON2_PIN);
  button2.currentState = button2.lastState;
  button3.lastState = digitalRead(BUTTON3_PIN);
  button3.currentState = button3.lastState;
  
  Serial.println("Botones físicos configurados:");
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
  
  // Manejar temporizadores de módulos
  handleModuleTimers();
  
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
  }
  registeredModules = 0;
  Serial.println("Estructura de módulos inicializada");
}

// =============================================================================
// MANEJO DE LOS BOTONES FÍSICOS
// =============================================================================

void handlePhysicalButtons() {
  // Manejar cada botón individualmente
  handleSingleButton(BUTTON1_PIN, &button1, 0); // Botón 1 → Módulo 1
  handleSingleButton(BUTTON2_PIN, &button2, 1); // Botón 2 → Módulo 2
  handleSingleButton(BUTTON3_PIN, &button3, 2); // Botón 3 → Módulo 3
}

void handleSingleButton(int pin, ButtonState* buttonState, int timerIndex) {
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
      if (buttonState->currentState == LOW) {
        buttonState->pressed = true;
        int moduleId = moduleTimers[timerIndex].moduleId;
        Serial.println("🔘 Botón " + String(timerIndex + 1) + " presionado → Activando módulo " + String(moduleId));
        
        // Activar módulo correspondiente
        activateModuleWithTimer(moduleId, timerIndex);
      }
      
      // Botón liberado
      else if (buttonState->pressed) {
        buttonState->pressed = false;
        Serial.println("🔘 Botón " + String(timerIndex + 1) + " liberado");
      }
    }
  }
  
  buttonState->lastState = reading;
}

void activateModuleWithTimer(int moduleId, int timerIndex) {
  // Verificar que el módulo esté online
  if (!modules[moduleId - 1].isOnline) {
    Serial.println("❌ Módulo " + String(moduleId) + " no está online");
    return;
  }
  
  // Si ya hay un temporizador activo para este módulo, cancelarlo
  if (moduleTimers[timerIndex].autoOffActive) {
    Serial.println("⏹️ Cancelando temporizador previo para módulo " + String(moduleId));
    moduleTimers[timerIndex].autoOffActive = false;
  }
  
  // Encender el módulo usando función rápida
  bool success = controlModuleFast(moduleId, true);
  
  if (success) {
    // Configurar temporizador para apagado automático
    moduleTimers[timerIndex].autoOffActive = true;
    moduleTimers[timerIndex].turnOnTime = millis();
    
    Serial.println("✅ Módulo " + String(moduleId) + " encendido (rápido)");
    Serial.println("⏰ Apagado automático en " + String(AUTO_OFF_DELAY / 1000) + " segundos");
  } else {
    Serial.println("❌ Error al encender módulo " + String(moduleId));
  }
}

void handleModuleTimers() {
  unsigned long currentTime = millis();
  
  // Verificar cada temporizador
  for (int i = 0; i < 3; i++) {
    if (moduleTimers[i].autoOffActive) {
      unsigned long elapsedTime = currentTime - moduleTimers[i].turnOnTime;
      
      // Verificar si ha pasado el tiempo de espera
      if (elapsedTime >= AUTO_OFF_DELAY) {
        int moduleId = moduleTimers[i].moduleId;
        
        // Desactivar temporizador ANTES de enviar comando
        moduleTimers[i].autoOffActive = false;
        
        Serial.println("⏰ Tiempo cumplido - Apagando módulo " + String(moduleId));
        
        // Apagar el módulo usando función rápida
        bool success = controlModuleFast(moduleId, false);
        
        if (success) {
          Serial.println("⏰ Módulo " + String(moduleId) + " apagado automáticamente (rápido)");
        } else {
          Serial.println("❌ Error al apagar módulo " + String(moduleId) + " automáticamente");
        }
      }
      // Remover cuenta regresiva para evitar spam en consola
    }
  }
}

// Función para cancelar temporizadores (opcional)
void cancelModuleTimer(int moduleId) {
  for (int i = 0; i < 3; i++) {
    if (moduleTimers[i].moduleId == moduleId && moduleTimers[i].autoOffActive) {
      moduleTimers[i].autoOffActive = false;
      Serial.println("⏹️ Temporizador cancelado para módulo " + String(moduleId));
      break;
    }
  }
}

// Función para verificar si un módulo tiene temporizador activo
bool hasActiveTimer(int moduleId) {
  for (int i = 0; i < 3; i++) {
    if (moduleTimers[i].moduleId == moduleId && moduleTimers[i].autoOffActive) {
      return true;
    }
  }
  return false;
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

// Función rápida para control de módulos sin esperar respuesta completa
bool controlModuleFast(int moduleId, bool state) {
  if (moduleId < 1 || moduleId > MAX_MODULES) {
    return false;
  }
  
  int index = moduleId - 1;
  if (!modules[index].isOnline) {
    return false;
  }
  
  WiFiClient client;
  
  // Timeout muy corto para conexión rápida
  if (client.connect(modules[index].ip, 80)) {
    String httpRequest = "GET ";
    httpRequest += (state ? "/on" : "/off");
    httpRequest += " HTTP/1.1";
    client.println(httpRequest);
    client.println("Host: " + modules[index].ip.toString());
    client.println("Connection: close");
    client.println();
    
    // No esperar respuesta, enviar y cerrar inmediatamente
    client.stop();
    
    // Actualizar estado local
    modules[index].isOn = state;
    return true;
  }
  
  return false;
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
  Serial.println(state ? "Encendiendo todas las tiras..." : "Apagando todas las tiras...");
  
  int successCount = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].isOnline) {
      if (controlModule(i + 1, state)) {
        successCount++;
      }
      delay(100); // Pequeña pausa entre comandos
    }
  }
  
  Serial.println("Comando ejecutado en " + String(successCount) + " módulos");
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
    
    client.stop();
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
  
  if (moduleId >= 1 && moduleId <= MAX_MODULES) {
    int index = moduleId - 1;
    modules[index].ip = client.remoteIP();
    modules[index].isOnline = true;
    modules[index].lastHeartbeat = millis();
    modules[index].status = "online";
    
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
    
    Serial.println("Módulo " + String(moduleId) + " registrado desde IP: " + client.remoteIP().toString());
    
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
  // Extraer ID del módulo
  int idStart = request.indexOf("id=") + 3;
  int idEnd = request.indexOf(" ", idStart);
  if (idEnd == -1) idEnd = request.indexOf("&", idStart);
  if (idEnd == -1) idEnd = request.length();
  
  int moduleId = request.substring(idStart, idEnd).toInt();
  
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
  html += "<title>Control de Tiras LED</title>";
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
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>🌈 Sistema de Control LED</h1>";
  
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
  Serial.println("Uptime: " + String(millis()/1000) + " segundos");
  
  // Mostrar estado de temporizadores activos
  for (int i = 0; i < 3; i++) {
    if (moduleTimers[i].autoOffActive) {
      unsigned long timeLeft = AUTO_OFF_DELAY - (millis() - moduleTimers[i].turnOnTime);
      Serial.println("Timer activo - Módulo " + String(moduleTimers[i].moduleId) + ": " + String(timeLeft/1000) + "s restantes");
    }
  }
  
  Serial.println("=========================\n");
}

void printModulesStatus() {
  Serial.println("\n=== ESTADO DE MÓDULOS ===");
  for (int i = 0; i < MAX_MODULES; i++) {
    Serial.print("Módulo " + String(i+1) + ": ");
    if (modules[i].isOnline) {
      Serial.print("ONLINE (" + modules[i].ip.toString() + ") ");
      Serial.print(modules[i].isOn ? "ON" : "OFF");
      Serial.println(" - Último heartbeat: " + String((millis() - modules[i].lastHeartbeat)/1000) + "s");
    } else {
      Serial.println("OFFLINE");
    }
  }
  Serial.println("========================\n");
}

void printSystemInfo() {
  Serial.println("\n=== INFORMACIÓN DEL SISTEMA ===");
  Serial.println("SSID: " + String(ssid));
  Serial.println("Password: " + String(password));
  Serial.println("IP Gateway: " + WiFi.localIP().toString());
  Serial.println("Puerto Web: 80");
  Serial.println("Puerto API: 8080");
  Serial.println("Máximo módulos: " + String(MAX_MODULES));
  Serial.println("Botones físicos:");
  Serial.println("  Botón 1: Pin " + String(BUTTON1_PIN) + " → Módulo 1");
  Serial.println("  Botón 2: Pin " + String(BUTTON2_PIN) + " → Módulo 2");
  Serial.println("  Botón 3: Pin " + String(BUTTON3_PIN) + " → Módulo 3");
  Serial.println("Auto-off: " + String(AUTO_OFF_DELAY / 1000) + " segundos");
  Serial.println("==============================\n");
}

void printCommands() {
  Serial.println("\n=== COMANDOS DISPONIBLES ===");
  Serial.println("COMANDOS SERIE:");
  Serial.println("on [1-12]     - Encender tira específica");
  Serial.println("off [1-12]    - Apagar tira específica");
  Serial.println("all_on        - Encender todas las tiras");
  Serial.println("all_off       - Apagar todas las tiras");
  Serial.println("wave          - Efecto onda secuencial");
  Serial.println("chase         - Efecto persecución");
  Serial.println("blink         - Efecto parpadeo");
  Serial.println("random        - Efecto aleatorio");
  Serial.println("auto_on       - Activar modo automático");
  Serial.println("auto_off      - Desactivar modo automático");
  Serial.println("speed [ms]    - Ajustar velocidad efecto");
  Serial.println("status        - Estado del sistema");
  Serial.println("modules       - Estado de módulos");
  Serial.println("reset         - Reiniciar sistema");
  Serial.println("help          - Mostrar esta ayuda");
  Serial.println("");
  Serial.println("BOTONES FÍSICOS:");
  Serial.println("Botón 1 (Pin " + String(BUTTON1_PIN) + ") → Módulo 1 (ON 2s → OFF)");
  Serial.println("Botón 2 (Pin " + String(BUTTON2_PIN) + ") → Módulo 2 (ON 2s → OFF)");
  Serial.println("Botón 3 (Pin " + String(BUTTON3_PIN) + ") → Módulo 3 (ON 2s → OFF)");
  Serial.println("============================\n");
}

void resetSystem() {
  Serial.println("Reiniciando sistema...");
  
  // Cancelar todos los temporizadores activos
  for (int i = 0; i < 3; i++) {
    moduleTimers[i].autoOffActive = false;
  }
  
  initializeModules();
  stopEffect();
  autoMode = false;
  Serial.println("Sistema reiniciado");
}