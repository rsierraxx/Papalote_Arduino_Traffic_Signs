/*
 * Sistema de Control de 12 Focos - Arduino UNO R4 WiFi (Maestro)
 * Versión: 7.0 - Simplificado para máxima eficiencia
 * 
 * Cambios en v7.0:
 * - Eliminada la comunicación excesiva de configuración
 * - UDP puro para comandos (sin confirmaciones)
 * - Sin heartbeat obligatorio para controladores
 * - Enfoque en velocidad y confiabilidad
 * - Código más limpio y mantenible
 */

#include "WiFiS3.h"
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN DEL SISTEMA
// =============================================================================

// WiFi
const char* ssid = "LED_CONTROL_SYSTEM";
const char* password = "12345678";
const IPAddress local_IP(192, 168, 4, 1);
const IPAddress gateway(192, 168, 4, 1);
const IPAddress subnet(255, 255, 255, 0);

// Sistema
#define MAX_MODULES 12
#define UDP_PORT 8888
#define AUTO_OFF_DELAY 3000  // 3 segundos
#define HEARTBEAT_INTERVAL 60000
#define MODULE_TIMEOUT 120000

// Botones físicos
#define BUTTON1_PIN 2
#define BUTTON2_PIN 3
#define BUTTON3_PIN 4
#define LED1_PIN 5
#define LED2_PIN 6
#define LED3_PIN 7
#define BUTTON_DEBOUNCE 50
#define LED_AUTO_OFF 3000

// Configuración de módulos por botón
const int BUTTON1_MODULES[] = {12};
const int BUTTON2_MODULES[] = {2, 0};
const int BUTTON3_MODULES[] = {3, 4, 5, 0};

// =============================================================================
// VARIABLES GLOBALES
// =============================================================================

WiFiServer server(80);
WiFiServer apiServer(8080);
WiFiUDP udp;

// Información de módulos
struct Module {
  uint8_t id;
  IPAddress ip;
  bool online;
  bool state;
  unsigned long lastSeen;
} modules[MAX_MODULES];

// Estado de botones
struct Button {
  bool lastState;
  bool currentState;
  unsigned long lastDebounce;
  unsigned long lastPress;
} buttons[3];

// Estado de LEDs
struct Led {
  bool on;
  unsigned long turnOnTime;
} leds[3];

// Variables del sistema
bool effectRunning = false;
String currentEffect = "none";
int effectStep = 0;
unsigned long lastEffectUpdate = 0;
unsigned long lastHeartbeatCheck = 0;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== SISTEMA DE CONTROL v7.0 ===");
  Serial.println("Modo: SIMPLIFICADO Y EFICIENTE");
  
  // Configurar hardware
  setupHardware();
  
  // Inicializar módulos
  for (int i = 0; i < MAX_MODULES; i++) {
    modules[i].id = i + 1;
    modules[i].ip = IPAddress(0, 0, 0, 0);
    modules[i].online = false;
    modules[i].state = false;
    modules[i].lastSeen = 0;
  }
  
  // Crear punto de acceso
  WiFi.config(local_IP, gateway, subnet);
  if (WiFi.beginAP(ssid, password) != WL_AP_LISTENING) {
    Serial.println("ERROR: No se pudo crear AP");
    while(1) delay(1000);
  }
  
  Serial.println("AP creado: " + String(ssid));
  Serial.println("IP: " + WiFi.localIP().toString());
  
  // Iniciar servicios
  server.begin();
  apiServer.begin();
  udp.begin(UDP_PORT);
  
  Serial.println("\n=== SISTEMA LISTO ===\n");
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Prioridad 1: Comandos UDP
  handleUDP();
  
  // Prioridad 2: Botones físicos
  handleButtons();
  
  // Prioridad 3: LEDs
  updateLeds();
  
  // Prioridad 4: Web y API
  handleWeb();
  handleAPI();
  
  // Prioridad 5: Mantenimiento
  if (millis() - lastHeartbeatCheck > 10000) {
    checkModules();
    lastHeartbeatCheck = millis();
  }
  
  // Prioridad 6: Efectos
  if (effectRunning) {
    runEffect();
  }
  
  // Comandos serie
  if (Serial.available()) {
    handleSerial();
  }
}

