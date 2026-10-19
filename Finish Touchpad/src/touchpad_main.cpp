#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ============================================================
// PIN DEFINITIONS
// ============================================================
#define TFT_CS    13
#define TFT_RST   26
#define TFT_DC    25
#define TFT_MOSI  23
#define TFT_SCK   18

#define FINISH_PIN  15
#define BTN_RESET   5

#define LED_RED   22
#define LED_WHITE 14
#define LED_GREEN 27

#define TOUCH_DEBOUNCE_MS 150
#define TOUCH_COOLDOWN_MS 2000
#define TOUCH_GRACE_MS    3000

#define BLINK_INTERVAL_MS 500 // toggle every 500ms = 1 full on/off cycle per second

// ============================================================
// DISPLAY
// ============================================================
// Hardware SPI uses the existing display wiring; CS remains library-controlled.
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);
constexpr uint32_t TFT_SPI_HZ = 20000000;

#define C_BG    0x0000
#define C_WHITE 0xFFFF
#define C_CYAN  0x07FF
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_GRAY  0x7BEF

// Display-only state. Race timestamps and touch detection remain independent.
constexpr unsigned long TIMER_REFRESH_MS = 50;
bool liveTimerDrawn = false;
unsigned long lastTimerRefresh = 0;
char lastTimerText[12] = {};
uint8_t lastTimerTextSize = 5;

// ============================================================
// ESP-NOW
// ============================================================
uint8_t starterMAC[] = {0xB0, 0xCB, 0xD8, 0xCD, 0x99, 0xCC};

struct StartMessage {
  uint8_t  type;
  uint8_t  poolType;
  uint16_t distance;
};

// Shared wire format with the Starter: finish type followed by elapsed ms.
struct __attribute__((packed)) FinishMessage {
  uint8_t  type;
  uint32_t totalTimeMs;
};
static_assert(sizeof(FinishMessage) == 5, "Finish message must be 5 bytes");

// ============================================================
// RACE STATE
// ============================================================
volatile bool raceStarted    = false;
volatile bool abortRequested = false;
bool finished                = false;

uint8_t  poolType        = 0;
uint16_t swimDistance    = 0;
uint8_t  expectedTouches = 0;
uint8_t  touchCount      = 0;

unsigned long startTime     = 0;
unsigned long lastTouchTime = 0;
unsigned long splitTimes[8] = {};
unsigned long legTimes[8]   = {};

// LED state: white is solid ON whenever powered, in every state,
// EXCEPT during swim-in-progress, where it's off and red blinks instead.
// Green keeps its existing finish-indicator behavior, untouched.
bool swimActive         = false;
bool redBlinkState      = false;
unsigned long lastBlinkToggle = 0;

// ============================================================
// HELPER: expected touchpad touches
// ============================================================
uint8_t calcExpectedTouches(uint8_t pt, uint16_t dist) {
  uint8_t poolLen    = (pt == 2) ? 50 : 25;
  uint8_t numLengths = dist / poolLen;
  uint8_t touches    = numLengths / 2;
  return max((uint8_t)1, touches);
}

// ============================================================
// TIME FORMATTING
// ============================================================
void formatTime(char* buf, size_t size, unsigned long ms) {
  unsigned long mins = ms / 60000;
  unsigned long secs = (ms / 1000) % 60;
  unsigned long cs   = (ms % 1000) / 10;
  snprintf(buf, size, "%lu:%02lu.%02lu", mins, secs, cs);
}

// ============================================================
// DISPLAY HELPERS
// ============================================================
void tftClear() {
  tft.fillScreen(C_BG);
  liveTimerDrawn = false;
  memset(lastTimerText, 0, sizeof(lastTimerText));
}

void tftTitle(const char* title, uint16_t color = C_CYAN) {
  tft.setTextColor(color);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print(title);
  tft.drawFastHLine(0, 34, 240, C_GRAY);
}

// ============================================================
// SCREEN: READY
// ============================================================
void showReady() {
  tftClear();
  tftTitle("Touchpad Ready");
  tft.setTextColor(C_GRAY);
  tft.setTextSize(2);
  tft.setCursor(10, 80);
  tft.print("Waiting for");
  tft.setCursor(10, 110);
  tft.print("start signal...");
}

