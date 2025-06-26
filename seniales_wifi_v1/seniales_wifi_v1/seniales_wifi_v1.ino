/*
 * Sistema de Control de 12 Focos - Arduino UNO R4 WiFi (Maestro)
 * Versión: 8.1 - Corregido problema de IP 0.0.0.0
 * 
 * CAMBIOS EN v8.1:
 * - Corregido problema de asignación de IP
 * - Mejorado manejo de DHCP
 * - Agregados reintentos para obtener IP válida
 * 
 * INSTRUCCIONES:
 * 1. Cambiar ROUTER_SSID y ROUTER_PASS con los datos de tu router
 * 2. Subir este código al Arduino UNO R4 WiFi
 * 3. Abrir monitor serie a 115200 baudios
 * 4. Verificar que obtenga una IP válida (no 0.0.0.0)
 * 
 * CARACTERÍSTICAS:
 * - Soporta 12 módulos ESP8266-01S sin límite de conexiones
 * - Control por grupos con 6 botones configurables
 * - Interfaz web moderna y responsive
 * - Auto-descubrimiento de módulos
 * - Efectos de iluminación programables
 * - Comandos UDP para baja latencia
 */

#include "WiFiS3.h"
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN DEL ROUTER - CAMBIAR ESTOS VALORES
// =============================================================================

const char* ROUTER_SSID = "pmn_seniales";      // CAMBIAR! Nombre de tu WiFi
const char* ROUTER_PASS = "2WC456403581";    // CAMBIAR! Contraseña de tu WiFi

// =============================================================================
// CONFIGURACIÓN DEL SISTEMA
// =============================================================================

// Sistema
#define MAX_MODULES 12
#define UDP_PORT 8888
#define AUTO_OFF_DELAY 3000      // 3 segundos auto-apagado
#define MODULE_TIMEOUT 120000    // 2 minutos timeout
#define EFFECT_DELAY 500         // Velocidad de efectos

// Pines de botones físicos (opcional)
#define BUTTON1_PIN 2
#define BUTTON2_PIN 3
#define BUTTON3_PIN 4
#define LED1_PIN 5
#define LED2_PIN 6
#define LED3_PIN 7
#define BUTTON_DEBOUNCE 50
#define LED_AUTO_OFF 3000

// Configuración de grupos (qué módulos controla cada botón)
const int BUTTON1_MODULES[] = {1,2,3,4,5,6,0};              // Botón 1: Solo módulo 1
const int BUTTON2_MODULES[] = {7,8,9,10,11,12,0};              // Botón 2: Solo módulo 2
const int BUTTON3_MODULES[] = {3, 4, 5, 0};        // Botón 3: Módulos 3, 4, 5
const int BUTTON4_MODULES[] = {6, 7, 8, 0};        // Grupo 4: Módulos 6, 7, 8
const int BUTTON5_MODULES[] = {9, 10, 11, 0};      // Grupo 5: Módulos 9, 10, 11
const int BUTTON6_MODULES[] = {12, 0};             // Grupo 6: Solo módulo 12

// =============================================================================
// VARIABLES GLOBALES
// =============================================================================

// Servidores
WiFiServer webServer(80);        // Servidor web
WiFiServer apiServer(8080);      // API para registro de módulos
WiFiUDP udp;                     // Comunicación UDP

// Información del sistema
IPAddress myIP;
bool wifiConnected = false;
unsigned long bootTime = 0;

// Estructura para módulos
struct Module {
  uint8_t id;
  IPAddress ip;
  bool online;
  bool state;
  unsigned long lastSeen;
  String version;
} modules[MAX_MODULES];

// Control de botones físicos
struct Button {
  bool lastState;
  bool currentState;
  unsigned long lastDebounce;
  unsigned long lastPress;
} buttons[3];

// Control de LEDs indicadores
struct Led {
  bool on;
  unsigned long turnOnTime;
} leds[3];

// Sistema de efectos
bool effectRunning = false;
String currentEffect = "none";
int effectStep = 0;
unsigned long lastEffectUpdate = 0;
int effectSpeed = EFFECT_DELAY;

// Estadísticas
struct Stats {
  unsigned long commandsReceived;
  unsigned long startTime;
  int peakModules;
} stats = {0, 0, 0};

// Mantenimiento
unsigned long lastMaintenanceCheck = 0;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  bootTime = millis();
  stats.startTime = bootTime;
  
  printHeader();
  
  // Configurar hardware
  setupHardware();
  
  // Inicializar módulos
  initializeModules();
  
  // Conectar al router
  if (connectToRouter()) {
    // Iniciar servicios
    startServices();
    printConnectionInfo();
  } else {
    Serial.println("\n❌ ERROR CRÍTICO: No se pudo conectar al router");
    Serial.println("Verifica SSID y contraseña, luego reinicia");
    while(1) { 
      // Parpadeo de error
      digitalWrite(LED1_PIN, HIGH);
      delay(100);
      digitalWrite(LED1_PIN, LOW);
      delay(100);
    }
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
  
  // Procesar comandos UDP (máxima prioridad)
  handleUDP();
  
  // Procesar botones físicos
  handleButtons();
  
  // Actualizar LEDs indicadores
  updateLeds();
  
  // Manejar clientes web
  handleWebServer();
  
  // Manejar API REST
  handleAPIServer();
  
  // Tareas de mantenimiento (cada 10 segundos)
  if (millis() - lastMaintenanceCheck > 10000) {
    performMaintenance();
    lastMaintenanceCheck = millis();
  }
  
  // Ejecutar efectos si están activos
  if (effectRunning) {
    runEffect();
  }
  
  // Procesar comandos serie
  if (Serial.available()) {
    handleSerialCommand();
  }
  
  delay(10);
}

