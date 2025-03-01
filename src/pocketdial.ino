#include <M5Dial.h> // Include the M5Dial library
#include <WiFi.h>   // Include the ESP32 WiFi library
#include <NimBLEDevice.h> // Include the NimBLE-Arduino library

// Define colors for the UI
#define BACKGROUND_COLOR TFT_LIGHTGREY // Grey background
#define TEXT_COLOR TFT_BLACK           // Black text
#define HIGHLIGHT_COLOR TFT_DARKGREY   // Highlight color for selected menu item

// Screen dimensions and safe area for circular display
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240
#define SAFE_RADIUS 110 // Safe radius to avoid clipping on circular edges
#define SAFE_WIDTH (SAFE_RADIUS * 2) // 220 pixels
#define SAFE_HEIGHT (SAFE_RADIUS * 2) // 220 pixels
#define SAFE_X_OFFSET ((SCREEN_WIDTH - SAFE_WIDTH) / 2) // 10 pixels
#define SAFE_Y_OFFSET ((SCREEN_HEIGHT - SAFE_HEIGHT) / 2) // 10 pixels

// Main menu items array (excluding battery, which will be handled separately)
const char* menuItems[] = {"Main", "BadUSB", "WiFi", "BLE", "Settings"};
const int numMainItems = sizeof(menuItems) / sizeof(menuItems[0]);
int selectedMainItem = 0; // Currently selected main menu item index (0 for battery, 1+ for menuItems)

// BadUSB submenu items (payload files)
const char* badUSBItems[] = {"Payload1", "Payload2", "Payload3", "Back"};
const int numBadUSBItems = sizeof(badUSBItems) / sizeof(badUSBItems[0]);
int selectedBadUSBItem = 0; // Currently selected BadUSB item index

// Settings submenu items
const char* settingsItems[] = {"Dial Sensitivity", "Back"};
const int numSettingsItems = sizeof(settingsItems) / sizeof(settingsItems[0]);
int selectedSettingsItem = 0; // Currently selected settings item index

// WiFi submenu state
bool inWiFiMenu = false; // Whether we're in the WiFi submenu
bool inPasswordMenu = false; // Whether we're in the password input menu
int wifiScanResults = 0; // Number of WiFi networks found
int selectedWiFiItem = 0; // Currently selected WiFi network index
int wifiDisplayOffset = 0; // Offset for scrolling display of WiFi networks
#define MAX_VISIBLE_WIFI_ITEMS 4 // Maximum number of WiFi items visible on screen at once
bool isConnected = false; // Track if we're connected to a WiFi network
String connectedSSID = ""; // Track the SSID of the connected network

// Password input state
String passwordInput = ""; // Current password being entered
int passwordCharIndex = 0; // Current character index being edited
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !@#$%^&*()-_=+"; // Available characters for password
int charSetLength = strlen(charSet);
int selectedCharIndex = 0; // Index of the currently selected character in the charset

// BLE submenu state
bool inBLEMenu = false; // Whether we're in the BLE submenu
int bleScanResults = 0; // Number of BLE devices found
int selectedBLEItem = 0; // Currently selected BLE device index
int bleDisplayOffset = 0; // Offset for scrolling display of BLE devices
#define MAX_VISIBLE_BLE_ITEMS 4 // Maximum number of BLE items visible on screen at once
std::vector<NimBLEAdvertisedDevice*> bleDevices; // Store discovered BLE devices

// Battery info screen state
bool inBatteryInfo = false; // Whether we're in the battery info screen
int selectedBatteryInfoItem = 0; // Selected item in battery info screen (0 for "Back")

// Settings submenu state
bool inSettingsMenu = false; // Whether we're in the settings submenu
bool inDialSensitivityMenu = false; // Whether we're in the dial sensitivity adjustment screen
float dialSensitivity = 1.0; // Sensitivity scaling factor (1.0 = normal, higher = less sensitive)
int tempDialSensitivity = 5; // Temporary value for adjustment (1 to 10 scale)
int selectedDialSensitivityItem = 0; // Selected item in dial sensitivity menu (0 for adjusting sensitivity, 1 for "Save")

// State flags
bool inMainMenu = true; // Whether we're in the main menu or a submenu
bool menuNeedsRedraw = true; // Flag to control when to redraw the menu

// Variables for encoder position tracking
long oldPosition = -999;
long lastSignificantChange = 0; // To debounce small changes based on sensitivity