// ============================================================
// SCREEN: LIVE TIMER
// ============================================================
void showLiveTimer(unsigned long elapsed) {
  if (touchCount == 0) {
    if (liveTimerDrawn && elapsed - lastTimerRefresh < TIMER_REFRESH_MS) return;

    if (!liveTimerDrawn) {
      tftClear();
      tftTitle("Swimming...", C_GRAY);
      liveTimerDrawn = true;
    }

    char buf[12];
    formatTime(buf, sizeof(buf), elapsed);
    size_t length = strlen(buf);
    // Keep even unusually long minute values within the display width.
    uint8_t textSize = min(5, (tft.width() - 10) / (6 * (int)length));
    if (textSize != lastTimerTextSize) {
      tft.fillRect(10, 100, tft.width() - 10, 40, C_BG);
      memset(lastTimerText, 0, sizeof(lastTimerText));
      lastTimerTextSize = textSize;
    }

    size_t oldLength = strlen(lastTimerText);
    for (size_t i = 0; i < length || i < oldLength; i++) {
      char next = (i < length) ? buf[i] : ' ';
      char previous = (i < oldLength) ? lastTimerText[i] : ' ';
      if (next != previous) {
        // Opaque characters overwrite old pixels without a blank-screen flash.
        tft.drawChar(10 + i * 6 * textSize, 100, next, C_WHITE, C_BG, textSize);
      }
    }
    memcpy(lastTimerText, buf, length + 1);
    lastTimerRefresh = elapsed;
  } else {
    // This path is called only for a newly recorded split.
    char buf[12];
    formatTime(buf, sizeof(buf), elapsed);
    tftClear();
    tft.drawFastHLine(0, 34, 240, C_GRAY);
    tft.setTextColor(C_GRAY);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("Split times:");

    int y = 50;
    for (uint8_t i = 0; i < touchCount && i < 6; i++) {
      char lb[12];
      formatTime(lb, sizeof(lb), legTimes[i]);
      char line[20];
      snprintf(line, sizeof(line), "L%u: %s", i + 1, lb);
      tft.setTextColor(C_CYAN);
      tft.setTextSize(1);
      tft.setCursor(10, y);
      tft.print(line);
      y += 18;
    }

    tft.setTextColor(C_WHITE);
    tft.setTextSize(3);
    tft.setCursor(10, 260);
    tft.print(buf);
  }
}

// ============================================================
// SCREEN: FINISH
// ============================================================
void showFinish() {
  tftClear();
  tftTitle("FINISH!", C_GREEN);

  unsigned long totalTime = splitTimes[touchCount - 1];
  char totalBuf[12];
  formatTime(totalBuf, sizeof(totalBuf), totalTime);

  // Total time big
  tft.setTextColor(C_WHITE);
  tft.setTextSize(4);
  tft.setCursor(10, 50);
  tft.print(totalBuf);

  // Leg splits below
  int y = 130;
  for (uint8_t i = 0; i < touchCount && i < 6; i++) {
    char lb[12];
    formatTime(lb, sizeof(lb), legTimes[i]);
    char line[20];
    snprintf(line, sizeof(line), "L%u: %s", i + 1, lb);
    tft.setTextColor(C_CYAN);
    tft.setTextSize(2);
    tft.setCursor(10, y);
    tft.print(line);
    y += 30;
  }

  tft.setTextColor(C_GRAY);
  tft.setTextSize(1);
  tft.setCursor(10, 305);
  tft.print("Press RESET to go again");
}

// ============================================================
// SCREEN: ABORTED
// ============================================================
void showAborted() {
  tftClear();
  tftTitle("Race Aborted", C_RED);
  tft.setTextColor(C_GRAY);
  tft.setTextSize(2);
  tft.setCursor(10, 80);
  tft.print("Waiting for");
  tft.setCursor(10, 110);
  tft.print("start signal...");
}

// ============================================================
// TOUCH DETECTION
// ============================================================
bool readTouch() {
  if (digitalRead(FINISH_PIN) == LOW) {
    unsigned long pressStart = millis();
    while (millis() - pressStart < TOUCH_DEBOUNCE_MS) {
      if (digitalRead(FINISH_PIN) == HIGH) return false;
    }
    return true;
  }
  return false;
}

