#include <M5Dial.h> // Include the M5Dial library
#include <WiFi.h>   // Include the ESP32 WiFi library
#include <NimBLEDevice.h> // Include the NimBLE-Arduino library

// Define colors for the UI
#define BACKGROUND_COLOR TFT_LIGHTGREY // Grey background
#define TEXT_COLOR TFT_BLACK           // Black text
#define HIGHLIGHT_COLOR TFT_DARKGREY   // Highlight color for selected menu item

// Main menu items array
const char* menuItems[] = {"Main", "BadUSB", "WiFi", "BLE", "Settings"};
const int numMainItems = sizeof(menuItems) / sizeof(menuItems[0]);
int selectedMainItem = 0; // Currently selected main menu item index

// BadUSB submenu items (payload files)
const char* badUSBItems[] = {"Payload1", "Payload2", "Payload3", "Back"};
const int numBadUSBItems = sizeof(badUSBItems) / sizeof(badUSBItems[0]);
int selectedBadUSBItem = 0; // Currently selected BadUSB item index

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

// State flags
bool inMainMenu = true; // Whether we're in the main menu or a submenu
bool menuNeedsRedraw = true; // Flag to control when to redraw the menu

// Variables for encoder position tracking
long oldPosition = -999;

void setup() {
  auto cfg = M5.config(); // Get default M5Stack configuration
  M5Dial.begin(cfg, true, false); // Initialize M5Dial with encoder enabled, RFID disabled

  // Set up the display
  M5Dial.Display.setTextColor(TEXT_COLOR); // Set text color to black
  M5Dial.Display.setTextDatum(MC_DATUM);   // Center-align text
  M5Dial.Display.setTextFont(2);           // Use smaller font for more readability in lists

  // Initialize WiFi in station mode for scanning
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Ensure WiFi is disconnected before scanning

  // Initialize NimBLE
  NimBLEDevice::init(""); // Initialize NimBLE with an empty device name
}

void loop() {
  M5Dial.update(); // Update M5Dial state (needed for button and encoder)

  // Handle rotary encoder for menu navigation
  long newPosition = M5Dial.Encoder.read();
  if (newPosition != oldPosition) {
    if (newPosition > oldPosition) {
      // Rotate clockwise: move down the menu
      if (inMainMenu) {
        selectedMainItem = (selectedMainItem + 1) % numMainItems;
      } else if (inWiFiMenu) {
        int maxItems = isConnected ? wifiScanResults + 1 : wifiScanResults;
        selectedWiFiItem = (selectedWiFiItem + 1) % maxItems;
        if (selectedWiFiItem >= wifiDisplayOffset + MAX_VISIBLE_WIFI_ITEMS) {
          wifiDisplayOffset++;
        }
      } else if (inBLEMenu) {
        selectedBLEItem = (selectedBLEItem + 1) % bleScanResults;
        if (selectedBLEItem >= bleDisplayOffset + MAX_VISIBLE_BLE_ITEMS) {
          bleDisplayOffset++;
        }
      } else if (inPasswordMenu) {
        selectedCharIndex = (selectedCharIndex + 1) % charSetLength;
      } else {
        selectedBadUSBItem = (selectedBadUSBItem + 1) % numBadUSBItems;
      }
    } else {
      // Rotate counterclockwise: move up the menu
      if (inMainMenu) {
        selectedMainItem = (selectedMainItem - 1 + numMainItems) % numMainItems;
      } else if (inWiFiMenu) {
        int maxItems = isConnected ? wifiScanResults + 1 : wifiScanResults;
        selectedWiFiItem = (selectedWiFiItem - 1 + maxItems) % maxItems;
        if (selectedWiFiItem < wifiDisplayOffset) {
          wifiDisplayOffset--;
        }
      } else if (inBLEMenu) {
        selectedBLEItem = (selectedBLEItem - 1 + bleScanResults) % bleScanResults;
        if (selectedBLEItem < bleDisplayOffset) {
          bleDisplayOffset--;
        }
      } else if (inPasswordMenu) {
        selectedCharIndex = (selectedCharIndex - 1 + charSetLength) % charSetLength;
      } else {
        selectedBadUSBItem = (selectedBadUSBItem - 1 + numBadUSBItems) % numBadUSBItems;
      }
    }
    oldPosition = newPosition;
    menuNeedsRedraw = true; // Mark menu for redraw
  }

  // Handle button press for selection
  if (M5Dial.BtnA.wasPressed()) {
    if (inMainMenu) {
      executeMainMenuItem(selectedMainItem);
    } else if (inWiFiMenu) {
      executeWiFiMenuItem(selectedWiFiItem);
    } else if (inBLEMenu) {
      executeBLEMenuItem(selectedBLEItem);
    } else if (inPasswordMenu) {
      executePasswordMenuAction();
    } else {
      executeBadUSBMenuItem(selectedBadUSBItem);
    }
    menuNeedsRedraw = true; // Redraw menu after action
  }

  // Redraw the menu if needed
  if (menuNeedsRedraw) {
    if (inMainMenu) {
      drawMenu(menuItems, numMainItems, selectedMainItem);
    } else if (inWiFiMenu) {
      drawWiFiMenu();
    } else if (inBLEMenu) {
      drawBLEMenu();
    } else if (inPasswordMenu) {
      drawPasswordMenu();
    } else {
      drawMenu(badUSBItems, numBadUSBItems, selectedBadUSBItem);
    }
    menuNeedsRedraw = false;
  }
}

