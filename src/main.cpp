#include <Arduino.h>
#include <Update.h>
#include <WiFiManager.h>
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

// Variables to save values from HTML form
String ssid;
String pass;
String ip;
String gateway;

// LED
const int ledPin = 18;

// count
volatile int count = 0;

// Time
int interval_send = 10;                // send data to the client every 10ms
unsigned long previousMillis_send = 0; // we use the "millis()" command for time reference

const long interval = 20000; // interval to wait for Wi-Fi connection (milliseconds)
unsigned long previousMillis = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // the websocket uses port 81 (standard port for websockets

// STA static IP
IPAddress localIP(192,168,1,99);
IPAddress localGateway(192,168,1,4);
IPAddress subnet(255, 255, 0, 0);
IPAddress dns(8, 8, 8, 8); // add DNS

// Init ESP32 timer 0
ESP32Timer ITimer0(0);
bool is_timer_run = false;

// Initialize SPIFFS
void initSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("An error has occurred while mounting SPIFFS");
    }
    Serial.println("SPIFFS mounted successfully");
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
            else if (String(command) == "increase")
            {
                count++;
                Serial.println("Command: " + String(command));
            }
            else if (String(command) == "decrease")
            {
                count--;
                Serial.println("Command: " + String(command));
            }
            else if (String(command) == "stop")
            {
                is_timer_run = false;
                ITimer0.stopTimer();
                Serial.println("Command: " + String(command));
            }
            else if (String(command) == "start")
            {
                if(!is_timer_run)
                {
                    ITimer0.restartTimer();
                    is_timer_run = true;
                }
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
    is_timer_run = true;
    initSPIFFS();

    // Wifi manager
    // WiFi.mode(WIFI_STA);
    WiFiManager wm;
    // reset settings - wipe stored credentials for testing
    // these are stored by the esp library
    // wm.resetSettings();
    wm.setConfigPortalTimeout(180);
    wm.setConnectTimeout(30);
    wm.setSTAStaticIPConfig(localIP, localGateway, subnet); // optional DNS 4th argument
    bool res;
    res = wm.autoConnect(AP_SSID, AP_PASSWORD); // password protected ap

    if(!res) {
        Serial.println("Failed to connect");
        ESP.restart();
    } 
    else {
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
        
        // Connected successfully
        digitalWrite(ledPin, HIGH);
    }

}

void loop()
{
    webSocket.loop();
    if (count >= 20)
    {
        count = 0;
    }
    else if(count < 0)  // change this
    {
        count = 19;
    }
    unsigned long now = millis(); // read out the current "time" ("millis()" gives the time in ms since the Arduino started)
    if ((unsigned long)(now - previousMillis_send) > interval_send)
    { // check if "interval" ms has passed since last time the clients were updated
        sendJson("count", count);
        previousMillis_send = now; // reset previousMillis
    }
}