#include <Wire.h>
#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h> 

// TFT Pins for Nano ESP32
// D11 -> MOSI
// D13 -> SCK
#define TFT_DC   2
#define TFT_RST  3
#define TFT_CS   4
#define TFT_LED  20  // Backlight pin if you're controlling it from the Nano

// Initialize the display object
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Initialize the capacitive touchscreen object
Adafruit_FT6206 ctp;

// --- Matrix Digital Rain Parameters ---
const int SCREEN_W = 240;  // ILI9341 default width (portrait)
const int SCREEN_H = 320;  // ILI9341 default height (portrait)

// Text scaling for the rain: 1 (many small columns)
const int TEXT_SCALE = 1;

// Built-in 5x7 font is effectively ~6x8 per character with spacing
const int CHAR_W = 6 * TEXT_SCALE;
const int CHAR_H = 8 * TEXT_SCALE;

const int NUM_COLS = SCREEN_W / CHAR_W;
const int NUM_ROWS = SCREEN_H / CHAR_H;

// Column-specific tail length range (in *rows*)
const int MIN_TAIL_LEN = 14;   // <- updated
const int MAX_TAIL_LEN = 20;   // <- updated

// Global speed range (ms per row step)
const uint16_t MIN_INTERVAL = 60;   // fast
const uint16_t MAX_INTERVAL = 160;  // slow

// --- Word/clock reveal parameters ---
const char REVEAL_TEXT[]          = "12:00";
const int  REVEAL_LENGTH          = sizeof(REVEAL_TEXT) - 1; // exclude null terminator
const uint16_t REVEAL_DURATION_MS = 3600; // <- doubled from 1800 (3.6 seconds)
const int REVEAL_TEXT_SIZE        = 6;    // BIGGER text for the clock

// Per-column state
struct ColumnState {
  int headRow;            // current head row (can be negative before entering screen)
  uint16_t intervalMs;    // ms per row step
  uint32_t lastUpdateMs;  // millis() when this column last updated
  uint8_t tailLength;     // per-column tail length
};

ColumnState columns[NUM_COLS];

// Character buffer so we can redraw trail characters
char glyphs[NUM_COLS][NUM_ROWS];

// Colors
uint16_t matrixHeadColor;
uint16_t matrixTrailBright;
uint16_t matrixTrailDim;
uint16_t matrixTrailDark;
uint16_t matrixBgColor;

// Reveal state
struct Reveal {
  bool active;
  uint32_t startMs;
  uint32_t endMs;
};

Reveal reveal = { false, 0, 0 };

// Global reveal area bounds (calculated once)
int revealAreaX, revealAreaY, revealAreaW, revealAreaH;
int startRevealCol, endRevealCol; // Optimization: column range for reveal

char randomGlyph();
void initColumns();
void resetColumn(int col);
void updateMatrixRain();
void handleTouch();
void updateReveal();
void calcRevealArea();
void drawRainChar(int x, int row, char ch, uint16_t color); // New helper

void setup() {
  Serial.begin(115200);
  Serial.println("Starting TFT and Touch Initialization...");
 
  // Backlight control (comment these out if your backlight is tied directly to 3.3V)
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  // --- TFT Display Setup ---
  tft.begin();
  tft.fillScreen(ILI9341_BLACK);


  // --- Touchscreen Setup (blocking with on-screen error) ---
  if (!ctp.begin()) {
    Serial.println("ERROR: Couldn't start FT6206 touchscreen controller.");

    tft.fillScreen(ILI9341_BLACK);
    tft.setTextSize(2);
    tft.setTextWrap(true);
    tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
    tft.setCursor(10, 40);
    tft.println("Touchscreen error");
    tft.setTextSize(1);
    tft.println();
    tft.println("FT6206 not detected.");
    tft.println("Check wiring/power,");
    tft.println("then reset board.");

    // Halt: no further action taken
    while (1) {
      delay(1000);
    }
  }

  Serial.println("FT6206 touchscreen initialized successfully.");

  // Set up text parameters for Matrix rain
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(TEXT_SCALE);
  tft.setTextWrap(false);

  // Colors for rain (head + two trail levels)
  matrixHeadColor    = ILI9341_WHITE;
  matrixTrailBright  = ILI9341_GREEN;
  matrixTrailDim     = ILI9341_DARKGREEN;
  matrixTrailDark = tft.color565(0, 70, 0);
  matrixBgColor = ILI9341_BLACK;

  // Seed RNG
  randomSeed(analogRead(A0));

  // Initialize column states and glyphs
  initColumns();
  
  // Pre-calculate the reveal area dimensions
  calcRevealArea();

  reveal.active = false;

  Serial.print("NUM_COLS = ");
  Serial.println(NUM_COLS);
  Serial.print("NUM_ROWS = ");
  Serial.println(NUM_ROWS);
  Serial.println("Initialization complete. Starting Matrix rain...");
}

