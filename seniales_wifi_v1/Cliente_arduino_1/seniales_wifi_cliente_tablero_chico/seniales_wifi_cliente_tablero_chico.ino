/*
 * Sistema de Control de Focos - Cliente Control Remoto
 * Versión: 4.0 - Con IP ESTÁTICA y inicio no bloqueante
 * 
 * Características:
 * - IP estática para conexión más rápida y estable
 * - Funciona inmediatamente aunque no haya WiFi
 * - Se reconecta automáticamente cuando el router esté listo
 * - LEDs funcionan siempre para feedback visual
 */

#include "WiFiS3.h"
#include <WiFiUdp.h>

// =============================================================================
// CONFIGURACIÓN
// =============================================================================

const char* ssid = "pmn_senviales";
const char* password = "2WC456403581";

// IP ESTÁTICA DEL CONTROL REMOTO
IPAddress local_IP(192, 168, 0, 99);      // IP del control remoto
IPAddress gateway(192, 168, 0, 1);         // Router
IPAddress subnet(255, 255, 255, 0);

// IP del maestro
const char* masterIP = "192.168.0.100";
const int udpPort = 8888;

// Pines
#define BUTTON1_PIN 2
#define BUTTON2_PIN 3
#define BUTTON3_PIN 4
#define BUTTON4_PIN 8    // Nuevo botón 4 Leds: {1};
#define BUTTON5_PIN 9    // Nuevo botón 5 Leds: {2,3};
#define BUTTON6_PIN 10   // Nuevo botón 6 Leds: {4,5};

#define LED1_PIN 5
#define LED2_PIN 6
#define LED3_PIN 7
#define LED4_PIN 11      // Nuevo LED 4
#define LED5_PIN 12      // Nuevo LED 5
#define LED6_PIN 13      // Nuevo LED 6

// Tiempos
#define DEBOUNCE_TIME 50
// #define MIN_PRESS_INTERVAL 3500  // 3.5 segundos entre pulsaciones
#define MIN_PRESS_INTERVAL 0  // 3.5 segundos entre pulsaciones
#define LED_ON_TIME 200         // LED encendido 200ms
#define WIFI_RETRY_INTERVAL 5000 // Reintentar WiFi cada 5 segundos

// Comandos
const char* CMD_GROUP_1 = "button_group_1";
const char* CMD_GROUP_2 = "button_group_2";
const char* CMD_GROUP_3 = "button_group_3";
const char* CMD_GROUP_4 = "button_group_4";
const char* CMD_GROUP_5 = "button_group_5";
const char* CMD_GROUP_6 = "button_group_6";

// =============================================================================
// VARIABLES
// =============================================================================

WiFiUDP udp;
bool wifiConnected = false;
bool udpStarted = false;
unsigned long lastWifiAttempt = 0;
int wifiAttempts = 0;

// Para mostrar estado WiFi
unsigned long wifiStatusBlink = 0;
bool wifiStatusLed = false;
int wifiIndicatorLed = LED6_PIN;  // Usar LED6 como indicador WiFi

struct Button {
  bool lastState = HIGH;
  bool currentState = HIGH;
  unsigned long lastDebounce = 0;
  unsigned long lastPress = 0;
};

Button buttons[6];
unsigned long ledOffTime[6] = {0, 0, 0, 0, 0, 0};

// Cola de comandos pendientes (opcional)
struct PendingCommand {
  int button;
  unsigned long timestamp;
  bool sent;
};
PendingCommand pendingCommands[10];
int pendingCount = 0;

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println(F("\n=== CONTROL REMOTO v4.0 - IP ESTATICA ==="));
  Serial.print(F("IP Control: "));
  Serial.println(local_IP);
  Serial.print(F("IP Maestro: "));
  Serial.println(masterIP);
  
  // Configurar pines
  setupHardware();
  
  // Inicializar comandos pendientes
  for (int i = 0; i < 10; i++) {
    pendingCommands[i].sent = true;
  }
  
  Serial.println(F("Sistema listo (sin WiFi)"));
  Serial.println(F("Esperando router..."));
  
  // Configurar IP estática antes de iniciar WiFi
  WiFi.config(local_IP, gateway, subnet);
  
  // Iniciar conexión WiFi (no bloqueante)
  WiFi.begin(ssid, password);
  lastWifiAttempt = millis();
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {
  // Manejar conexión WiFi (no bloqueante)
  handleWiFiConnection();
  
  // Leer y procesar botones - SIEMPRE funciona
  handleButtons();
  
  // Actualizar LEDs - SIEMPRE funciona
  updateLeds();
  
  // Procesar comandos pendientes si hay WiFi
  if (wifiConnected && pendingCount > 0) {
    processPendingCommands();
  }
  
  // Mostrar estado WiFi en LED6
  updateWiFiIndicator();
  
  // delay(10);
  delay(5);
}

