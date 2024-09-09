#include <Arduino.h>
#ifdef ESP32
  #include <WiFi.h>
  #define RF_RECEIVER 13
  #define RELAY_PIN_1 12
  #define RELAY_PIN_2 14
#else
  #include <ESP8266WiFi.h>
  #define RF_RECEIVER 5
  #define RELAY_PIN_1 4
  #define RELAY_PIN_2 14
#endif

#define SERIAL_BAUDRATE 115200

// Alexa integration
#include "fauxmoESP.h"
fauxmoESP fauxmo;

//// WOL (Wake On LAN) Setup /////
#include <WiFiUdp.h>
#include <WakeOnLan.h>

WiFiUDP Udp;
WakeOnLan WOL(Udp); // Pass WiFiUDP class

// Telegram Bot
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#define TELEGRAM_BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"

// Wi-Fi credentials (replace with your own)
#define WIFI_SSID "Your_WiFi_SSID"
#define WIFI_PASS "Your_WiFi_Password"

// Structure to store device details
struct targetDevice {
  String mac;        // The target's MAC address
  String deviceName; // The device name
};

// Add or remove devices from this list
targetDevice devices[] = {
  {"00:00:00:00:00:00", "Device_1"}, // Replace with real MAC and device name
  {"11:11:11:11:11:11", "Device_2"}, // Add more devices here
};

int numDevices = sizeof(devices) / sizeof(devices[0]); // Automatically calculate number of devices

WiFiClientSecure client;
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);

int delayBetweenChecks = 1000;
unsigned long lastTimeChecked;   // Last time the messages' scan has been done

// Wi-Fi Connection
void wifiSetup() {
  // Set WIFI module to STA mode
  WiFi.mode(WIFI_STA);

  // Connect
  Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  // Wait
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  // Connected!
  Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  
  // Connect to Wi-Fi
  wifiSetup();

  //// Telegram setup ////
  client.setInsecure();  
  bot.longPoll = 60; // Increase Telegram bot's response time
  // longPoll keeps the request to Telegram open for the given amount of seconds if there are no messages
  // This hugely improves response time of the bot, but is only really suitable for projects
  // where the the initial interaction comes from Telegram as the requests will block the loop for
  // the length of the long poll
  ////////////////////////

  //// WOL Setup ////
  Udp.begin(9);
  WOL.setRepeat(3, 100); // Repeat packet 3 times with 100ms delay
  WOL.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());
  //////////////////

  //// Alexa Setup ////
  // By default, fauxmoESP creates it's own webserver on the defined port
  // The TCP port must be 80 for gen3 devices (default is 1901)
  // This has to be done before the call to enable()
  fauxmo.createServer(true);
  fauxmo.setPort(80);
  fauxmo.enable(true);

  // Add devices to Alexa
  for (int i = 0; i < numDevices; i++) {
    fauxmo.addDevice(devices[i].deviceName.c_str());
  }

  // Callback when a command from Alexa is received. 
    // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
    // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
    // Just remember not to delay too much here, this is a callback, exit as soon as possible.
    // If you have to do something more involved here set a flag and process it in your main loop.
  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    Serial.printf("[ALEXA] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);

    for (int i = 0; i < numDevices; i++) {
      if (strcmp(device_name, devices[i].deviceName.c_str()) == 0) {
        if (state) {
          WOL.sendMagicPacket(devices[i].mac.c_str());
          Serial.printf("[ALEXA] WOL sent to %s\n", devices[i].deviceName.c_str());
        } else {
          Serial.printf("[ALEXA] %s already ON\n", devices[i].deviceName.c_str());
        }
      }
    }
  });
  ///////////////////////
}

void loop() {
  //// Alexa ////
  // fauxmoESP uses an async TCP server but a sync UDP server
  // Therefore, we have to manually poll for UDP packets
  fauxmo.handle();
  ///////////////

  //// Telegram Bot Loop ////
  if (millis() > lastTimeChecked + delayBetweenChecks) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages) {
      handleNewMessages(numNewMessages);
    }
    lastTimeChecked = millis();
  }
  //////////////////////////
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    if (bot.messages[i].type == F("callback_query")) {
      String text = bot.messages[i].text;
      if (text.startsWith("WOL")) {
        text.replace("WOL", "");
        int index = text.toInt();
        Serial.printf("[TELEGRAM] Sending WOL to: %s\n", devices[index].deviceName.c_str());
        WOL.sendMagicPacket(devices[index].mac.c_str());
      }
    } else {
      String chat_id = String(bot.messages[i].chat_id);
      String text = bot.messages[i].text;

      if (text == F("/wol")) {
        // Keyboard Json is an array of arrays.
        // The size of the main array is how many row options the keyboard has
        // The size of the sub arrays is how many coloums that row has

        // "The Text" property is what shows up in the keyboard
        // The "callback_data" property is the text that gets sent when pressed
        String keyboardJson = "[";
        for (int i = 0; i < numDevices; i++) {
          keyboardJson += "[{ \"text\" : \"" + devices[i].deviceName + "\", \"callback_data\" : \"WOL" + String(i) + "\" }]";
          if (i + 1 < numDevices) {
            keyboardJson += ",";
          }
        }
        keyboardJson += "]";
        bot.sendMessageWithInlineKeyboard(chat_id, "Send WOL to the following devices:", "", keyboardJson);
      }

      if (text == F("/start")) {
        bot.sendMessage(chat_id, "/wol : Returns list of devices to send WOL to", "Markdown");
      }
    }
  }
}