// =============================================================================
// CONFIGURACIÓN INICIAL
// =============================================================================

void printHeader() {
  Serial.println("\n");
  Serial.println("╔════════════════════════════════════════════════╗");
  Serial.println("║    SISTEMA DE CONTROL DE ILUMINACIÓN v8.1      ║");
  Serial.println("║          Modo: Router WiFi Externo             ║");
  Serial.println("║           Soporta: 12 Módulos LED              ║");
  Serial.println("╚════════════════════════════════════════════════╝");
  Serial.println();
}

void setupHardware() {
  Serial.println("📌 Configurando hardware...");
  
  // Configurar botones con pull-up interno
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  // Configurar LEDs indicadores
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  // Apagar LEDs
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  
  // Estado inicial de botones
  for (int i = 0; i < 3; i++) {
    buttons[i].lastState = HIGH;
    buttons[i].currentState = HIGH;
    buttons[i].lastDebounce = 0;
    buttons[i].lastPress = 0;
    
    leds[i].on = false;
    leds[i].turnOnTime = 0;
  }
  
  Serial.println("✅ Hardware configurado");
}

void initializeModules() {
  for (int i = 0; i < MAX_MODULES; i++) {
    modules[i].id = i + 1;
    modules[i].ip = IPAddress(0, 0, 0, 0);
    modules[i].online = false;
    modules[i].state = false;
    modules[i].lastSeen = 0;
    modules[i].version = "unknown";
  }
  Serial.println("✅ Estructura de módulos inicializada");
}

// =============================================================================
// CONEXIÓN WIFI - CORREGIDO PARA PROBLEMA DE IP
// =============================================================================

bool connectToRouter() {
  Serial.println("\n🌐 Conectando al router...");
  Serial.println("   SSID: " + String(ROUTER_SSID));
  Serial.print("   ");
  
  // Iniciar conexión WiFi
  WiFi.begin(ROUTER_SSID, ROUTER_PASS);
  
  // Esperar conexión
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {
    delay(500);
    Serial.print(".");
    if (attempts % 20 == 19) Serial.print("\n   ");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n\n✅ WiFi conectado! Obteniendo IP...");
    
    // IMPORTANTE: Esperar un momento para que el DHCP asigne la IP
    delay(2000);
    
    // Intentar obtener la IP varias veces
    myIP = WiFi.localIP();
    int ipRetries = 0;
    
    while (myIP == IPAddress(0,0,0,0) && ipRetries < 15) {
      delay(1000);
      myIP = WiFi.localIP();
      Serial.println("   Esperando IP del DHCP... intento " + String(ipRetries + 1) + "/15");
      
      // En algunos casos, reconectar ayuda
      if (ipRetries == 10) {
        Serial.println("   Forzando renovación DHCP...");
        WiFi.disconnect();
        delay(1000);
        WiFi.begin(ROUTER_SSID, ROUTER_PASS);
        delay(3000);
      }
      
      ipRetries++;
    }
    
    // Verificar si obtuvimos IP válida
    if (myIP == IPAddress(0,0,0,0)) {
      Serial.println("\n❌ ERROR: No se pudo obtener IP del DHCP");
      Serial.println("   El router no asignó una IP válida");
      Serial.println("   Posibles causas:");
      Serial.println("   - DHCP deshabilitado en el router");
      Serial.println("   - Pool de IPs agotado");
      Serial.println("   - Filtrado MAC activo");
      
      wifiConnected = false;
      return false;
    }
    
    wifiConnected = true;
    Serial.println("\n✅ IP OBTENIDA CORRECTAMENTE!");
    
    // Mostrar efecto visual en LEDs
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
    
    return true;
  }
  
  Serial.println("\n❌ No se pudo conectar al WiFi");
  Serial.println("   Estado: " + String(WiFi.status()));
  return false;
}

bool checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      wifiConnected = false;
      Serial.println("\n⚠️ WiFi desconectado! Intentando reconectar...");
    }
    
    static unsigned long lastReconnectAttempt = 0;
    if (millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();
      WiFi.disconnect();
      delay(1000);
      return connectToRouter();
    }
    return false;
  }
  
  // Verificar que mantengamos una IP válida
  if (WiFi.localIP() == IPAddress(0,0,0,0)) {
    Serial.println("\n⚠️ IP perdida! Reconectando...");
    wifiConnected = false;
    WiFi.disconnect();
    delay(1000);
    return connectToRouter();
  }
  
  return true;
}

// =============================================================================
// SERVICIOS DE RED
// =============================================================================

void startServices() {
  Serial.println("\n🚀 Iniciando servicios...");
  
  // Servidor Web
  webServer.begin();
  Serial.println("   ✅ Servidor Web en puerto 80");
  
  // Servidor API
  apiServer.begin();
  Serial.println("   ✅ API REST en puerto 8080");
  
  // UDP
  if (udp.begin(UDP_PORT)) {
    Serial.println("   ✅ Servidor UDP en puerto " + String(UDP_PORT));
  } else {
    Serial.println("   ❌ Error iniciando UDP");
  }
  
  // Anunciar presencia en la red
  delay(1000);
  announcePresence();
}