// =============================================================================
// CONFIGURACIÓN DE HARDWARE
// =============================================================================

void setupHardware() {
  // Botones con pull-up
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  
  // LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  // Estado inicial
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
  digitalWrite(LED3_PIN, LOW);
  
  // Leer estados iniciales de botones
  for (int i = 0; i < 3; i++) {
    buttons[i].lastState = HIGH;
    buttons[i].currentState = HIGH;
    buttons[i].lastDebounce = 0;
    buttons[i].lastPress = 0;
  }
}

// =============================================================================
// MANEJO UDP (MÁXIMA PRIORIDAD)
// =============================================================================

void handleUDP() {
  int packetSize = udp.parsePacket();
  if (!packetSize) return;
  
  char buffer[64];
  int len = udp.read(buffer, 63);
  if (len > 0) buffer[len] = 0;
  
  String cmd = String(buffer);
  IPAddress remoteIP = udp.remoteIP();
  
  // Comandos de módulos ESP8266 (ON/OFF)
  if (cmd == "ON" || cmd == "OFF") {
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].ip == remoteIP) {
        modules[i].state = (cmd == "ON");
        return;
      }
    }
  }
  
  // Comandos del controlador remoto
  cmd.toLowerCase();
  
  if (cmd == "button_group_1") {
    activateGroup(BUTTON1_MODULES);
  }
  else if (cmd == "button_group_2") {
    activateGroup(BUTTON2_MODULES);
  }
  else if (cmd == "button_group_3") {
    activateGroup(BUTTON3_MODULES);
  }
  else if (cmd == "all_on") {
    allModules(true);
  }
  else if (cmd == "all_off") {
    allModules(false);
    stopEffect();
  }
  else if (cmd == "wave" || cmd == "chase" || cmd == "blink" || cmd == "random") {
    startEffect(cmd);
  }
  else if (cmd == "stop") {
    stopEffect();
  }
}

// =============================================================================
// MANEJO DE BOTONES
// =============================================================================

void handleButtons() {
  // Leer estado de cada botón
  bool states[3];
  states[0] = digitalRead(BUTTON1_PIN);
  states[1] = digitalRead(BUTTON2_PIN);
  states[2] = digitalRead(BUTTON3_PIN);
  
  // Procesar cada botón
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
          // Verificar tiempo mínimo entre pulsaciones
          if (millis() - buttons[i].lastPress >= AUTO_OFF_DELAY + 500) {
            processButton(i);
            buttons[i].lastPress = millis();
          }
        }
      }
    }
    
    buttons[i].lastState = states[i];
  }
}

void processButton(int buttonIndex) {
  // Encender LED
  digitalWrite(LED1_PIN + buttonIndex, HIGH);
  leds[buttonIndex].on = true;
  leds[buttonIndex].turnOnTime = millis();
  
  // Activar módulos correspondientes
  const int* moduleList = nullptr;
  
  switch(buttonIndex) {
    case 0: moduleList = BUTTON1_MODULES; break;
    case 1: moduleList = BUTTON2_MODULES; break;
    case 2: moduleList = BUTTON3_MODULES; break;
  }
  
  if (moduleList) {
    Serial.print("Botón " + String(buttonIndex + 1) + " → Módulos: ");
    activateGroup(moduleList);
  }
}

// =============================================================================
// CONTROL DE MÓDULOS
// =============================================================================

void activateGroup(const int* moduleList) {
  int i = 0;
  while (moduleList[i] != 0) {
    sendToModule(moduleList[i], true);
    Serial.print(String(moduleList[i]) + " ");
    i++;
  }
  Serial.println("");
}

void sendToModule(int moduleId, bool state) {
  if (moduleId < 1 || moduleId > MAX_MODULES) return;
  
  int index = moduleId - 1;
  if (!modules[index].online) return;
  
  // Solo enviar ON (el módulo se apaga solo)
  if (state) {
    udp.beginPacket(modules[index].ip, UDP_PORT);
    udp.print("ON");
    udp.endPacket();
    modules[index].state = true;
  }
}

