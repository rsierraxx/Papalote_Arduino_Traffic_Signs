/*
  Control de Foco 50W DC con Arduino Uno R4 WiFi + Módulo Relé 4 Canales
  
  Características:
  - Control local con botón (Pin D2)
  - Control remoto vía WiFi
  - Visualización en matriz LED 12x8
  - Módulo relé 4 canales (solo canal 1 en uso)
  - Interfaz web para control
  
  Hardware:
  - Arduino Uno R4 WiFi
  - Módulo relé 4 canales (5V, activo LOW)
  - Botón con resistencia pull-down 10kΩ
  - Foco 50W DC (12V/24V)
  
  Conexiones:
  - Pin D2: Botón (con pull-down)
  - Pin D7: Canal 1 del módulo relé (Foco)
  - Pin D8: Canal 2 (disponible)
  - Pin D9: Canal 3 (disponible)
  - Pin D10: Canal 4 (disponible)
*/

#include <WiFiS3.h>
#include <ArduinoGraphics.h>
#include <Arduino_LED_Matrix.h>

// Configuración WiFi
const char* ssid = "Darth_Router";           // Cambia por tu red WiFi
const char* password = "2WC456403581";   // Cambia por tu contraseña

// Configuración de pines
const int BOTON_PIN = 2;        // Botón físico
const int RELE_CANAL1 = 7;      // Canal 1 - Foco 1
const int LED_CANAL1 = 3;      // Canal 1 - Led 1
const int RELE_CANAL2 = 8;      // Canal 2 - Disponible
const int RELE_CANAL3 = 9;      // Canal 3 - Disponible  
const int RELE_CANAL4 = 10;     // Canal 4 - Disponible

// Estados del sistema
bool estadoFoco = false;        // Estado actual del foco
bool ultimoEstadoBoton = false; // Para detectar flancos del botón
bool btnPress = false; // Para detectar flancos del botón
unsigned long ultimoDebounce = 0;
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long FOCO_DELAY = 2000;

// Servidor web
WiFiServer server(80);

// Matriz LED
ArduinoLEDMatrix matrix;

// Iconos para la matriz LED (8x12) - Formato correcto para Arduino_LED_Matrix
// const uint32_t iconoFocoON[] = {
//   0x3C42A5A5,
//   0xA542423C,
//   0x18181800
// };
const uint32_t iconoFocoON[] = {
		0x7228a48a,
		0x88b08a88,
		0xa48a2722
};

// byte iconoFocoON[3] = {
//   // O      K
//   {0,1,1,1,0,0,1,0,0,0,1,0},  // Fila 0
//   {1,0,0,0,1,0,1,0,0,1,0,0},  // Fila 1
//   {1,0,0,0,1,0,1,0,1,0,0,0},  // Fila 2
//   {1,0,0,0,1,0,1,1,0,0,0,0},  // Fila 3
//   {1,0,0,0,1,0,1,1,0,0,0,0},  // Fila 4
//   {1,0,0,0,1,0,1,0,1,0,0,0},  // Fila 5
//   {1,0,0,0,1,0,1,0,0,1,0,0},  // Fila 6
//   {0,1,1,1,0,0,1,0,0,0,1,0}   // Fila 7
// };


const uint32_t iconoFocoOFF[] = {
  0x3C424242,
  0x42423C18,
  0x18180000  
};

const uint32_t iconoWiFi[] = {
    0x19819,
    0x80000001,
    0x81f8000
};

// Animaciones para la matriz
const uint32_t animacionConectando[][3] = {
  {0x08000800, 0x08000800, 0x08000000},
  {0x18001800, 0x18001800, 0x18000000},
  {0x3C003C00, 0x3C003C00, 0x3C000000}
};