void announcePresence() {
  Serial.println("\n📢 Anunciando presencia del maestro...");
  
  // Calcular IP de broadcast según la red
  IPAddress broadcastIP;
  if (myIP[0] == 192 && myIP[1] == 168) {
    // Red típica doméstica
    broadcastIP = IPAddress(myIP[0], myIP[1], myIP[2], 255);
  } else if (myIP[0] == 10) {
    // Red 10.x.x.x
    broadcastIP = IPAddress(10, 255, 255, 255);
  } else {
    // Red desconocida, usar broadcast general
    broadcastIP = IPAddress(255, 255, 255, 255);
  }
  
  // Enviar anuncio múltiples veces para mayor confiabilidad
  for (int i = 0; i < 3; i++) {
    udp.beginPacket(broadcastIP, UDP_PORT);
    udp.print("MASTER:" + myIP.toString());
    udp.endPacket();
    delay(100);
  }
  
  Serial.println("   Broadcast enviado a: " + broadcastIP.toString());
  Serial.println("   IP anunciada: " + myIP.toString());
}

void printConnectionInfo() {
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║           SISTEMA INICIADO CON ÉXITO           ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ 🌐 Red WiFi: " + String(ROUTER_SSID));
  Serial.println("║ 📍 IP Maestro: " + myIP.toString());
  Serial.println("║ 🚪 Gateway: " + WiFi.gatewayIP().toString());
  Serial.println("║ 📊 Señal: " + String(WiFi.RSSI()) + " dBm");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ 🌐 Interfaz Web: http://" + myIP.toString());
  Serial.println("║ 🔌 Puerto API: 8080 (registro módulos)");
  Serial.println("║ 📡 Puerto UDP: 8888 (comandos)");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ Los ESP8266 deben conectarse a la misma red   ║");
  Serial.println("║ y se registrarán automáticamente              ║");
  Serial.println("╚════════════════════════════════════════════════╝");
  Serial.println("\nComandos disponibles: escribe 'help'");
}

// =============================================================================
// MANEJO UDP
// =============================================================================

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (!packetSize) return;
  
  char buffer[64];
  int len = udp.read(buffer, 63);
  if (len > 0) buffer[len] = 0;
  
  String command = String(buffer);
  IPAddress remoteIP = udp.remoteIP();
  
  // Log del comando
  Serial.println("📡 UDP [" + remoteIP.toString() + "]: " + command);
  stats.commandsReceived++;
  
  // Comandos de módulos ESP8266
  if (command == "ON" || command == "OFF") {
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].ip == remoteIP) {
        modules[i].state = (command == "ON");
        return;
      }
    }
  }
  
  // Comando de descubrimiento
  if (command == "DISCOVER") {
    udp.beginPacket(remoteIP, UDP_PORT);
    udp.print("MASTER:" + myIP.toString());
    udp.endPacket();
    Serial.println("   → Respondido con IP del maestro");
    return;
  }
  
  // Comandos de control
  command.toLowerCase();
  processControlCommand(command);
}

void processControlCommand(String cmd) {
  if (cmd == "button_group_1") activateGroup(BUTTON1_MODULES);
  else if (cmd == "button_group_2") activateGroup(BUTTON2_MODULES);
  else if (cmd == "button_group_3") activateGroup(BUTTON3_MODULES);
  else if (cmd == "button_group_4") activateGroup(BUTTON4_MODULES);
  else if (cmd == "button_group_5") activateGroup(BUTTON5_MODULES);
  else if (cmd == "button_group_6") activateGroup(BUTTON6_MODULES);
  else if (cmd == "all_on") allModulesOn();
  else if (cmd == "all_off") allModulesOff();
  else if (cmd == "wave") startEffect("wave");
  else if (cmd == "chase") startEffect("chase");
  else if (cmd == "blink") startEffect("blink");
  else if (cmd == "random") startEffect("random");
  else if (cmd == "stop") stopEffect();
}

// =============================================================================
// CONTROL DE MÓDULOS
// =============================================================================

void activateGroup(const int* moduleList) {
  Serial.print("🔸 Activando grupo: ");
  
  int i = 0;
  int activated = 0;
  
  while (moduleList[i] != 0) {
    int moduleId = moduleList[i];
    if (sendToModule(moduleId, true)) {
      Serial.print(String(moduleId) + " ");
      activated++;
    }
    i++;
  }
  
  if (activated > 0) {
    Serial.println("\n   → " + String(activated) + " módulos activados (auto-off en " + 
                   String(AUTO_OFF_DELAY/1000.0) + "s)");
  } else {
    Serial.println("(ninguno online)");
  }
}

bool sendToModule(int moduleId, bool state) {
  if (moduleId < 1 || moduleId > MAX_MODULES) return false;
  
  int index = moduleId - 1;
  if (!modules[index].online) return false;
  
  // Solo enviar comando ON (los módulos se apagan solos)
  if (state) {
    udp.beginPacket(modules[index].ip, UDP_PORT);
    udp.print("ON");
    udp.endPacket();
    modules[index].state = true;
    return true;
  }
  
  return false;
}