void drawMenu(const char* items[], int numItems, int selectedItem) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  int startY = (M5Dial.Display.height() - (numItems * 30)) / 2;
  for (int i = 0; i < numItems; i++) {
    int yPos = startY + (i * 30);
    if (i == selectedItem) {
      M5Dial.Display.fillRect(0, yPos - 10, M5Dial.Display.width(), 20, HIGHLIGHT_COLOR);
    }
    M5Dial.Display.drawString(items[i], M5Dial.Display.width() / 2, yPos);
  }
}

void drawWiFiMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  if (wifiScanResults == 0 && !isConnected) {
    M5Dial.Display.drawString("No Networks", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    return;
  }
  int maxItems = isConnected ? wifiScanResults + 1 : wifiScanResults;
  int startIndex = wifiDisplayOffset;
  int endIndex = min(startIndex + MAX_VISIBLE_WIFI_ITEMS, maxItems);
  for (int i = startIndex; i < endIndex; i++) {
    int yPos = (i - startIndex) * 30 + 30;
    if (i == selectedWiFiItem) {
      M5Dial.Display.fillRect(0, yPos - 10, M5Dial.Display.width(), 20, HIGHLIGHT_COLOR);
    }
    if (i < wifiScanResults) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String displayText = ssid + " (" + String(rssi) + " dBm)";
      if (ssid == connectedSSID) {
        displayText += " [Conn]";
      }
      M5Dial.Display.drawString(displayText, M5Dial.Display.width() / 2, yPos);
    } else if (isConnected && i == wifiScanResults) {
      M5Dial.Display.drawString("Disconnect", M5Dial.Display.width() / 2, yPos);
    }
  }
}

void drawBLEMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  if (bleScanResults == 0) {
    M5Dial.Display.drawString("No Devices", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    return;
  }
  int startIndex = bleDisplayOffset;
  int endIndex = min(startIndex + MAX_VISIBLE_BLE_ITEMS, bleScanResults);
  for (int i = startIndex; i < endIndex; i++) {
    int yPos = (i - startIndex) * 30 + 30;
    if (i == selectedBLEItem) {
      M5Dial.Display.fillRect(0, yPos - 10, M5Dial.Display.width(), 20, HIGHLIGHT_COLOR);
    }
    String deviceName = bleDevices[i]->getName().c_str();
    if (deviceName == "") {
      deviceName = "Unnamed (" + String(bleDevices[i]->getAddress().toString().c_str()) + ")";
    }
    int rssi = bleDevices[i]->getRSSI();
    String displayText = deviceName + " (" + String(rssi) + " dBm)";
    M5Dial.Display.drawString(displayText, M5Dial.Display.width() / 2, yPos);
  }
}

