/**
 * @file WOL_bridge.ino
 * @brief An ESP32/ESP8266 project that acts as a Wake-on-LAN (WOL) bridge.
 *
 * This firmware allows an ESP device to send WOL magic packets to configured
 * devices on a local network. It can be triggered via Amazon Alexa, a Telegram bot,
 * or by receiving a WOL packet from an external network, which it then relays.
 *
 * @author Jules
 * @date 2025-08-09
 */
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

// User Configuration
#include "config.h"

// Constants
#define SERIAL_BAUDRATE 115200
#define WOL_UDP_PORT 9
#define MAGIC_PACKET_SIZE 102
#define TELEGRAM_POLL_INTERVAL 1000

// Alexa integration
#include "fauxmoESP.h"
fauxmoESP fauxmo;

// WOL (Wake On LAN) Setup
#include <WiFiUdp.h>
#include <WakeOnLan.h>
WiFiUDP Udp;
WakeOnLan WOL(Udp);

// Telegram Bot
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

IPAddress subnetMask;

#ifdef ESP8266
// For Telegram on ESP8266
BearSSL::WiFiClientSecure client;
#else
// For Telegram on ESP32
WiFiClientSecure client;
#endif

/// @brief The main object for interacting with the Telegram Bot API.
UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);

/// @brief Stores the last time Telegram messages were checked, for polling.
unsigned long lastTimeChecked;

/**
 * @brief Converts a MAC address string to a byte array.
 *
 * This function parses a string in the format "XX:XX:XX:XX:XX:XX" and converts
 * it into a 6-byte array. It performs strict validation to ensure the format is correct.
 *
 * @param macStr A C-style string containing the MAC address. Must be 17 characters long.
 * @param macBytes A pointer to a 6-byte array where the result will be stored.
 * @return true if the conversion is successful, false otherwise.
 */
bool macStringToBytes(const char* macStr, byte* macBytes) {
  if (macStr == nullptr || strlen(macStr) != 17) {
    return false;
  }

  char temp[3];
  temp[2] = '\0';
  char* end;

  for (int i = 0; i < 6; ++i) {
    // Check for the ':' separator between bytes
    if (i > 0) {
      if (*macStr != ':') return false;
      macStr++;
    }

    // Copy the two hex characters for the current byte
    memcpy(temp, macStr, 2);
    macStr += 2;

    // Convert hex string to a byte
    long val = strtol(temp, &end, 16);
    if (*end != '\0' || val < 0 || val > 255) {
      // This indicates that the characters were not valid hex, or the value is out of range
      return false;
    }
    macBytes[i] = (byte)val;
  }

  // After parsing 6 bytes, we should be at the end of the string
  return (*macStr == '\0');
}

/**
 * @brief Connects the ESP device to the configured Wi-Fi network.
 *
 * This function initializes the Wi-Fi connection in station mode and waits
 * until the connection is established. It prints the progress to the Serial monitor.
 */
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

/**
 * @brief Initializes the device and all services.
 *
 * This function runs once on startup. It sets up Serial communication,
 * connects to Wi-Fi, configures the Telegram bot, initializes the WOL service,
 * and sets up the Alexa (fauxmoESP) integration.
 */
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
    Serial.printf("  Adding device #%d: %s\n", i, devices[i].deviceName);
    fauxmo.addDevice(devices[i].deviceName);
  }

  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    Serial.printf("[ALEXA] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);
    for (int i = 0; i < numDevices; i++) {
      if (strcmp(device_name, devices[i].deviceName) == 0) {
        if (state) { // If Alexa says "ON"
          Serial.printf("[ALEXA] Sending WOL to %s (%s)\n", devices[i].deviceName, devices[i].mac);
          WOL.sendMagicPacket(devices[i].mac);
        } else { // If Alexa says "OFF" (does nothing for WOL)
          Serial.printf("[ALEXA] Received OFF for %s, no action taken.\n", devices[i].deviceName);
        }
        break; // Found the device, exit the inner loop
      }
    }
  });

  Serial.println("Setup finished.");
}

// Forward declarations
void handleNewMessages(int numNewMessages);
void processTelegramCallback(const telegramMessage& msg);
void processTelegramMessage(const telegramMessage& msg);
void handleUdpPacket();
void handleTelegram();


/**
 * @brief The main execution loop.
 *
 * This function is called repeatedly. It is responsible for polling the
 * Alexa, Telegram, and UDP handlers to process incoming events.
 */
void loop() {
  fauxmo.handle();
  handleTelegram();
  handleUdpPacket();
}

/**
 * @brief Checks for and handles new Telegram messages.
 *
 * This function polls the Telegram bot for new messages at a set interval.
 * If new messages are found, it calls handleNewMessages to process them.
 */
void handleTelegram() {
  if (millis() > lastTimeChecked + TELEGRAM_POLL_INTERVAL) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages > 0) {
       Serial.printf("[TELEGRAM] Received %d new messages\n", numNewMessages);
       handleNewMessages(numNewMessages);
    } else if (numNewMessages < 0) {
       Serial.println("[TELEGRAM] Error checking messages");
    }
    lastTimeChecked = millis();
  }
}

/**
 * @brief Listens for and processes incoming UDP packets for WOL functionality.
 *
 * This function acts as a WOL bridge. It listens for UDP packets on the WOL port.
 * If a valid Magic Packet is received from an external network for a configured device,
 * it re-broadcasts the WOL packet to the local network.
 * Packets originating from the local subnet are ignored to prevent loops.
 */
