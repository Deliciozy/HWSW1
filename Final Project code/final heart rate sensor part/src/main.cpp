#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>

// ===== Hardware =====
const int PULSE_PIN = D0;
const int LED_PIN = D2;

// ===== Target Config =====
const char* TARGET_NAME = "FIND_ME";
uint8_t TARGET_MAC[] = {0x98, 0x3D, 0xAE, 0xAA, 0x71, 0x99};

// ===== Detection Tuning =====
const int FINGER_THRESHOLD = 3000;
const int MARGIN = 130;
const int MIN_BPM = 45;
const int MAX_BPM = 180;
const unsigned long REFRACTORY_MS = 350;

// ===== State =====
bool connected = false;
uint8_t current_target[6];

// ===== Packet =====
typedef struct __attribute__((packed)) {
  uint32_t seq;
  uint16_t bpm;   // processed / smoothed BPM
  uint8_t finger;
  uint8_t state;  // 0=UNKNOWN, 1=CALM, 2=NEUTRAL, 3=AROUSED
} HeartPacket;

HeartPacket packet = {0, 0, 0, 0};

// ===== BPM Variables =====
float baseline = 0;
bool baselineInitialized = false;
bool inBeat = false;
unsigned long lastBeatTime = 0;
int bpmHistory[5] = {0};
int bpmIndex = 0;
int stableBpm = 0;

// ===== Simple Signal Processing =====
float smoothedBpm = 0.0f;
float prevSmoothedBpm = 0.0f;
float bpmDelta = 0.0f;
bool emotionInitialized = false;

// =========================
// AUTO-HUNT LOGIC
// =========================
bool autoHunt() {
  Serial.println("\n[SEARCHING] Looking for " + String(TARGET_NAME) + "...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  int n = WiFi.scanNetworks();
  if (n <= 0) return false;

  int nameMatchIdx = -1;
  int nameMatchCount = 0;
  bool macFound = false;

  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == TARGET_NAME) {
      nameMatchIdx = i;
      nameMatchCount++;
    }

    uint8_t* b = WiFi.BSSID(i);
    if (memcmp(b, TARGET_MAC, 6) == 0) {
      macFound = true;
    }
  }

  // 1. If exactly one name match, use it.
  if (nameMatchCount == 1) {
    memcpy(current_target, WiFi.BSSID(nameMatchIdx), 6);
    Serial.println("[MATCH] Found unique " + String(TARGET_NAME));
    return true;
  }

  // 2. Otherwise fallback to specific MAC
  if (macFound) {
    memcpy(current_target, TARGET_MAC, 6);
    Serial.println("[MATCH] Found Target by MAC address.");
    return true;
  }

  return false;
}

void setupESPNOW() {
  if (esp_now_init() != ESP_OK) return;

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, current_target, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
    connected = true;
    Serial.println("[OK] ESP-NOW Link Established.");
  }
}

// =========================
// SENSOR LOGIC
// =========================
void processBPM(int raw) {
  unsigned long now = millis();
  bool finger = (raw < FINGER_THRESHOLD);
  packet.finger = finger ? 1 : 0;

  if (!finger) {
    stableBpm = 0;
    baselineInitialized = false;
    inBeat = false;
    lastBeatTime = 0;
    for (int i = 0; i < 5; i++) bpmHistory[i] = 0;
    bpmIndex = 0;
    return;
  }

  if (!baselineInitialized) {
    baseline = raw;
    baselineInitialized = true;
    return;
  }

  baseline = baseline * 0.98f + raw * 0.02f;
  int threshold = (int)baseline + MARGIN;

  if (!inBeat && raw > threshold && (now - lastBeatTime > REFRACTORY_MS)) {
    inBeat = true;

    if (lastBeatTime > 0) {
      int currentBpm = 60000 / (now - lastBeatTime);

      if (currentBpm >= MIN_BPM && currentBpm <= MAX_BPM) {
        bpmHistory[bpmIndex] = currentBpm;
        bpmIndex = (bpmIndex + 1) % 5;

        int sum = 0;
        int count = 0;
        for (int i = 0; i < 5; i++) {
          if (bpmHistory[i] > 0) {
            sum += bpmHistory[i];
            count++;
          }
        }

        if (count > 0) {
          stableBpm = sum / count;
        }
      }
    }

    lastBeatTime = now;
  }

  if (inBeat && raw < baseline) {
    inBeat = false;
  }
}

// =========================
// SIMPLE SIGNAL PROCESSING
// All emotion processing is done here
// =========================
void updateProcessedOutput() {
  // No finger or no valid BPM => clear state
  if (!packet.finger || stableBpm == 0) {
    smoothedBpm = 0;
    prevSmoothedBpm = 0;
    bpmDelta = 0;
    emotionInitialized = false;

    packet.bpm = 0;
    packet.state = 0;  // UNKNOWN
    return;
  }

  // Exponential smoothing parameters
  const float alpha = 0.25f;  // BPM smoothing
  const float beta  = 0.20f;  // delta smoothing

  if (!emotionInitialized) {
    smoothedBpm = stableBpm;
    prevSmoothedBpm = stableBpm;
    bpmDelta = 0;
    emotionInitialized = true;
  } else {
    prevSmoothedBpm = smoothedBpm;
    smoothedBpm = alpha * stableBpm + (1.0f - alpha) * smoothedBpm;

    float d = fabs(smoothedBpm - prevSmoothedBpm);
    bpmDelta = beta * d + (1.0f - beta) * bpmDelta;
  }

  packet.bpm = (uint16_t)(smoothedBpm + 0.5f);

  // Very simple emotion mapping
  if (packet.bpm < 72 && bpmDelta < 1.5f) {
    packet.state = 1;  // CALM
  } else if (packet.bpm > 90 || bpmDelta > 3.0f) {
    packet.state = 3;  // AROUSED
  } else {
    packet.state = 2;  // NEUTRAL
  }
}

// =========================
// MAIN
// =========================
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  analogReadResolution(12);

  while (!connected) {
    digitalWrite(LED_PIN, HIGH);
    if (autoHunt()) {
      setupESPNOW();
    } else {
      Serial.println("[RETRY] Target not found. Retrying in 5s...");
      digitalWrite(LED_PIN, LOW);
      delay(5000);
    }
  }

  digitalWrite(LED_PIN, HIGH);
  delay(1000);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  int raw = analogRead(PULSE_PIN);
  processBPM(raw);
  updateProcessedOutput();

  static unsigned long lastSend = 0;
  if (millis() - lastSend > 800) {
    lastSend = millis();

    packet.seq++;
    esp_now_send(current_target, (uint8_t*)&packet, sizeof(packet));

    Serial.printf("RAW_BPM:%d | OUT_BPM:%d | Finger:%d | State:%d | Delta:%.2f\n",
                  stableBpm, packet.bpm, packet.finger, packet.state, bpmDelta);
  }

  delay(10);
}