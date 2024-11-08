#include <WiFi.h>
#include <WebSocketsClient.h>
#include <FastLED.h>

const char* ssid = "UI24 L&D Sound";
const char* password = "olhzwieber";
const char* ui24_ip = "10.10.2.2"; // Replace with your UI24's IP address

// Global arrays for mic and autotune channels
int mic_channels[] = {0, 1};
int autotune_channels[] = {2, 3};

const int buttonPin = 23; // Button connected to GPIO23
bool lastButtonState = HIGH;
bool currentButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
unsigned long lastKeepAliveTime = 0;
unsigned long keepAliveInterval = 10000;
unsigned long muteAllDelay = 3000;

// LED setup
#define LED_PIN 2       // Pin connected to the LED
#define NUM_LEDS 1      // Number of LEDs in the strip
#define BRIGHTNESS 50   // LED brightness (adjust for dimming effect)
CRGB leds[NUM_LEDS];     // Array to hold LED data

// Global mute state tracking
bool muteState = false;
bool initialSetupDone = false;
bool longPressActive = false;  // Flag to track if purple mode is active
bool buttonHeldLongEnough = false; // Flag for detecting long press
unsigned long buttonPressTime = 0; // Time when button press starts
bool previousMuteState = false; // Variable to store mute state before entering purple mode
bool exitingPurpleMode = false;  // Flag to prevent toggle when exiting purple mode

WebSocketsClient webSocket;

void setup() {
  Serial.begin(115200);
  
  pinMode(buttonPin, INPUT_PULLUP); // Set button pin as input with pull-up resistor
  
  // Initialize LED
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  
  // Connect to WiFi
  connectToWiFi();

  // Initialize WebSocket connection
  webSocket.begin(ui24_ip, 80, "/socket.io/?transport=websocket");
  webSocket.onEvent(webSocketEvent);
}

void loop() {
  // Reconnect to WiFi if disconnected
  if (WiFi.status() != WL_CONNECTED) {
    connectToWiFi();
  }

  // WebSocket loop to keep connection alive
  webSocket.loop();

  // Initial setup of channels
  if (!initialSetupDone && webSocket.isConnected()) {
    setupInitialState(); 
    setLEDColor(CRGB::Green); // Set to dim green once initial setup is complete
    initialSetupDone = true; 
  }

  // Send keep-alive message every 10 seconds
  if (millis() - lastKeepAliveTime > keepAliveInterval) {
    sendKeepAlive();
    lastKeepAliveTime = millis();
  }

  // Check button state and debounce
  int reading = digitalRead(buttonPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading == LOW && currentButtonState == HIGH) {
      // Button pressed: Start tracking time
      buttonPressTime = millis();
      buttonHeldLongEnough = false;
      currentButtonState = LOW;
    } else if (reading == LOW && (millis() - buttonPressTime >= muteAllDelay) && !buttonHeldLongEnough) {
      // Button held for 5 seconds
      buttonHeldLongEnough = true;
      longPressActive = !longPressActive; // Toggle purple mode

      if (longPressActive) {
        // Entering purple mode, save the current mute state
        previousMuteState = muteState;
        setLEDColor(CRGB::Purple);
        
        // Mute all channels immediately by setting muteState to true
        muteState = true;
        toggleMute();
      } else {
        // Exiting purple mode, restore previous mute state and LED color
        muteState = previousMuteState;
        setLEDColor(muteState ? CRGB::Blue : CRGB::Green);
        toggleMute(); // Apply the previous mute state to channels
        exitingPurpleMode = true; // Set flag to ignore toggle on next button release
      }
    } else if (reading == HIGH && currentButtonState == LOW) {
      // Button released, handle regular toggle if purple mode is off and not exiting
      if (!longPressActive && !exitingPurpleMode) {
        muteState = !muteState; // Toggle mute state
        toggleMute();
        
        // Update LED color based on mute state if not in purple mode
        setLEDColor(muteState ? CRGB::Blue : CRGB::Green);
      }
      currentButtonState = HIGH;
      exitingPurpleMode = false; // Clear the flag after button release
    }
  }
  
  lastButtonState = reading;
}

// Function to connect to WiFi
void connectToWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    setLEDColor(CRGB::Red); // Solid red when WiFi is not connected
  }
  Serial.println("Connected to WiFi!");
  setLEDColor(CRGB::Green); 
}

// Event handler for WebSocket
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  String message = String((char*)payload);

  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("WebSocket Disconnected");
      initialSetupDone = false;
      setLEDColor(CRGB::Orange); // Blinking amber when WebSocket disconnects
      break;
    case WStype_CONNECTED:
      Serial.println("WebSocket Connected");
      setLEDColor(CRGB::Green); // Reset to dim green on reconnection
      break;
    default:
      break;
  }
}

// Function to send mute/unmute commands over WebSocket
void sendMuteCommand(int channel, bool mute) {
  if (WiFi.status() == WL_CONNECTED && webSocket.isConnected()) {
    String command = "3:::" + String("SETD^i.") + String(channel) + String(".mute^") + String(mute ? "1" : "0");
    webSocket.sendTXT(command);
    Serial.println("Mute command sent: " + command);
  } else {
    Serial.println("Cannot send command, WebSocket not connected.");
  }
}

// Function to send the keep-alive message every 10 seconds
void sendKeepAlive() {
  if (WiFi.status() == WL_CONNECTED && webSocket.isConnected()) {
    String keepAliveMessage = "3:::ALIVE";
    webSocket.sendTXT(keepAliveMessage);
  } else {
    Serial.println("Cannot send keep-alive, WebSocket not connected.");
  }
}

// Function to mute/unmute channels based on the current muteState
void toggleMute() {
  if (!longPressActive) { // Only toggle mute when purple mode is inactive
    for (int i = 0; i < sizeof(mic_channels) / sizeof(mic_channels[0]); i++) {
      sendMuteCommand(mic_channels[i], muteState);
    }
    for (int i = 0; i < sizeof(autotune_channels) / sizeof(autotune_channels[0]); i++) {
      sendMuteCommand(autotune_channels[i], !muteState);
    }
  } else {
    // In purple mode, mute both mic and autotune channels
    for (int i = 0; i < sizeof(mic_channels) / sizeof(mic_channels[0]); i++) {
      sendMuteCommand(mic_channels[i], true);
    }
    for (int i = 0; i < sizeof(autotune_channels) / sizeof(autotune_channels[0]); i++) {
      sendMuteCommand(autotune_channels[i], true);
    }
  }
}
// Function to setup initial states (mic unmuted, autotune muted)
void setupInitialState() {
  for (int i = 0; i < sizeof(autotune_channels)/sizeof(autotune_channels[0]); i++) {
    sendMuteCommand(autotune_channels[i], true);
  }
  for (int i = 0; i < sizeof(mic_channels)/sizeof(mic_channels[0]); i++) {
    sendMuteCommand(mic_channels[i], false);
  }
  Serial.println("Initial state setup complete.");
}

// Function to set LED color
void setLEDColor(CRGB color) {
  leds[0] = color;
  FastLED.show();
}