void allModulesOn() {
  Serial.println("🔆 Encendiendo todos los módulos...");
  
  int count = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online) {
      udp.beginPacket(modules[i].ip, UDP_PORT);
      udp.print("ON");
      udp.endPacket();
      modules[i].state = true;
      count++;
      delay(20);
    }
  }
  
  Serial.println("   → " + String(count) + " módulos encendidos");
}

void allModulesOff() {
  Serial.println("🔅 Apagando todos los módulos...");
  
  int count = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online) {
      udp.beginPacket(modules[i].ip, UDP_PORT);
      udp.print("OFF");
      udp.endPacket();
      modules[i].state = false;
      count++;
      delay(20);
    }
  }
  
  Serial.println("   → " + String(count) + " módulos apagados");
  stopEffect();
}

// =============================================================================
// BOTONES FÍSICOS
// =============================================================================

void handleButtons() {
  bool states[3] = {
    digitalRead(BUTTON1_PIN),
    digitalRead(BUTTON2_PIN),
    digitalRead(BUTTON3_PIN)
  };
  
  for (int i = 0; i < 3; i++) {
    // Debounce
    if (states[i] != buttons[i].lastState) {
      buttons[i].lastDebounce = millis();
    }
    
    if ((millis() - buttons[i].lastDebounce) > BUTTON_DEBOUNCE) {
      if (states[i] != buttons[i].currentState) {
        buttons[i].currentState = states[i];
        
        // Botón presionado (LOW con pull-up)
        if (buttons[i].currentState == LOW) {
          // Verificar intervalo mínimo
          if (millis() - buttons[i].lastPress >= AUTO_OFF_DELAY + 500) {
            processButtonPress(i);
            buttons[i].lastPress = millis();
          }
        }
      }
    }
    
    buttons[i].lastState = states[i];
  }
}

void processButtonPress(int buttonIndex) {
  Serial.println("🔘 Botón " + String(buttonIndex + 1) + " presionado");
  
  // Encender LED indicador
  digitalWrite(LED1_PIN + buttonIndex, HIGH);
  leds[buttonIndex].on = true;
  leds[buttonIndex].turnOnTime = millis();
  
  // Activar grupo correspondiente
  const int* moduleList = nullptr;
  
  switch(buttonIndex) {
    case 0: moduleList = BUTTON1_MODULES; break;
    case 1: moduleList = BUTTON2_MODULES; break;
    case 2: moduleList = BUTTON3_MODULES; break;
  }
  
  if (moduleList) {
    activateGroup(moduleList);
  }
}

void updateLeds() {
  unsigned long now = millis();
  
  for (int i = 0; i < 3; i++) {
    if (leds[i].on && (now - leds[i].turnOnTime >= LED_AUTO_OFF)) {
      digitalWrite(LED1_PIN + i, LOW);
      leds[i].on = false;
    }
  }
}

// =============================================================================
// EFECTOS DE ILUMINACIÓN
// =============================================================================

void startEffect(String effect) {
  currentEffect = effect;
  effectRunning = true;
  effectStep = 0;
  lastEffectUpdate = millis();
  Serial.println("🎨 Efecto iniciado: " + effect);
}

void stopEffect() {
  if (effectRunning) {
    effectRunning = false;
    currentEffect = "none";
    allModulesOff();
    Serial.println("🎨 Efecto detenido");
  }
}

void runEffect() {
  if (millis() - lastEffectUpdate < effectSpeed) return;
  lastEffectUpdate = millis();
  
  if (currentEffect == "wave") {
    // Apagar todos
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].online && modules[i].state) {
        udp.beginPacket(modules[i].ip, UDP_PORT);
        udp.print("OFF");
        udp.endPacket();
        modules[i].state = false;
      }
    }
    // Encender actual
    if (modules[effectStep].online) {
      udp.beginPacket(modules[effectStep].ip, UDP_PORT);
      udp.print("ON");
      udp.endPacket();
      modules[effectStep].state = true;
    }
    effectStep = (effectStep + 1) % MAX_MODULES;
  }
  else if (currentEffect == "chase") {
    // Similar a wave pero con 3 módulos encendidos
    for (int i = 0; i < MAX_MODULES; i++) {
      bool shouldBeOn = false;
      for (int j = 0; j < 3; j++) {
        if (i == (effectStep + j) % MAX_MODULES) {
          shouldBeOn = true;
          break;
        }
      }
      
      if (modules[i].online && modules[i].state != shouldBeOn) {
        udp.beginPacket(modules[i].ip, UDP_PORT);
        udp.print(shouldBeOn ? "ON" : "OFF");
        udp.endPacket();
        modules[i].state = shouldBeOn;
      }
    }
    effectStep = (effectStep + 1) % MAX_MODULES;
  }
  else if (currentEffect == "blink") {
    bool targetState = (effectStep % 2 == 0);
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].online) {
        udp.beginPacket(modules[i].ip, UDP_PORT);
        udp.print(targetState ? "ON" : "OFF");
        udp.endPacket();
        modules[i].state = targetState;
      }
    }
    effectStep++;
    if (effectStep >= 10) stopEffect(); // 5 parpadeos y parar
  }
  else if (currentEffect == "random") {
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].online && random(0, 3) == 0) {
        bool newState = !modules[i].state;
        udp.beginPacket(modules[i].ip, UDP_PORT);
        udp.print(newState ? "ON" : "OFF");
        udp.endPacket();
        modules[i].state = newState;
      }
    }
  }
}

