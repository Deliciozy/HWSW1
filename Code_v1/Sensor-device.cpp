#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

BLECharacteristic *pCharacteristic;

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"

int value = 0;

void setup() {
  Serial.begin(115200);

  BLEDevice::init("SensorDevice");

  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->start();
}

void loop() {
  value = (value + 5) % 101;

  char buffer[10];
  sprintf(buffer, "%d", value);

  pCharacteristic->setValue(buffer);
  pCharacteristic->notify();

  Serial.println(value);

  delay(1000);
}
