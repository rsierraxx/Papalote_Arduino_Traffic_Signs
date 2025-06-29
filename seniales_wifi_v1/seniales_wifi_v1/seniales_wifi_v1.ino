/*
 * Sistema de Control - Arduino Maestro v9.0
 * Para usar con ESP8266 con IPs ESTÁTICAS
 * 
 * VERSIÓN CON INICIO NO BLOQUEANTE
 * - El sistema funciona aunque no haya WiFi
 * - Se reconecta automáticamente cuando el router esté listo
 * - Los botones y LEDs funcionan siempre
 */

#include "WiFiS3.h"
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN
// =============================================================================

const char* ROUTER_SSID = "pmn_senviales";
const char* ROUTER_PASS = "2WC456403581";

// IP ESTÁTICA DEL MAESTRO - IMPORTANTE!
IPAddress local_IP(192, 168, 0, 100);     // IP del maestro
IPAddress gateway(192, 168, 0, 1);         // Router
IPAddress subnet(255, 255, 255, 0);

// IPs PREDEFINIDAS DE LOS MÓDULOS
const IPAddress MODULE_IPS[12] = {
  IPAddress(192, 168, 0, 101),  // Módulo 1
  IPAddress(192, 168, 0, 102),  // Módulo 2
  IPAddress(192, 168, 0, 103),  // Módulo 3
  IPAddress(192, 168, 0, 104),  // Módulo 4
  IPAddress(192, 168, 0, 105),  // Módulo 5
  IPAddress(192, 168, 0, 106),  // Módulo 6
  IPAddress(192, 168, 0, 107),  // Módulo 7
  IPAddress(192, 168, 0, 108),  // Módulo 8
  IPAddress(192, 168, 0, 109),  // Módulo 9
  IPAddress(192, 168, 0, 110),  // Módulo 10
  IPAddress(192, 168, 0, 111),  // Módulo 11
  IPAddress(192, 168, 0, 112)   // Módulo 12
};

#define MAX_MODULES 12
#define UDP_PORT 8888
#define HEARTBEAT_TIMEOUT 60000  // 1 minuto

// Pines
#define BUTTON1_PIN 2
#define BUTTON2_PIN 3
#define BUTTON3_PIN 4
#define LED1_PIN 5
#define LED2_PIN 6
#define LED3_PIN 7

// Grupos
// const byte BUTTON1_MODULES[] = {1, 2, 3, 4, 5, 6 ,7, 0}; // Normal Boton 1
// const byte BUTTON2_MODULES[] = {8, 9, 10, 11, 12, 0}; // Normal Boton 2

const byte BUTTON1_MODULES[] = {1, 2, 3, 0}; // Normal Boton 1
const byte BUTTON2_MODULES[] = {4, 5, 0}; // Normal Boton 2
const byte BUTTON3_MODULES[] = {6, 7, 0}; // Normal Boton 3
const byte BUTTON4_MODULES[] = {10, 0}; // Mini Boton 1
const byte BUTTON5_MODULES[] = {9, 11, 0}; // Mini Boton 2
const byte BUTTON6_MODULES[] = {8, 12, 0}; // Mini Boton 3

// =============================================================================
// VARIABLES
// =============================================================================

WiFiUDP udp;
unsigned long bootTime = 0;

// Estados del sistema
bool wifiConnected = false;
bool udpStarted = false;
bool waitingForWifi = true;
unsigned long wifiCheckTime = 0;
unsigned long lastWifiAttempt = 0;
int wifiAttempts = 0;

struct Module {
  bool online;
  bool state;
  unsigned long lastSeen;
} modules[MAX_MODULES];

struct Button {
  bool lastState;
  unsigned long lastPress;
} buttons[3];

unsigned long ledOffTime[3] = {0, 0, 0};
unsigned long lastMaintenance = 0;

