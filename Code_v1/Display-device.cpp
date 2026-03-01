#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define SDA_PIN 6
#define SCL_PIN 7

#define STEP_PIN 2
#define DIR_PIN 3

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

BLEClient*  pClient;
BLERemoteCharacteristic* pRemoteCharacteristic;

int currentPosition = 0;

void stepMotor(int steps, bool direction) {
  digitalWrite(DIR_PIN, direction);
  for (int i = 0; i < steps; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(800);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(800);
  }
}

void moveToPosition(int target) {
  int stepsPerUnit = 3;
  int targetSteps = target * stepsPerUnit;
  int delta = targetSteps - currentPosition;

  if (delta > 0) stepMotor(delta, HIGH);
  else if (delta < 0) stepMotor(-delta, LOW);

  currentPosition = targetSteps;
}

class MyCallbacks: public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {}
  void onDisconnect(BLEClient* pclient) {
    Serial.println("Disconnected");
  }
};

void setup() {
  Serial.begin(115200);

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);

  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  BLEDevice::init("");
  pClient  = BLEDevice::createClient();
  pClient->setClientCallbacks(new MyCallbacks());

  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);

  BLEScanResults foundDevices = pBLEScan->start(5);

  for (int i = 0; i < foundDevices.getCount(); i++) {
    BLEAdvertisedDevice device = foundDevices.getDevice(i);
    if (device.getName() == "SensorDevice") {
      pClient->connect(&device);
      BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
      pRemoteCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
    }
  }
}

void loop() {
  if (pRemoteCharacteristic && pClient->isConnected()) {

    std::string value = pRemoteCharacteristic->readValue();
    int receivedValue = atoi(value.c_str());

    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.setCursor(0,20);
    display.print("Value:");
    display.println(receivedValue);
    display.display();

    moveToPosition(receivedValue);
  }

  delay(500);
}