void setup() {
  Serial.begin(115200);
  Serial.println("=================================");
  Serial.println("Control de Foco WiFi - Arduino R4");
  Serial.println("=================================");
  
  // Configurar pines
  pinMode(BOTON_PIN, INPUT);      // Botón con pull-down externa
  pinMode(RELE_CANAL1, OUTPUT);   // Canal 1 - Foco 1
  pinMode(LED_CANAL1, OUTPUT);   // Canal 1 - Led 1
  pinMode(RELE_CANAL2, OUTPUT);   // Canal 2 - Disponible
  pinMode(RELE_CANAL3, OUTPUT);   // Canal 3 - Disponible
  pinMode(RELE_CANAL4, OUTPUT);   // Canal 4 - Disponible
  
  // Inicializar relés (módulo activo LOW)
  digitalWrite(RELE_CANAL1, HIGH); // OFF inicial
  digitalWrite(LED_CANAL1, LOW); // OFF inicial
  digitalWrite(RELE_CANAL2, HIGH); // OFF
  digitalWrite(RELE_CANAL3, HIGH); // OFF
  digitalWrite(RELE_CANAL4, HIGH); // OFF
  Serial.println("✓ Relés inicializados (todos OFF)");
  
  // Inicializar matriz LED
  // matrix.begin();
  // mostrarIconoWiFi();

  // matrix.begin();

  // matrix.beginDraw();
  // matrix.stroke(0xFFFFFFFF);
  // // add some static text
  // // will only show "UNO" (not enough space on the display)
  // const char text[] = "UNO r4";
  // matrix.textFont(Font_4x6);
  // matrix.beginText(0, 1, 0xFFFFFF);
  // matrix.println(text);
  // matrix.endText();

  // matrix.endDraw();

  // Mostrar mensaje de bienvenida
  // Make it scroll!
  matrix.begin();
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textScrollSpeed(50);
  // add the text
  const char text[] = "    Seniales    ";
  matrix.textFont(Font_5x7);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println(text);
  matrix.endText(SCROLL_LEFT);
  matrix.endDraw();  

  delay(2000);

  Serial.println("✓ Matriz LED inicializada");
  
  // Conectar a WiFi
  conectarWiFi();
  
  // Iniciar servidor web
  server.begin();
  Serial.print("✓ Servidor web iniciado en: http://");
  Serial.println(WiFi.localIP());
  
  // Mostrar estado inicial
  actualizarMatrizLED();
  mostrarInfoSistema();
  Serial.println("🚀 Sistema listo para usar!");
  Serial.println("=================================");
}

void loop() {

  // // Make it scroll!
  // matrix.beginDraw();

  // matrix.stroke(0xFFFFFFFF);
  // matrix.textScrollSpeed(50);

  // // add the text
  // const char text[] = "    Hello World!    ";
  // matrix.textFont(Font_5x7);
  // matrix.beginText(0, 1, 0xFFFFFF);
  // matrix.println(text);
  // matrix.endText(SCROLL_LEFT);

  // matrix.endDraw();

  // Manejar botón físico
  manejarBoton();
  
  // Manejar cliente web
  manejarClienteWeb();
  
  // Mostrar info cada 30 segundos
  static unsigned long ultimoInfo = 0;
  if (millis() - ultimoInfo > 30000) {
    mostrarInfoSistema();
    ultimoInfo = millis();
  }
  
  // Pequeña pausa
  delay(10);
}

