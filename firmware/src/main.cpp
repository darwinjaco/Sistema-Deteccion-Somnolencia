#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <BluetoothSerial.h>
#include <EEPROM.h>
#include <WebServer.h>
#include "credentials.h"  // No incluido en el repo - ver credentials.example.h

// ========== PINES ==========
const int pinMotor = 18;

// ========== PWM ==========
const int pwmChannel = 0;
const int freq       = 5000;
const int resolution = 8;

// ========== EEPROM ==========
#define EEPROM_SIZE  200
#define SSID1_ADDR   0
#define PASS1_ADDR   50
#define SSID2_ADDR   100
#define PASS2_ADDR   150

// ========== AP WIFI ==========
WebServer server(80);

BluetoothSerial SerialBT;

// ========== EEPROM HELPERS ==========
void writeStringToEEPROM(int addr, const String &data) {
    for (int i = 0; i < 50; ++i)
        EEPROM.write(addr + i, i < (int)data.length() ? data[i] : 0);
}

String readStringFromEEPROM(int addr) {
    char data[51];
    for (int i = 0; i < 50; ++i)
        data[i] = EEPROM.read(addr + i);
    data[50] = '\0';
    return String(data);
}

// ========== WIFI ==========
bool waitForConnection() {
    for (int i = 0; i < 10; i++) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("Conectado. IP: ");
            Serial.println(WiFi.localIP());
            return true;
        }
        delay(500);
    }
    return false;
}

bool connectToStoredNetworks() {
    String ssid1 = readStringFromEEPROM(SSID1_ADDR);
    String pass1 = readStringFromEEPROM(PASS1_ADDR);
    String ssid2 = readStringFromEEPROM(SSID2_ADDR);
    String pass2 = readStringFromEEPROM(PASS2_ADDR);

    if (ssid1.length() > 0) {
        WiFi.begin(ssid1.c_str(), pass1.c_str());
        Serial.print("Conectando a: "); Serial.println(ssid1);
        if (waitForConnection()) return true;
    }
    if (ssid2.length() > 0) {
        WiFi.begin(ssid2.c_str(), pass2.c_str());
        Serial.print("Conectando a: "); Serial.println(ssid2);
        if (waitForConnection()) return true;
    }
    return false;
}

// ========== SERVIDOR WEB AP ==========
void handleRoot() {
    String html = R"rawliteral(
    <!DOCTYPE html><html><head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1'>
    <title>Configurar WiFi</title>
    <style>
        body { font-family: sans-serif; text-align: center; margin-top: 40px; background: #f0f0f0; }
        form { background: white; padding: 20px; display: inline-block; border-radius: 10px; }
        input { display: block; margin: 10px auto; padding: 8px; width: 80%; border-radius: 5px; border: 1px solid #ccc; }
        button { padding: 10px 20px; border: none; background: #007bff; color: white; border-radius: 5px; cursor: pointer; }
    </style></head><body>
    <h2>Configurar WiFi</h2>
    <form action='/save' method='POST'>
        <input name='ssid1' placeholder='SSID 1' required>
        <input name='pass1' placeholder='Contrasena 1' required>
        <input name='ssid2' placeholder='SSID 2 (opcional)'>
        <input name='pass2' placeholder='Contrasena 2'>
        <button type='submit'>Guardar</button>
    </form>
    </body></html>
    )rawliteral";
    server.send(200, "text/html", html);
}

void handleSave() {
    writeStringToEEPROM(SSID1_ADDR, server.arg("ssid1"));
    writeStringToEEPROM(PASS1_ADDR, server.arg("pass1"));
    writeStringToEEPROM(SSID2_ADDR, server.arg("ssid2"));
    writeStringToEEPROM(PASS2_ADDR, server.arg("pass2"));
    EEPROM.commit();
    server.send(200, "text/html", "<h2>Redes guardadas. Reinicia la ESP32.</h2>");
}

void setupAPWebServer() {
    server.on("/", handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
}

// ========== TELEGRAM ==========
void enviarTelegram(const String& mensaje) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Telegram] Sin WiFi.");
        return;
    }

    SerialBT.end();
    delay(300);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    char url[512];
    snprintf(url, sizeof(url),
        "https://api.telegram.org/bot%s/sendMessage?chat_id=%s&text=%s",
        TELEGRAM_TOKEN, TELEGRAM_CHAT_ID, mensaje.c_str());

    if (http.begin(client, url)) {
        int code = http.GET();
        Serial.printf("[Telegram] HTTP %d\n", code);
        http.end();
    } else {
        Serial.println("[Telegram] Error al conectar.");
    }

    delay(300);
    SerialBT.begin("ESP32-Somnolencia");
}

// ========== FASES DE SOMNOLENCIA ==========
void fase1() {
    Serial.println("[FASE 1] Somnolencia leve - motor apagado");
    ledcWrite(pwmChannel, 0);
}

void fase2() {
    Serial.println("[FASE 2] Somnolencia media - motor 50%");
    ledcWrite(pwmChannel, 128);
    enviarTelegram("ALERTA: Fase 2 - Somnolencia intermedia detectada.");
    delay(2000);
    ledcWrite(pwmChannel, 0);
}

void fase3() {
    Serial.println("[FASE 3] Somnolencia critica - motor 100%");
    ledcWrite(pwmChannel, 255);
    enviarTelegram("ALERTA CRITICA: Fase 3 - Nivel critico de somnolencia detectado.");
    delay(2000);
    ledcWrite(pwmChannel, 0);
}

// ========== SETUP ==========
void setup() {
    Serial.begin(115200);
    Serial.println("\n===== Sistema Deteccion Somnolencia v1.0 =====\n");

    ledcSetup(pwmChannel, freq, resolution);
    ledcAttachPin(pinMotor, pwmChannel);

    EEPROM.begin(EEPROM_SIZE);

    if (!connectToStoredNetworks()) {
        Serial.println("Sin WiFi guardado. Iniciando AP...");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.print("AP activo. IP: ");
        Serial.println(WiFi.softAPIP());
        setupAPWebServer();
    }

    SerialBT.begin("ESP32-Somnolencia");
    Serial.println("Bluetooth listo. Esperando comandos...\n");
}

// ========== LOOP ==========
void loop() {
    if (WiFi.getMode() == WIFI_AP) {
        server.handleClient();
    }

    if (SerialBT.available()) {
        char command = SerialBT.read();
        Serial.printf("[BT] Comando recibido: %c\n", command);
        switch (command) {
            case '1': fase1(); break;
            case '2': fase2(); break;
            case '3': fase3(); break;
            default:  Serial.println("[BT] Comando invalido"); break;
        }
    }

    delay(10);
}
