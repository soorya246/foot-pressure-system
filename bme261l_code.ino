// ==========================================
// 1. LIBRARIES & DEFINITIONS
// ==========================================
#include <BLEDevice.h>   
#include <BLEServer.h>   
#include <BLEUtils.h>    
#include <BLE2902.h>     
#include "mbedtls/aes.h" 

// --- BLE Identifiers (UUIDs) ---
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b" 
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" 

// --- Hardware Pins ---
const int NUM_COLS = 3;  
const int NUM_ROWS = 4;  

// DIGITAL OUTPUTS (Columns/Power)
int colPins[NUM_COLS] = {4, 5, 12}; 

// ANALOG INPUTS (Rows/Reading) 
int rowPins[NUM_ROWS] = {32, 33, 34, 35}; 

// Interface Pins
const int BLUETOOTH_LED_PIN = 2;  
const int BATTERY_PIN = 36;       // Keep this as an ADC1 pin!

// --- State & Timing Variables ---
BLEServer* pServer = NULL;               
BLECharacteristic* pCharacteristic = NULL; 
bool deviceConnected = false;            

unsigned long previousSampleMillis = 0;  
const long sampleInterval = 10;          

unsigned long previousBlinkMillis = 0;   
bool ledState = LOW;                     

int sensorData[NUM_COLS][NUM_ROWS];

// AES-128 Encryption Key
unsigned char aes_key[16] = "FootSandalKey12"; 

// ==========================================
// 2. BLE CONNECTION CALLBACKS
// ==========================================
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true; 
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false; 
      BLEDevice::startAdvertising(); 
    }
};

// ==========================================
// 3. SETUP FUNCTION 
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(BLUETOOTH_LED_PIN, OUTPUT); 
  pinMode(BATTERY_PIN, INPUT);        

  for (int i = 0; i < NUM_COLS; i++) {
    pinMode(colPins[i], OUTPUT);
    digitalWrite(colPins[i], LOW);
  }
  
  for (int i = 0; i < NUM_ROWS; i++) {
    pinMode(rowPins[i], INPUT);
  }

  // --- Initialize Bluetooth Server ---
  BLEDevice::init("GaitAnalysisPlatform"); 
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY 
                    );

  pCharacteristic->addDescriptor(new BLE2902()); 
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  
  BLEDevice::startAdvertising();

  Serial.println("System Initialized. Waiting for Windows app...");
}

// ==========================================
// 4. MAIN LOOP FUNCTION
// ==========================================
void loop() {
  unsigned long currentMillis = millis();

  // --------------------------------------------------
  // Feature A: Bluetooth LED Indicator
  // --------------------------------------------------
  if (deviceConnected) {
    if (currentMillis - previousBlinkMillis >= 500) { 
      previousBlinkMillis = currentMillis; 
      ledState = !ledState;                
      digitalWrite(BLUETOOTH_LED_PIN, ledState); 
    }
  } else {
    digitalWrite(BLUETOOTH_LED_PIN, LOW); 
  }

  // --------------------------------------------------
  // Feature B: Data Acquisition & Transmission
  // --------------------------------------------------
  // The ESP32 will continuously stream data as long as it is connected.
  // The Windows app will handle the logic of when to "start" or "pause" recording.
  if (deviceConnected) {
      
      if (currentMillis - previousSampleMillis >= sampleInterval) {
          previousSampleMillis = currentMillis; 
          
          // --- Step 1: Matrix Scanning ---
          for (int c = 0; c < NUM_COLS; c++) {
              digitalWrite(colPins[c], HIGH);
              delayMicroseconds(5); 
              
              for (int r = 0; r < NUM_ROWS; r++) {
                  sensorData[c][r] = analogRead(rowPins[r]);
              }
              digitalWrite(colPins[c], LOW);
          }
          
          // --- Step 2: Read Battery ---
          int rawBattery = analogRead(BATTERY_PIN);
          int batteryPercent = map(rawBattery, 0, 4095, 0, 100); 

          // --- Step 3: Format Data String ---
          String dataString = "";
          for (int c = 0; c < NUM_COLS; c++) {
            for (int r = 0; r < NUM_ROWS; r++) {
              dataString += String(sensorData[c][r]) + ",";
            }
          }
          dataString += String(batteryPercent); 
          
          // --- Step 4: AES-128 Encryption ---
          int strLen = dataString.length();
          int paddedLen = ((strLen / 16) + 1) * 16;
          
          unsigned char payload[paddedLen] = {0}; 
          dataString.toCharArray((char*)payload, strLen + 1); 
          unsigned char encrypted_payload[paddedLen] = {0};
          
          mbedtls_aes_context aes;
          mbedtls_aes_init(&aes);
          mbedtls_aes_setkey_enc(&aes, aes_key, 128); 
          
          for(int i = 0; i < paddedLen; i += 16) {
             mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (const unsigned char*)(payload + i), (encrypted_payload + i));
          }
        
          mbedtls_aes_free(&aes);

          // --- Step 5: Transmit via BLE ---
          pCharacteristic->setValue(encrypted_payload, paddedLen);
          pCharacteristic->notify();
      }
  }
}