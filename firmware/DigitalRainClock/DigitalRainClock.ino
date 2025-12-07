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
const int FONT_PIXEL_W = 6;
const int FONT_PIXEL_H = 8;
const int CHAR_W = FONT_PIXEL_W * TEXT_SCALE;
const int CHAR_H = FONT_PIXEL_H * TEXT_SCALE;

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

struct ColorScheme {
  uint16_t head;
  uint16_t bright;
  uint16_t dim;
  uint16_t dark;
};

const uint8_t NUM_COLOR_SCHEMES = 5;
ColorScheme colorSchemes[NUM_COLOR_SCHEMES];
uint8_t currentColorScheme = 0;

const int COLOR_TOGGLE_REGION_W = 60;
const int COLOR_TOGGLE_REGION_H = 60;
const uint16_t COLOR_TOGGLE_DEBOUNCE_MS = 250;
uint32_t lastColorToggleMs = 0;

// Reveal state
struct Reveal {
  bool active;
  uint32_t startMs;
  uint32_t endMs;
  bool needsDraw;
};

Reveal reveal = { false, 0, 0, false };

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
void initColorSchemes();
void applyColorScheme(uint8_t index);
void cycleColorScheme();
bool isColorToggleTouch(int x, int y);

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

  // Colors for rain (head + trail levels)
  matrixBgColor = ILI9341_BLACK;
  initColorSchemes();
  applyColorScheme(0);

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
  // Handle touch eents 
  handleTouch();    

  // Update rain characters
  updateMatrixRain(); // Normal falling rain

  // Update displayed time
  updateReveal(); 
}


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
  int charPixelW = FONT_PIXEL_W * REVEAL_TEXT_SIZE;
  int charPixelH = FONT_PIXEL_H * REVEAL_TEXT_SIZE;
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
  revealAreaW = textPixelW - 1;
  revealAreaH = charPixelH + vPad; 

  // Optimization: Calculate which columns intersect with this X-range
  // Column width is CHAR_W
  startRevealCol = revealAreaX / CHAR_W;
  endRevealCol   = (revealAreaX + revealAreaW) / CHAR_W;
}

void initColorSchemes() {
  colorSchemes[0] = {
    ILI9341_WHITE,
    ILI9341_GREEN,
    ILI9341_DARKGREEN,
    tft.color565(0, 70, 0)
  };

  colorSchemes[1] = {
    tft.color565(255, 220, 220),
    ILI9341_RED,
    tft.color565(120, 0, 0),
    tft.color565(60, 0, 0)
  };

  colorSchemes[2] = {
    tft.color565(220, 240, 255),
    tft.color565(0, 180, 255),
    tft.color565(0, 90, 170),
    tft.color565(0, 40, 90)
  };

  colorSchemes[3] = {
    tft.color565(255, 255, 210),
    ILI9341_YELLOW,
    tft.color565(180, 140, 0),
    tft.color565(120, 90, 0)
  };

  colorSchemes[4] = {
    tft.color565(240, 210, 255),
    ILI9341_MAGENTA,
    tft.color565(120, 0, 150),
    tft.color565(70, 0, 90)
  };
}

void applyColorScheme(uint8_t index) {
  if (index >= NUM_COLOR_SCHEMES) {
    index = 0;
  }
  currentColorScheme = index;
  matrixHeadColor   = colorSchemes[index].head;
  matrixTrailBright = colorSchemes[index].bright;
  matrixTrailDim    = colorSchemes[index].dim;
  matrixTrailDark   = colorSchemes[index].dark;
}

void cycleColorScheme() {
  uint8_t next = (currentColorScheme + 1) % NUM_COLOR_SCHEMES;
  applyColorScheme(next);
  if (reveal.active) {
    reveal.needsDraw = true;
  }
}

bool isColorToggleTouch(int x, int y) {
  return (x >= (SCREEN_W - COLOR_TOGGLE_REGION_W)) &&
         (y >= (SCREEN_H - COLOR_TOGGLE_REGION_H));
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

    uint32_t elapsed = now - c.lastUpdateMs;
    if (elapsed < c.intervalMs) {
      continue;  // not time to advance this column yet
    }
    c.lastUpdateMs += c.intervalMs;
    if (now - c.lastUpdateMs >= c.intervalMs) {
      c.lastUpdateMs = now;
    }

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
  // Return if no touch detected
  if (!ctp.touched()) {
    return;
  }

  // Get current time
  uint32_t now = millis();

  // Calculate touch coordinates
  TS_Point p = ctp.getPoint();
  int touchX = map(p.x, 0, 240, 240, 0); 
  int touchY = map(p.y, 0, 320, 320, 0);

  // Toggle color scheme if touch is in the color toggle region
  if (isColorToggleTouch(touchX, touchY)) {
    if (now - lastColorToggleMs > COLOR_TOGGLE_DEBOUNCE_MS) {
      cycleColorScheme();
      lastColorToggleMs = now;
    }
    return;
  }

  // If reveal is starting fresh, clear the keep-out area immediately
  if (!reveal.active) {
    // Erase existing rain in the box using pre-calculated values
    tft.fillRect(revealAreaX, revealAreaY, revealAreaW, revealAreaH, matrixBgColor);
  }

  // Start or refresh the reveal timer
  reveal.active  = true;
  reveal.startMs = now;
  reveal.endMs   = now + REVEAL_DURATION_MS;
  reveal.needsDraw = true;
}

void updateReveal() {
  if (!reveal.active) return;

  uint32_t now = millis();
  if (now > reveal.endMs) {
    reveal.active = false;
    reveal.needsDraw = false;
    return;
  }

  if (!reveal.needsDraw) {
    return;
  }
  reveal.needsDraw = false;

  // Recalculate Y position for text specifically since revealAreaY includes padding
  int vPad = (1 * REVEAL_TEXT_SIZE) + 2;
  int textY = revealAreaY + vPad;

  // Draw big "12:00" in the center
  tft.setTextSize(REVEAL_TEXT_SIZE);
  tft.setTextWrap(false);
  tft.setCursor(revealAreaX, textY);
  tft.setTextColor(matrixTrailBright, matrixBgColor);
  tft.print(REVEAL_TEXT);
}