// Battery percentage state with mock data
int batteryPercentage = 75; // Mock value for now (0-100%)
float batteryVoltage = 3.8;  // Mock value in volts (e.g., 3.0V to 4.2V typical for LiPo)
bool isCharging = false;    // Mock value for now
unsigned long lastBatteryUpdate = 0; // Timestamp of last battery update
const unsigned long batteryUpdateInterval = 10000; // Update every 10 seconds

void setup() {
  auto cfg = M5.config(); // Get default M5Stack configuration
  M5Dial.begin(cfg, true, false); // Initialize M5Dial with encoder enabled, RFID disabled

  // Initialize Serial for debugging
  Serial.begin(115200);
  Serial.println("M5Dial Started - Battery Info Debug");

  // Set up the display
  M5Dial.Display.setTextColor(TEXT_COLOR); // Set text color to black
  M5Dial.Display.setTextDatum(MC_DATUM);   // Center-align text for main content
  M5Dial.Display.setTextFont(2);           // Use smaller font for more readability in lists

  // Initialize WiFi in station mode for scanning
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Ensure WiFi is disconnected before scanning

  // Initialize NimBLE
  NimBLEDevice::init(""); // Initialize NimBLE with an empty device name

  // Initial battery info reading
  updateBatteryInfo();
}

void loop() {
  M5Dial.update(); // Update M5Dial state (needed for button and encoder)

  // Periodically update battery info
  unsigned long currentMillis = millis();
  if (currentMillis - lastBatteryUpdate >= batteryUpdateInterval) {
    updateBatteryInfo();
    lastBatteryUpdate = currentMillis;
    menuNeedsRedraw = true; // Redraw to update the percentage
  }

  // Handle rotary encoder for menu navigation
  long newPosition = M5Dial.Encoder.read();
  if (newPosition != oldPosition) {
    // Apply sensitivity scaling
    long positionChange = newPosition - oldPosition;
    float scaledChange = positionChange / dialSensitivity;
    int direction = scaledChange >= 1 ? 1 : (scaledChange <= -1 ? -1 : 0);

    if (direction != 0) {
      if (inMainMenu) {
        selectedMainItem = (selectedMainItem + direction + (numMainItems + 1)) % (numMainItems + 1);
      } else if (inSettingsMenu) {
        selectedSettingsItem = (selectedSettingsItem + direction + numSettingsItems) % numSettingsItems;
      } else if (inDialSensitivityMenu) {
        // Adjust either the sensitivity value or navigate to "Save"
        if (selectedDialSensitivityItem == 0) {
          selectedDialSensitivityItem = 1; // Move to "Save"
        } else {
          selectedDialSensitivityItem = 0; // Back to adjusting sensitivity
        }
        if (selectedDialSensitivityItem == 0) {
          tempDialSensitivity = constrain(tempDialSensitivity + direction, 1, 10);
        }
      } else if (inWiFiMenu) {
        int maxItems = (wifiScanResults == 0 && !isConnected) ? 1 : (isConnected ? wifiScanResults + 2 : wifiScanResults + 1);
        selectedWiFiItem = (selectedWiFiItem + direction + maxItems) % maxItems;
        if (selectedWiFiItem >= wifiDisplayOffset + MAX_VISIBLE_WIFI_ITEMS) {
          wifiDisplayOffset++;
        } else if (selectedWiFiItem < wifiDisplayOffset) {
          wifiDisplayOffset--;
        }
      } else if (inBLEMenu) {
        int maxItems = (bleScanResults == 0) ? 1 : bleScanResults;
        selectedBLEItem = (selectedBLEItem + direction + maxItems) % maxItems;
        if (selectedBLEItem >= bleDisplayOffset + MAX_VISIBLE_BLE_ITEMS) {
          bleDisplayOffset++;
        } else if (selectedBLEItem < bleDisplayOffset) {
          bleDisplayOffset--;
        }
      } else if (inPasswordMenu) {
        selectedCharIndex = (selectedCharIndex + direction + charSetLength) % charSetLength;
      } else if (inBatteryInfo) {
        selectedBatteryInfoItem = (selectedBatteryInfoItem + direction + 1) % 1;
      } else {
        selectedBadUSBItem = (selectedBadUSBItem + direction + numBadUSBItems) % numBadUSBItems;
      }
      oldPosition = newPosition;
      menuNeedsRedraw = true; // Mark menu for redraw
    }
  }

  // Handle button press for selection
  if (M5Dial.BtnA.wasPressed()) {
    if (inMainMenu) {
      executeMainMenuItem(selectedMainItem);
    } else if (inSettingsMenu) {
      executeSettingsMenuItem(selectedSettingsItem);
    } else if (inDialSensitivityMenu) {
      executeDialSensitivityMenuAction();
    } else if (inWiFiMenu) {
      executeWiFiMenuItem(selectedWiFiItem);
    } else if (inBLEMenu) {
      executeBLEMenuItem(selectedBLEItem);
    } else if (inPasswordMenu) {
      executePasswordMenuAction();
    } else if (inBatteryInfo) {
      executeBatteryInfoAction(selectedBatteryInfoItem);
    } else {
      executeBadUSBMenuItem(selectedBadUSBItem);
    }
    menuNeedsRedraw = true; // Redraw menu after action
  }

  // Redraw the menu if needed
  if (menuNeedsRedraw) {
    if (inMainMenu) {
      drawMainMenu();
    } else if (inSettingsMenu) {
      drawMenu(settingsItems, numSettingsItems, selectedSettingsItem);
    } else if (inDialSensitivityMenu) {
      drawDialSensitivityMenu();
    } else if (inWiFiMenu) {
      drawWiFiMenu();
    } else if (inBLEMenu) {
      drawBLEMenu();
    } else if (inPasswordMenu) {
      drawPasswordMenu();
    } else if (inBatteryInfo) {
      drawBatteryInfoScreen();
    } else {
      drawMenu(badUSBItems, numBadUSBItems, selectedBadUSBItem);
    }
    menuNeedsRedraw = false;
  }
}