// =============================================================================
// SERVIDOR WEB
// =============================================================================

void handleWebServer() {
  WiFiClient client = webServer.available();
  if (!client) return;
  
  String request = client.readStringUntil('\r');
  client.flush();
  
  // Página principal
  if (request.indexOf("GET / ") != -1) {
    serveMainPage(client);
  }
  // API para AJAX
  else if (request.indexOf("/api/status") != -1) {
    serveStatusJSON(client);
  }
  else if (request.indexOf("/api/command") != -1) {
    handleWebCommand(client, request);
  }
  // 404
  else {
    client.println("HTTP/1.1 404 Not Found\r\n\r\n");
  }
  
  client.stop();
}

void serveMainPage(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  
  // HTML moderno y responsive
  client.println("<!DOCTYPE html>");
  client.println("<html><head>");
  client.println("<title>Control de Iluminación</title>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  client.println("<style>");
  client.println("body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;");
  client.println("margin:0;padding:20px;background:#1a1a1a;color:#fff}");
  client.println(".container{max-width:1200px;margin:0 auto}");
  client.println("h1{text-align:center;font-size:2.5em;margin-bottom:10px;");
  client.println("background:linear-gradient(45deg,#00b4d8,#0077b6);-webkit-background-clip:text;");
  client.println("-webkit-text-fill-color:transparent}");
  client.println(".status{background:#2d2d2d;border-radius:15px;padding:20px;margin-bottom:30px;");
  client.println("display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px}");
  client.println(".stat-box{background:#1a1a1a;padding:15px;border-radius:10px;text-align:center}");
  client.println(".stat-value{font-size:2em;font-weight:bold;color:#00b4d8}");
  client.println(".stat-label{color:#888;margin-top:5px}");
  client.println(".modules{display:grid;grid-template-columns:repeat(auto-fill,minmax(100px,1fr));");
  client.println("gap:15px;margin-bottom:30px}");
  client.println(".module{aspect-ratio:1;border-radius:15px;display:flex;flex-direction:column;");
  client.println("align-items:center;justify-content:center;cursor:pointer;transition:all 0.3s;");
  client.println("background:#2d2d2d;border:2px solid transparent;position:relative;overflow:hidden}");
  client.println(".module.online{background:linear-gradient(135deg,#2d2d2d,rgba(0,180,216,0.2));");
  client.println("border-color:#00b4d8}");
  client.println(".module.on::before{content:'';position:absolute;top:-50%;left:-50%;");
  client.println("width:200%;height:200%;background:radial-gradient(circle,rgba(0,180,216,0.4) 0%,transparent 70%);");
  client.println("animation:pulse 2s infinite}");
  client.println("@keyframes pulse{0%{transform:scale(0.8);opacity:1}50%{transform:scale(1.2);opacity:0.7}");
  client.println("100%{transform:scale(0.8);opacity:1}}");
  client.println(".module-id{font-size:2em;font-weight:bold}");
  client.println(".module-status{font-size:0.9em;color:#888;margin-top:5px}");
  client.println(".controls{background:#2d2d2d;border-radius:15px;padding:25px}");
  client.println(".control-group{margin-bottom:20px}");
  client.println(".control-group h3{margin-bottom:15px;color:#00b4d8}");
  client.println(".btn-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}");
  client.println("button{background:#0077b6;color:white;border:none;padding:15px 25px;");
  client.println("border-radius:10px;cursor:pointer;font-size:1em;font-weight:500;");
  client.println("transition:all 0.3s;position:relative;overflow:hidden}");
  client.println("button:hover{background:#00b4d8;transform:translateY(-2px);");
  client.println("box-shadow:0 5px 15px rgba(0,180,216,0.4)}");
  client.println("button:active{transform:translateY(0)}");
  client.println("button.danger{background:#e63946}");
  client.println("button.danger:hover{background:#d62828}");
  client.println(".loading{position:fixed;top:20px;right:20px;background:#0077b6;");
  client.println("padding:15px 25px;border-radius:10px;display:none;");
  client.println("box-shadow:0 5px 15px rgba(0,0,0,0.3)}");
  client.println("</style>");
  client.println("</head><body>");
  
  client.println("<div class='container'>");
  client.println("<h1>Sistema de Control de Iluminación</h1>");
  client.println("<p style='text-align:center;color:#888;margin-bottom:30px'>12 Módulos LED WiFi</p>");
  
  // Estado del sistema
  client.println("<div class='status' id='status'>");
  client.println("<div class='stat-box'><div class='stat-value'>-</div><div class='stat-label'>Módulos Online</div></div>");
  client.println("<div class='stat-box'><div class='stat-value'>-</div><div class='stat-label'>Estado</div></div>");
  client.println("<div class='stat-box'><div class='stat-value'>-</div><div class='stat-label'>Uptime</div></div>");
  client.println("<div class='stat-box'><div class='stat-value'>-</div><div class='stat-label'>Comandos</div></div>");
  client.println("</div>");
  
  // Grid de módulos
  client.println("<h2 style='margin-bottom:20px'>Módulos</h2>");
  client.println("<div class='modules' id='modules'></div>");
  
  // Controles
  client.println("<div class='controls'>");
  
  client.println("<div class='control-group'>");
  client.println("<h3>Control Global</h3>");
  client.println("<div class='btn-grid'>");
  client.println("<button onclick='cmd(\"all_on\")'>🔆 Encender Todos</button>");
  client.println("<button class='danger' onclick='cmd(\"all_off\")'>🔅 Apagar Todos</button>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("<div class='control-group'>");
  client.println("<h3>Efectos de Iluminación</h3>");
  client.println("<div class='btn-grid'>");
  client.println("<button onclick='cmd(\"wave\")'>🌊 Onda</button>");
  client.println("<button onclick='cmd(\"chase\")'>🏃 Persecución</button>");
  client.println("<button onclick='cmd(\"blink\")'>⚡ Parpadeo</button>");
  client.println("<button onclick='cmd(\"random\")'>🎲 Aleatorio</button>");
  client.println("<button class='danger' onclick='cmd(\"stop\")'>⏹️ Detener</button>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("<div class='control-group'>");
  client.println("<h3>Control por Grupos</h3>");
  client.println("<div class='btn-grid'>");
  client.println("<button onclick='cmd(\"button_group_1\")'>Grupo 1</button>");
  client.println("<button onclick='cmd(\"button_group_2\")'>Grupo 2</button>");
  client.println("<button onclick='cmd(\"button_group_3\")'>Grupo 3</button>");
  client.println("<button onclick='cmd(\"button_group_4\")'>Grupo 4</button>");
  client.println("<button onclick='cmd(\"button_group_5\")'>Grupo 5</button>");
  client.println("<button onclick='cmd(\"button_group_6\")'>Grupo 6</button>");
  client.println("</div>");
  client.println("</div>");
  
  client.println("</div>");
  client.println("</div>");
  
  client.println("<div class='loading' id='loading'>Enviando comando...</div>");
  
  // JavaScript
  client.println("<script>");
  client.println("function cmd(command){");
  client.println("  document.getElementById('loading').style.display='block';");
  client.println("  fetch('/api/command?cmd='+command).then(()=>{");
  client.println("    document.getElementById('loading').style.display='none';");
  client.println("    updateStatus();");
  client.println("  });");
  client.println("}");
  
  client.println("function updateStatus(){");
  client.println("  fetch('/api/status').then(r=>r.json()).then(data=>{");
  client.println("    const stats=document.getElementById('status').children;");
  client.println("    stats[0].children[0].textContent=data.onlineModules+'/'+data.totalModules;");
  client.println("    stats[1].children[0].textContent=data.currentEffect;");
  client.println("    stats[2].children[0].textContent=Math.floor(data.uptime/60000)+'m';");
  client.println("    stats[3].children[0].textContent=data.commands;");
  
  client.println("    const grid=document.getElementById('modules');");
  client.println("    grid.innerHTML='';");
  client.println("    data.modules.forEach(m=>{");
  client.println("      const div=document.createElement('div');");
  client.println("      div.className='module '+(m.online?'online ':'')+(m.state?'on':'');");
  client.println("      div.innerHTML=`<div class='module-id'>${m.id}</div>");
  client.println("        <div class='module-status'>${m.online?(m.state?'ON':'OFF'):'OFFLINE'}</div>`;");
  client.println("      grid.appendChild(div);");
  client.println("    });");
  client.println("  });");
  client.println("}");
  
  client.println("updateStatus();");
  client.println("setInterval(updateStatus,2000);");
  client.println("</script>");
  
  client.println("</body></html>");
}

void serveStatusJSON(WiFiClient& client) {
  String json = "{";
  json += "\"onlineModules\":" + String(getOnlineCount()) + ",";
  json += "\"totalModules\":" + String(MAX_MODULES) + ",";
  json += "\"currentEffect\":\"" + currentEffect + "\",";
  json += "\"uptime\":" + String(millis() - bootTime) + ",";
  json += "\"commands\":" + String(stats.commandsReceived) + ",";
  json += "\"modules\":[";
  
  for (int i = 0; i < MAX_MODULES; i++) {
    if (i > 0) json += ",";
    json += "{\"id\":" + String(modules[i].id);
    json += ",\"online\":" + String(modules[i].online ? "true" : "false");
    json += ",\"state\":" + String(modules[i].state ? "true" : "false");
    json += "}";
  }
  
  json += "]}";
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

void handleWebCommand(WiFiClient& client, String request) {
  int pos = request.indexOf("cmd=") + 4;
  int end = request.indexOf(" ", pos);
  String cmd = request.substring(pos, end);
  
  processControlCommand(cmd);
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println("{\"status\":\"ok\"}");
}

// =============================================================================
// SERVIDOR API
// =============================================================================

void handleAPIServer() {
  WiFiClient client = apiServer.available();
  if (!client) return;
  
  String request = client.readStringUntil('\r');
  client.flush();
  
  Serial.println("📥 API: " + request.substring(0, 50) + "...");
  
  // Registro de módulo
  if (request.indexOf("/register?id=") != -1) {
    handleModuleRegistration(client, request);
  }
  // Heartbeat
  else if (request.indexOf("/heartbeat?id=") != -1) {
    handleModuleHeartbeat(client, request);
  }
  // Configuración
  else if (request.indexOf("/config") != -1) {
    serveConfiguration(client);
  }
  // 404
  else {
    client.println("HTTP/1.1 404 Not Found\r\n\r\n");
  }
  
  client.stop();
}

void handleModuleRegistration(WiFiClient& client, String request) {
  int pos = request.indexOf("id=") + 3;
  int end = request.indexOf(" ", pos);
  if (end == -1) end = request.indexOf("&", pos);
  if (end == -1) end = request.length();
  
  int moduleId = request.substring(pos, end).toInt();
  
  if (moduleId >= 1 && moduleId <= MAX_MODULES) {
    int index = moduleId - 1;
    modules[index].ip = client.remoteIP();
    modules[index].online = true;
    modules[index].lastSeen = millis();
    
    // Extraer versión si está presente
    int versionPos = request.indexOf("version=");
    if (versionPos != -1) {
      versionPos += 8;
      int versionEnd = request.indexOf("&", versionPos);
      if (versionEnd == -1) versionEnd = request.indexOf(" ", versionPos);
      if (versionEnd == -1) versionEnd = request.length();
      modules[index].version = request.substring(versionPos, versionEnd);
    }
    
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"status\":\"ok\",\"master\":\"" + myIP.toString() + "\"}");
    
    Serial.println("✅ Módulo " + String(moduleId) + " registrado desde " + 
                   client.remoteIP().toString());
    
    // Actualizar estadísticas
    int online = getOnlineCount();
    if (online > stats.peakModules) {
      stats.peakModules = online;
    }
  } else {
    client.println("HTTP/1.1 400 Bad Request\r\n\r\n{\"error\":\"Invalid ID\"}");
  }
}

// =============================================================================
// CORRECCIÓN PARA handleModuleHeartbeat - Reemplazar la función completa
// =============================================================================

void handleModuleHeartbeat(WiFiClient& client, String request) {
  // IMPORTANTE: Capturar la IP del cliente ANTES de procesar
  IPAddress clientIP = client.remoteIP();
  
  int pos = request.indexOf("id=") + 3;
  int end = request.indexOf(" ", pos);
  if (end == -1) end = request.indexOf("&", pos);
  if (end == -1) end = request.length();
  
  int moduleId = request.substring(pos, end).toInt();
  
  if (moduleId >= 1 && moduleId <= MAX_MODULES) {
    int index = moduleId - 1;
    
    // CRÍTICO: Actualizar la IP del módulo
    modules[index].ip = clientIP;  // ← ESTA ES LA LÍNEA QUE FALTA
    modules[index].lastSeen = millis();
    modules[index].online = true;
    
    // Extraer estado si está presente
    int statePos = request.indexOf("state=");
    if (statePos != -1) {
      statePos += 6;
      modules[index].state = (request.charAt(statePos) == '1');
    }
    
    client.println("HTTP/1.1 200 OK\r\n\r\n{\"status\":\"ok\"}");
  } else {
    client.println("HTTP/1.1 400 Bad Request\r\n\r\n{\"error\":\"Invalid ID\"}");
  }
}

void serveConfiguration(WiFiClient& client) {
  String json = "{";
  json += "\"auto_off_delay\":" + String(AUTO_OFF_DELAY) + ",";
  json += "\"version\":\"8.1\",";
  json += "\"max_modules\":" + String(MAX_MODULES) + ",";
  json += "\"udp_port\":" + String(UDP_PORT) + ",";
  json += "\"master_ip\":\"" + myIP.toString() + "\"";
  json += "}";
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(json);
}

// =============================================================================
// MANTENIMIENTO
// =============================================================================

void performMaintenance() {
  checkModuleTimeouts();
  
  // Mostrar estado cada minuto
  static int maintenanceCounter = 0;
  maintenanceCounter++;
  
  if (maintenanceCounter >= 6) { // 60 segundos
    maintenanceCounter = 0;
    printSystemStatus();
  }
}

void checkModuleTimeouts() {
  unsigned long now = millis();
  int timedOut = 0;
  
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online && (now - modules[i].lastSeen > MODULE_TIMEOUT)) {
      modules[i].online = false;
      modules[i].state = false;
      timedOut++;
      Serial.println("⚠️ Módulo " + String(i + 1) + " timeout");
    }
  }
  
  if (timedOut > 0) {
    Serial.println("   Total timeouts: " + String(timedOut));
  }
}

int getOnlineCount() {
  int count = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online) count++;
  }
  return count;
}