// Para mostrar estado WiFi en LEDs
unsigned long wifiLedBlink = 0;
bool wifiLedState = false;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  bootTime = millis();
  Serial.println(F("\n=== MAESTRO v9.0 - INICIO NO BLOQUEANTE ===\n"));
  
  // Hardware - SIEMPRE funciona
  setupHardware();
  
  // Inicializar módulos
  for (int i = 0; i < MAX_MODULES; i++) {
    modules[i].online = false;
    modules[i].state = false;
    modules[i].lastSeen = 0;
  }
  
  // Indicar que estamos esperando WiFi
  Serial.println(F("Sistema listo (sin WiFi)"));
  Serial.println(F("Esperando router..."));
  
  // Configurar WiFi pero NO bloquear
  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ROUTER_SSID, ROUTER_PASS);
  lastWifiAttempt = millis();
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Verificar/Reconectar WiFi (NO BLOQUEANTE)
  handleWiFiConnection();
  
  // Botones - SIEMPRE funcionan
  handleButtons();
  
  // LEDs - SIEMPRE funcionan
  updateLeds();
  
  // Si hay WiFi, manejar UDP
  if (wifiConnected && udpStarted) {
    handleUDP();
    
    // Mantenimiento
    if (millis() - lastMaintenance > 10000) {
      performMaintenance();
      lastMaintenance = millis();
    }
  }
  
  // Mostrar estado WiFi en LED3 (parpadeo si no hay WiFi)
  if (!wifiConnected && millis() - wifiLedBlink > 500) {
    wifiLedState = !wifiLedState;
    digitalWrite(LED3_PIN, wifiLedState);
    wifiLedBlink = millis();
  }
  
  // Comandos serie - SIEMPRE funcionan
  if (Serial.available()) {
    handleSerialCommand();
  }
  
  delay(10);
}

// =============================================================================
// FUNCIONES
// =============================================================================

void setupHardware() {
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED1_PIN + i, LOW);
    buttons[i].lastState = HIGH;
    buttons[i].lastPress = 0;
  }
}

void handleWiFiConnection() {
  // Si ya estamos conectados, verificar que siga así
  if (wifiConnected) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi perdido! Reconectando..."));
      wifiConnected = false;
      udpStarted = false;
      waitingForWifi = true;
      udp.stop();
      // Marcar todos los módulos como offline
      for (int i = 0; i < MAX_MODULES; i++) {
        modules[i].online = false;
      }
    }
    return;
  }
  
  // Si no estamos conectados, intentar conectar cada 5 segundos
  if (!wifiConnected && millis() - lastWifiAttempt > 5000) {
    lastWifiAttempt = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      // ¡Conectado!
      wifiConnected = true;
      waitingForWifi = false;
      Serial.println(F("\n*** WiFi CONECTADO! ***"));
      Serial.print(F("IP: "));
      Serial.println(WiFi.localIP());
      
      // Iniciar UDP
      udp.begin(UDP_PORT);
      udpStarted = true;
      Serial.println(F("UDP iniciado"));
      
      // Apagar LED3 (indicador WiFi)
      digitalWrite(LED3_PIN, LOW);
      
      // Ping inicial a todos
      delay(500);
      pingAllModules();
      
    } else {
      // Aún no conectado
      wifiAttempts++;
      
      // Cada 12 intentos (1 minuto), reintentar begin
      if (wifiAttempts % 12 == 0) {
        Serial.print(F("."));
        WiFi.begin(ROUTER_SSID, ROUTER_PASS);
      }
      
      // Cada 60 intentos (5 minutos), mostrar mensaje
      if (wifiAttempts % 60 == 0) {
        Serial.print(F("\nEsperando router ("));
        Serial.print(wifiAttempts * 5 / 60);
        Serial.println(F(" min)"));
      }
    }
  }
}