void loop() {
  handleTouch();      // Touch triggers reveal
  updateMatrixRain(); // Normal falling rain
  updateReveal();     // Overlay "12:00" if active
}

// ------------------- Matrix Rain Logic -------------------

void initColumns() {
  // Pre-fill glyphs
  for (int col = 0; col < NUM_COLS; col++) {
    for (int row = 0; row < NUM_ROWS; row++) {
      glyphs[col][row] = randomGlyph();
    }
  }

  // Initialize each column's state
  for (int col = 0; col < NUM_COLS; col++) {
    resetColumn(col);
    // Start headRow somewhere above the visible area so streams "fall in"
    columns[col].headRow = random(-NUM_ROWS, 0);
  }
}

// Speed tied to tail length, using global MIN_INTERVAL / MAX_INTERVAL
void resetColumn(int col) {
  ColumnState &c = columns[col];

  // Random tail length per column
  c.tailLength = random(MIN_TAIL_LEN, MAX_TAIL_LEN + 1);  // inclusive range

  // Map tail length to speed:
  // - shorter tail => smaller interval => faster
  // - longer tail  => larger interval  => slower
  int tailSpan = MAX_TAIL_LEN - MIN_TAIL_LEN;
  if (tailSpan < 1) tailSpan = 1; // avoid division by zero

  uint16_t baseInterval = MIN_INTERVAL +
    (uint16_t)((long)(c.tailLength - MIN_TAIL_LEN) * (MAX_INTERVAL - MIN_INTERVAL) / tailSpan);

  // Add a little jitter so similar tails aren't perfectly identical
  int16_t jitter = random(-15, 16); // -15 .. +15
  int32_t intervalWithJitter = (int32_t)baseInterval + jitter;
  if (intervalWithJitter < 20) intervalWithJitter = 20; // clamp to sane min

  c.intervalMs   = (uint16_t)intervalWithJitter;
  c.lastUpdateMs = millis();
}

char randomGlyph() {
  // Random printable ASCII; tweak range if you want other characters
  return (char)random(33, 126); // '!' to '~'
}

// Calculate the reveal area once
void calcRevealArea() {
  int charPixelW = 6 * REVEAL_TEXT_SIZE;
  int charPixelH = 8 * REVEAL_TEXT_SIZE;
  int textPixelW = REVEAL_LENGTH * charPixelW;

  // Calculate the padding size: 1 font-pixel row + 2 extra pixels
  int vPad = (1 * REVEAL_TEXT_SIZE) + 2;

  int tx = (SCREEN_W - textPixelW) / 2;
  if (tx < 0) tx = 0;
  
  int ty = (SCREEN_H - charPixelH) / 2;
  if (ty < 0) ty = 0;

  // Store globally
  revealAreaX = tx;
  revealAreaY = ty - vPad;
  revealAreaW = textPixelW;
  revealAreaH = charPixelH + vPad; 

  // Optimization: Calculate which columns intersect with this X-range
  // Column width is CHAR_W
  startRevealCol = revealAreaX / CHAR_W;
  endRevealCol   = (revealAreaX + revealAreaW) / CHAR_W;
}

// Helper to check if a rain cell overlaps the reveal text box
bool isRevealArea(int col, int y, int h) {
  if (!reveal.active) return false;

  // 1. Fast Check: Is this column even near the text?
  if (col < startRevealCol || col > endRevealCol) return false;

  // 2. Precise Check: Y-coordinate overlap
  return (y < revealAreaY + revealAreaH && y + h > revealAreaY);
}

// New helper to centralize drawing logic
void drawRainChar(int col, int row, char ch, uint16_t color) {
  // Boundary check
  if (row < 0 || row >= NUM_ROWS) return;

  int x = col * CHAR_W;
  int y = row * CHAR_H;

  // Check collision
  if (isRevealArea(col, y, CHAR_H)) return;

  // Draw
  tft.setCursor(x, y);
  tft.setTextColor(color, matrixBgColor);
  tft.write(ch);
}

// Overload for clearing (drawing a rectangle)
void clearRainChar(int col, int row) {
  // Boundary check
  if (row < 0 || row >= NUM_ROWS) return;

  int x = col * CHAR_W;
  int y = row * CHAR_H;

  // Check collision
  if (isRevealArea(col, y, CHAR_H)) return;

  // Erase
  tft.fillRect(x, y, CHAR_W, CHAR_H, matrixBgColor);
}