// =============================================================================
// COMANDOS SERIE
// =============================================================================

void handleSerialCommand() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();
  
  if (cmd == "help") {
    printHelp();
  }
  else if (cmd == "status") {
    printSystemStatus();
  }
  else if (cmd == "modules") {
    printModulesDetail();
  }
  else if (cmd == "network") {
    printNetworkInfo();
  }
  else if (cmd == "stats") {
    printStatistics();
  }
  else if (cmd == "announce") {
    announcePresence();
  }
  else if (cmd == "reset") {
    Serial.println("Reiniciando...");
    delay(1000);
    // En Arduino UNO R4, usar NVIC_SystemReset
    NVIC_SystemReset();
  }
  else if (cmd.startsWith("test ")) {
    int moduleId = cmd.substring(5).toInt();
    testModule(moduleId);
  }
  else {
    // Intentar procesar como comando de control
    processControlCommand(cmd);
  }
}

void printHelp() {
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║                   COMANDOS                     ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ help      - Mostrar esta ayuda                 ║");
  Serial.println("║ status    - Estado general del sistema         ║");
  Serial.println("║ modules   - Detalle de todos los módulos       ║");
  Serial.println("║ network   - Información de red                 ║");
  Serial.println("║ stats     - Estadísticas del sistema           ║");
  Serial.println("║ announce  - Anunciar presencia en la red       ║");
  Serial.println("║ test X    - Probar módulo X (1-12)            ║");
  Serial.println("║ reset     - Reiniciar sistema                  ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ all_on    - Encender todos los módulos         ║");
  Serial.println("║ all_off   - Apagar todos los módulos           ║");
  Serial.println("║ wave      - Efecto onda                        ║");
  Serial.println("║ chase     - Efecto persecución                 ║");
  Serial.println("║ blink     - Efecto parpadeo                    ║");
  Serial.println("║ random    - Efecto aleatorio                   ║");
  Serial.println("║ stop      - Detener efecto                     ║");
  Serial.println("╚════════════════════════════════════════════════╝");
}