void handleUDP() {
  if (!udp.parsePacket()) return;
  
  char buffer[32];
  int len = udp.read(buffer, 31);
  buffer[len] = 0;
  
  IPAddress senderIP = udp.remoteIP();
  
  // Procesar heartbeat: HB:ID:STATE
  if (strncmp(buffer, "HB:", 3) == 0) {
    int id = atoi(buffer + 3);
    if (id >= 1 && id <= MAX_MODULES) {
      modules[id-1].online = true;
      modules[id-1].lastSeen = millis();
      
      char* statePtr = strchr(buffer + 3, ':');
      if (statePtr) {
        modules[id-1].state = (statePtr[1] == '1');
      }
    }
  }
  // HELLO:ID:IP
  else if (strncmp(buffer, "HELLO:", 6) == 0) {
    int id = atoi(buffer + 6);
    if (id >= 1 && id <= MAX_MODULES) {
      modules[id-1].online = true;
      modules[id-1].lastSeen = millis();
      Serial.print(F("Modulo "));
      Serial.print(id);
      Serial.println(F(" conectado"));
    }
  }
  // PONG:ID
  else if (strncmp(buffer, "PONG:", 5) == 0) {
    int id = atoi(buffer + 5);
    if (id >= 1 && id <= MAX_MODULES) {
      modules[id-1].online = true;
      modules[id-1].lastSeen = millis();
    }
  }
  // Comandos
  else if (strcmp(buffer, "button_group_1") == 0) activateGroup(BUTTON1_MODULES);
  else if (strcmp(buffer, "button_group_2") == 0) activateGroup(BUTTON2_MODULES);
  else if (strcmp(buffer, "button_group_3") == 0) activateGroup(BUTTON3_MODULES);
  else if (strcmp(buffer, "button_group_4") == 0) activateGroup(BUTTON4_MODULES);
  else if (strcmp(buffer, "button_group_5") == 0) activateGroup(BUTTON5_MODULES);
  else if (strcmp(buffer, "button_group_6") == 0) activateGroup(BUTTON6_MODULES);
  else if (strcmp(buffer, "all_on") == 0) allModulesOn();
  else if (strcmp(buffer, "all_off") == 0) allModulesOff();
}

void activateGroup(const byte* moduleList) {
  if (!wifiConnected) {
    Serial.println(F("Sin WiFi - comando guardado"));
    // Aquí podrías guardar el comando para ejecutarlo cuando haya WiFi
    return;
  }
  
  int i = 0;
  int count = 0;
  
  while (moduleList[i] != 0) {
    int id = moduleList[i] - 1;
    if (id >= 0 && id < MAX_MODULES && modules[id].online) {
      udp.beginPacket(MODULE_IPS[id], UDP_PORT);
      udp.print(F("ON"));
      udp.endPacket();
      modules[id].state = true;
      count++;
      delay(20);
    }
    i++;
  }
  
  Serial.print(F("Grupo: "));
  Serial.print(count);
  Serial.println(F(" on"));
}

void allModulesOn() {
  if (!wifiConnected) {
    Serial.println(F("Sin WiFi"));
    return;
  }
  
  int count = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online) {
      udp.beginPacket(MODULE_IPS[i], UDP_PORT);
      udp.print(F("ON"));
      udp.endPacket();
      modules[i].state = true;
      count++;
      delay(20);
    }
  }
  Serial.print(F("Todos on: "));
  Serial.println(count);
}

void allModulesOff() {
  if (!wifiConnected) {
    Serial.println(F("Sin WiFi"));
    return;
  }
  
  int count = 0;
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online) {
      udp.beginPacket(MODULE_IPS[i], UDP_PORT);
      udp.print(F("OFF"));
      udp.endPacket();
      modules[i].state = false;
      count++;
      delay(20);
    }
  }
  Serial.print(F("Todos off: "));
  Serial.println(count);
}

void pingAllModules() {
  Serial.println(F("Ping todos..."));
  for (int i = 0; i < MAX_MODULES; i++) {
    udp.beginPacket(MODULE_IPS[i], UDP_PORT);
    udp.print(F("PING"));
    udp.endPacket();
    delay(20);
  }
}

