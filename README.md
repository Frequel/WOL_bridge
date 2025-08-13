# ESP32/ESP8266 Wake-on-LAN (WOL) Bridge

This project transforms an ESP32 or ESP8266 into a versatile Wake-on-LAN (WOL) bridge, allowing you to wake up network devices remotely. It can be controlled via Amazon Alexa, a Telegram bot, or by relaying WOL packets from an external network.

## Features
- **Multiple Control Methods:** Wake devices using Alexa voice commands ("Alexa, turn on My PC") or a user-friendly Telegram bot.
- **WOL Bridge/Relay:** Forwards WOL packets received from external networks to your local LAN. This is useful for waking devices when you are not on the same network (e.g., over a VPN).
- **Easy Configuration:** All settings (Wi-Fi, Telegram token, devices) are centralized in a single `config.h` file.
- **Multi-Device Support:** Configure and control multiple target devices.
- **ESP32 & ESP8266 Compatible:** Works on both popular microcontrollers.

## How it Works
The device operates in three main ways:
1.  **Alexa Integration:** It uses the `fauxmoESP` library to emulate Philips Hue devices on your network. When you ask Alexa to turn on a device, the ESP catches this command and sends a WOL magic packet to the corresponding MAC address.
2.  **Telegram Bot:** It runs a simple Telegram bot that listens for commands. Sending `/wol` presents you with an inline keyboard of configured devices. Tapping a device name sends the WOL packet.
3.  **WOL Bridge:** The device listens on the standard WOL UDP port (9). If it receives a magic packet from an IP address *outside* its own subnet, it validates the packet and re-broadcasts it to the local network. This allows WOL requests to traverse routers, which typically don't forward broadcast packets. To prevent packet loops, it ignores packets that originate from within its own subnet.

## Hardware Requirements
- ESP32 or ESP8266 module.
- Local network access (Wi-Fi).
- An Alexa-enabled device (e.g., Amazon Echo) for voice control (optional).
- A smartphone with Telegram for bot control (optional).

## Libraries
This project requires the following libraries. You can install them using the Arduino Library Manager or PlatformIO.
- **[WakeOnLan](https://github.com/a7md0/WakeOnLan)** by a7md0
- **[fauxmoESP](https://github.com/vintlabs/fauxmoESP)** by vintlabs
- **[UniversalTelegramBot](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot)** by witnessmenow
- **ArduinoJson** by bblanchon

For PlatformIO, you can add the following to your `platformio.ini`:
```ini
lib_deps =
  a7md0/WakeOnLan
  vintlabs/fauxmoESP
  witnessmenow/UniversalTelegramBot
  bblanchon/ArduinoJson
```

## Setup and Configuration
All configuration is done in the `config.h` file.

1.  **Wi-Fi Credentials:** Set your network's SSID and password.
    ```cpp
    #define WIFI_SSID "Your_WiFi_SSID"
    #define WIFI_PASS "Your_WiFi_Password"
    ```
2.  **Telegram Bot Token:** If you want to use the Telegram bot, create a bot using [BotFather](https://core.telegram.org/bots#botfather) and paste the token here.
    ```cpp
    #define TELEGRAM_BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"
    ```
3.  **Target Devices:** Add the devices you want to control to the `devices` array. The `deviceName` is used for both Alexa and the Telegram bot.
    ```cpp
    targetDevice devices[] = {
      {"00:11:22:33:44:55", "My PC"},
      {"AA:BB:CC:DD:EE:FF", "Laptop"},
      // Add more devices here
    };
    ```

## Installation
1.  **Install Libraries:** Install all the required libraries listed above.
2.  **Configure:** Edit the `config.h` file with your details.
3.  **Flash:** Upload the code to your ESP32 or ESP8266.
4.  **Alexa Discovery:** Open the Alexa app and ask it to discover new devices. Your configured device names should appear.
5.  **Telegram Bot:** Send the `/start` command to your bot in Telegram to begin.

## Documentation
The source code is commented using Doxygen-style comments. You can generate a full set of technical documentation by running Doxygen in the project's root directory.
```bash
# Make sure you are in the project root directory
doxygen docs/Doxyfile
```
This will generate HTML documentation in the `docs/html` folder.

## Troubleshooting
- **Device Not Connecting to Wi-Fi:** Double-check your SSID and password in `config.h`. Check the Serial Monitor for connection error messages.
- **Alexa Cannot Discover Devices:** Ensure your Alexa device and the ESP are on the same Wi-Fi network. Make sure no other service is using port 80 on your network.
- **Telegram Bot Not Responding:** Verify your `TELEGRAM_BOT_TOKEN` is correct. For ESP8266, you may need to uncomment `client.setInsecure()` in `WOL_bridge.ino` if you are having SSL/TLS certificate issues, but be aware of the security implications.
- **WOL Not Working:**
    - Confirm that Wake-on-LAN is enabled in the BIOS/UEFI and the network adapter settings of the target computer.
    - Verify the MAC address in `config.h` is correct and in the format `XX:XX:XX:XX:XX:XX`.
    - Ensure the target device is connected to the network via an Ethernet cable (WOL over Wi-Fi is unreliable and not universally supported).