void drawPasswordMenu() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Password:", M5Dial.Display.width() / 2, 20);
  M5Dial.Display.drawString(passwordInput, M5Dial.Display.width() / 2, 50);
  String charDisplay = String(charSet[selectedCharIndex]);
  M5Dial.Display.drawString("Char: " + charDisplay, M5Dial.Display.width() / 2, 80);
  M5Dial.Display.drawString("[Hold: Done]", M5Dial.Display.width() / 2, 110);
  M5Dial.Display.drawString("[Press: Next Char]", M5Dial.Display.width() / 2, 140);
}

void scanWiFiNetworks() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Scanning...", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
  delay(500);
  wifiScanResults = WiFi.scanNetworks();
  if (wifiScanResults == 0) {
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("No Networks", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    delay(2000);
  } else {
    selectedWiFiItem = 0;
    wifiDisplayOffset = 0;
    inWiFiMenu = true;
    inMainMenu = false;
    inBLEMenu = false;
  }
}

void scanBLEDevices() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Scanning BLE...", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
  delay(500);

  // Clear previous scan results and free memory
  for (auto device : bleDevices) {
    delete device;
  }
  bleDevices.clear();
  bleScanResults = 0;

  // Start BLE scan using NimBLE with a completion callback
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);

  // Use a completion callback instead of per-device callback to avoid override issues
  bool scanSuccess = pScan->start(5, [](NimBLEScanResults results) {
    // Collect devices from scan results when scan completes
    for (int i = 0; i < results.getCount(); i++) {
      NimBLEAdvertisedDevice* device = new NimBLEAdvertisedDevice(*results.getDevice(i));
      bleDevices.push_back(device);
    }
    bleScanResults = bleDevices.size();
  }, false);

  if (!scanSuccess) {
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("BLE Scan Failed", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    delay(2000);
    bleScanResults = 0;
  } else if (bleScanResults == 0) {
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("No Devices", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    delay(2000);
  } else {
    selectedBLEItem = 0;
    bleDisplayOffset = 0;
    inBLEMenu = true;
    inMainMenu = false;
    inWiFiMenu = false;
  }
}

void attemptWiFiConnection(String ssid, String password) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Connecting to", M5Dial.Display.width() / 2, 20);
  M5Dial.Display.drawString(ssid, M5Dial.Display.width() / 2, 40);
  M5Dial.Display.drawString("Please wait...", M5Dial.Display.width() / 2, 60);
  WiFi.begin(ssid.c_str(), password.c_str());
  int timeout = 10000;
  int elapsed = 0;
  while (WiFi.status() != WL_CONNECTED && elapsed < timeout) {
    delay(500);
    elapsed += 500;
    M5Dial.Display.fillRect(0, 80, M5Dial.Display.width(), 20, BACKGROUND_COLOR);
    M5Dial.Display.drawString("Attempt " + String(elapsed / 1000) + "s", M5Dial.Display.width() / 2, 80);
  }
  if (WiFi.status() == WL_CONNECTED) {
    isConnected = true;
    connectedSSID = ssid;
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("Connected!", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    delay(2000);
  } else {
    isConnected = false;
    connectedSSID = "";
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    M5Dial.Display.drawString("Failed to connect", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
    delay(2000);
  }
}

void disconnectWiFi() {
  WiFi.disconnect();
  isConnected = false;
  connectedSSID = "";
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.drawString("Disconnected", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
  delay(2000);
}

void executeMainMenuItem(int item) {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  switch (item) {
    case 0: // Main
      M5Dial.Display.drawString("Main Selected", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
      delay(1000);
      break;
    case 1: // BadUSB
      inMainMenu = false;
      inWiFiMenu = false;
      inBLEMenu = false;
      inPasswordMenu = false;
      selectedBadUSBItem = 0;
      break;
    case 2: // WiFi
      scanWiFiNetworks();
      break;
    case 3: // BLE
      scanBLEDevices();
      break;
    case 4: // Settings
      M5Dial.Display.drawString("Settings Selected", M5Dial.Display.width() / 2, M5Dial.Display.height() / 2);
      delay(1000);
      break;
  }
}

void executeWiFiMenuItem(int item) {
  if (item < wifiScanResults) {
    passwordInput = "";
    passwordCharIndex = 0;
    selectedCharIndex = 0;
    inWiFiMenu = false;
    inPasswordMenu = true;
    inBLEMenu = false;
  } else if (isConnected && item == wifiScanResults) {
    disconnectWiFi();
    inWiFiMenu = true;
    inPasswordMenu = false;
    inMainMenu = false;
    inBLEMenu = false;
  } else {
    inWiFiMenu = false;
    inPasswordMenu = false;
    inMainMenu = true;
    inBLEMenu = false;
    selectedMainItem = 2;
  }
}

void executeBLEMenuItem(int item) {
  if (item < bleScanResults) {
    M5Dial.Display.fillScreen(BACKGROUND_COLOR);
    String deviceName = bleDevices[item]->getName().c_str();
    if (deviceName == "") {
      deviceName = "Unnamed";
    }
    String address = bleDevices[item]->getAddress().toString().c_str();
    int rssi = bleDevices[item]->getRSSI();
    M5Dial.Display.drawString("Name: " + deviceName, M5Dial.Display.width() / 2, 20);
    M5Dial.Display.drawString("Addr: " + address, M5Dial.Display.width() / 2, 50);
    M5Dial.Display.drawString("RSSI: " + String(rssi) + " dBm", M5Dial.Display.width() / 2, 80);
    M5Dial.Display.drawString("Connect? (Not Impl)", M5Dial.Display.width() / 2, 110);
    delay(3000);
  }
  inBLEMenu = false;
  inMainMenu = true;
  inWiFiMenu = false;
  inPasswordMenu = false;
  selectedMainItem = 3;
}

void executePasswordMenuAction() {
  if (M5Dial.BtnA.pressedFor(1000)) {
    String ssid = WiFi.SSID(selectedWiFiItem);
    attemptWiFiConnection(ssid, passwordInput);
    inPasswordMenu = false;
    inWiFiMenu = true;
    inMainMenu = false;
    inBLEMenu = false;
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
      selectedMainItem = 1;
      break;
  }
}

void executePayload1() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.setTextSize(1);
  M5Dial.Display.setCursor(10, 10);
  M5Dial.Display.println("Payload1 Running...");
  M5Dial.Display.println("1. Open Notepad");
  M5Dial.Display.println("2. Type 'Hello World!'");
  M5Dial.Display.println("3. Press Enter");
  delay(2000);
}

void executePayload2() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.setTextSize(1);
  M5Dial.Display.setCursor(10, 10);
  M5Dial.Display.println("Payload2 Running...");
  M5Dial.Display.println("1. Open CMD");
  M5Dial.Display.println("2. Type 'echo Hello'");
  M5Dial.Display.println("3. Press Enter");
  delay(2000);
}

void executePayload3() {
  M5Dial.Display.fillScreen(BACKGROUND_COLOR);
  M5Dial.Display.setTextSize(1);
  M5Dial.Display.setCursor(10, 10);
  M5Dial.Display.println("Payload3 Running...");
  M5Dial.Display.println("1. Open Browser");
  M5Dial.Display.println("2. Go to 'example.com'");
  M5Dial.Display.println("3. Press Enter");
  delay(2000);
}