void handleButtons() {
  for (int i = 0; i < 3; i++) {
    bool state = digitalRead(BUTTON1_PIN + i);
    
    if (state == LOW && buttons[i].lastState == HIGH) {
      if (millis() - buttons[i].lastPress > 3500) {
        // LED siempre se enciende (funciona sin WiFi)
        digitalWrite(LED1_PIN + i, HIGH);
        ledOffTime[i] = millis() + 1000;
        
        // Si hay WiFi, enviar comando
        if (wifiConnected) {
          const byte* group = nullptr;
          switch(i) {
            case 0: group = BUTTON1_MODULES; break;
            case 1: group = BUTTON2_MODULES; break;
            case 2: group = BUTTON3_MODULES; break;
          }
          
          if (group) activateGroup(group);
        } else {
          Serial.print(F("Boton "));
          Serial.print(i + 1);
          Serial.println(F(" (sin WiFi)"));
        }
        
        buttons[i].lastPress = millis();
      }
    }
    
    buttons[i].lastState = state;
  }
}

void updateLeds() {
  unsigned long now = millis();
  for (int i = 0; i < 3; i++) {
    if (ledOffTime[i] > 0 && now >= ledOffTime[i]) {
      digitalWrite(LED1_PIN + i, LOW);
      ledOffTime[i] = 0;
    }
  }
}

void performMaintenance() {
  unsigned long now = millis();
  int online = 0;
  
  for (int i = 0; i < MAX_MODULES; i++) {
    if (modules[i].online) {
      if (now - modules[i].lastSeen > HEARTBEAT_TIMEOUT) {
        modules[i].online = false;
        Serial.print(F("Timeout M"));
        Serial.println(i + 1);
      } else {
        online++;
      }
    }
  }
  
  static byte counter = 0;
  if (++counter >= 6) {
    counter = 0;
    Serial.print(F("["));
    Serial.print(online);
    Serial.print(F("/"));
    Serial.print(MAX_MODULES);
    Serial.println(F("]"));
  }
}

void handleSerialCommand() {
  char cmd = Serial.read();
  
  switch(cmd) {
    case '1': activateGroup(BUTTON1_MODULES); break;
    case '2': activateGroup(BUTTON2_MODULES); break;
    case '3': activateGroup(BUTTON3_MODULES); break;
    case '4': activateGroup(BUTTON4_MODULES); break;
    case '5': activateGroup(BUTTON5_MODULES); break;
    case '6': activateGroup(BUTTON6_MODULES); break;
    case 'a': allModulesOn(); break;
    case 'o': allModulesOff(); break;
    case 'p': 
      if (wifiConnected) pingAllModules();
      else Serial.println(F("Sin WiFi"));
      break;
    case 's': printStatus(); break;
    case 'h': printHelp(); break;
    case 'w': 
      Serial.print(F("WiFi: "));
      Serial.println(wifiConnected ? F("OK") : F("NO"));
      break;
  }
}

void printStatus() {
  Serial.println(F("\n--- ESTADO ---"));
  Serial.print(F("WiFi: "));
  Serial.println(wifiConnected ? F("CONECTADO") : F("DESCONECTADO"));
  
  if (wifiConnected) {
    for (int i = 0; i < MAX_MODULES; i++) {
      Serial.print(F("M"));
      Serial.print(i + 1);
      Serial.print(F(" ("));
      Serial.print(MODULE_IPS[i]);
      Serial.print(F("): "));
      
      if (modules[i].online) {
        Serial.print(modules[i].state ? F("ON") : F("OFF"));
        Serial.print(F(" ["));
        Serial.print((millis() - modules[i].lastSeen) / 1000);
        Serial.print(F("s]"));
      } else {
        Serial.print(F("OFFLINE"));
      }
      Serial.println();
    }
  } else {
    Serial.println(F("(Esperando conexion)"));
  }
}

void printHelp() {
  Serial.println(F("\n--- AYUDA ---"));
  Serial.println(F("1-6: Grupos"));
  Serial.println(F("a: Todos ON"));
  Serial.println(F("o: Todos OFF"));
  Serial.println(F("p: Ping todos"));
  Serial.println(F("s: Estado"));
  Serial.println(F("w: Estado WiFi"));
  Serial.println(F("h: Ayuda"));
}