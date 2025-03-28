#include <Arduino.h>
#include <cstdio>  // For sscanf
#include <cstring> // For memcmp and strlen
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

// Wi-Fi credentials
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

IPAddress subnetMask;

#ifdef ESP8266
// For Telegram on ESP8266
BearSSL::WiFiClientSecure client;
#else
// For Telegram on ESP32
WiFiClientSecure client;
#endif

UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);

int delayBetweenChecks = 1000;
unsigned long lastTimeChecked;   // Last time the messages' scan has been done

// Helper function to convert a MAC string "XX:XX:XX:XX:XX:XX" to a 6-byte array
bool macStringToBytes(const char* macStr, byte* macBytes) {
  // Expected format: XX:XX:XX:XX:XX:XX (17 characters)
  if (macStr == nullptr || strlen(macStr) != 17) {
    return false; // Invalid string or incorrect length
  }
  int values[6];
  // Use sscanf to read the 6 hexadecimal values separated by ':'
  int result = sscanf(macStr, "%x:%x:%x:%x:%x:%x%*c", // %*c consumes potential extra characters at the end
                      &values[0], &values[1], &values[2],
                      &values[3], &values[4], &values[5]);

  if (result == 6) {
      // sscanf successfully read 6 values
      for (int i = 0; i < 6; ++i) {
        // Check if values are valid for a byte (0-255)
        if (values[i] < 0 || values[i] > 255) return false;
        macBytes[i] = (byte)values[i];
      }
      return true; // Conversion successful
  }
  return false; // Parsing failed
}

// Wi-Fi Connection
void wifiSetup() {
  WiFi.mode(WIFI_STA);
  Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500); // Slightly increased delay to avoid overloading
  }
  Serial.println();
  Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println("Booting...");

  wifiSetup();

  Serial.println("Setting up Telegram...");
#ifdef ESP8266
  // Necessary for ESP8266 to avoid issues with expired certificates
  // Set the clock via NTP. Important for SSL/TLS validation
  configTime(0, 0, "pool.ntp.org");
  client.setInsecure(); // If you have SSL problems, otherwise comment out. Consider the risks.
  // Safer alternative for ESP8266 (requires more memory):
  // client.setTrustAnchors(&cert); // Where 'cert' is the root certificate
#endif
  bot.longPoll = 60;

  Serial.println("Setting up WOL...");
  Udp.begin(9); // Listen on port 9 AND use port 9 as source for sending
  WOL.setRepeat(3, 100);
  subnetMask = WiFi.subnetMask();
  // WOL.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());
  WOL.calculateBroadcastAddress(WiFi.localIP(), subnetMask);
  // Serial.printf("WOL Broadcast address calculated: %s\n", WOL.getBroadcastAddress().toString().c_str()); // Print the calculated broadcast address

  Serial.println("Setting up Alexa (fauxmoESP)...");
  fauxmo.createServer(true);
  fauxmo.setPort(80);
  fauxmo.enable(true);

  Serial.printf("Adding %d devices to Alexa...\n", numDevices);
  for (int i = 0; i < numDevices; i++) {
    Serial.printf("  Adding device #%d: %s\n", i, devices[i].deviceName.c_str());
    fauxmo.addDevice(devices[i].deviceName.c_str());
  }

  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    Serial.printf("[ALEXA] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);
    for (int i = 0; i < numDevices; i++) {
      if (strcmp(device_name, devices[i].deviceName.c_str()) == 0) {
        if (state) { // If Alexa says "ON"
          Serial.printf("[ALEXA] Sending WOL to %s (%s)\n", devices[i].deviceName.c_str(), devices[i].mac.c_str());
          WOL.sendMagicPacket(devices[i].mac.c_str());
        } else { // If Alexa says "OFF" (does nothing for WOL)
          Serial.printf("[ALEXA] Received OFF for %s, no action taken.\n", devices[i].deviceName.c_str());
        }
        break; // Found the device, exit the inner loop
      }
    }
  });

  Serial.println("Setup finished.");
}