void updateBatteryInfo() {
  // Attempt to read battery info with debug output
  int rawPercentage = M5.Power.getBatteryLevel();
  int rawVoltage = M5.Power.getBatteryVoltage(); // In millivolts
  bool rawCharging = M5.Power.isCharging();

  // Debug output to Serial
  Serial.print("Raw Battery Level (%): ");
  Serial.println(rawPercentage);
  Serial.print("Raw Battery Voltage (mV): ");
  Serial.println(rawVoltage);
  Serial.print("Raw Charging: ");
  Serial.println(rawCharging ? "Yes" : "No");

  // Use mock data since real readings are inaccurate
  batteryPercentage = 75; // Mock value
  batteryVoltage = 3800;  // Mock value (3.8V)
  isCharging = false;     // Mock value

  // If real readings become available, replace mock data here
  // Example for ADC reading (uncomment if you have hardware details):
  /*
  #define BATTERY_PIN 34 // Replace with actual ADC pin if known
  int rawValue = analogRead(BATTERY_PIN);
  float voltage = rawValue * (3.3 / 4095.0) * 2; // Assuming a 1:1 voltage divider
  batteryVoltage = voltage * 1000; // Convert to millivolts
  batteryPercentage = map(batteryVoltage, 3000, 4200, 0, 100); // Map 3.0V to 4.2V to 0-100%
  isCharging = false; // Replace with GPIO pin reading if applicable
  */
}

void drawMainMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  int usableHeight = SAFE_HEIGHT - 20; // Leave space for battery percentage
  int startY = SAFE_Y_OFFSET + 20;

  // Draw battery percentage as selectable item
  String batteryText = "Battery: " + String(batteryPercentage) + "%";
  if (selectedMainItem == 0) {
    M5Dial.Display.fillRect(SAFE_X_OFFSET, startY - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
  }
  M5Dial.Display.drawString(batteryText, SCREEN_WIDTH / 2, startY);

  // Draw main menu items below
  startY += 30; // Space below battery percentage
  for (int i = 0; i < numMainItems; i++) {
    int yPos = startY + (i * 25); // Reduced spacing to 25 pixels for better fit
    if (selectedMainItem == (i + 1)) { // +1 because battery is at index 0
      M5Dial.Display.fillRect(SAFE_X_OFFSET, yPos - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
    }
    M5Dial.Display.drawString(menuItems[i], SCREEN_WIDTH / 2, yPos);
  }
}

void drawMenu(const char* items[], int numItems, int selectedItem) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  int usableHeight = SAFE_HEIGHT - 20;
  int startY = SAFE_Y_OFFSET + (usableHeight - (numItems * 25)) / 2;
  for (int i = 0; i < numItems; i++) {
    int yPos = startY + (i * 25); // Reduced spacing to 25 pixels for better fit
    if (i == selectedItem) {
      M5Dial.Display.fillRect(SAFE_X_OFFSET, yPos - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
    }
    M5Dial.Display.drawString(items[i], SCREEN_WIDTH / 2, yPos);
  }
}

void drawDialSensitivityMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  int startY = SAFE_Y_OFFSET + 20;

  // Display current sensitivity value
  String sensitivityText = "Sensitivity: " + String(tempDialSensitivity);
  if (selectedDialSensitivityItem == 0) {
    M5Dial.Display.fillRect(SAFE_X_OFFSET, startY - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
  }
  M5Dial.Display.drawString(sensitivityText, SCREEN_WIDTH / 2, startY);

  // Display range hint
  M5Dial.Display.drawString("(1 low - 10 high)", SCREEN_WIDTH / 2, startY + 30);

  // "Save" option below
  if (selectedDialSensitivityItem == 1) {
    M5Dial.Display.fillRect(SAFE_X_OFFSET, startY + 60 - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
  }
  M5Dial.Display.drawString("Save", SCREEN_WIDTH / 2, startY + 60);
}

void drawBatteryInfoScreen() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  int startY = SAFE_Y_OFFSET + 20;

  // Display detailed battery info (mock data for now)
  String percentageText = "Level: " + String(batteryPercentage) + "%";
  M5Dial.Display.drawString(percentageText, SCREEN_WIDTH / 2, startY);

  String voltageText = "Voltage: " + String(batteryVoltage / 1000.0, 2) + "V";
  M5Dial.Display.drawString(voltageText, SCREEN_WIDTH / 2, startY + 30);

  String chargingText = "Charging: " + String(isCharging ? "Yes" : "No");
  M5Dial.Display.drawString(chargingText, SCREEN_WIDTH / 2, startY + 60);

  // Note about mock data
  M5Dial.Display.drawString("Note: Mock Data", SCREEN_WIDTH / 2, startY + 90);

  if (selectedBatteryInfoItem == 0) {
    M5Dial.Display.fillRect(SAFE_X_OFFSET, startY + 120 - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
  }
  M5Dial.Display.drawString("Back", SCREEN_WIDTH / 2, startY + 120);
}

void drawWiFiMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  if (wifiScanResults == 0 && !isConnected) {
    int startY = (SAFE_HEIGHT - 30) / 2 + SAFE_Y_OFFSET;
    if (selectedWiFiItem == 0) {
      M5Dial.Display.fillRect(SAFE_X_OFFSET, startY - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
    }
    M5Dial.Display.drawString("Back", SCREEN_WIDTH / 2, startY);
    return;
  }

  // When networks are found, include "Back" option (and "Disconnect" if connected)
  int maxItems = isConnected ? wifiScanResults + 2 : wifiScanResults + 1; // +1 for "Back", +2 if "Disconnect" also present
  int startIndex = wifiDisplayOffset;
  int endIndex = min(startIndex + MAX_VISIBLE_WIFI_ITEMS, maxItems);
  int usableHeight = SAFE_HEIGHT - 30;
  int startY = SAFE_Y_OFFSET + 20;

  for (int i = startIndex; i < endIndex; i++) {
    int yPos = startY + ((i - startIndex) * 25); // Reduced spacing to 25 pixels
    if (i == selectedWiFiItem) {
      M5Dial.Display.fillRect(SAFE_X_OFFSET, yPos - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
    }
    if (i < wifiScanResults) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String displayText = ssid + " (" + String(rssi) + " dBm)";
      if (ssid == connectedSSID) {
        displayText += " [Conn]";
      }
      if (M5Dial.Display.textWidth(displayText) > SAFE_WIDTH - 10) {
        displayText = displayText.substring(0, 20) + "...";
      }
      M5Dial.Display.drawString(displayText, SCREEN_WIDTH / 2, yPos);
    } else if (isConnected && i == wifiScanResults) {
      M5Dial.Display.drawString("Disconnect", SCREEN_WIDTH / 2, yPos);
    } else if (i == (isConnected ? wifiScanResults + 1 : wifiScanResults)) {
      M5Dial.Display.drawString("Back", SCREEN_WIDTH / 2, yPos);
    }
  }
}

void drawBLEMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  if (bleScanResults == 0) {
    int startY = (SAFE_HEIGHT - 30) / 2 + SAFE_Y_OFFSET;
    if (selectedBLEItem == 0) {
      M5Dial.Display.fillRect(SAFE_X_OFFSET, startY - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
    }
    M5Dial.Display.drawString("Back", SCREEN_WIDTH / 2, startY);
    return;
  }
  int startIndex = bleDisplayOffset;
  int endIndex = min(startIndex + MAX_VISIBLE_BLE_ITEMS, bleScanResults);
  int usableHeight = SAFE_HEIGHT - 30;
  int startY = SAFE_Y_OFFSET + 20;
  for (int i = startIndex; i < endIndex; i++) {
    int yPos = startY + ((i - startIndex) * 25);
    if (i == selectedBLEItem) {
      M5Dial.Display.fillRect(SAFE_X_OFFSET, yPos - 10, SAFE_WIDTH, 20, HIGHLIGHT_COLOR);
    }
    String deviceName = bleDevices[i]->getName().c_str();
    if (deviceName == "") {
      deviceName = "Unnamed (" + String(bleDevices[i]->getAddress().toString().c_str()) + ")";
    }
    int rssi = bleDevices[i]->getRSSI();
    String displayText = deviceName + " (" + String(rssi) + " dBm)";
    if (M5Dial.Display.textWidth(displayText) > SAFE_WIDTH - 10) {
      displayText = displayText.substring(0, 20) + "...";
    }
    M5Dial.Display.drawString(displayText, SCREEN_WIDTH / 2, yPos);
  }
}

void drawPasswordMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  int startY = SAFE_Y_OFFSET + 20;
  M5Dial.Display.drawString("Password:", SCREEN_WIDTH / 2, startY);
  String displayPassword = passwordInput;
  if (M5Dial.Display.textWidth(displayPassword) > SAFE_WIDTH - 10) {
    displayPassword = displayPassword.substring(0, 20) + "...";
  }
  M5Dial.Display.drawString(displayPassword, SCREEN_WIDTH / 2, startY + 30);
  String charDisplay = String(charSet[selectedCharIndex]);
  M5Dial.Display.drawString("Char: " + charDisplay, SCREEN_WIDTH / 2, startY + 60);
  M5Dial.Display.drawString("[Hold: Done]", SCREEN_WIDTH / 2, startY + 90);
  M5Dial.Display.drawString("[Press: Next Char]", SCREEN_WIDTH / 2, startY + 120);
}

void scanWiFiNetworks() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Scanning...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
  delay(500);
  wifiScanResults = WiFi.scanNetworks();
  selectedWiFiItem = 0;
  wifiDisplayOffset = 0;
  inWiFiMenu = true;
  inMainMenu = false;
  inBLEMenu = false;
}

