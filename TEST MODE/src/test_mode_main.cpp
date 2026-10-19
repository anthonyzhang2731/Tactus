#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "test_mode_state.h"

// Same ESP32, display wiring, rotation, and active-LOW switches as Finish Touchpad.
constexpr uint8_t TFT_CS = 13;
constexpr uint8_t TFT_RST = 26;
constexpr uint8_t TFT_DC = 25;
constexpr uint8_t TFT_MOSI = 23;
constexpr uint8_t TFT_SCK = 18;
constexpr uint8_t FINISH_PIN = 15;
constexpr uint8_t BTN_RESET = 5;
constexpr uint8_t LED_RED = 22;
constexpr uint8_t LED_WHITE = 14;
constexpr uint8_t LED_GREEN = 27;
constexpr uint32_t TFT_SPI_HZ = 20000000;

constexpr uint16_t C_BG = 0x0000;
constexpr uint16_t C_WHITE = 0xFFFF;
constexpr uint16_t C_CYAN = 0x07FF;
constexpr uint16_t C_GREEN = 0x07E0;
constexpr uint16_t C_GRAY = 0x7BEF;

Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RST);
tactus::TouchTest test;

enum class Status { Waiting, Ready, Release, Detected, Complete };
Status lastStatus = Status::Waiting;
bool statusDrawn = false;
char lastCounter[8] = {};
uint16_t lastCount = UINT16_MAX;
bool redOn = false;
uint32_t lastBlink = 0;

void printAt(int16_t x, int16_t y, uint8_t size, uint16_t color, const char* text) {
  tft.setTextColor(color, C_BG);
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.print(text);
}

void drawFrame() {
  tft.fillScreen(C_BG);
  printAt(10, 10, 2, C_CYAN, "TACTUS | TEST MODE");
  tft.drawFastHLine(0, 34, tft.width(), C_GRAY);
  printAt(10, 50, 2, C_GRAY, "Successful touches");
  tft.drawRect(10, 132, tft.width() - 20, 10, C_GRAY);
  tft.drawFastHLine(10, 198, tft.width() - 20, C_GRAY);
}

void drawCounter() {
  if (test.count() == lastCount) return;
  char counter[8];
  snprintf(counter, sizeof(counter), "%3u/400", unsigned(test.count()));
  for (uint8_t i = 0; i < 7; ++i) {
    if (counter[i] != lastCounter[i]) {
      // Opaque, fixed-width cells keep the counter visible without screen clears.
      tft.drawChar(10 + i * 30, 78, counter[i], C_WHITE, C_BG, 5);
      lastCounter[i] = counter[i];
    }
  }
  const int16_t barWidth = tft.width() - 24;
  const int16_t filled = uint32_t(test.count()) * barWidth / tactus::TEST_LIMIT;
  if (filled > 0) tft.fillRect(12, 134, filled, 6, C_CYAN);
  if (filled < barWidth) tft.fillRect(12 + filled, 134, barWidth - filled, 6, C_BG);
  lastCount = test.count();
}

void drawFooter() {
  tft.fillRect(10, 205, tft.width() - 20, 28, C_BG);
  if (test.phase() == tactus::TestPhase::Ready) {
    printAt(10, 205, 1, C_GRAY, "Press the touchpad RESET button to start.");
    printAt(10, 221, 1, C_GRAY, "Touchpad only - starter not required.");
  } else if (test.phase() == tactus::TestPhase::Complete) {
    printAt(10, 205, 1, C_GRAY, "Result held. Press RESET for a new test.");
    printAt(10, 221, 1, C_GRAY, "A new test starts at zero.");
  } else {
    printAt(10, 205, 1, C_GRAY, "Press, then fully release the touchpad.");
    printAt(10, 221, 1, C_GRAY, "Stops automatically at 400 touches.");
  }
}

void drawStatus(uint32_t now) {
  Status next;
  if (test.phase() == tactus::TestPhase::Ready) next = Status::Waiting;
  else if (test.phase() == tactus::TestPhase::Complete) next = Status::Complete;
  else if (test.hitFeedback(now)) next = Status::Detected;
  else if (!test.needsRelease()) next = Status::Ready;
  else next = Status::Release;

  if (statusDrawn && next == lastStatus) return;
  tft.fillRect(10, 151, tft.width() - 20, 40, C_BG);
  switch (next) {
    case Status::Waiting:
      printAt(10, 155, 2, C_CYAN, "Press button to start");
      break;
    case Status::Ready:
      printAt(10, 155, 2, C_CYAN, "Ready for touch");
      break;
    case Status::Release:
      printAt(10, 155, 2, C_GRAY, "Release touchpad");
      break;
    case Status::Detected:
      printAt(10, 155, 2, C_GREEN, "Touch detected!");
      break;
    case Status::Complete:
      printAt(10, 151, 2, C_GREEN, "TEST COMPLETE");
      printAt(10, 174, 2, C_GREEN, "400/400 detected");
      break;
  }
  lastStatus = next;
  statusDrawn = true;
}

void updateLeds(uint32_t now) {
  const bool running = test.phase() == tactus::TestPhase::Running;
  const bool complete = test.phase() == tactus::TestPhase::Complete;
  if (running && uint32_t(now - lastBlink) >= 500) {
    lastBlink = now;
    redOn = !redOn;
  }
  if (!running) redOn = false;
  digitalWrite(LED_RED, redOn ? HIGH : LOW);
  digitalWrite(LED_WHITE, running ? LOW : HIGH);
  digitalWrite(LED_GREEN, (complete || test.hitFeedback(now)) ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(FINISH_PIN, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_WHITE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_WHITE, HIGH);
  digitalWrite(LED_GREEN, LOW);

  // Explicit pins keep the hardware SPI default CS (GPIO 5) off the start button.
  SPI.begin(TFT_SCK, -1, TFT_MOSI, -1);
  tft.begin(TFT_SPI_HZ);
  tft.setRotation(3);  // Existing landscape orientation: 320 x 240.
  tft.setTextWrap(false);
  test.begin(millis(), digitalRead(BTN_RESET) == LOW, digitalRead(FINISH_PIN) == LOW);
  drawFrame();
  drawCounter();
  drawFooter();
  drawStatus(millis());
  Serial.println("TACTUS TEST MODE ready. Press the touchpad RESET button to start.");
}

void loop() {
  const uint32_t now = millis();
  const tactus::TestPhase previousPhase = test.phase();
  const bool accepted = test.update(now, digitalRead(BTN_RESET) == LOW,
                                   digitalRead(FINISH_PIN) == LOW);
  if (test.phase() != previousPhase) {
    if (test.phase() == tactus::TestPhase::Running) {
      lastBlink = now;
      redOn = true;
      Serial.println("Test started: 0/400. Release the pad between presses.");
    }
    drawFooter();
  }
  if (accepted) {
    Serial.printf("Touch detected: %u/400 at %lu ms\n", unsigned(test.count()),
                  static_cast<unsigned long>(now));
    if (test.phase() == tactus::TestPhase::Complete) {
      Serial.println("TEST COMPLETE: 400/400. Counter locked until a new button press.");
    }
  }
  updateLeds(now);
  drawCounter();
  drawStatus(now);
  delay(1);  // Yield without a race cooldown or a blocking press/release wait.
}