void printSystemStatus() {
  int online = getOnlineCount();
  int on = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].state) on++;
  }
  
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║              ESTADO DEL SISTEMA                ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ Módulos online: " + String(online) + "/" + String(MAX_MODULES) + 
                 " (" + String((online * 100) / MAX_MODULES) + "%)");
  Serial.println("║ Módulos encendidos: " + String(on));
  Serial.println("║ Efecto actual: " + currentEffect);
  Serial.println("║ Comandos recibidos: " + String(stats.commandsReceived));
  Serial.println("║ Máximo módulos simultáneos: " + String(stats.peakModules));
  Serial.println("║ Uptime: " + formatUptime(millis() - bootTime));
  Serial.println("╚════════════════════════════════════════════════╝");
}

void printModulesDetail() {
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║            DETALLE DE MÓDULOS                  ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  
  for (int i = 0; i < MAX_MODULES; i++) {
    Serial.print("║ Módulo " + String(i + 1));
    if (i + 1 < 10) Serial.print(" ");
    Serial.print(": ");
    
    if (modules[i].online) {
      Serial.print("ONLINE  ");
      Serial.print("IP: " + modules[i].ip.toString());
      Serial.print(" Estado: " + String(modules[i].state ? "ON " : "OFF"));
      Serial.print(" v" + modules[i].version);
      Serial.println(" Último: " + String((millis() - modules[i].lastSeen) / 1000) + "s");
    } else {
      Serial.println("OFFLINE");
    }
  }
  
  Serial.println("╚════════════════════════════════════════════════╝");
}