void scanBLEDevices() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Scanning BLE...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
  delay(500);

  for (auto device : bleDevices) {
    delete device;
  }
  bleDevices.clear();
  bleScanResults = 0;

  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);

  bool scanSuccess = pScan->start(5, [](NimBLEScanResults results) {
    for (int i = 0; i < results.getCount(); i++) {
      NimBLEAdvertisedDevice* device = new NimBLEAdvertisedDevice(*results.getDevice(i));
      bleDevices.push_back(device);
    }
    bleScanResults = bleDevices.size();
  }, false);

  if (!scanSuccess) {
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("BLE Scan Failed", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    delay(2000);
    bleScanResults = 0;
  }
  selectedBLEItem = 0;
  bleDisplayOffset = 0;
  inBLEMenu = true;
  inMainMenu = false;
  inWiFiMenu = false;
}

void attemptWiFiConnection(String ssid, String password) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  int startY = SAFE_Y_OFFSET + 20;
  M5Dial.Display.drawString("Connecting to", SCREEN_WIDTH / 2, startY);
  String displaySSID = ssid;
  if (M5Dial.Display.textWidth(displaySSID) > SAFE_WIDTH - 10) {
    displaySSID = displaySSID.substring(0, 20) + "...";
  }
  M5Dial.Display.drawString(displaySSID, SCREEN_WIDTH / 2, startY + 20);
  M5Dial.Display.drawString("Please wait...", SCREEN_WIDTH / 2, startY + 40);
  WiFi.begin(ssid.c_str(), password.c_str());
  int timeout = 10000;
  int elapsed = 0;
  while (WiFi.status() != WL_CONNECTED && elapsed < timeout) {
    delay(500);
    elapsed += 500;
    M5Dial.Display.fillRect(SAFE_X_OFFSET, startY + 50, SAFE_WIDTH, 20, BACKGROUND_COLOR);
    M5Dial.Display.drawString("Attempt " + String(elapsed / 1000) + "s", SCREEN_WIDTH / 2, startY + 50);
  }
  if (WiFi.status() == WL_CONNECTED) {
    isConnected = true;
    connectedSSID = ssid;
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("Connected!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    delay(2000);
  } else {
    isConnected = false;
    connectedSSID = "";
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("Failed to connect", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    delay(2000);
  }
}

void disconnectWiFi() {
  WiFi.disconnect();
  isConnected = false;
  connectedSSID = "";
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Disconnected", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
  delay(2000);
}

void executeMainMenuItem(int item) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  if (item == 0) { // Battery percentage selected
    inMainMenu = false;
    inBatteryInfo = true;
    selectedBatteryInfoItem = 0; // Start with "Back" selected
    return;
  }
  int menuIndex = item - 1;
  switch (menuIndex) {
    case 0: // Main
      M5Dial.Display.drawString("Main Selected", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
      delay(1000);
      break;
    case 1: // BadUSB
      inMainMenu = false;
      inWiFiMenu = false;
      inBLEMenu = false;
      inPasswordMenu = false;
      inBatteryInfo = false;
      inSettingsMenu = false;
      inDialSensitivityMenu = false;
      selectedBadUSBItem = 0;
      break;
    case 2: // WiFi
      scanWiFiNetworks();
      break;
    case 3: // BLE
      scanBLEDevices();
      break;
    case 4: // Settings
      inMainMenu = false;
      inSettingsMenu = true;
      selectedSettingsItem = 0;
      break;
  }
}

void executeSettingsMenuItem(int item) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  switch (item) {
    case 0: // Dial Sensitivity
      inSettingsMenu = false;
      inDialSensitivityMenu = true;
      tempDialSensitivity = (int)(dialSensitivity * 2); // Approximate mapping for display
      if (tempDialSensitivity < 1) tempDialSensitivity = 1;
      if (tempDialSensitivity > 10) tempDialSensitivity = 10;
      selectedDialSensitivityItem = 0; // Start with adjusting sensitivity
      break;
    case 1: // Back
      inSettingsMenu = false;
      inMainMenu = true;
      selectedMainItem = 5; // Select "Settings" in main menu
      break;
  }
}