// ============================================================
// ESP-NOW CALLBACKS
// ============================================================
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void onReceive(const uint8_t *mac_addr, const uint8_t *data, int len) {
  if (len < 1) return;

  if (data[0] == 1 && !raceStarted && !finished) {
    StartMessage* msg = (StartMessage*)data;
    poolType         = msg->poolType;
    swimDistance     = msg->distance;
    expectedTouches  = calcExpectedTouches(poolType, swimDistance);
    touchCount       = 0;
    lastTouchTime    = 0;
    memset(splitTimes, 0, sizeof(splitTimes));
    memset(legTimes,   0, sizeof(legTimes));
    startTime        = millis();
    raceStarted      = true;

    // Swim is now active: white off, red begins blinking.
    // Green is left untouched here (still off at this point).
    digitalWrite(LED_WHITE, LOW);
    swimActive = true;
    redBlinkState = true;
    digitalWrite(LED_RED, HIGH);
    lastBlinkToggle = millis();

    Serial.print("START received. Touches expected: ");
    Serial.println(expectedTouches);
  }

  if (data[0] == 4) {
    abortRequested = true;
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  pinMode(FINISH_PIN, INPUT_PULLUP);
  pinMode(BTN_RESET,  INPUT_PULLUP);
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_WHITE,  OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  digitalWrite(LED_RED,   LOW);
  digitalWrite(LED_WHITE, HIGH); // on permanently whenever powered, per spec
  digitalWrite(LED_GREEN, LOW);

  // Explicit SPI pins and library-controlled CS leave GPIO 5 for the reset button.
  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  tft.begin(TFT_SPI_HZ);
  tft.setRotation(3);
  tftClear();

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    return;
  }
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, starterMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  showReady();
  Serial.println("Touchpad ready.");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {

  // --- ABORT ---
  if (abortRequested) {
    abortRequested = false;
    raceStarted    = false;
    finished       = false;
    touchCount     = 0;
    memset(splitTimes, 0, sizeof(splitTimes));
    memset(legTimes,   0, sizeof(legTimes));

    swimActive = false;
    digitalWrite(LED_WHITE, HIGH);
    digitalWrite(LED_RED,   LOW);
    digitalWrite(LED_GREEN, LOW); // unchanged from original behavior

    showAborted();
    Serial.println("Aborted by Starter.");
  }

  // Non-blocking red LED blink while swim is in progress
  if (swimActive) {
    if (millis() - lastBlinkToggle >= BLINK_INTERVAL_MS) {
      lastBlinkToggle = millis();
      redBlinkState = !redBlinkState;
      digitalWrite(LED_RED, redBlinkState ? HIGH : LOW);
    }
  }

  // --- RACING ---
  if (raceStarted && !finished) {
    unsigned long elapsed = millis() - startTime;
    // Keep the live timer until the first touch, then hold the split screen.
    if (touchCount == 0) showLiveTimer(elapsed);

    bool graceOk    = elapsed > TOUCH_GRACE_MS;
    bool cooldownOk = (lastTouchTime == 0) || ((millis() - lastTouchTime) > TOUCH_COOLDOWN_MS);

    if (graceOk && cooldownOk && readTouch()) {
      unsigned long touchTime = millis() - startTime;
      lastTouchTime = millis();

      unsigned long prevTime   = (touchCount == 0) ? 0 : splitTimes[touchCount - 1];
      legTimes[touchCount]     = touchTime - prevTime;
      splitTimes[touchCount]   = touchTime;
      touchCount++;

      Serial.print("Touch "); Serial.print(touchCount);
      Serial.print(" at ");   Serial.print(touchTime); Serial.println(" ms");

      if (touchCount >= expectedTouches) {
        finished    = true;
        raceStarted = false;

        // Swim is over: white back on, red off (no longer blinking),
        // green on to indicate finish — same as original behavior.
        swimActive = false;
        digitalWrite(LED_WHITE, HIGH);
        digitalWrite(LED_RED,   LOW);
        digitalWrite(LED_GREEN, HIGH);

        showFinish();

        FinishMessage finishMsg = {3, (uint32_t)splitTimes[touchCount - 1]};
        esp_now_send(starterMAC, (uint8_t*)&finishMsg, sizeof(finishMsg));

        Serial.print("FINISH. Total: ");
        Serial.print(splitTimes[touchCount - 1]);
        Serial.println(" ms");
      } else {
        // Redraw all recorded splits only when another touch is registered.
        showLiveTimer(touchTime);
      }
    }
  }

  // --- RESET BUTTON ---
  if (finished && digitalRead(BTN_RESET) == LOW) {
    delay(50);
    if (digitalRead(BTN_RESET) == LOW) {
      while (digitalRead(BTN_RESET) == LOW) delay(10);

      finished    = false;
      raceStarted = false;
      touchCount  = 0;
      memset(splitTimes, 0, sizeof(splitTimes));
      memset(legTimes,   0, sizeof(legTimes));

      swimActive = false;
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_WHITE, HIGH);
      digitalWrite(LED_RED,   LOW);

      showReady();

      uint8_t resetMsg = 2;
      esp_now_send(starterMAC, &resetMsg, sizeof(resetMsg));

      Serial.println("Touchpad reset.");
    }
  }
}
