#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <stdint.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SwitecX25.h>

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== Hardware =====
const int LED_PIN = D9;
const int BUTTON_PIN = D7;

// ===== Motor (SwitecX25) =====
// 按你刚刚验证能工作的写法
#define MOTOR_STEPS (315 * 3)
SwitecX25 motor1(MOTOR_STEPS, D0, D1, D2, D3);

// ===== ESP-NOW Struct =====
typedef struct __attribute__((packed)) {
  uint32_t seq;
  uint16_t bpm;
  uint8_t finger;
  uint8_t state;   // 0=UNKNOWN, 1=CALM, 2=NEUTRAL, 3=AROUSED
} HeartPacket;

volatile bool newData = false;
HeartPacket incoming = {0, 0, 0, 0};

unsigned long lastPacketTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long lastUiRefresh = 0;
unsigned long lastButtonChangeTime = 0;

bool ledState = false;
bool paused = false;
bool lastButtonReading = HIGH;
bool buttonLatched = false;

// 当前电机目标位置
int motorTarget = MOTOR_STEPS / 2;

// =========================
// Helpers
// =========================
bool linkAlive() {
  return (millis() - lastPacketTime) < 2000;
}

const char* emotionText(uint8_t state, uint8_t finger, bool linked) {
  if (!linked) return "NO LINK";
  if (!finger) return "NO FINGER";

  switch (state) {
    case 1: return "CALM";
    case 2: return "NEUTRAL";
    case 3: return "AROUSED";
    default: return "UNKNOWN";
  }
}

// 用 state 映射到 motor 位置
int getMotorPositionFromState(uint8_t state, uint8_t finger, bool linked, bool pausedNow) {
  if (pausedNow || !linked || !finger) {
    return MOTOR_STEPS / 2;   // 没有信号时回中间
  }

  switch (state) {
    case 1: return MOTOR_STEPS * 1 / 4;   // CALM    左侧
    case 2: return MOTOR_STEPS * 2 / 4;   // NEUTRAL 中间
    case 3: return MOTOR_STEPS * 3 / 4;   // AROUSED 右侧
    default: return MOTOR_STEPS / 2;
  }
}

// 暂停时把线圈拉低
void releaseMotor() {
  pinMode(D0, OUTPUT);
  pinMode(D1, OUTPUT);
  pinMode(D2, OUTPUT);
  pinMode(D3, OUTPUT);

  digitalWrite(D0, LOW);
  digitalWrite(D1, LOW);
  digitalWrite(D2, LOW);
  digitalWrite(D3, LOW);
}

void drawPausedScreen() {
  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Display");

  display.setTextSize(2);
  display.setCursor(0, 22);
  display.println("PAUSED");

  display.setTextSize(1);
  display.setCursor(0, 52);
  display.println("Press button to resume");

  display.display();
}

void drawScreen() {
  if (paused) {
    drawPausedScreen();
    return;
  }

  display.clearDisplay();
  display.setTextColor(WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Display");
  display.setCursor(82, 0);
  display.print("Link: ");
  display.print(linkAlive() ? "OK" : "??");

  display.setTextSize(1);
  display.setCursor(0, 18);
  display.print("Emotion:");

  display.setTextSize(2);
  display.setCursor(0, 30);
  display.println(emotionText(incoming.state, incoming.finger, linkAlive()));

  display.drawFastHLine(0, 54, 128, WHITE);
  display.setTextSize(1);
  display.setCursor(0, 57);
  display.print("BPM:");
  display.print(incoming.bpm);

  display.display();
}

void togglePause() {
  paused = !paused;

  if (paused) {
    digitalWrite(LED_PIN, LOW);
    releaseMotor();
    drawPausedScreen();
  } else {
    // 恢复后重新设目标位置
    motorTarget = getMotorPositionFromState(incoming.state, incoming.finger, linkAlive(), false);
    motor1.setPosition(motorTarget);
    drawScreen();
  }
}

void handleButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastButtonChangeTime = millis();
  }

  if ((millis() - lastButtonChangeTime) > 40) {
    if (reading == LOW && !buttonLatched) {
      buttonLatched = true;
      togglePause();
    }

    if (reading == HIGH) {
      buttonLatched = false;
    }
  }

  lastButtonReading = reading;
}

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
  if (len == sizeof(HeartPacket)) {
    memcpy((void*)&incoming, data, sizeof(HeartPacket));
    lastPacketTime = millis();
    newData = true;
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(D4, D5);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
      delay(10);
    }
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("FIND_ME", "12345678", 1);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_recv_cb(onReceive);
  }

  // ===== SwitecX25 init =====
  // 先归零，再回到中间
  motor1.zero();
  motor1.setPosition(MOTOR_STEPS / 2);

  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Waiting for sensor...");
  display.setCursor(0, 25);
  display.println("MAC:");
  display.println(WiFi.macAddress());
  display.display();
  delay(2000);
}

// =========================
// Loop
// =========================
void loop() {
  unsigned long now = millis();

  handleButton();

  if (paused) {
    digitalWrite(LED_PIN, LOW);
    releaseMotor();
    delay(5);
    return;
  }

  if (now - lastBlinkTime > 500) {
    lastBlinkTime = now;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
  }

  if (newData || (now - lastUiRefresh > 500)) {
    newData = false;
    lastUiRefresh = now;
    drawScreen();
  }

  // 根据收到的 state 更新目标位置
  motorTarget = getMotorPositionFromState(incoming.state, incoming.finger, linkAlive(), paused);
  motor1.setPosition(motorTarget);

  // SwitecX25 必须频繁 update
  motor1.update();

  delay(1);
}