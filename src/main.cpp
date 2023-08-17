#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include "ESP32TimerInterrupt.h"
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#define TIMER0_INTERVAL_MS 1000

// Replace with your network credentials
const char *ssid = "Minh Huyen";
const char *password = "@nttn0368460347";

// LED
const int ledPin = 18;

// count
volatile int count = 0;

// We want to periodically send values to the clients, so we need to define an "interval" and remember the last time we sent data to the client (with "previousMillis")
int interval = 10;              // send data to the client every 10ms
unsigned long previousMillis = 0; // we use the "millis()" command for time reference and this will output an unsigned long

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // the websocket uses port 81 (standard port for websockets

IPAddress local_IP(192, 168, 1, 199);
IPAddress gateway(192, 168, 1, 254);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8); // add DNS

// Init ESP32 timer 0
ESP32Timer ITimer0(0);

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
    object[l_type] = l_value;                  // write data into the JSON object
    serializeJson(doc, jsonString);     // convert JSON object to string
    webSocket.broadcastTXT(jsonString); // send JSON string to all clients
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

    // Initialize SPIFFS
    if (!SPIFFS.begin(true))
    {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    // Configures static IP address
    if (!WiFi.config(local_IP, gateway, subnet, dns))
    {
        Serial.println("STA Failed to configure");
    }

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    digitalWrite(ledPin, HIGH); // indicate successful wifi connection
    Serial.println();

    // Print ESP Local IP Address
    Serial.print("Web Server at: ");
    Serial.println(WiFi.localIP());

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

void loop()
{
    webSocket.loop();
    if (count >= 20)
    {
        count = 0;
    }
    unsigned long now = millis(); // read out the current "time" ("millis()" gives the time in ms since the Arduino started)
    if ((unsigned long)(now - previousMillis) > interval)
    { // check if "interval" ms has passed since last time the clients were updated
        sendJson("count", count);
        previousMillis = now; // reset previousMillis
    }
}