void handleUdpPacket() {
  int packetSize = Udp.parsePacket();
  if (packetSize == 0) {
    return; // No packet received
  }

  IPAddress remoteIp = Udp.remoteIP();

  // Ignore packets sent from this device itself
  if (remoteIp == WiFi.localIP()) {
    Udp.flush();
    return;
  }

  // --- WOL Bridge Logic ---
  // If the packet comes from within the same LAN, we assume the target device
  // already received it. We only act as a bridge for packets from outside.
  uint32_t localNetwork_u32 = (uint32_t)WiFi.localIP() & (uint32_t)subnetMask;
  uint32_t remoteNetwork_u32 = (uint32_t)remoteIp & (uint32_t)subnetMask;
  if (localNetwork_u32 == remoteNetwork_u32) {
    Udp.flush(); // Discard packet from local network
    return;
  }

  Serial.printf("[UDP] Received packet from external IP %s (Size: %d bytes)\n", remoteIp.toString().c_str(), packetSize);

  // A magic packet is exactly 102 bytes long.
  if (packetSize != MAGIC_PACKET_SIZE) {
    Serial.println("[UDP] Packet is not 102 bytes. Ignoring.");
    Udp.flush();
    return;
  }

  byte buffer[MAGIC_PACKET_SIZE];
  int len = Udp.read(buffer, MAGIC_PACKET_SIZE);

  if (len != MAGIC_PACKET_SIZE) {
    Serial.printf("[UDP] Error reading packet. Expected %d bytes, got %d.\n", MAGIC_PACKET_SIZE, len);
    return;
  }

  // -- Validate the Magic Packet structure --

  // 1. Check for the 6-byte synchronization stream (0xFF)
  bool isMagic = true;
  for (int i = 0; i < 6; ++i) {
    if (buffer[i] != 0xFF) {
      isMagic = false;
      break;
    }
  }

  // 2. Check that the MAC address is repeated 16 times
  if (isMagic) {
    byte* macStart = buffer + 6;
    for (int i = 1; i < 16; ++i) {
      if (memcmp(macStart, macStart + (i * 6), 6) != 0) {
        isMagic = false;
        break;
      }
    }
  }

  if (isMagic) {
    Serial.println("[UDP] Valid Magic Packet structure detected.");
    byte* receivedMac = buffer + 6; // The MAC is the 7th byte onwards

    // Check if the received MAC matches any of our configured devices
    for (int i = 0; i < numDevices; ++i) {
      byte deviceMac[6];
      if (macStringToBytes(devices[i].mac, deviceMac)) {
        if (memcmp(receivedMac, deviceMac, 6) == 0) {
          Serial.printf("[UDP] Packet is for configured device: %s\n", devices[i].deviceName);
          Serial.printf("[UDP] Re-broadcasting WOL for %s (%s)\n", devices[i].deviceName, devices[i].mac);
          WOL.sendMagicPacket(devices[i].mac);
          // Packet handled, no need to check other devices
          return;
        }
      }
    }
    Serial.println("[UDP] Magic Packet was for an unknown device.");

  } else {
    Serial.println("[UDP] Packet is not a valid Magic Packet.");
  }
}

/**
 * @brief Processes a batch of new messages from the Telegram bot.
 * @param numNewMessages The number of new messages to process.
 */
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    if (bot.messages[i].type == F("callback_query")) {
      processTelegramCallback(bot.messages[i]);
    } else if (bot.messages[i].type == F("message")) {
      processTelegramMessage(bot.messages[i]);
    }
  }
}

/**
 * @brief Handles incoming callback queries from Telegram inline keyboards.
 * @param msg The Telegram message object containing the callback query.
 */
void processTelegramCallback(const telegramMessage& msg) {
  String queryId = msg.query_id;
  String text = msg.text;
  String chat_id = String(msg.chat_id);

  Serial.printf("[TELEGRAM] Callback Query: text='%s', FromID='%s'\n", text.c_str(), chat_id.c_str());

  if (text.startsWith("WOL")) {
    text.replace("WOL", "");
    int index = text.toInt();
    if (index >= 0 && index < numDevices) {
      Serial.printf("[TELEGRAM] Sending WOL to: %s\n", devices[index].deviceName);
      WOL.sendMagicPacket(devices[index].mac);

      char reply[100];
      snprintf(reply, sizeof(reply), "WOL sent to %s", devices[index].deviceName);
      bot.answerCallbackQuery(queryId, reply, false);
    } else {
      Serial.printf("[TELEGRAM] Error: Invalid device index (%d)\n", index);
      bot.answerCallbackQuery(queryId, "Error: Invalid device", true);
    }
  } else {
    bot.answerCallbackQuery(queryId, "Action not recognized", true);
  }
}

/**
 * @brief Handles incoming text messages from Telegram.
 * @param msg The Telegram message object.
 */
void processTelegramMessage(const telegramMessage& msg) {
  String text = msg.text;
  String chat_id = String(msg.chat_id);
  Serial.printf("[TELEGRAM] Message: Text='%s', FromID='%s'\n", text.c_str(), chat_id.c_str());

  if (text == F("/wol")) {
    if (numDevices > 0) {
      // The UniversalTelegramBot library unfortunately relies heavily on String for this.
      // We will build the String here, but keep its scope local.
      String keyboardJson = "[";
      for (int i = 0; i < numDevices; i++) {
        keyboardJson += "[{ \"text\" : \"" + String(devices[i].deviceName) + "\", \"callback_data\" : \"WOL" + String(i) + "\" }]";
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
    const char* welcome = "Hello! Use /wol to send a Wake-on-LAN packet.\n"
                          "You can also control devices via Alexa if configured.\n"
                          "Receiving a Magic Packet via UDP on port 9 for a configured device will trigger sending WOL to that device.\n";
    bot.sendMessage(chat_id, welcome, "");
  }
}