void executeDialSensitivityMenuAction() {
  if (selectedDialSensitivityItem == 1) { // "Save" selected
    // Map tempDialSensitivity (1-10) to dialSensitivity (0.5-5.0)
    dialSensitivity = tempDialSensitivity / 2.0;
    if (dialSensitivity < 0.5) dialSensitivity = 0.5;
    if (dialSensitivity > 5.0) dialSensitivity = 5.0;
    inDialSensitivityMenu = false;
    inSettingsMenu = true;
    selectedSettingsItem = 0; // Return to "Dial Sensitivity" in settings menu
  }
}

void executeBatteryInfoAction(int item) {
  if (item == 0) { // "Back" selected
    inBatteryInfo = false;
    inMainMenu = true;
    selectedMainItem = 0; // Return to battery percentage selection
  }
}

void executeWiFiMenuItem(int item) {
  if (wifiScanResults == 0 && !isConnected) {
    // If no networks found and not connected, return to main menu
    inWiFiMenu = false;
    inPasswordMenu = false;
    inMainMenu = true;
    inBLEMenu = false;
    inBatteryInfo = false;
    inSettingsMenu = false;
    inDialSensitivityMenu = false;
    selectedMainItem = 3;
    return;
  }

  int backIndex = isConnected ? wifiScanResults + 1 : wifiScanResults;
  if (item == backIndex) {
    // "Back" selected
    inWiFiMenu = false;
    inPasswordMenu = false;
    inMainMenu = true;
    inBLEMenu = false;
    inBatteryInfo = false;
    inSettingsMenu = false;
    inDialSensitivityMenu = false;
    selectedMainItem = 3; // Return to "WiFi" in main menu
    return;
  }

  if (item < wifiScanResults) {
    passwordInput = "";
    passwordCharIndex = 0;
    selectedCharIndex = 0;
    inWiFiMenu = false;
    inPasswordMenu = true;
    inBLEMenu = false;
    inBatteryInfo = false;
    inSettingsMenu = false;
    inDialSensitivityMenu = false;
  } else if (isConnected && item == wifiScanResults) {
    disconnectWiFi();
    inWiFiMenu = true;
    inPasswordMenu = false;
    inMainMenu = false;
    inBLEMenu = false;
    inBatteryInfo = false;
    inSettingsMenu = false;
    inDialSensitivityMenu = false;
  }
}