void allModules(bool state) {
  Serial.println(state ? "Todos ON" : "Todos OFF");
  
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online) {
      udp.beginPacket(modules[i].ip, UDP_PORT);
      udp.print(state ? "ON" : "OFF");
      udp.endPacket();
      modules[i].state = state;
      delay(20);
    }
  }
}

// =============================================================================
// MANEJO DE LEDS
// =============================================================================

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
// EFECTOS
// =============================================================================

void startEffect(String effect) {
  currentEffect = effect;
  effectRunning = true;
  effectStep = 0;
  lastEffectUpdate = millis();
  Serial.println("Efecto: " + effect);
}

void stopEffect() {
  effectRunning = false;
  currentEffect = "none";
  allModules(false);
}

void runEffect() {
  if (millis() - lastEffectUpdate < 500) return;
  lastEffectUpdate = millis();
  
  if (currentEffect == "wave") {
    // Apagar todos
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].online && modules[i].state) {
        sendToModule(i + 1, false);
      }
    }
    // Encender actual
    if (modules[effectStep].online) {
      sendToModule(effectStep + 1, true);
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
        sendToModule(i + 1, shouldBeOn);
      }
    }
    effectStep = (effectStep + 1) % MAX_MODULES;
  }
  else if (currentEffect == "blink") {
    bool state = (effectStep % 2 == 0);
    allModules(state);
    effectStep++;
  }
  else if (currentEffect == "random") {
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].online && random(0, 2)) {
        sendToModule(i + 1, !modules[i].state);
      }
    }
  }
}

// =============================================================================
// SERVIDOR WEB
// =============================================================================

void handleWeb() {
  WiFiClient client = server.available();
  if (!client) return;
  
  String request = client.readStringUntil('\r');
  client.flush();
  
  // API simple
  if (request.indexOf("/toggle?id=") != -1) {
    int pos = request.indexOf("id=") + 3;
    int id = request.substring(pos, pos + 2).toInt();
    if (id >= 1 && id <= MAX_MODULES) {
      modules[id-1].state = !modules[id-1].state;
      sendToModule(id, modules[id-1].state);
    }
    client.println("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK");
    client.stop();
    return;
  }
  
  // Página web simple
  String html = "<!DOCTYPE html><html><head><title>Control Focos</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;text-align:center;background:#222;color:#fff}";
  html += ".module{display:inline-block;width:60px;height:60px;margin:5px;";
  html += "border:2px solid #555;border-radius:10px;cursor:pointer;line-height:60px}";
  html += ".online{background:#2a2}";
  html += ".offline{background:#a22}";
  html += ".on{box-shadow:0 0 20px #4f4}";
  html += "button{background:#25a;color:#fff;border:none;padding:10px 20px;";
  html += "margin:5px;border-radius:5px;cursor:pointer;font-size:16px}";
  html += "</style></head><body>";
  html += "<h1>Control de Focos</h1>";
  html += "<div id='modules'></div>";
  html += "<div style='margin:20px'>";
  html += "<button onclick='cmd(\"all_on\")'>Todos ON</button>";
  html += "<button onclick='cmd(\"all_off\")'>Todos OFF</button><br>";
  html += "<button onclick='cmd(\"wave\")'>Wave</button>";
  html += "<button onclick='cmd(\"chase\")'>Chase</button>";
  html += "<button onclick='cmd(\"blink\")'>Blink</button>";
  html += "<button onclick='cmd(\"random\")'>Random</button>";
  html += "<button onclick='cmd(\"stop\")'>Stop</button>";
  html += "</div>";
  html += "<script>";
  html += "function toggle(id){fetch('/toggle?id='+id).then(()=>update())}";
  html += "function cmd(c){fetch('/cmd?c='+c).then(()=>update())}";
  html += "function update(){";
  html += "fetch('/status').then(r=>r.json()).then(d=>{";
  html += "let h='';";
  html += "for(let i=0;i<12;i++){";
  html += "let m=d.modules[i];";
  html += "let cls='module '+(m.online?'online ':'offline ')+(m.state?'on':'');";
  html += "h+=`<div class='${cls}' onclick='toggle(${i+1})'>${i+1}</div>`;";
  html += "}";
  html += "document.getElementById('modules').innerHTML=h;";
  html += "})}";
  html += "update();setInterval(update,2000);";
  html += "</script></body></html>";
  
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println(html);
  client.stop();
}

