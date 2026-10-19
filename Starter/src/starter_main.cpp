#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <DFRobotDFPlayerMini.h>
#include <HardwareSerial.h>

// ============================================================
// CONFIGURATION
// ============================================================
#define TAKEYOURMARKS_MS  1300

// ============================================================
// PIN DEFINITIONS
// ============================================================
#define TFT_CS    13
#define TFT_RST   26
#define TFT_DC    25
#define TFT_MOSI  23
#define TFT_SCK   18

#define BTN_CANCEL    15
#define BTN_NAV       5
#define BTN_CONFIRM   22

#define LED_RED   14
#define LED_WHITE 27

HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;
bool dfPlayerReady = false;

// ============================================================
// DISPLAY
// ============================================================
// Hardware SPI uses the existing display wiring; CS remains library-controlled.
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);
constexpr uint32_t TFT_SPI_HZ = 20000000;

#define C_BG      0x0000
#define C_WHITE   0xFFFF
#define C_CYAN    0x07FF
#define C_RED     0xF800
#define C_GREEN   0x07E0
#define C_GRAY    0x7BEF

// ============================================================
// ESP-NOW
// ============================================================
uint8_t touchpadMAC[] = {0xB0, 0xCB, 0xD8, 0xCB, 0x99, 0x3C};

struct StartMessage {
  uint8_t  type;
  uint8_t  poolType;
  uint16_t distance;
};

// Shared wire format with the Touchpad: finish type followed by elapsed ms.
struct __attribute__((packed)) FinishMessage {
  uint8_t  type;
  uint32_t totalTimeMs;
};
static_assert(sizeof(FinishMessage) == 5, "Finish message must be 5 bytes");

// ============================================================
// STATE MACHINE
// ============================================================
enum StarterState {
  POOL_SELECT,
  DIST_SELECT,
  DELAY_SELECT,
  READY,
  RUNNING,
  SWIM_IN_PROGRESS,
  SWIM_DONE,
  ABORT_CONFIRM
};
volatile StarterState currentState = POOL_SELECT;

volatile bool flagSwimDone      = false;
volatile bool flagTouchpadReset = false;
volatile uint32_t finalTimeMs   = 0;

// LED state: white is solid ON whenever powered, in every state,
// EXCEPT during swim-in-progress, where it's off and red blinks instead.
bool swimActive        = false;
bool redBlinkState      = false;
unsigned long lastBlinkToggle = 0;
#define BLINK_INTERVAL_MS 500 // toggle every 500ms = 1 full on/off cycle per second

// ============================================================
// SELECTION VARIABLES
// ============================================================
uint8_t poolIndex  = 0;
uint8_t distIndex  = 0;
uint8_t delayIndex = 0;

const char* poolLabels[]     = {"25 yd", "25 m", "50 m"};
const uint8_t delayOptions[] = {10, 15, 20, 30};
const uint8_t delayCount     = 4;

const uint16_t dist25[]  = {25, 50, 100, 200};
const uint16_t dist50[]  = {50, 100, 200, 400};
const uint8_t  distCount = 4;

// ============================================================
// ROW GEOMETRY
// ============================================================
#define ROW_START_Y  50
#define ROW_HEIGHT   40
#define ROW_H        28
#define ROW_W        320

int rowY(int i) { return ROW_START_Y + i * ROW_HEIGHT; }

// ============================================================
// DISPLAY HELPERS
// ============================================================
void formatTime(char* buf, size_t size, unsigned long ms) {
  unsigned long mins = ms / 60000;
  unsigned long secs = (ms / 1000) % 60;
  unsigned long cs   = (ms % 1000) / 10;
  snprintf(buf, size, "%lu:%02lu.%02lu", mins, secs, cs);
}

void tftClear() {
  tft.fillScreen(C_BG);
}

