#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include "ESP32TimerInterrupt.h"
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#define TIMER0_INTERVAL_MS 1000

// ESP32 credentials in Access point mode
const char *AP_SSID = "ESP-WIFI-MANAGER";
const char *AP_PASSWORD = NULL; // NULL sets an open Access Point

// Search for parameter in HTTP POST request
const char *PARAM_INPUT_1 = "ssid";
const char *PARAM_INPUT_2 = "pass";
const char *PARAM_INPUT_3 = "ip";
const char *PARAM_INPUT_4 = "gateway";

// Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// File paths to save input values permanently
const char *ssidPath = "/ssid.txt";
const char *passPath = "/pass.txt";
const char *ipPath = "/ip.txt";
const char *gatewayPath = "/gateway.txt";

// LED
const int ledPin = 18;

// count
volatile int count = 0;

// Time
int interval_send = 10;                // send data to the client every 10ms
unsigned long previousMillis_send = 0; // we use the "millis()" command for time reference

const long interval = 10000; // interval to wait for Wi-Fi connection (milliseconds)
unsigned long previousMillis = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // the websocket uses port 81 (standard port for websockets

IPAddress localIP;
IPAddress localGateway;
IPAddress subnet(255, 255, 0, 0);
IPAddress dns(8, 8, 8, 8); // add DNS

// Init ESP32 timer 0
ESP32Timer ITimer0(0);

// Initialize SPIFFS
void initSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("An error has occurred while mounting SPIFFS");
    }
    Serial.println("SPIFFS mounted successfully");
}

// Read File from SPIFFS
String readFile(fs::FS &fs, const char *path)
{
    Serial.printf("Reading file: %s\r\n", path);

    File file = fs.open(path);
    if (!file || file.isDirectory())
    {
        Serial.println("- failed to open file for reading");
        return String();
    }

    String fileContent;
    while (file.available())
    {
        fileContent = file.readStringUntil('\n');
        break;
    }
    return fileContent;
}

// Write file to SPIFFS
void writeFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- file written");
    }
    else
    {
        Serial.println("- write failed");
    }
}

// Initialize WiFi
bool initWiFi()
{
    if (ssid == "" || ip == "")
    {
        Serial.println("Undefined SSID or IP address.");
        return false;
    }

    WiFi.mode(WIFI_STA);
    localIP.fromString(ip.c_str());
    localGateway.fromString(gateway.c_str());

    if (!WiFi.config(localIP, localGateway, subnet, dns))
    {
        Serial.println("STA Failed to configure");
        return false;
    }
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.println("Connecting to WiFi...");

    unsigned long currentMillis = millis();
    previousMillis = currentMillis;

    while (WiFi.status() != WL_CONNECTED)
    {

        currentMillis = millis();
        if (currentMillis - previousMillis >= interval)
        {
            Serial.println("Failed to connect!");
            return false;
        }
    }
    Serial.print("Connected! Your Web Server is at: ");
    Serial.println(WiFi.localIP());
    return true;
}

// timer interrupt handler
bool IRAM_ATTR TimerHandler0(void *timerNo)
{
    count++;
    return true;
}

void webSocketEvent(byte num, WStype_t type, uint8_t *payload, size_t length)
{ // the parameters of this callback function are always the same -> num: id of the client who send the event, type: type of message, payload: actual data sent and length: length of payload
    switch (type)
    {                         // switch on the type of information sent
    case WStype_DISCONNECTED: // if a client is disconnected, then type == WStype_DISCONNECTED
        Serial.println("Client " + String(num) + " disconnected");
        break;
    case WStype_CONNECTED: // if a client is connected, then type == WStype_CONNECTED
        Serial.println("Client " + String(num) + " connected");
        // optionally you can add code here what to do when connected
        break;
    case WStype_TEXT: // if a client has sent data, then type == WStype_TEXT
        // try to decipher the JSON string received
        StaticJsonDocument<200> doc; // create a JSON container
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
        }
        else
        {
            // JSON string was received correctly, so information can be retrieved:
            const char *command = doc["command"];
            Serial.println("Received command from user: " + String(num));
            if (String(command) == "reset")
            {
                count = 0;
                Serial.println("Command: " + String(command));
            }
            else if (String(command) == "stop")
            {
                ITimer0.disableTimer();
                Serial.println("Command: " + String(command));
            }
        }
        Serial.println("");
        break;
    }
}

