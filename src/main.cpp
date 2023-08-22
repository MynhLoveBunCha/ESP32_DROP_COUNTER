#include <Arduino.h>
#include <Update.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "SPIFFS.h"
#include "ESP32TimerInterrupt.h"
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include "SPI.h"
#include <TFT_eSPI.h>
#include "Free_Fonts.h"

// Define sensor pin
#define SENSOR_IN_1 35
#define SENSOR_IN_2 34
#define RESET_PIN 33    // reset
#define INCREASE_PIN 4  // increase
#define DECREASE_PIN 15 // decrease
#define LOOP_PERIOD 10  // Display updates time

// button
volatile bool buttonState = HIGH;
volatile unsigned long debounceDelay = 50;
volatile unsigned long lastDebounceTime = 0;

// screen
TFT_eSPI tft = TFT_eSPI(); // Evoke library for tft
uint16_t mainBg = TFT_BLACK;
uint32_t updateTime = 0;

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
volatile static bool is_sen_1_trig = false;
volatile static int sen_1_val;
volatile static int sen_2_val;

// Time
int interval_send = 5;                // send data to the client every 10ms
unsigned long previousMillis_send = 0; // we use the "millis()" command for time reference
int interval_tft = 1;                // send data to the tft every 10ms
unsigned long previousMillis_tft = 0; // we use the "millis()" command for time reference

const long interval = 20000; // interval to wait for Wi-Fi connection (milliseconds)
unsigned long previousMillis = 0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // the websocket uses port 81 (standard port for websockets

// STA static IP
IPAddress localIP(192, 168, 1, 99);
IPAddress localGateway(192, 168, 1, 4);
IPAddress subnet(255, 255, 0, 0);
IPAddress dns(8, 8, 8, 8); // add DNS

// button interrupt routine
void IRAM_ATTR ISR_RESET()
{
    unsigned long currentTime = millis();

    if (currentTime - lastDebounceTime >= debounceDelay) // Check time
    {
        if (digitalRead(RESET_PIN) != buttonState) // Check if the button state has changed
        {
            buttonState = !buttonState; // Update button state
            if (buttonState == LOW)
            {
                count = 0;
            }
            lastDebounceTime = currentTime; // Update time
        }
    }
}

void IRAM_ATTR ISR_INC()
{
    unsigned long currentTime = millis();

    if (currentTime - lastDebounceTime >= debounceDelay)
    {
        if (digitalRead(INCREASE_PIN) != buttonState)
        {
            buttonState = !buttonState;
            if (buttonState == LOW)
            {
                count++;
            }
            lastDebounceTime = currentTime;
        }
    }
}

void IRAM_ATTR ISR_DEC()
{
    unsigned long currentTime = millis();

    if (currentTime - lastDebounceTime >= debounceDelay)
    {
        if (digitalRead(DECREASE_PIN) != buttonState)
        {
            buttonState = !buttonState;
            if (buttonState == LOW)
            {
                count--;
            }
            lastDebounceTime = currentTime;
        }
    }
}

// read sensor
void IRAM_ATTR ISR_SENSOR_1()
{
    sen_2_val = digitalRead(SENSOR_IN_2);
    if (sen_2_val == 1)
    {
        is_sen_1_trig = true;
    }
}

void IRAM_ATTR ISR_SENSOR_2()
{
    sen_1_val = digitalRead(SENSOR_IN_1);
    if (sen_1_val == 1 && is_sen_1_trig)
    {
        count++;
    }
    is_sen_1_trig = false;
}

// Initialize SPIFFS
void initSPIFFS()
{
    if (!SPIFFS.begin(true))
    {
        Serial.println("An error has occurred while mounting SPIFFS");
    }
    Serial.println("SPIFFS mounted successfully");
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

// Update display
void updateTFT()
{
    // draw rectangle border
    tft.drawRect(0, 0, 320, 240, TFT_WHITE);
    tft.drawRect(1, 1, 320 - 2, 240 - 2, TFT_WHITE);
    tft.drawRect(3, 3, 320 - 6, 240 - 6, TFT_WHITE);
    tft.drawRect(4, 4, 320 - 8, 240 - 8, TFT_WHITE);

    tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
    tft.setTextSize(2);
    tft.setTextFont(7);
    tft.setTextDatum(MC_DATUM);
    tft.setTextPadding(tft.width() - 10);
    tft.drawNumber(count, tft.width() / 2, tft.height() / 2 + 40);
}

void setup()
{
    // Serial port for debugging purposes
    Serial.begin(115200);

    // led for wifi
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, LOW);

    // set pin mode for interrupt
    pinMode(SENSOR_IN_1, INPUT_PULLUP);
    pinMode(SENSOR_IN_2, INPUT_PULLUP);
    pinMode(RESET_PIN, INPUT_PULLUP);
    pinMode(INCREASE_PIN, INPUT_PULLUP);
    pinMode(DECREASE_PIN, INPUT_PULLUP);

    // attach interrupt
    attachInterrupt(SENSOR_IN_1, ISR_SENSOR_1, FALLING); // sensor 1 falling-edge
    attachInterrupt(SENSOR_IN_2, ISR_SENSOR_2, RISING);  // sensor 2 rising-edge
    attachInterrupt(RESET_PIN, ISR_RESET, CHANGE);       // reset count
    attachInterrupt(INCREASE_PIN, ISR_INC, CHANGE);      // increase count
    attachInterrupt(DECREASE_PIN, ISR_DEC, CHANGE);      // decrease count

    // tft screen
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(mainBg);
    delay(1000);

    // file system
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

    if (!res)
    {
        Serial.println("Failed to connect");
        ESP.restart();
    }
    else
    {
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

        // draw words
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setFreeFont(FSBI24);
        tft.setTextDatum(TC_DATUM);
        tft.drawString("COUNT:", tft.width() / 2 - 3, 40);
    }
}

void loop()
{
    webSocket.loop();

    // reset if count is negative
    if (count < 0)
    {
        count = 0;
    }

    // update period
    unsigned long now = millis(); // read out the current "time" ("millis()" gives the time in ms since the Arduino started)
    if ((unsigned long)(now - previousMillis_send) > interval_send)
    { // check if "interval" ms has passed since last time the clients were updated
        sendJson("count", count);
        previousMillis_send = now; // reset previousMillis
    }

    if ((unsigned long)(now - previousMillis_tft) > interval_tft)
    { // check if "interval" ms has passed since last time the clients were updated
        updateTFT();
        previousMillis_tft = now; // reset previousMillis
    }
}