// ==========================================
// 1. LIBRARIES & DEFINITIONS
// ==========================================
// These '#include' statements bring in pre-written code (libraries) so we don't 
// have to write the incredibly complex Bluetooth and encryption math from scratch.

#include <BLEDevice.h>   // Core library for setting up the ESP32 as a Bluetooth device
#include <BLEServer.h>   // Allows the ESP32 to act as a "Server" that the Windows app connects to
#include <BLEUtils.h>    // Utility functions for Bluetooth operations
#include <BLE2902.h>     // A specific descriptor needed so the ESP32 can "push" notifications to the app
#include "mbedtls/aes.h" // The standard, built-in ESP32 library for Advanced Encryption Standard (AES)

// --- BLE Identifiers (UUIDs) ---
// UUIDs are like phone numbers for Bluetooth. When the Windows app scans the area, 
// it looks for these exact strings to know it found your specific shoe, not a random smart TV.
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b" // The main ID for the device
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" // The ID for the specific data stream

// --- Hardware Pins ---
// Defining the size of our physical copper tape matrix (Updated to 3x4)
const int NUM_COLS = 3;  // 3 Vertical strips
const int NUM_ROWS = 4;  // 4 Horizontal strips 

// DIGITAL OUTPUTS (Columns/Power)
// These pins will take turns sending out 3.3V of power, one column at a time.
int colPins[NUM_COLS] = {4, 5, 12}; 

// ANALOG INPUTS (Rows/Reading) 
// These pins read the returning voltage after it passes through the Velostat.
// They MUST be ADC1 pins, because Bluetooth automatically breaks ADC2 pins.
int rowPins[NUM_ROWS] = {32, 33, 34, 35}; 

// Interface Pins
const int BUTTON_PIN = 27;        // The pin connected to your physical start/pause button
const int BLUETOOTH_LED_PIN = 2;  // The pin for the built-in blue LED on the ESP32 board
const int BATTERY_PIN = 26;       // The analog pin connected to your battery circuit

// --- State & Timing Variables ---
// These variables act as the "memory" for the system, keeping track of what is happening.
BLEServer* pServer = NULL;               // Creates an empty pointer for our Bluetooth server
BLECharacteristic* pCharacteristic = NULL; // Creates an empty pointer for our data stream
bool deviceConnected = false;            // Is the Windows app currently connected? Starts as false.
bool isRecording = false;                // Is the user currently recording data? Starts as false.

// Timing variables to ensure we hit that 100-200 Hz requirement without using delay()
// Using delay() would freeze the whole board, ruining our Bluetooth connection.
unsigned long previousSampleMillis = 0;  // Remembers the last time we scanned the foot
const long sampleInterval = 10;          // 10 milliseconds = 100 times a second (100 Hz)

// Variables for the "Debounce" logic. Physical buttons are springy metal. When you press them, 
// they rapidly bounce between ON and OFF for a few milliseconds. This stops double-clicks.
int lastButtonState = HIGH;              // The previous reading of the button
int buttonState = HIGH;                  // The confirmed current reading of the button
unsigned long lastDebounceTime = 0;      // A timer for the button press
const unsigned long debounceDelay = 50;  // Wait 50ms to ensure the button press is real

// Variables for blinking the Bluetooth LED
unsigned long previousBlinkMillis = 0;   // Remembers the last time the LED flipped on/off
bool ledState = LOW;                     // Keeps track of whether the LED is currently ON or OFF

// A 2D array (like a spreadsheet) to hold the 12 pressure values from our matrix
int sensorData[NUM_COLS][NUM_ROWS];

// AES-128 Encryption Key. This is the secret password used to lock the data.
// It MUST be exactly 16 characters long. The Windows app needs this exact string to unlock the data.
unsigned char aes_key[16] = "FootSandalKey12"; 


