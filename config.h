#ifndef CONFIG_H
#define CONFIG_H

// =================================================================
// ====================    USER CONFIGURATION   ====================
// =================================================================

// -- Wi-Fi Credentials --
#define WIFI_SSID "Your_WiFi_SSID"
#define WIFI_PASS "Your_WiFi_Password"

// -- Telegram Bot --
#define TELEGRAM_BOT_TOKEN "YOUR_TELEGRAM_BOT_TOKEN"

// -- Target Devices --
// Structure to store device details.
// MAC addresses should be in "XX:XX:XX:XX:XX:XX" format.
// The deviceName is used for Alexa and the Telegram Bot.
struct targetDevice {
  const char* mac;        // The target's MAC address (as a string)
  const char* deviceName; // The device name for Alexa and Telegram
};

// Add or remove devices from this list
// EXAMPLE:
// targetDevice devices[] = {
//   {"00:11:22:33:44:55", "My PC"},
//   {"AA:BB:CC:DD:EE:FF", "Laptop"},
// };
targetDevice devices[] = {
  {"00:00:00:00:00:00", "Device_1"}, // Replace with real MAC and device name
  {"11:11:11:11:11:11", "Device_2"}, // Add more devices here
};

// =================================================================
// =================== END OF USER CONFIGURATION ===================
// =================================================================

// Automatically calculate the number of configured devices
const int numDevices = sizeof(devices) / sizeof(devices[0]);

#endif // CONFIG_H