void updateMatrixRain() {
  uint32_t now = millis();

  // Ensure rain always uses the small text size
  tft.setTextSize(TEXT_SCALE);
  tft.setTextWrap(false);

  for (int col = 0; col < NUM_COLS; col++) {
    ColumnState &c = columns[col];

    if (now - c.lastUpdateMs < c.intervalMs) {
      continue;  // not time to advance this column yet
    }
    c.lastUpdateMs = now;

    int prevHead = c.headRow;
    c.headRow++;

    int tailLen = c.tailLength;

    // When the entire stream (head + tail) has passed off-screen, respawn
    if (c.headRow >= NUM_ROWS + tailLen) {
      // Reset this column with a new tail length and speed
      resetColumn(col);
      // Start above the screen again
      c.headRow = random(-NUM_ROWS, 0);

      // Use new tailLen after reset
      tailLen = c.tailLength;
    }

    // --- 1) Erase tail end cell ---
    clearRainChar(col, c.headRow - tailLen);

    // --- 2) Draw new head (white) ---
    if (c.headRow >= 0 && c.headRow < NUM_ROWS) {
      char ch = randomGlyph();
      glyphs[col][c.headRow] = ch;
      drawRainChar(col, c.headRow, ch, matrixHeadColor);
    }

    // --- 3) Previous head becomes bright trail ---
    if (prevHead >= 0 && prevHead < NUM_ROWS) {
      char ch = glyphs[col][prevHead];
      drawRainChar(col, prevHead, ch, matrixTrailBright);
    }

    // --- 4) Tail fade: bright -> dim -> dark -> cleared

    // Bright region length = 20% of tail
    int brightDist = tailLen / 5;      // ~20%
    if (brightDist < 1) brightDist = 1;
    if (brightDist >= tailLen) brightDist = tailLen - 1;

    // Dark region starts at 80% of tail
    int darkStartDist = (tailLen * 4) / 5;  // ~80%
    if (darkStartDist <= brightDist + 1) darkStartDist = brightDist + 2;
    if (darkStartDist >= tailLen) darkStartDist = tailLen - 1;

    // (a) Cell leaving the bright zone becomes *dim* trail
    int dimRow = c.headRow - (brightDist + 1);
    if (dimRow >= 0 && dimRow < NUM_ROWS) {
      int dist = c.headRow - dimRow;
      if (dist < tailLen && dist >= 0 && dist < darkStartDist) {
        drawRainChar(col, dimRow, glyphs[col][dimRow], matrixTrailDim);
      }
    }

    // (b) Cell entering the last 20% of the tail becomes *dark* trail
    int darkRow = c.headRow - darkStartDist;
    if (darkRow >= 0 && darkRow < NUM_ROWS) {
      int dist = c.headRow - darkRow;
      if (dist < tailLen && dist >= darkStartDist) {
        drawRainChar(col, darkRow, glyphs[col][darkRow], matrixTrailDark);
      }
    }
  }
}


// ------------------- Touch & Reveal Logic -------------------

void handleTouch() {
  if (!ctp.touched()) {
    return;
  }

  uint32_t now = millis();

  // Read touch point just to clear it; we don't use x/y for now
  TS_Point p = ctp.getPoint();
  (void)p; // suppress unused warning

  // If reveal is starting fresh, clear the keep-out area immediately
  if (!reveal.active) {
    // Erase existing rain in the box using pre-calculated values
    tft.fillRect(revealAreaX, revealAreaY, revealAreaW, revealAreaH, matrixBgColor);
  }

  // Start or refresh the reveal timer
  reveal.active  = true;
  reveal.startMs = now;
  reveal.endMs   = now + REVEAL_DURATION_MS;
}

void updateReveal() {
  if (!reveal.active) return;

  uint32_t now = millis();
  if (now > reveal.endMs) {
    reveal.active = false;
    return;
  }

  // Always white for the clock text
  uint16_t color = ILI9341_WHITE;

  // Recalculate Y position for text specifically since revealAreaY includes padding
  int vPad = (1 * REVEAL_TEXT_SIZE) + 2;
  int textY = revealAreaY + vPad;

  // Draw big "12:00" in the center
  tft.setTextSize(REVEAL_TEXT_SIZE);
  tft.setTextWrap(false);
  tft.setCursor(revealAreaX, textY);
  tft.setTextColor(ILI9341_GREEN, matrixBgColor);
  tft.print(REVEAL_TEXT);
}