void tftTitle(const char* title) {
  tft.setTextColor(C_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print(title);
  tft.drawFastHLine(0, 34, 320, C_GRAY);
}

void drawRow(const char* label, int index, bool selected) {
  int y = rowY(index);
  if (selected) {
    tft.fillRect(0, y - 4, ROW_W, ROW_H, C_CYAN);
    tft.setTextColor(C_BG);
  } else {
    tft.fillRect(0, y - 4, ROW_W, ROW_H, C_BG);
    tft.setTextColor(C_WHITE);
  }
  tft.setTextSize(2);
  tft.setCursor(10, y);
  tft.print(label);
}

// ============================================================
// SCREEN: POOL SELECT
// ============================================================
void showPoolSelect() {
  tftClear();
  tftTitle("Pool Length");
  for (int i = 0; i < 3; i++) {
    drawRow(poolLabels[i], i, i == poolIndex);
  }
  tft.setTextColor(C_GRAY);
  tft.setTextSize(1);
  tft.setCursor(10, 225);
  tft.print("NAV: scroll   CONFIRM: select");
}

void updatePoolHighlight(uint8_t oldIdx, uint8_t newIdx) {
  drawRow(poolLabels[oldIdx], oldIdx, false);
  drawRow(poolLabels[newIdx], newIdx, true);
}

// ============================================================
// SCREEN: DISTANCE SELECT
// ============================================================
static char distLabels[4][12];

void buildDistLabels() {
  const uint16_t* dists = (poolIndex == 2) ? dist50 : dist25;
  const char* unit = (poolIndex == 0) ? " yd" : " m";
  for (int i = 0; i < distCount; i++) {
    snprintf(distLabels[i], sizeof(distLabels[i]), "%u%s", dists[i], unit);
  }
}

void showDistSelect() {
  buildDistLabels();
  tftClear();
  tftTitle("Swim Distance");
  for (int i = 0; i < distCount; i++) {
    drawRow(distLabels[i], i, i == distIndex);
  }
  tft.setTextColor(C_GRAY);
  tft.setTextSize(1);
  tft.setCursor(10, 225);
  tft.print("NAV: scroll   CONFIRM: next   CANCEL: back");
}

void updateDistHighlight(uint8_t oldIdx, uint8_t newIdx) {
  drawRow(distLabels[oldIdx], oldIdx, false);
  drawRow(distLabels[newIdx], newIdx, true);
}

// ============================================================
// SCREEN: DELAY SELECT
// ============================================================
static char delayLabels[4][10];

void buildDelayLabels() {
  for (int i = 0; i < delayCount; i++) {
    snprintf(delayLabels[i], sizeof(delayLabels[i]), "%u sec", delayOptions[i]);
  }
}

void showDelaySelect() {
  buildDelayLabels();
  tftClear();
  tftTitle("Start Delay");
  for (int i = 0; i < delayCount; i++) {
    drawRow(delayLabels[i], i, i == delayIndex);
  }
  tft.setTextColor(C_GRAY);
  tft.setTextSize(1);
  tft.setCursor(10, 225);
  tft.print("NAV: scroll   CONFIRM: next   CANCEL: back");
}

void updateDelayHighlight(uint8_t oldIdx, uint8_t newIdx) {
  drawRow(delayLabels[oldIdx], oldIdx, false);
  drawRow(delayLabels[newIdx], newIdx, true);
}

// ============================================================
// SCREEN: READY
// ============================================================
void showReady() {
  const uint16_t* dists = (poolIndex == 2) ? dist50 : dist25;
  const char* unit = (poolIndex == 0) ? "yd" : "m";
  tftClear();
  tftTitle("Ready");

  char buf[32];
  snprintf(buf, sizeof(buf), "%s | %u%s", poolLabels[poolIndex], dists[distIndex], unit);
  tft.setTextColor(C_WHITE); tft.setTextSize(2);
  tft.setCursor(10, 50); tft.print(buf);

  snprintf(buf, sizeof(buf), "Delay: %u sec", delayOptions[delayIndex]);
  tft.setTextColor(C_GRAY); tft.setTextSize(2);
  tft.setCursor(10, 82); tft.print(buf);

  tft.fillRect(0, 145, 320, 50, C_CYAN);
  tft.setTextColor(C_BG); tft.setTextSize(3);
  tft.setCursor(90, 157); tft.print("START");

  tft.setTextColor(C_GRAY); tft.setTextSize(1);
  tft.setCursor(10, 225); tft.print("CANCEL: back to menu");
}

// ============================================================
// SCREEN: COUNTDOWN
// ============================================================
int16_t countdownX = 0;
uint8_t countdownChars = 0;

void showCountdownInit(int secs) {
  tftClear();
  tftTitle("Get Ready");
  tft.setTextColor(C_GRAY); tft.setTextSize(1);
  tft.setCursor(10, 225); tft.print("CANCEL to abort");
  tft.setTextColor(C_WHITE); tft.setTextSize(8);
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", secs);
  int x = (secs >= 10) ? 60 : 120;
  tft.setCursor(x, 80); tft.print(buf);
  countdownX = x;
  countdownChars = strlen(buf);
}

void updateCountdownNumber(int secs) {
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", secs);
  int x = (secs >= 10) ? 60 : 120;
  uint8_t chars = strlen(buf);
  if (x != countdownX || chars != countdownChars) {
    // Clear only the old number when changing from two digits to one.
    tft.fillRect(countdownX, 80, countdownChars * 48, 64, C_BG);
  }
  tft.setTextColor(C_WHITE, C_BG); tft.setTextSize(8);
  tft.setCursor(x, 80); tft.print(buf);
  countdownX = x;
  countdownChars = chars;
}

// ============================================================
// SCREEN: TAKE YOUR MARKS
// ============================================================
void showTakeYourMarks() {
  tftClear();
  tft.setTextColor(C_CYAN); tft.setTextSize(3);
  tft.setCursor(20, 100); tft.print("Take your");
  tft.setCursor(60, 135); tft.print("marks...");
}

// ============================================================
// SCREEN: SWIM IN PROGRESS — static
// ============================================================
void showSwimInProgress() {
  tftClear();
  tft.setTextColor(C_CYAN); tft.setTextSize(3);
  tft.setCursor(30, 90);  tft.print("Swim in");
  tft.setCursor(30, 130); tft.print("progress...");
  tft.setTextColor(C_GRAY); tft.setTextSize(1);
  tft.setCursor(10, 225); tft.print("CANCEL to abort");
}

// ============================================================
// SCREEN: ABORT CONFIRM
// ============================================================
void showAbortConfirm() {
  tftClear();
  tftTitle("Abort Race?");
  tft.setTextColor(C_GREEN); tft.setTextSize(2);
  tft.setCursor(10, 80); tft.print("CONFIRM: go back");
  tft.setTextColor(C_RED); tft.setTextSize(2);
  tft.setCursor(10, 120); tft.print("CANCEL: abort");
}

// ============================================================
// SCREEN: SWIM DONE
// ============================================================
void showSwimDone() {
  tftClear();
  tftTitle("Swim Done");
  tft.setTextColor(C_GREEN); tft.setTextSize(2);
  tft.setCursor(10, 80); tft.print("Touchpad recorded");

  char buf[12];
  formatTime(buf, sizeof(buf), finalTimeMs);
  tft.setTextColor(C_WHITE); tft.setTextSize(4);
  tft.setCursor(10, 110); tft.print(buf);

  tft.setTextColor(C_GRAY); tft.setTextSize(2);
  tft.setCursor(10, 170); tft.print("Reset touchpad");
  tft.setCursor(10, 200); tft.print("to continue.");
}

// ============================================================
// BUTTON HELPER
// ============================================================
bool btnPressed(uint8_t pin) {
  if (digitalRead(pin) == LOW) {
    delay(50);
    if (digitalRead(pin) == LOW) {
      while (digitalRead(pin) == LOW) delay(10);
      return true;
    }
  }
  return false;
}

// ============================================================
// DFPLAYER DEBUG HELPERS
// ============================================================
// Decodes DFPlayer error/status codes into readable serial output.
// Call this any time dfPlayer.available() is true to see what it reported.
void printDFPlayerDetail(uint8_t type, int value) {
  switch (type) {
    case TimeOut:
      Serial.println("[DFPlayer] Time Out! (no response from module — check UART wiring/baud)");
      break;
    case WrongStack:
      Serial.println("[DFPlayer] Wrong Stack (bad response frame)");
      break;
    case DFPlayerCardInserted:
      Serial.println("[DFPlayer] SD card inserted");
      break;
    case DFPlayerCardRemoved:
      Serial.println("[DFPlayer] SD card removed");
      break;
    case DFPlayerCardOnline:
      Serial.println("[DFPlayer] SD card online");
      break;
    case DFPlayerPlayFinished:
      Serial.print("[DFPlayer] Finished playing track: ");
      Serial.println(value);
      break;
    case DFPlayerError:
      Serial.print("[DFPlayer] ERROR code: ");
      Serial.println(value);
      switch (value) {
        case Busy:
          Serial.println("  -> Busy: SD card not found / not readable. Check FAT32 format + card seated.");
          break;
        case Sleeping:
          Serial.println("  -> Sleeping: module in sleep mode.");
          break;
        case SerialWrongStack:
          Serial.println("  -> Serial Wrong Stack: bad UART frame received.");
          break;
        case CheckSumNotMatch:
          Serial.println("  -> Checksum mismatch: noisy/unstable UART line.");
          break;
        case FileIndexOut:
          Serial.println("  -> File Index Out: requested track number doesn't exist on card.");
          break;
        case FileMismatch:
          Serial.println("  -> File Mismatch.");
          break;
        case Advertise:
          Serial.println("  -> Advertise error.");
          break;
        default:
          Serial.println("  -> Unrecognized error value.");
          break;
      }
      break;
    default:
      Serial.print("[DFPlayer] Unrecognized status type: ");
      Serial.print(type);
      Serial.print("  value: ");
      Serial.println(value);
      break;
  }
}

// Call this anywhere you want a snapshot of DFPlayer's reported state.
void pollDFPlayerStatus(const char* context) {
  if (dfPlayer.available()) {
    Serial.print("[DFPlayer status @ ");
    Serial.print(context);
    Serial.println("]");
    printDFPlayerDetail(dfPlayer.readType(), dfPlayer.read());
  } else {
    Serial.print("[DFPlayer] No status update available @ ");
    Serial.println(context);
  }
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
  if (data[0] == 3 && len == sizeof(FinishMessage) && currentState == SWIM_IN_PROGRESS) {
    FinishMessage msg;
    memcpy(&msg, data, sizeof(msg));
    finalTimeMs = msg.totalTimeMs;
    currentState = SWIM_DONE;
    flagSwimDone = true;
  }
  if (data[0] == 2 && currentState == SWIM_DONE) {
    flagTouchpadReset = true;
    poolIndex = distIndex = delayIndex = 0;
    currentState = POOL_SELECT;
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500); // give serial monitor time to attach before init logs fire

  pinMode(BTN_CANCEL,  INPUT_PULLUP);
  pinMode(BTN_NAV,     INPUT_PULLUP);
  pinMode(BTN_CONFIRM, INPUT_PULLUP);
  pinMode(LED_RED,     OUTPUT);
  pinMode(LED_WHITE,   OUTPUT);
  digitalWrite(LED_RED,   LOW);
  digitalWrite(LED_WHITE, HIGH); // on permanently when powered

  // Explicit SPI pins and library-controlled CS leave GPIO 5 for the navigation button.
  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  tft.begin(TFT_SPI_HZ);
  tft.setRotation(3);

  Serial.println("=== Starter boot ===");

  dfSerial.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("Initializing DFPlayer...");

  if (!dfPlayer.begin(dfSerial)) {
    Serial.println("DFPlayer init FAILED");
    Serial.println("Check: UART TX/RX wiring, 1k resistor on RX line, SD card seated, SD card is FAT32.");
    pollDFPlayerStatus("init-fail");
    dfPlayerReady = false;
  } else {
    Serial.println("DFPlayer OK");
    dfPlayer.volume(30);
    dfPlayer.EQ(DFPLAYER_EQ_BASS);
    Serial.println("Volume set to 30 (max), EQ set to BASS");
    dfPlayerReady = true;
  }

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init FAILED");
    return;
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, touchpadMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  showPoolSelect();
  Serial.println("Starter ready.");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {

  // Continuously drain any pending DFPlayer status messages so
  // errors show up in serial even outside of play() calls.
  if (dfPlayer.available()) {
    pollDFPlayerStatus("loop-poll");
  }

  if (flagSwimDone) {
    flagSwimDone = false;
    swimActive = false;
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_WHITE, HIGH);
    showSwimDone();
  }

  // Non-blocking red LED blink while swim is in progress
  if (swimActive) {
    if (millis() - lastBlinkToggle >= BLINK_INTERVAL_MS) {
      lastBlinkToggle = millis();
      redBlinkState = !redBlinkState;
      digitalWrite(LED_RED, redBlinkState ? HIGH : LOW);
    }
  }
  if (flagTouchpadReset) {
    flagTouchpadReset = false;
    showPoolSelect();
  }

  // --------------------------------------------------------
  // POOL SELECT
  // --------------------------------------------------------
  if (currentState == POOL_SELECT) {
    if (btnPressed(BTN_NAV)) {
      uint8_t old = poolIndex;
      poolIndex = (poolIndex + 1) % 3;
      distIndex = 0;
      updatePoolHighlight(old, poolIndex);
    }
    if (btnPressed(BTN_CONFIRM)) {
      currentState = DIST_SELECT;
      showDistSelect();
    }
  }

  // --------------------------------------------------------
  // DIST SELECT
  // --------------------------------------------------------
  else if (currentState == DIST_SELECT) {
    if (btnPressed(BTN_NAV)) {
      uint8_t old = distIndex;
      distIndex = (distIndex + 1) % distCount;
      updateDistHighlight(old, distIndex);
    }
    if (btnPressed(BTN_CANCEL)) {
      currentState = POOL_SELECT;
      showPoolSelect();
    }
    if (btnPressed(BTN_CONFIRM)) {
      currentState = DELAY_SELECT;
      showDelaySelect();
    }
  }

  // --------------------------------------------------------
  // DELAY SELECT
  // --------------------------------------------------------
  else if (currentState == DELAY_SELECT) {
    if (btnPressed(BTN_NAV)) {
      uint8_t old = delayIndex;
      delayIndex = (delayIndex + 1) % delayCount;
      updateDelayHighlight(old, delayIndex);
    }
    if (btnPressed(BTN_CANCEL)) {
      currentState = DIST_SELECT;
      showDistSelect();
    }
    if (btnPressed(BTN_CONFIRM)) {
      currentState = READY;
      showReady();
    }
  }

  // --------------------------------------------------------
  // READY
  // --------------------------------------------------------
  else if (currentState == READY) {
    if (btnPressed(BTN_CANCEL)) {
      currentState = POOL_SELECT;
      poolIndex = distIndex = delayIndex = 0;
      showPoolSelect();
    }
    if (btnPressed(BTN_CONFIRM)) {
      currentState = RUNNING;

      uint8_t countSecs = delayOptions[delayIndex];
      showCountdownInit(countSecs);

      for (int i = countSecs; i > 0; i--) {
        if (i < countSecs) updateCountdownNumber(i);
        unsigned long t = millis();
        while (millis() - t < 1000) {
          if (btnPressed(BTN_CANCEL)) {
            currentState = POOL_SELECT;
            poolIndex = distIndex = delayIndex = 0;
            showPoolSelect();
            return;
          }
          delay(10);
        }
      }

      showTakeYourMarks();

      Serial.print("Playing track 1 (Take your marks). dfPlayerReady=");
      Serial.println(dfPlayerReady ? "true" : "false");
      dfPlayer.play(1);
      delay(50);
      pollDFPlayerStatus("after play(1)");

      delay(TAKEYOURMARKS_MS);

      delay(random(1000, 3001));

      const uint16_t* dists = (poolIndex == 2) ? dist50 : dist25;
      StartMessage msg;
      msg.type     = 1;
      msg.poolType = poolIndex;
      msg.distance = dists[distIndex];
      esp_now_send(touchpadMAC, (uint8_t*)&msg, sizeof(msg));

      Serial.println("Playing track 2 (start beep).");
      dfPlayer.play(2);
      delay(50);
      pollDFPlayerStatus("after play(2)");

      digitalWrite(LED_WHITE, HIGH);
      delay(300);
      digitalWrite(LED_WHITE, LOW);

      digitalWrite(LED_RED, HIGH);
      showSwimInProgress();
      currentState = SWIM_IN_PROGRESS;
    }
  }

  // --------------------------------------------------------
  // SWIM IN PROGRESS
  // --------------------------------------------------------
  else if (currentState == SWIM_IN_PROGRESS) {
    if (btnPressed(BTN_CANCEL)) {
      currentState = ABORT_CONFIRM;
      showAbortConfirm();
    }
  }

  // --------------------------------------------------------
  // ABORT CONFIRM
  // --------------------------------------------------------
  else if (currentState == ABORT_CONFIRM) {
    if (btnPressed(BTN_CONFIRM)) {
      currentState = SWIM_IN_PROGRESS;
      showSwimInProgress();
    }
    if (btnPressed(BTN_CANCEL)) {
      uint8_t abortMsg = 4;
      esp_now_send(touchpadMAC, &abortMsg, sizeof(abortMsg));
      digitalWrite(LED_RED, LOW);
      poolIndex = distIndex = delayIndex = 0;
      currentState = POOL_SELECT;
      showPoolSelect();
      Serial.println("Race aborted.");
    }
  }
}