// Simple function to send information to the web clients
void sendJson(String l_type, int l_value)
{
    String jsonString = "";                   // create a JSON string for sending data to the client
    StaticJsonDocument<200> doc;              // create JSON container
    JsonObject object = doc.to<JsonObject>(); // create a JSON Object
    object[l_type] = l_value;                 // write data into the JSON object
    serializeJson(doc, jsonString);           // convert JSON object to string
    webSocket.broadcastTXT(jsonString);       // send JSON string to all clients
}

void setup()
{
    // Serial port for debugging purposes
    Serial.begin(115200);

    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);

    // Interval in microseconds
    if (!ITimer0.attachInterruptInterval(TIMER0_INTERVAL_MS * 1000, TimerHandler0))
    {
        Serial.println(F("Can't set ITimer0. Select another Timer, freq. or timer"));
    }

    initSPIFFS();

    // Load values saved in SPIFFS
    ssid = readFile(SPIFFS, ssidPath);
    pass = readFile(SPIFFS, passPath);
    ip = readFile(SPIFFS, ipPath);
    gateway = readFile(SPIFFS, gatewayPath);

    // comment this
    Serial.println("--------------DEBUG MODE--------------");
    Serial.println("WiFi SSID: " + ssid);
    Serial.println("WiFi password: " + pass);
    Serial.println("WiFi IP: " + ip);
    Serial.println("WiFi gateway: " + gateway);
    Serial.println("--------------DEBUG MODE--------------");

    if (initWiFi())
    {
        // Connected successfully
        digitalWrite(ledPin, LOW);

        // Route for root / web page
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { // define here wat the webserver needs to do
            request->send(SPIFFS, "/index.html", "text/html");
        });

        server.onNotFound([](AsyncWebServerRequest *request)
                          { request->send(404, "text/plain", "File not found"); });

        server.serveStatic("/", SPIFFS, "/");

        webSocket.begin();                 // start websocket
        webSocket.onEvent(webSocketEvent); // define a callback function -> what does the ESP32 need to do when an event from the websocket is received? -> run function "webSocketEvent()"

        server.begin(); // start server -> best practise is to start the server after the websocket
    }
    else
    {
        // Connection failed -> bringing up configuration portal
        Serial.println("Setting AP (Access Point)");

        // NULL sets an open Access Point
        WiFi.softAP(AP_SSID, AP_PASSWORD);

        IPAddress IP = WiFi.softAPIP();
        Serial.print("AP IP address: ");
        Serial.println(IP);

        // Web Server Root URL
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(SPIFFS, "/wifi_manager.html", "text/html"); });

        server.serveStatic("/", SPIFFS, "/");

        server.on("/", HTTP_POST, [](AsyncWebServerRequest *request)
        {
            int params = request->params();
            for(int i=0;i<params;i++){
                AsyncWebParameter* p = request->getParam(i);
                if(p->isPost()){
                    // HTTP POST ssid value
                    if (p->name() == PARAM_INPUT_1) {
                        ssid = p->value().c_str();
                        Serial.print("SSID set to: ");
                        Serial.println(ssid);
                        // Write file to save value
                        writeFile(SPIFFS, ssidPath, ssid.c_str());
                    }
                    // HTTP POST pass value
                    if (p->name() == PARAM_INPUT_2) {
                        pass = p->value().c_str();
                        Serial.print("Password set to: ");
                        Serial.println(pass);
                        // Write file to save value
                        writeFile(SPIFFS, passPath, pass.c_str());
                    }
                    // HTTP POST ip value
                    if (p->name() == PARAM_INPUT_3) {
                        ip = p->value().c_str();
                        Serial.print("IP Address set to: ");
                        Serial.println(ip);
                        // Write file to save value
                        writeFile(SPIFFS, ipPath, ip.c_str());
                    }
                    // HTTP POST gateway value
                    if (p->name() == PARAM_INPUT_4) {
                        gateway = p->value().c_str();
                        Serial.print("Gateway set to: ");
                        Serial.println(gateway);
                        // Write file to save value
                        writeFile(SPIFFS, gatewayPath, gateway.c_str());
                    }
                    //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
                }
            }
            request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
            delay(3000);
            ESP.restart(); 
        });
        server.begin();
    }
    
}

void loop()
{
    webSocket.loop();
    if (count >= 20)
    {
        count = 0;
    }
    unsigned long now = millis(); // read out the current "time" ("millis()" gives the time in ms since the Arduino started)
    if ((unsigned long)(now - previousMillis_send) > interval_send)
    { // check if "interval" ms has passed since last time the clients were updated
        sendJson("count", count);
        previousMillis_send = now; // reset previousMillis
    }
}