// =============================================================================
// FUNCIONES
// =============================================================================

void setupHardware() {
  // Configurar botones
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  pinMode(BUTTON3_PIN, INPUT_PULLUP);
  pinMode(BUTTON4_PIN, INPUT_PULLUP);
  pinMode(BUTTON5_PIN, INPUT_PULLUP);
  pinMode(BUTTON6_PIN, INPUT_PULLUP);
  
  // Configurar LEDs
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);
  pinMode(LED4_PIN, OUTPUT);
  pinMode(LED5_PIN, OUTPUT);
  pinMode(LED6_PIN, OUTPUT);
  
  // LEDs apagados
  for (int i = 0; i < 6; i++) {
    int ledPin = (i < 3) ? (LED1_PIN + i) : (LED4_PIN + (i - 3));
    digitalWrite(ledPin, LOW);
  }
}

void handleWiFiConnection() {
  // Si ya estamos conectados, verificar que siga así
  if (wifiConnected) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("WiFi perdido! Reconectando..."));
      wifiConnected = false;
      udpStarted = false;
      udp.stop();
    }
    return;
  }
  
  // Si no estamos conectados, intentar cada WIFI_RETRY_INTERVAL
  if (!wifiConnected && millis() - lastWifiAttempt > WIFI_RETRY_INTERVAL) {
    lastWifiAttempt = millis();
    
    if (WiFi.status() == WL_CONNECTED) {
      // ¡Conectado!
      wifiConnected = true;
      Serial.println(F("\n*** WiFi CONECTADO! ***"));
      Serial.print(F("IP asignada: "));
      Serial.println(WiFi.localIP());
      
      // Verificar que tenemos la IP correcta
      if (WiFi.localIP() != local_IP) {
        Serial.println(F("ADVERTENCIA: IP diferente a la configurada!"));
      }
      
      // Iniciar UDP
      udp.begin(udpPort);
      udpStarted = true;
      Serial.println(F("UDP iniciado"));
      
      // Enviar anuncio al maestro
      udp.beginPacket(masterIP, udpPort);
      udp.print("REMOTE_CONNECTED:192.168.0.99");
      udp.endPacket();
      
      // Animación de confirmación
      celebrateConnection();
      
    } else {
      // Aún no conectado
      wifiAttempts++;
      
      // Cada 12 intentos (1 minuto), reintentar WiFi.begin
      if (wifiAttempts % 12 == 0) {
        Serial.print(F("."));
        // Reconfigurar IP estática por si acaso
        WiFi.config(local_IP, gateway, subnet);
        WiFi.begin(ssid, password);
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

void handleButtons() {
  // Leer todos los botones
  bool states[6] = {
    digitalRead(BUTTON1_PIN),
    digitalRead(BUTTON2_PIN),
    digitalRead(BUTTON3_PIN),
    digitalRead(BUTTON4_PIN),
    digitalRead(BUTTON5_PIN),
    digitalRead(BUTTON6_PIN)
  };
  
  // Procesar cada botón
  for (int i = 0; i < 6; i++) {
    // Debounce
    if (states[i] != buttons[i].lastState) {
      buttons[i].lastDebounce = millis();
    }
    
    if ((millis() - buttons[i].lastDebounce) > DEBOUNCE_TIME) {
      if (states[i] != buttons[i].currentState) {
        buttons[i].currentState = states[i];
        
        // Botón presionado (LOW)
        if (buttons[i].currentState == LOW) {
          // Verificar intervalo mínimo
          if (millis() - buttons[i].lastPress >= MIN_PRESS_INTERVAL) {
            processButton(i);
            buttons[i].lastPress = millis();
          } else {
            // Mostrar que debe esperar
            showWaitFeedback(i);
          }
        }
      }
    }
    
    buttons[i].lastState = states[i];
  }
}

void processButton(int button) {
  Serial.print(F("Boton "));
  Serial.print(button + 1);
  
  // LED siempre se enciende (funciona sin WiFi)
  int ledPin = (button < 3) ? (LED1_PIN + button) : (LED4_PIN + (button - 3));
  digitalWrite(ledPin, HIGH);
  ledOffTime[button] = millis() + LED_ON_TIME;
  
  if (wifiConnected) {
    // Enviar comando inmediatamente
    const char* command = getCommand(button);
    
    udp.beginPacket(masterIP, udpPort);
    udp.print(command);
    udp.endPacket();
    
    Serial.print(F(" -> "));
    Serial.println(command);
  } else {
    // Guardar comando pendiente
    if (pendingCount < 10) {
      pendingCommands[pendingCount].button = button;
      pendingCommands[pendingCount].timestamp = millis();
      pendingCommands[pendingCount].sent = false;
      pendingCount++;
      
      Serial.println(F(" (guardado - sin WiFi)"));
    } else {
      Serial.println(F(" (sin WiFi - cola llena)"));
    }
  }
}

const char* getCommand(int button) {
  switch(button) {
    case 0: return CMD_GROUP_1;
    case 1: return CMD_GROUP_2;
    case 2: return CMD_GROUP_3;
    case 3: return CMD_GROUP_4;
    case 4: return CMD_GROUP_5;
    case 5: return CMD_GROUP_6;
    default: return "";
  }
}

void processPendingCommands() {
  int sent = 0;
  
  for (int i = 0; i < 10; i++) {
    if (!pendingCommands[i].sent) {
      const char* command = getCommand(pendingCommands[i].button);
      
      udp.beginPacket(masterIP, udpPort);
      udp.print(command);
      udp.endPacket();
      
      pendingCommands[i].sent = true;
      sent++;
      
      Serial.print(F("Enviando pendiente: "));
      Serial.println(command);
      
      delay(50); // Pequeña pausa entre envíos
    }
  }
  
  if (sent > 0) {
    pendingCount = 0;
    Serial.print(F("Comandos pendientes enviados: "));
    Serial.println(sent);
  }
}

void updateLeds() {
  unsigned long now = millis();
  for (int i = 0; i < 6; i++) {
    if (ledOffTime[i] > 0 && now >= ledOffTime[i]) {
      int ledPin = (i < 3) ? (LED1_PIN + i) : (LED4_PIN + (i - 3));
      digitalWrite(ledPin, LOW);
      ledOffTime[i] = 0;
    }
  }
}

void updateWiFiIndicator() {
  if (!wifiConnected) {
    // Parpadeo lento = esperando WiFi
    if (millis() - wifiStatusBlink > 1000) {
      wifiStatusLed = !wifiStatusLed;
      digitalWrite(wifiIndicatorLed, wifiStatusLed);
      wifiStatusBlink = millis();
    }
  } else {
    // WiFi conectado = LED apagado
    if (digitalRead(wifiIndicatorLed) == HIGH) {
      digitalWrite(wifiIndicatorLed, LOW);
    }
  }
}

void showWaitFeedback(int button) {
  // Parpadeo rápido = debe esperar
  int ledPin = (button < 3) ? (LED1_PIN + button) : (LED4_PIN + (button - 3));
  
  for (int j = 0; j < 3; j++) {
    digitalWrite(ledPin, HIGH);
    delay(50);
    digitalWrite(ledPin, LOW);
    delay(50);
  }
  
  Serial.print(F("Boton "));
  Serial.print(button + 1);
  Serial.println(F(" - esperar 3.5s"));
}

void celebrateConnection() {
  // Animación de barrido cuando se conecta
  for (int cycle = 0; cycle < 2; cycle++) {
    // Barrido hacia adelante
    for (int i = 0; i < 6; i++) {
      int ledPin = (i < 3) ? (LED1_PIN + i) : (LED4_PIN + (i - 3));
      digitalWrite(ledPin, HIGH);
      delay(50);
      digitalWrite(ledPin, LOW);
    }
    // Barrido hacia atrás
    for (int i = 5; i >= 0; i--) {
      int ledPin = (i < 3) ? (LED1_PIN + i) : (LED4_PIN + (i - 3));
      digitalWrite(ledPin, HIGH);
      delay(50);
      digitalWrite(ledPin, LOW);
    }
  }
}