// =============================================================================
// API MÍNIMA
// =============================================================================

void handleAPI() {
  WiFiClient client = apiServer.available();
  if (!client) return;
  
  String request = client.readStringUntil('\r');
  client.flush();
  
  // Registro de módulo
  if (request.indexOf("/register?id=") != -1) {
    int pos = request.indexOf("id=") + 3;
    int id = request.substring(pos, pos + 2).toInt();
    
    if (id >= 1 && id <= MAX_MODULES) {
      modules[id-1].ip = client.remoteIP();
      modules[id-1].online = true;
      modules[id-1].lastSeen = millis();
      
      client.println("HTTP/1.1 200 OK\r\n\r\n{\"status\":\"ok\"}");
      Serial.println("Módulo " + String(id) + " registrado");
    }
  }
  // Heartbeat
  else if (request.indexOf("/heartbeat?id=") != -1) {
    int pos = request.indexOf("id=") + 3;
    int id = request.substring(pos, pos + 2).toInt();
    
    if (id >= 1 && id <= MAX_MODULES) {
      modules[id-1].lastSeen = millis();
      client.println("HTTP/1.1 200 OK\r\n\r\n{\"status\":\"ok\"}");
    }
  }
  // Estado para web
  else if (request.indexOf("/status") != -1) {
    String json = "{\"modules\":[";
    for (int i = 0; i < MAX_MODULES; i++) {
      if (i > 0) json += ",";
      json += "{\"id\":" + String(i+1);
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
  // Comando web
  else if (request.indexOf("/cmd?c=") != -1) {
    int pos = request.indexOf("c=") + 2;
    int end = request.indexOf(" ", pos);
    String cmd = request.substring(pos, end);
    
    // Simular comando UDP
    if (cmd == "all_on") allModules(true);
    else if (cmd == "all_off") { allModules(false); stopEffect(); }
    else if (cmd == "wave" || cmd == "chase" || cmd == "blink" || cmd == "random") startEffect(cmd);
    else if (cmd == "stop") stopEffect();
    
    client.println("HTTP/1.1 200 OK\r\n\r\nOK");
  }
  
  client.stop();
}

// =============================================================================
// MANTENIMIENTO
// =============================================================================

void checkModules() {
  unsigned long now = millis();
  int online = 0;
  
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online && (now - modules[i].lastSeen > MODULE_TIMEOUT)) {
      modules[i].online = false;
      modules[i].state = false;
      Serial.println("Módulo " + String(i+1) + " offline");
    }
    if (modules[i].online) online++;
  }
}

// =============================================================================
// COMANDOS SERIE
// =============================================================================

void handleSerial() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  
  if (cmd == "status") {
    Serial.println("\n=== ESTADO ===");
    int online = 0;
    for (int i = 0; i < MAX_MODULES; i++) {
      if (modules[i].online) {
        online++;
        Serial.print("M" + String(i+1) + ":" + (modules[i].state ? "ON " : "OFF "));
      }
    }
    Serial.println("\nOnline: " + String(online) + "/" + String(MAX_MODULES));
    Serial.println("Efecto: " + currentEffect);
  }
  else if (cmd == "help") {
    Serial.println("\n=== COMANDOS ===");
    Serial.println("status - Ver estado");
    Serial.println("all_on/all_off - Control global");
    Serial.println("wave/chase/blink/random - Efectos");
    Serial.println("stop - Detener efecto");
  }
  else {
    // Reenviar como comando UDP local
    handleUDP(); // Procesar como si fuera UDP
  }
}