// ==========================================
// 2. BLE CONNECTION CALLBACKS
// ==========================================
// This small block of code runs automatically in the background whenever a device 
// connects or disconnects from the ESP32.
class MyServerCallbacks: public BLEServerCallbacks {
    // What to do when the Windows app connects:
    void onConnect(BLEServer* pServer) {
      deviceConnected = true; // Update our status variable
    };
    // What to do when the Windows app disconnects (or crashes/walks out of range):
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false; // Update our status variable
      isRecording = false;     // Safety catch: stop trying to record data to save battery
      BLEDevice::startAdvertising(); // Start broadcasting our signal again so the app can reconnect
    }
};


// ==========================================
// 3. SETUP FUNCTION
// ==========================================
void setup() {
  // Start the Serial Monitor at a fast speed so we can print debug messages to the computer
  Serial.begin(115200);
  
  // Configure our basic interface pins
  pinMode(BLUETOOTH_LED_PIN, OUTPUT); // LED pin sends power out
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button pin reads power in (uses internal resistor)
  pinMode(BATTERY_PIN, INPUT);        // Battery pin reads analog voltage in

  // Configure all 3 column pins as OUTPUTs and set them to LOW (off) initially
  for (int i = 0; i < NUM_COLS; i++) {
    pinMode(colPins[i], OUTPUT);
    digitalWrite(colPins[i], LOW);
  }
  
  // Configure all 4 row pins as INPUTs so they can read the returning voltage
  for (int i = 0; i < NUM_ROWS; i++) {
    pinMode(rowPins[i], INPUT);
  }

  // --- Initialize Bluetooth Server ---
  // 1. Give the device a name that will show up in Bluetooth menus
  BLEDevice::init("GaitAnalysisPlatform"); 
  
  // 2. Create the actual server using our device
  pServer = BLEDevice::createServer();
  
  // 3. Attach the callback functions we wrote above (onConnect, onDisconnect)
  pServer->setCallbacks(new MyServerCallbacks());

  // 4. Create a "Service" using our primary UUID
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // 5. Create a "Characteristic" (the actual data channel) inside that service.
  //    PROPERTY_READ means the app can ask for data. 
  //    PROPERTY_NOTIFY means the ESP32 can aggressively push data to the app without being asked.
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY 
                    );

  // 6. Add a descriptor required by the Bluetooth standard to allow notifications
  pCharacteristic->addDescriptor(new BLE2902()); 
  
  // 7. Start the service
  pService->start();
  
  // 8. Start broadcasting our existence to the world so the Windows app can find us
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  
  BLEDevice::startAdvertising();

  // Print a success message to the Serial Monitor
  Serial.println("System Initialized. Waiting for Windows app...");
}