void printNetworkInfo() {
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║           INFORMACIÓN DE RED                   ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ SSID: " + String(ROUTER_SSID));
  Serial.println("║ IP Local: " + myIP.toString());
  Serial.println("║ Gateway: " + WiFi.gatewayIP().toString());
  Serial.println("║ Subnet: " + WiFi.subnetMask().toString());
  Serial.println("║ DNS: " + WiFi.dnsIP().toString());
  
  // MAC address en Arduino UNO R4
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", 
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println("║ MAC: " + String(macStr));
  
  Serial.println("║ RSSI: " + String(WiFi.RSSI()) + " dBm");
  Serial.println("╚════════════════════════════════════════════════╝");
}

void printStatistics() {
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║              ESTADÍSTICAS                      ║");
  Serial.println("╠════════════════════════════════════════════════╣");
  Serial.println("║ Comandos totales: " + String(stats.commandsReceived));
  Serial.println("║ Promedio por minuto: " + 
                 String((stats.commandsReceived * 60000) / (millis() - bootTime)));
  Serial.println("║ Máximo módulos online: " + String(stats.peakModules));
  Serial.println("║ Tiempo activo: " + formatUptime(millis() - bootTime));
  Serial.println("╚════════════════════════════════════════════════╝");
}

void testModule(int moduleId) {
  if (moduleId < 1 || moduleId > MAX_MODULES) {
    Serial.println("❌ ID inválido (1-12)");
    return;
  }
  
  int index = moduleId - 1;
  if (!modules[index].online) {
    Serial.println("❌ Módulo " + String(moduleId) + " no está online");
    return;
  }
  
  Serial.println("🧪 Probando módulo " + String(moduleId) + "...");
  
  // Encender
  udp.beginPacket(modules[index].ip, UDP_PORT);
  udp.print("ON");
  udp.endPacket();
  Serial.println("   → ON enviado");
  delay(1000);
  
  // Apagar
  udp.beginPacket(modules[index].ip, UDP_PORT);
  udp.print("OFF");
  udp.endPacket();
  Serial.println("   → OFF enviado");
  
  Serial.println("✅ Prueba completada");
}

// =============================================================================
// UTILIDADES
// =============================================================================

String formatUptime(unsigned long ms) {
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  if (days > 0) {
    return String(days) + "d " + String(hours % 24) + "h";
  } else if (hours > 0) {
    return String(hours) + "h " + String(minutes % 60) + "m";
  } else {
    return String(minutes) + "m " + String(seconds % 60) + "s";
  }
}