void loop() {

  // Handler for Alexa (fauxmoESP)
  fauxmo.handle();

  // Check Telegram messages
  if (millis() > lastTimeChecked + delayBetweenChecks) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages > 0) {
       Serial.printf("[TELEGRAM] Received %d new messages\n", numNewMessages);
       handleNewMessages(numNewMessages);
    } else if (numNewMessages < 0) {
       Serial.println("[TELEGRAM] Error checking messages"); // Added for debugging
    } else {
       // Optional: If you want to know when getUpdates returns 0 (no new messages)
       // Serial.println("[TELEGRAM] No new messages");
    }
    lastTimeChecked = millis();
  }

  int packetSize = Udp.parsePacket(); // Check if a UDP packet has arrived

  if (packetSize > 0) { // Packet received
    IPAddress remoteIp = Udp.remoteIP(); // Sender IP

    if (remoteIp == WiFi.localIP()) {
      Serial.printf("[UDP TRIGGER] Ignored packet from self (%s)\n", remoteIp.toString().c_str());
      Udp.flush();
    } else {

      // Mittente non è l'ESP stesso. Controlla se è locale PRIMA di validare.
      IPAddress localIp = WiFi.localIP();
      uint32_t localNetwork_u32 = (uint32_t)localIp & (uint32_t)subnetMask;
      uint32_t remoteNetwork_u32 = (uint32_t)remoteIp & (uint32_t)subnetMask;

      if (localNetwork_u32 == remoteNetwork_u32) {
        // Assumption: if magic packet is received from a device under the same LAN then it is already broadcasted to the real recipient
        Serial.printf("[UDP TRIGGER] Sender %s is on the local subnet. Assuming broadcast already sent, ignoring packet (size %d).\n",
                      remoteIp.toString().c_str(), packetSize);
        Udp.flush(); // Discard

      } else {
        // The sender is not itself, so can proceed in verification
        Serial.printf("[UDP TRIGGER] Received UDP packet from %s:%u (Size: %d bytes)\n",
                      remoteIp.toString().c_str(), Udp.remotePort(), packetSize);

        if (packetSize == 102) { // 1. Check the exact Magic Packet size
          byte udpBuffer[102];
          int len = Udp.read(udpBuffer, 102);

          if (len == 102) {
            // Read completed successfully
            bool isMagic = true;

            // 2. Check the first 6 Sync bytes (must be 0xFF)
            for (int i = 0; i < 6; i++) {
              if (udpBuffer[i] != 0xFF) {
                isMagic = false; break;
              }
            }

            // 3. Check the consistency of MAC repetition
            if (isMagic) {
              byte* firstMacBlock = udpBuffer + 6;
              for (int i = 1; i < 16; i++) {
                if (memcmp(firstMacBlock, udpBuffer + 6 + (i * 6), 6) != 0) {
                  isMagic = false; break;
                }
              }
            }

            // 4. If the structure is valid, EXTRACT the MAC and SEARCH in our array
            if (isMagic) {
              Serial.println("[UDP TRIGGER] Valid Magic Packet structure."); // Shortened message

              byte* receivedMacBytes = udpBuffer + 6; // Pointer to the MAC in the received packet
              bool deviceFoundAndSent = false;
              // Scan the configured devices array
              for (int j = 0; j < numDevices; j++) {
                byte deviceMacBytes[6];
                // Convert the MAC string of device j to a byte array
                if (macStringToBytes(devices[j].mac.c_str(), deviceMacBytes)) {
                  // Compare the MAC received in the packet with the MAC of device j
                  if (memcmp(receivedMacBytes, deviceMacBytes, 6) == 0) {
                    // MATCH FOUND!
                    Serial.printf("[UDP TRIGGER] Magic Packet targets known device: %s (%s)\n",
                                  devices[j].deviceName.c_str(), devices[j].mac.c_str());
                    Serial.printf("[UDP TRIGGER] Sending WOL packet to %s\n", devices[j].mac.c_str());
                    WOL.sendMagicPacket(devices[j].mac.c_str());
                    deviceFoundAndSent = true;
                    break; // Exit the for loop, the packet has been handled
                  }
                } else {
                    Serial.printf("[ERROR] Cannot parse MAC string for device %s: %s\n",
                                  devices[j].deviceName.c_str(), devices[j].mac.c_str());
                }
              } // End of for loop over devices

              if (!deviceFoundAndSent) {
                Serial.println("[UDP TRIGGER] Valid Magic Packet, but targets an unknown/unconfigured device.");
              }
            } else {
              Serial.println("[UDP TRIGGER] Packet is 102 bytes, but NOT a valid Magic Packet structure.");
            }
          } else {
            Serial.printf("[UDP TRIGGER] Error reading 102 byte packet (%d bytes read).\n", len);
          }
        } else {
          // Packet with incorrect size
            Serial.printf("[UDP TRIGGER] Packet from %s has incorrect size (%d bytes), ignored.\n", remoteIp.toString().c_str(), packetSize);
            Udp.flush(); // Discard
        }
      }
    } // End 'else' to check source IP

  } // End 'if (packetSize > 0)'

}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    // Log received message for debugging
    // Serial.printf("[TELEGRAM DEBUG] Msg Type: %s, Text: %s, From: %s, ChatID: %s\n", bot.messages[i].type.c_str(), bot.messages[i].text.c_str(), bot.messages[i].from_name.c_str(), String(bot.messages[i].chat_id).c_str());

    String chat_id = String(bot.messages[i].chat_id); // Get chat_id to reply

    if (bot.messages[i].type == F("callback_query")) {
      String queryId = bot.messages[i].query_id; // ID to answer the callback query
      String text = bot.messages[i].text;  // text associated with the pressed button (e.g., "WOL0")

      Serial.printf("[TELEGRAM] Callback Query: text='%s', FromID='%s'\n", text.c_str(), chat_id.c_str());

      if (text.startsWith("WOL")) {
        text.replace("WOL", "");
        int index = text.toInt();
        // Safety check on the index
        if (index >= 0 && index < numDevices) {
          Serial.printf("[TELEGRAM] Sending WOL to: %s\n", devices[index].deviceName.c_str());
          WOL.sendMagicPacket(devices[index].mac.c_str());
          // Answer the callback query to make the loading icon disappear on the Telegram client
          bot.answerCallbackQuery(queryId, "WOL sent to " + devices[index].deviceName, false); // false = do not show alert popup
        } else {
          Serial.printf("[TELEGRAM] Error: Invalid device index (%d)\n", index);
          bot.answerCallbackQuery(queryId, "Error: Invalid device", true); // true = show alert popup
        }
      } else {
        // Response for unhandled callbacks
        bot.answerCallbackQuery(queryId, "Action not recognized", true);
      }

    } else if (bot.messages[i].type == F("message")) { // Handling normal messages
      String text = bot.messages[i].text;
      Serial.printf("[TELEGRAM] Message: Text='%s', FromID='%s'\n", text.c_str(), chat_id.c_str());

      if (text == F("/wol")) {
        if (numDevices > 0) {
            String keyboardJson = "[";
            for (int i = 0; i < numDevices; i++) {
              keyboardJson += "[{ \"text\" : \"" + devices[i].deviceName + "\", \"callback_data\" : \"WOL" + String(i) + "\" }]";
              if (i + 1 < numDevices) {
                keyboardJson += ",";
              }
            }
            keyboardJson += "]";
            bot.sendMessageWithInlineKeyboard(chat_id, "Select the device to send WOL to:", "", keyboardJson);
        } else {
            bot.sendMessage(chat_id, "No device configured for WOL.", "");
        }
      } else if (text == F("/start")) {
        String welcome = "Hello! Use /wol to send a Wake-on-LAN packet.\n";
        welcome += "You can also control devices via Alexa if configured.\n";
        welcome += "Receiving a Magic Packet via UDP on port 9 for a configured device will trigger sending WOL to that device.\n";
        bot.sendMessage(chat_id, welcome, ""); 
      } else {
        // Response to unrecognized commands (optional)
        // bot.sendMessage(chat_id, "Command not recognized. Use /start or /wol.", "");
      }
    }
  }
}