// ==========================================
// 4. MAIN LOOP FUNCTION
// ==========================================
void loop() {
  // Grab the current "uptime" of the board in milliseconds. 
  // We use this like a stopwatch to manage all our timing.
  unsigned long currentMillis = millis();

  // --------------------------------------------------
  // Feature A: Bluetooth LED Indicator
  // --------------------------------------------------
  if (deviceConnected) {
    // If connected, check if 500ms (half a second) has passed since the last blink
    if (currentMillis - previousBlinkMillis >= 500) { 
      previousBlinkMillis = currentMillis; // Reset the stopwatch
      ledState = !ledState;                // Flip the state (if ON make OFF, if OFF make ON)
      digitalWrite(BLUETOOTH_LED_PIN, ledState); // Apply the new state to the physical LED
    }
  } else {
    // If we are NOT connected, ensure the LED stays firmly OFF
    digitalWrite(BLUETOOTH_LED_PIN, LOW); 
  }

  // --------------------------------------------------
  // Feature B: Start/Pause Button (Debounced)
  // --------------------------------------------------
  // Read the raw physical state of the button pin
  int reading = digitalRead(BUTTON_PIN);
  
  // If the raw reading is different from the last loop (meaning it was just pressed or released),
  // reset the debounce timer.
  if (reading != lastButtonState) {
    lastDebounceTime = currentMillis;
  }

  // If the button has stayed in this new state longer than our 50ms threshold...
  if ((currentMillis - lastDebounceTime) > debounceDelay) {
    // ...and if this state is actually a change from what we formally considered the button state...
    if (reading != buttonState) {
      buttonState = reading; // ...update the formal button state.
      
      // If the formal state is LOW, it means the button was fully pushed down.
      if (buttonState == LOW) { 
        isRecording = !isRecording; // Toggle our recording status (flip true to false, false to true)
        
        // Print a helpful debug message depending on the new state
        Serial.println(isRecording ? "Recording Started" : "Recording Paused");
      }
    }
  }
  // Save the raw reading for the next time the loop runs
  lastButtonState = reading; 

  // --------------------------------------------------
  // Feature C: Data Acquisition & Transmission
  // --------------------------------------------------
  // We only want to scan the foot and drain the battery doing math IF the app is connected 
  // AND the user has actually pressed the start button.
  if (deviceConnected && isRecording) {
      
      // Check if 10ms have passed since the last time we scanned (This gives us 100 Hz)
      if (currentMillis - previousSampleMillis >= sampleInterval) {
          previousSampleMillis = currentMillis; // Reset the sampling stopwatch
          
          // --- Step 1: Matrix Scanning ---
          // Loop through every single column (0 to 2)
          for (int c = 0; c < NUM_COLS; c++) {
              // Turn the current column ON (apply 3.3V)
              digitalWrite(colPins[c], HIGH);
              
              // Wait 5 microseconds for the electricity to stabilize across the Velostat
              delayMicroseconds(5); 
              
              // While this column is powered, quickly read the voltage on all 4 rows
              for (int r = 0; r < NUM_ROWS; r++) {
                  // Save the voltage (0 to 4095) into our 2D array
                  sensorData[c][r] = analogRead(rowPins[r]);
              }
              // Turn the current column OFF before moving to the next one
              digitalWrite(colPins[c], LOW);
          }
          
          // --- Step 2: Read Battery ---
          // Read the raw voltage from the battery pin (0 to 4095)
          int rawBattery = analogRead(BATTERY_PIN);
          // Use the map() function to convert that 0-4095 scale into a clean 0-100 percentage
          int batteryPercent = map(rawBattery, 0, 4095, 0, 100); 

          // --- Step 3: Format Data String ---
          // We need to turn our 12 separate numbers into one long text sentence.
          String dataString = "";
          
          for (int c = 0; c < NUM_COLS; c++) {
            for (int r = 0; r < NUM_ROWS; r++) {
              // Add the number to the string, followed by a comma
              dataString += String(sensorData[c][r]) + ",";
            }
          }
          // Finally, stick the battery percentage at the very end of the sentence
          dataString += String(batteryPercent); 
          
          // --- Step 4: AES-128 Encryption ---
          // AES encryption requires data to be chunked into blocks of exactly 16 bytes.
          // First, we find out how long our data sentence is.
          int strLen = dataString.length();
          
          // Next, we calculate how much "padding" we need to make it a perfect multiple of 16.
          int paddedLen = ((strLen / 16) + 1) * 16;
          
          // Create an empty byte array (filled with zeros) using that perfect padded length
          unsigned char payload[paddedLen] = {0}; 
          
          // Copy our data sentence into that padded byte array
          dataString.toCharArray((char*)payload, strLen + 1); 
          
          // Create a second empty array to hold the final encrypted gibberish
          unsigned char encrypted_payload[paddedLen] = {0};
          
          // Set up the ESP32's built-in encryption engine
          mbedtls_aes_context aes;
          mbedtls_aes_init(&aes);
          
          // Load our secret 16-character password into the engine
          mbedtls_aes_setkey_enc(&aes, aes_key, 128); 
          
          // A loop to encrypt the data block by block (jumping 16 bytes at a time)
          for(int i = 0; i < paddedLen; i += 16) {
             mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (const unsigned char*)(payload + i), (encrypted_payload + i));
          }
        
          // Clean up the encryption engine to free up the ESP32's memory
          mbedtls_aes_free(&aes);

          // --- Step 5: Transmit via BLE ---
          // Load the encrypted gibberish into our Bluetooth Characteristic
          pCharacteristic->setValue(encrypted_payload, paddedLen);
          
          // Tell the ESP32 to push (notify) this data packet out to the Windows app immediately
          pCharacteristic->notify();
      }
  }
}