void conectarWiFi() {
  Serial.print("🌐 Conectando a WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 30) {
    delay(500);
    Serial.print(".");
    
    // Mostrar animación en matriz
    matrix.loadFrame(animacionConectando[intentos % 3]);
    intentos++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("✅ WiFi conectado exitosamente!");
    Serial.print("📍 IP asignada: ");
    Serial.println(WiFi.localIP());
    Serial.print("📶 Señal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    // Mostrar mensaje de conexion wifi exitosa
    // Make it scroll!
    matrix.beginDraw();
    matrix.stroke(0xFFFFFFFF);
    matrix.textScrollSpeed(50);
    // add the text
    const char text[] = "    ✅ WiFi conectado exitosamente!    ";
    matrix.textFont(Font_5x7);
    matrix.beginText(0, 1, 0xFFFFFF);
    matrix.println(text);
    matrix.endText(SCROLL_LEFT);
    matrix.endDraw();    
  } else {
    Serial.println();
    Serial.println("❌ Error: No se pudo conectar a WiFi");
    Serial.println("Verifica SSID y contraseña");
  }
}

void manejarBoton() {
  bool estadoActualBoton = digitalRead(BOTON_PIN);
  // Serial.print(">>> estadoActualBoton: ");
  // Serial.println(ultimoDebounce);
  // Debounce
  if (estadoActualBoton != ultimoEstadoBoton) {
    ultimoDebounce = millis();
    Serial.println(">>> ----------------------------- <<<");
    Serial.print(">>> estadoActualBoton: ");
    Serial.println(estadoActualBoton);
    // Serial.print(">>> ultimoEstadoBoton: ");
    // Serial.println(ultimoEstadoBoton);
    Serial.print(">>> ultimoDebounce: ");
    Serial.println(ultimoDebounce);
    Serial.print(">>> DEBOUNCE_DELAY: ");
    Serial.println(DEBOUNCE_DELAY);
  }
  
  if ((millis() - ultimoDebounce) > DEBOUNCE_DELAY) {
    // Serial.print(">>> Aqui 1");
    // Serial.print(">>> ultimoDebounce: ");
    // Serial.println(ultimoDebounce);
    // Si el botón cambió de LOW a HIGH (presionado)
    // if (estadoActualBoton && !ultimoEstadoBoton) {
    // if ((estadoActualBoton == 1) && (ultimoEstadoBoton == 0)) {
    if (estadoActualBoton == 1) {
      // Serial.print("==>>> ultimoEstadoBoton: ");
      // Serial.println(ultimoEstadoBoton);      
      if (btnPress == false){
        Serial.println(">>> Aqui 2");
        btnPress = true;
        // toggleFoco();
        encenderFoco();
        Serial.println("🔘 Botón físico presionado - Estado cambiado");
        // delay(20);
      }
    }else{
      // Serial.println(">>> Aqui 3");
      btnPress = false;
      apagarFoco();
      // ultimoEstadoBoton = estadoActualBoton;
    }
  }
  
  ultimoEstadoBoton = estadoActualBoton;
}

void encenderFoco() {
  if (!estadoFoco) {
    estadoFoco = true;
    digitalWrite(RELE_CANAL1, LOW);  // Módulo activo LOW
    digitalWrite(LED_CANAL1, HIGH);  // Led 1 On
    actualizarMatrizLED();
    Serial.println("✅ Foco ENCENDIDO - Canal 1 activado");
  } else {
    Serial.println("ℹ️ El foco ya estaba encendido");
  }
}

void apagarFoco() {
  if (estadoFoco) {
    delay(FOCO_DELAY);
    estadoFoco = false;
    digitalWrite(RELE_CANAL1, HIGH); // Módulo activo LOW
    digitalWrite(LED_CANAL1, LOW);  // Led 1 Off
    actualizarMatrizLED();
    Serial.println("❌ Foco APAGADO - Canal 1 desactivado");
  } else {
    // Serial.println("ℹ️ El foco ya estaba apagado");
  }
}

void manejarClienteWeb() {
  WiFiClient client = server.available();
  
  if (client) {
    Serial.println("🌐 Nuevo cliente web conectado");
    String request = "";
    
    while (client.connected()) {
      if (client.available()) {
        String line = client.readStringUntil('\r');
        request += line;
        
        if (line.length() == 1 && line[0] == '\n') {
          break;
        }
      }
    }
    
    // Procesar comandos
    if (request.indexOf("GET /encender") >= 0) {
      encenderFoco();
      Serial.println("🌐 Comando web: Encender foco");
    }
    else if (request.indexOf("GET /apagar") >= 0) {
      apagarFoco();
      Serial.println("🌐 Comando web: Apagar foco");
    }
    else if (request.indexOf("GET /toggle") >= 0) {
      toggleFoco();
      Serial.println("🌐 Comando web: Toggle foco");
    }
    else if (request.indexOf("GET /estado") >= 0) {
      Serial.println("🌐 Comando web: Consulta de estado");
    }
    
    // Enviar respuesta HTML
    enviarPaginaWeb(client);
    
    client.stop();
    Serial.println("🌐 Cliente web desconectado");
  }
}

void enviarPaginaWeb(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html; charset=UTF-8");
  client.println("Connection: close");
  client.println();
  
  // HTML de la página web
  client.println("<!DOCTYPE html>");
  client.println("<html lang='es'>");
  client.println("<head>");
  client.println("<title>Control Foco WiFi - Arduino R4</title>");
  client.println("<meta charset='UTF-8'>");
  client.println("<meta name='viewport' content='width=device-width, initial-scale=1.0'>");
  client.println("<style>");
  client.println("*{margin:0;padding:0;box-sizing:border-box}");
  client.println("body{font-family:'Segoe UI',Arial,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px}");
  client.println(".container{max-width:800px;margin:0 auto;background:rgba(255,255,255,0.95);padding:30px;border-radius:20px;box-shadow:0 8px 32px rgba(0,0,0,0.1);backdrop-filter:blur(10px)}");
  client.println("h1{color:#333;text-align:center;margin-bottom:10px;font-size:2.5em;text-shadow:2px 2px 4px rgba(0,0,0,0.1)}");
  client.println("h2{color:#666;text-align:center;margin-bottom:30px;font-weight:300}");
  client.println(".status{text-align:center;font-size:28px;margin:30px 0;padding:20px;border-radius:15px;box-shadow:0 4px 15px rgba(0,0,0,0.1);transition:all 0.3s ease}");
  client.println(".on{background:linear-gradient(135deg,#4ecdc4,#44a08d);color:white;transform:scale(1.02)}");
  client.println(".off{background:linear-gradient(135deg,#ff9a9e,#fecfef);color:#333}");
  client.println(".controls{text-align:center;margin:40px 0;display:flex;gap:15px;justify-content:center;flex-wrap:wrap}");
  client.println(".button{display:inline-block;padding:18px 35px;font-size:18px;font-weight:600;text-decoration:none;border-radius:50px;color:white;text-align:center;min-width:140px;transition:all 0.3s ease;box-shadow:0 4px 15px rgba(0,0,0,0.2)}");
  client.println(".btn-on{background:linear-gradient(135deg,#56ab2f,#a8e6cf)} .btn-off{background:linear-gradient(135deg,#ff512f,#dd2476)} .btn-toggle{background:linear-gradient(135deg,#4facfe,#00f2fe)}");
  client.println(".button:hover{transform:translateY(-3px);box-shadow:0 6px 20px rgba(0,0,0,0.3)}");
  client.println(".info{background:rgba(255,255,255,0.8);padding:25px;border-radius:15px;margin:25px 0;border-left:5px solid #4facfe;box-shadow:0 4px 15px rgba(0,0,0,0.05)}");
  client.println(".channel{background:rgba(248,249,250,0.8);padding:18px;margin:12px 0;border-radius:12px;border:1px solid rgba(222,226,230,0.8);transition:all 0.3s ease}");
  client.println(".channel:hover{background:rgba(255,255,255,0.9);transform:translateX(5px)}");
  client.println(".available{color:#6c757d;font-style:italic}");
  client.println(".active{color:#28a745;font-weight:bold}");
  client.println(".inactive{color:#dc3545;font-weight:bold}");
  client.println(".system-info{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:15px;margin:20px 0}");
  client.println(".info-card{background:rgba(255,255,255,0.9);padding:15px;border-radius:10px;text-align:center;box-shadow:0 2px 10px rgba(0,0,0,0.05)}");
  client.println(".refresh{position:fixed;top:20px;right:20px;background:#4facfe;color:white;border:none;padding:10px 15px;border-radius:50px;cursor:pointer;box-shadow:0 4px 15px rgba(0,0,0,0.2)}");
  client.println("@media (max-width:600px){.controls{flex-direction:column;align-items:center} .button{min-width:80%}}");
  client.println("</style>");
  client.println("</head>");
  client.println("<body>");
  
  client.println("<button class='refresh' onclick='location.reload()'>🔄</button>");
  
  client.println("<div class='container'>");
  client.println("<h1>🏠 Control de Foco WiFi</h1>");
  client.println("<h2>Arduino Uno R4 WiFi + Módulo 4 Canales</h2>");
  
  // Estado actual con indicador visual mejorado
  client.print("<div class='status ");
  if (estadoFoco) {
    client.println("on'>💡 FOCO ENCENDIDO<br><small>Sistema activo y funcionando</small></div>");
  } else {
    client.println("off'>💡 FOCO APAGADO<br><small>Listo para activar</small></div>");
  }
  
  // Controles principales
  client.println("<div class='controls'>");
  client.println("<a href='/encender' class='button btn-on'>🔆 ENCENDER</a>");
  client.println("<a href='/apagar' class='button btn-off'>🔅 APAGAR</a>");
  client.println("<a href='/toggle' class='button btn-toggle'>🔄 ALTERNAR</a>");
  client.println("</div>");
  
  // Estado de canales
  client.println("<div class='info'>");
  client.println("<h3>📊 Estado de Canales del Módulo Relé</h3>");
  
  client.println("<div class='channel'>");
  client.print("<strong>🔌 Canal 1 (Foco Principal):</strong> ");
  client.print(estadoFoco ? "<span class='active'>🟢 ACTIVO - Foco encendido</span>" : "<span class='inactive'>🔴 INACTIVO - Foco apagado</span>");
  client.println("</div>");
  
  client.println("<div class='channel'>");
  client.println("<strong>🔌 Canal 2:</strong> <span class='available'>⚪ Disponible para luz exterior o ventilador</span>");
  client.println("</div>");
  
  client.println("<div class='channel'>");
  client.println("<strong>🔌 Canal 3:</strong> <span class='available'>⚪ Disponible para bomba de agua o motor</span>");
  client.println("</div>");
  
  client.println("<div class='channel'>");
  client.println("<strong>🔌 Canal 4:</strong> <span class='available'>⚪ Disponible para sistema de riego</span>");
  client.println("</div>");
  client.println("</div>");
  
  // Información del sistema
  client.println("<div class='info'>");
  client.println("<h3>ℹ️ Información del Sistema</h3>");
  client.println("<div class='system-info'>");
  
  client.println("<div class='info-card'>");
  client.println("<strong>🌐 Dirección IP</strong><br>");
  client.println(WiFi.localIP().toString());
  client.println("</div>");
  
  client.println("<div class='info-card'>");
  client.println("<strong>📡 Señal WiFi</strong><br>");
  client.print(WiFi.RSSI());
  client.println(" dBm");
  client.println("</div>");
  
  client.println("<div class='info-card'>");
  client.println("<strong>⏱️ Tiempo activo</strong><br>");
  client.print(millis() / 1000);
  client.println(" seg");
  client.println("</div>");
  
  client.println("<div class='info-card'>");
  client.println("<strong>🔧 Hardware</strong><br>");
  client.println("Arduino R4 WiFi");
  client.println("</div>");
  
  client.println("</div>");
  
  client.println("<p style='margin-top:20px;color:#666;text-align:center'>");
  client.println("<strong>Control disponible:</strong> Botón físico (Pin D2) + Interfaz web WiFi<br>");
  client.println("<strong>Visualización:</strong> Matriz LED 12x8 integrada + LEDs del módulo relé");
  client.println("</p>");
  client.println("</div>");
  
  // Comandos API
  client.println("<div class='info'>");
  client.println("<h3>🔧 API de Control</h3>");
  client.println("<p><strong>Comandos disponibles:</strong></p>");
  client.println("<code style='background:#f8f9fa;padding:10px;border-radius:5px;display:block;margin:10px 0'>");
  client.println("GET /encender - Encender foco<br>");
  client.println("GET /apagar - Apagar foco<br>");
  client.println("GET /toggle - Alternar estado<br>");
  client.println("GET /estado - Consultar estado");
  client.println("</code>");
  client.println("</div>");
  
  client.println("</div>");
  
  // Auto-refresh cada 10 segundos
  client.println("<script>");
  client.println("let autoRefresh = setInterval(() => {");
  client.println("  fetch('/estado').then(() => window.location.reload());");
  client.println("}, 10000);");
  client.println("document.addEventListener('visibilitychange', () => {");
  client.println("  if (document.visibilityState === 'hidden') clearInterval(autoRefresh);");
  client.println("  else autoRefresh = setInterval(() => window.location.reload(), 10000);");
  client.println("});");
  client.println("</script>");
  
  client.println("</body>");
  client.println("</html>");
}

void toggleFoco() {
  if (estadoFoco) {
    apagarFoco();
  } else {
    encenderFoco();
  }
}

void actualizarMatrizLED() {
  if (estadoFoco) {
    // matrix.loadFrame(iconoFocoON);
  } else {
    // matrix.loadFrame(iconoFocoOFF);
  }
}

void mostrarIconoWiFi() {
  matrix.loadFrame(iconoWiFi);
  delay(1000);
}

// Función para mostrar información de sistema en Serial
void mostrarInfoSistema() {
  Serial.println("=== INFORMACIÓN DEL SISTEMA ===");
  Serial.print("WiFi IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Señal WiFi: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.print("Estado Foco: ");
  Serial.println(estadoFoco ? "ENCENDIDO" : "APAGADO");
  Serial.print("Uptime: ");
  Serial.print(millis() / 1000);
  Serial.println(" segundos");
  Serial.println("===============================");
}