void executeBLEMenuItem(int item) {
  if (bleScanResults == 0) {
    inBLEMenu = false;
    inMainMenu = true;
    inWiFiMenu = false;
    inPasswordMenu = false;
    inBatteryInfo = false;
    inSettingsMenu = false;
    inDialSensitivityMenu = false;
    selectedMainItem = 4;
    return;
  }
  if (item < bleScanResults) {
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    int startY = SAFE_Y_OFFSET + 20;
    String deviceName = bleDevices[item]->getName().c_str();
    if (deviceName == "") {
      deviceName = "Unnamed";
    }
    String address = bleDevices[item]->getAddress().toString().c_str();
    int rssi = bleDevices[item]->getRSSI();
    if (M5Dial.Display.textWidth(deviceName) > SAFE_WIDTH - 10) {
      deviceName = deviceName.substring(0, 20) + "...";
    }
    if (M5Dial.Display.textWidth(address) > SAFE_WIDTH - 10) {
      address = address.substring(0, 20) + "...";
    }
    M5Dial.Display.drawString("Name: " + deviceName, SCREEN_WIDTH / 2, startY);
    M5Dial.Display.drawString("Addr: " + address, SCREEN_WIDTH / 2, startY + 30);
    M5Dial.Display.drawString("RSSI: " + String(rssi) + " dBm", SCREEN_WIDTH / 2, startY + 60);
    M5Dial.Display.drawString("Connect? (Not Impl)", SCREEN_WIDTH / 2, startY + 90);
    delay(3000);
  }
  inBLEMenu = false;
  inMainMenu = true;
  inWiFiMenu = false;
  inPasswordMenu = false;
  inBatteryInfo = false;
  inSettingsMenu = false;
  inDialSensitivityMenu = false;
  selectedMainItem = 4;
}

void executePasswordMenuAction() {
  if (M5Dial.BtnA.pressedFor(1000)) {
    String ssid = WiFi.SSID(selectedWiFiItem);
    attemptWiFiConnection(ssid, passwordInput);
    inPasswordMenu = false;
    inWiFiMenu = true;
    inMainMenu = false;
    inBLEMenu = false;
    inBatteryInfo = false;
    inSettingsMenu = false;
    inDialSensitivityMenu = false;
  } else {
    passwordInput += charSet[selectedCharIndex];
    passwordCharIndex++;
    selectedCharIndex = 0;
  }
}

void executeBadUSBMenuItem(int item) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  switch (item) {
    case 0: executePayload1(); break;
    case 1: executePayload2(); break;
    case 2: executePayload3(); break;
    case 3:
      inMainMenu = true;
      inWiFiMenu = false;
      inBLEMenu = false;
      inPasswordMenu = false;
      inBatteryInfo = false;
      inSettingsMenu = false;
      inDialSensitivityMenu = false;
      selectedMainItem = 2;
      break;
  }
}

void executePayload1() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.setTextSize(1);
  int startY = SAFE_Y_OFFSET + 20;
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY);
  M5Dial.Display.println("Payload1 Running...");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 20);
  M5Dial.Display.println("1. Open Notepad");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 40);
  M5Dial.Display.println("2. Type 'Hello World!'");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 60);
  M5Dial.Display.println("3. Press Enter");
  delay(2000);
}

void executePayload2() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.setTextSize(1);
  int startY = SAFE_Y_OFFSET + 20;
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY);
  M5Dial.Display.println("Payload2 Running...");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 20);
  M5Dial.Display.println("1. Open CMD");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 40);
  M5Dial.Display.println("2. Type 'echo Hello'");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 60);
  M5Dial.Display.println("3. Press Enter");
  delay(2000);
}

void executePayload3() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.setTextSize(1);
  int startY = SAFE_Y_OFFSET + 20;
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY);
  M5Dial.Display.println("Payload3 Running...");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 20);
  M5Dial.Display.println("1. Open Browser");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 40);
  M5Dial.Display.println("2. Go to 'example.com'");
  M5Dial.Display.setCursor(SAFE_X_OFFSET + 5, startY + 60);
  M5Dial.Display.println("3. Press Enter");
  delay(2000);
}
