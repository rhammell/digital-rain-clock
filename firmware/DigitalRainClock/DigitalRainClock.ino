#include <Wire.h>
#include <Adafruit_FT6206.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h> 
#include <ESP32Time.h>

// TFT Pins for Nano ESP32
// D11 -> MOSI
// D13 -> SCK
// A4 -> SDA
// A5 -> SCL
#define TFT_DC   2
#define TFT_RST  3
#define TFT_CS   4
#define TFT_LED  5

// Initialize the display object
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Initialize the capacitive touchscreen object
Adafruit_FT6206 ctp;

// Screen dimensions - portrait
const int SCREEN_W = 320;
const int SCREEN_H = 240;

// Text scale
const int TEXT_SCALE = 1;

// Font pixel width and height
const int FONT_PIXEL_W = 6;
const int FONT_PIXEL_H = 8;
const int CHAR_W = FONT_PIXEL_W * TEXT_SCALE;
const int CHAR_H = FONT_PIXEL_H * TEXT_SCALE;

// Number of columns and rows
const int NUM_COLS = SCREEN_W / CHAR_W;
const int NUM_ROWS = SCREEN_H / CHAR_H;

// Column-specific tail length range (in rows)
const int MIN_TAIL_LEN = 14;
const int MAX_TAIL_LEN = 20;

// Global speed range (ms per row step)
const uint16_t MIN_INTERVAL = 60;
const uint16_t MAX_INTERVAL = 160;

// Clock overlay parameters
const int  TIME_TEXT_LENGTH          = 5;
const uint16_t TIME_OVERLAY_DURATION_MS = 3600;
const int TIME_TEXT_SIZE        = 6;
char timeText[TIME_TEXT_LENGTH + 1] = "12:00";

// Column state
struct ColumnState {
  int headRow;
  uint16_t intervalMs;
  uint32_t lastUpdateMs;
  uint8_t tailLength;
};

// Array of column states
ColumnState columns[NUM_COLS];

// Character buffer for trail characters
char glyphs[NUM_COLS][NUM_ROWS];

// Colors for rain (head + trail levels) and background
uint16_t matrixHeadColor;
uint16_t matrixTrailBright;
uint16_t matrixTrailDim;
uint16_t matrixTrailDark;
uint16_t matrixBgColor;

// Real-time clock
ESP32Time rtc;

// Last displayed minute
int lastDisplayedMinute = -1;

// Color scheme struct
struct ColorScheme {
  uint16_t head;
  uint16_t bright;
  uint16_t dim;
  uint16_t dark;
};

// Number of color schemes
const uint8_t NUM_COLOR_SCHEMES = 5;

// Array of color schemes
ColorScheme colorSchemes[NUM_COLOR_SCHEMES];

// Current color scheme
uint8_t currentColorScheme = 0;

// Color toggle region
const int COLOR_TOGGLE_REGION_W = 60;
const int COLOR_TOGGLE_REGION_H = 60;
const uint16_t COLOR_TOGGLE_DEBOUNCE_MS = 250;
uint32_t lastColorToggleMs = 0;

// Settings toggle region
const int SETTINGS_TOGGLE_REGION_W = 60;
const int SETTINGS_TOGGLE_REGION_H = 60;
const uint16_t SETTINGS_TOGGLE_DEBOUNCE_MS = 250;
uint32_t lastSettingsToggleMs = 0;
const uint16_t SETTINGS_BUTTON_DEBOUNCE_MS = 200;
uint32_t lastSettingsButtonMs = 0;

// Time overlay state struct
struct TimeOverlay {
  bool active;
  uint32_t startMs;
  uint32_t endMs;
  bool needsDraw;
};

// Time overlay struct
TimeOverlay overlay = { false, 0, 0, false };

// Settings menu state
bool settingsActive = false;
int settingsHour = 12;
int settingsMinute = 0;

// Button region struct
struct ButtonRegion {
  int x;
  int y;
  int w;
  int h;
};

// Button regions
ButtonRegion hourUpButton = {0};
ButtonRegion hourDownButton = {0};
ButtonRegion minuteUpButton = {0};
ButtonRegion minuteDownButton = {0};

// Settings time display region
int settingsTimeDisplayX = 0;
int settingsTimeDisplayY = 0;
int settingsTimeDisplayW = 0;
int settingsTimeDisplayH = 0;

// Settings menu constants
const int SETTINGS_PANEL_MARGIN = 10;
const int SETTINGS_BUTTON_W = 80;
const int SETTINGS_BUTTON_H = 40;
const int SETTINGS_BUTTON_SPACING = 10;
const int SETTINGS_LABEL_OFFSET = 15;
const int SETTINGS_TIME_TEXT_SIZE = 3;
const int SETTINGS_TITLE_TEXT_SIZE = 2;
const int SETTINGS_TITLE_OFFSET_Y = 25;
const int SETTINGS_BUTTON_VERTICAL_OFFSET = 10;
const int SETTINGS_LABEL_OFFSET_ADJUST = 5;

// Global overlay area bounds 
int overlayAreaX, overlayAreaY, overlayAreaW, overlayAreaH;
int startOverlayCol, endOverlayCol; 

// Function prototypes
char randomGlyph();
void initColumns();
void resetColumn(int col);
void updateMatrixRain();
void handleTouch();
void updateTimeOverlay();
void calcTimeOverlayArea();
void drawRainChar(int x, int row, char ch, uint16_t color);
void initColorSchemes();
void applyColorScheme(uint8_t index);
void cycleColorScheme();
bool isColorToggleTouch(int x, int y);
bool isSettingsToggleTouch(int x, int y);
void enterSettingsMenu();
void exitSettingsMenu();
void drawSettingsMenu();
void startTimeOverlay(uint32_t now);
void updateTimeTextFromClock();
void checkMinuteTick();
void handleSettingsTouch(int x, int y);
void drawTimeAdjustControls();
void updateSettingsTimeDisplay();
void drawButton(int x, int y, int w, int h, const char *label);
bool pointInRect(int x, int y, int rx, int ry, int rw, int rh);

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println("Starting TFT and Touch Initialization...");
 
  // Backlight control
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);

  // TFT Display Setup
  tft.begin();
  tft.fillScreen(ILI9341_BLACK);

  // Touchscreen Setup
  if (!ctp.begin()) {
    // Display error message if touchscreen initialization fails
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

  // Set up text parameters for digital rain
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextSize(TEXT_SCALE);
  tft.setTextWrap(false);

  // Colors for digital rain (head + trail levels)
  matrixBgColor = ILI9341_BLACK;
  initColorSchemes();
  applyColorScheme(0);

  // Set initial time
  rtc.setTime(0, 0, 12, 1, 1, 2024);
  updateTimeTextFromClock();

  // Seed random number generator
  randomSeed(analogRead(A0));

  // Initialize digital rain column states and glyphs
  initColumns();
  
  // Pre-calculate the time overlay area dimensions
  calcTimeOverlayArea();

  // Initialize time overlay
  overlay.active = false;
  lastDisplayedMinute = rtc.getMinute();
  startTimeOverlay(millis());
}

void loop() {
  // Handle touch events 
  handleTouch();    

  // Check if the minute has changed and update the time text if it has
  checkMinuteTick();

  // If settings are active, do not update the time overlay
  if (settingsActive) {
    return;
  }

  // Update digital rain characters
  updateMatrixRain();

  // Update time overlay
  updateTimeOverlay(); 
}


// Initialize digital rain columns
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

// Reset a digital rain column
void resetColumn(int col) {
  // Get the column state
  ColumnState &c = columns[col];

  // Generate a random tail length for the column
  c.tailLength = random(MIN_TAIL_LEN, MAX_TAIL_LEN + 1);  // inclusive range

  // Map tail length to speed: shorter tail => smaller interval => faster, longer tail => larger interval => slower
  int tailSpan = MAX_TAIL_LEN - MIN_TAIL_LEN;
  if (tailSpan < 1) tailSpan = 1; // Avoid division by zero

  // Generate a base interval for the column
  uint16_t baseInterval = MIN_INTERVAL +
    (uint16_t)((long)(c.tailLength - MIN_TAIL_LEN) * (MAX_INTERVAL - MIN_INTERVAL) / tailSpan);

  // Add a little jitter so similar tails aren't perfectly identical to avoid repetition
  int16_t jitter = random(-15, 16); // -15 .. +15
  int32_t intervalWithJitter = (int32_t)baseInterval + jitter;
  if (intervalWithJitter < 20) intervalWithJitter = 20; // Clamp to sane minimum to avoid too fast falling

  // Set the interval for the column
  c.intervalMs   = (uint16_t)intervalWithJitter;
  c.lastUpdateMs = millis();

  // Set the last update time for the column
  c.lastUpdateMs = millis();
}

// Generate a random glyph
char randomGlyph() {
  // Random printable ASCII; tweak range if you want other characters
  return (char)random(33, 126); // '!' to '~'
}

// Calculate the time overlay area once
void calcTimeOverlayArea() {
  int charPixelW = FONT_PIXEL_W * TIME_TEXT_SIZE;
  int charPixelH = FONT_PIXEL_H * TIME_TEXT_SIZE;
  int textPixelW = TIME_TEXT_LENGTH * charPixelW;

  // Calculate the padding size: 1 font-pixel row + 2 extra pixels to avoid text being cut off
  int vPad = (1 * TIME_TEXT_SIZE) + 2;

  // Calculate the x position of the time overlay
  int tx = (SCREEN_W - textPixelW) / 2;
  if (tx < 0) tx = 0;
  
  // Calculate the y position of the time overlay
  int ty = (SCREEN_H - charPixelH) / 2;
  if (ty < 0) ty = 0;

  // Store the time overlay area dimensions globally
  overlayAreaX = tx;
  overlayAreaY = ty - vPad;
  overlayAreaW = textPixelW - 1;
  overlayAreaH = charPixelH + vPad; 

  // Calculate which columns intersect with this X-range for optimization
  // Column width is CHAR_W
  startOverlayCol = overlayAreaX / CHAR_W;
  endOverlayCol   = (overlayAreaX + overlayAreaW) / CHAR_W;
}

// Initialize color schemes
void initColorSchemes() {
  // Color scheme 0: Green
  colorSchemes[0] = {
    ILI9341_WHITE,
    ILI9341_GREEN,
    ILI9341_DARKGREEN,
    tft.color565(0, 70, 0)
  };

  // Color scheme 1: Red
  colorSchemes[1] = {
    tft.color565(255, 220, 220),
    ILI9341_RED,
    tft.color565(120, 0, 0),
    tft.color565(60, 0, 0)
  };

  // Color scheme 2: Blue
  colorSchemes[2] = {
    tft.color565(220, 240, 255),
    tft.color565(0, 180, 255),
    tft.color565(0, 90, 170),
    tft.color565(0, 40, 90)
  };

  // Color scheme 3: Yellow
  colorSchemes[3] = {
    tft.color565(255, 255, 210),
    ILI9341_YELLOW,
    tft.color565(180, 140, 0),
    tft.color565(120, 90, 0)
  };

  // Color scheme 4: Purple
  colorSchemes[4] = {
    tft.color565(240, 210, 255),
    ILI9341_MAGENTA,
    tft.color565(120, 0, 150),
    tft.color565(70, 0, 90)
  };
}

// Apply a color scheme
void applyColorScheme(uint8_t index) {
  // If the index is out of bounds, set it to 0
  if (index >= NUM_COLOR_SCHEMES) {
    index = 0;
  }

  // Set the current color scheme
  currentColorScheme = index;

  // Set the color scheme colors to the global variables
  matrixHeadColor   = colorSchemes[index].head;
  matrixTrailBright = colorSchemes[index].bright;
  matrixTrailDim    = colorSchemes[index].dim;
  matrixTrailDark   = colorSchemes[index].dark;
}

// Cycle through the color schemes
void cycleColorScheme() {
  uint8_t next = (currentColorScheme + 1) % NUM_COLOR_SCHEMES;
  applyColorScheme(next);
  if (overlay.active) {
    overlay.needsDraw = true;
  }
}

// Check if the color toggle is touched
bool isColorToggleTouch(int x, int y) {
  return (x >= (SCREEN_W - COLOR_TOGGLE_REGION_W)) &&
         (y >= (SCREEN_H - COLOR_TOGGLE_REGION_H));
}

// Check if the settings toggle is touched
bool isSettingsToggleTouch(int x, int y) {
  return (x <= SETTINGS_TOGGLE_REGION_W) && (y <= SETTINGS_TOGGLE_REGION_H);
}

// Enter the settings menu
void enterSettingsMenu() {
  settingsActive = true;
  settingsHour = rtc.getHour(true);
  settingsMinute = rtc.getMinute();
  tft.fillScreen(matrixBgColor);
  drawSettingsMenu();
}

// Exit the settings menu
void exitSettingsMenu() {
  // Set the settings active flag to false
  settingsActive = false;

  // Get the current time
  int currentYear = rtc.getYear();
  int currentMonth = rtc.getMonth() + 1; // Month is 0-11, so add 1
  int currentDay = rtc.getDay();

  // Set the time to the current time
  rtc.setTime(0, settingsMinute, settingsHour, currentDay, currentMonth, currentYear);
  lastDisplayedMinute = settingsMinute;
  updateTimeTextFromClock();

  // Fill the screen with the background color
  tft.fillScreen(matrixBgColor);
  initColumns();

  // Reset the time overlay
  overlay.active = false;
  overlay.needsDraw = false;

  // Start the time overlay
  startTimeOverlay(millis());
}

// Draw the settings menu
void drawSettingsMenu() {
  // Set the text size and wrap and color
  tft.setTextSize(SETTINGS_TITLE_TEXT_SIZE);
  tft.setTextWrap(true);
  tft.setTextColor(matrixTrailBright, matrixBgColor);

  // Calculate the panel x, y, width and height
  int panelX = SETTINGS_PANEL_MARGIN;
  int panelY = SETTINGS_PANEL_MARGIN;
  int panelW = SCREEN_W - (SETTINGS_PANEL_MARGIN * 2);
  int panelH = SCREEN_H - (SETTINGS_PANEL_MARGIN * 2);

  // Draw the panel
  tft.drawRect(panelX, panelY, panelW, panelH, matrixTrailBright);

  // Draw the title
  const char *title = "Current Time";
  int titleWidth = strlen(title) * FONT_PIXEL_W * SETTINGS_TITLE_TEXT_SIZE;
  int titleX = panelX + (panelW - titleWidth) / 2;
  int titleY = panelY + SETTINGS_TITLE_OFFSET_Y;
  tft.setCursor(titleX, titleY);
  tft.println(title);

  // Draw the time adjust controls
  drawTimeAdjustControls();
}


// Draw the time adjust controls
void drawTimeAdjustControls() {
  // Calculate the panel x, y, width and height
  int panelX = SETTINGS_PANEL_MARGIN;
  int panelY = SETTINGS_PANEL_MARGIN;
  int panelW = SCREEN_W - (SETTINGS_PANEL_MARGIN * 2);

  // Calculate the time display x, y, width and height
  settingsTimeDisplayX = panelX + 20;
  settingsTimeDisplayY = panelY + 65;
  settingsTimeDisplayW = panelW - 40;
  settingsTimeDisplayH = 60;

  // Draw the time display
  tft.drawRect(settingsTimeDisplayX, settingsTimeDisplayY, settingsTimeDisplayW, settingsTimeDisplayH, matrixTrailBright);
  updateSettingsTimeDisplay();

  // Calculate the controls top
  int controlsTop = settingsTimeDisplayY + settingsTimeDisplayH + 30 + SETTINGS_LABEL_OFFSET_ADJUST;

  // Calculate the hour column x and minute column x
  int hourColumnX = panelX + 30;
  int minuteColumnX = panelX + panelW - SETTINGS_BUTTON_W - 30;

  // Set the text size
  tft.setTextSize(2);

  // Draw the hour label and minute label
  const char *hourLabel = "Hour";
  const char *minuteLabel = "Minute";
  int hourLabelWidth = strlen(hourLabel) * FONT_PIXEL_W * 2;
  int minuteLabelWidth = strlen(minuteLabel) * FONT_PIXEL_W * 2;

  // Calculate the hour label x and minute label x
  int hourLabelX = hourColumnX + (SETTINGS_BUTTON_W - hourLabelWidth) / 2;
  int minuteLabelX = minuteColumnX + (SETTINGS_BUTTON_W - minuteLabelWidth) / 2;

  // Calculate the label y
  int labelY = controlsTop - SETTINGS_LABEL_OFFSET;

  // Draw the hour label and minute label
  tft.setCursor(hourLabelX, labelY);
  tft.println(hourLabel);
  tft.setCursor(minuteLabelX, labelY);
  tft.println(minuteLabel);

  // Calculate the button top
  int buttonTop = controlsTop + SETTINGS_BUTTON_VERTICAL_OFFSET + SETTINGS_LABEL_OFFSET_ADJUST;

  // Calculate the hour up button x, y, width and height
  hourUpButton = { hourColumnX, buttonTop, SETTINGS_BUTTON_W, SETTINGS_BUTTON_H };
  hourDownButton = { hourColumnX, buttonTop + SETTINGS_BUTTON_H + SETTINGS_BUTTON_SPACING, SETTINGS_BUTTON_W, SETTINGS_BUTTON_H };

  // Calculate the minute up button x, y, width and height
  minuteUpButton = { minuteColumnX, buttonTop, SETTINGS_BUTTON_W, SETTINGS_BUTTON_H };
  minuteDownButton = { minuteColumnX, buttonTop + SETTINGS_BUTTON_H + SETTINGS_BUTTON_SPACING, SETTINGS_BUTTON_W, SETTINGS_BUTTON_H };

  // Draw the hour up button, hour down button, minute up button and minute down button
  drawButton(hourUpButton.x, hourUpButton.y, hourUpButton.w, hourUpButton.h, "+");
  drawButton(hourDownButton.x, hourDownButton.y, hourDownButton.w, hourDownButton.h, "-");
  drawButton(minuteUpButton.x, minuteUpButton.y, minuteUpButton.w, minuteUpButton.h, "+");
  drawButton(minuteDownButton.x, minuteDownButton.y, minuteDownButton.w, minuteDownButton.h, "-");
}

// Update the settings time display
void updateSettingsTimeDisplay() {
  // If the time display width or height is 0, return
  if (settingsTimeDisplayW == 0 || settingsTimeDisplayH == 0) {
    return;
  }

  // Fill the time display with the background color
  tft.fillRect(settingsTimeDisplayX + 1, settingsTimeDisplayY + 1,
               settingsTimeDisplayW - 2, settingsTimeDisplayH - 2, matrixBgColor);

  // Create a buffer for the time display
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", settingsHour, settingsMinute);

  // Calculate the text width and height
  int textWidth = strlen(buffer) * FONT_PIXEL_W * SETTINGS_TIME_TEXT_SIZE;
  int textHeight = FONT_PIXEL_H * SETTINGS_TIME_TEXT_SIZE;

  // Calculate the cursor x and y
  int cursorX = settingsTimeDisplayX + (settingsTimeDisplayW - textWidth) / 2;
  int cursorY = settingsTimeDisplayY + (settingsTimeDisplayH - textHeight) / 2;

  // Set the text size and color
  tft.setTextSize(SETTINGS_TIME_TEXT_SIZE);
  tft.setTextColor(matrixTrailBright, matrixBgColor);
  tft.setCursor(cursorX, cursorY);

  // Print the time display
  tft.print(buffer);
}

// Draw a button
void drawButton(int x, int y, int w, int h, const char *label) {
  // Draw the button rectangle with the trail bright color
  tft.drawRect(x, y, w, h, matrixTrailBright);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, matrixBgColor);

  // Calculate the text width and height and cursor x and y
  int textWidth = strlen(label) * FONT_PIXEL_W * 2;
  int textHeight = FONT_PIXEL_H * 2;
  int cursorX = x + (w - textWidth) / 2;
  int cursorY = y + (h - textHeight) / 2;

  // Draw button text
  tft.setTextSize(2);
  tft.setTextColor(matrixTrailBright, matrixBgColor);
  tft.setCursor(cursorX, cursorY);
  tft.print(label);
}

// Check if a point is in a rectangle
bool pointInRect(int x, int y, int rx, int ry, int rw, int rh) {
  return (x >= rx && x <= rx + rw && y >= ry && y <= ry + rh);
}

// Handle settings touch
void handleSettingsTouch(int x, int y) {
  // Get the current time
  uint32_t now = millis();

  // If the last settings button press was less than the debounce time, return
  if (now - lastSettingsButtonMs < SETTINGS_BUTTON_DEBOUNCE_MS) {
    return;
  }

  // Set the updated flag to false
  bool updated = false;

  // Check if the touch is in the hour up button
  if (pointInRect(x, y, hourUpButton.x, hourUpButton.y, hourUpButton.w, hourUpButton.h)) {
    // Increment the hour
    settingsHour = (settingsHour + 1) % 24;
    updated = true;
  } else if (pointInRect(x, y, hourDownButton.x, hourDownButton.y, hourDownButton.w, hourDownButton.h)) {
    // Decrement the hour
    settingsHour = (settingsHour - 1);
    if (settingsHour < 0) settingsHour = 23;
    updated = true;
  } else if (pointInRect(x, y, minuteUpButton.x, minuteUpButton.y, minuteUpButton.w, minuteUpButton.h)) {
    // Increment the minute
    settingsMinute = (settingsMinute + 1) % 60;
    updated = true;
  } else if (pointInRect(x, y, minuteDownButton.x, minuteDownButton.y, minuteDownButton.w, minuteDownButton.h)) {
    // Decrement the minute
    settingsMinute = (settingsMinute - 1);
    if (settingsMinute < 0) settingsMinute = 59;
    updated = true;
  }

  // If the updated flag is true, update the settings time display and set the last settings button press time
  if (updated) {
    updateSettingsTimeDisplay();
    lastSettingsButtonMs = now;
  }
}

// Check if a rain cell overlaps the overlay text box
bool isOverlayArea(int col, int y, int h) {
  // If the overlay is not active, return false
  if (!overlay.active) return false;

  // Fast Check: Is this column even near the text?
  if (col < startOverlayCol || col > endOverlayCol) return false;

  // Precise Check: Y-coordinate overlap
  return (y < overlayAreaY + overlayAreaH && y + h > overlayAreaY);
}

// Draw a rain character
void drawRainChar(int col, int row, char ch, uint16_t color) {
  // If the row is out of bounds, return
  if (row < 0 || row >= NUM_ROWS) return;

  // Calculate the x and y
  int x = col * CHAR_W;
  int y = row * CHAR_H;

  // Check if the rain cell overlaps the overlay text box
  if (isOverlayArea(col, y, CHAR_H)) return;

  // Draw the rain character
  tft.setCursor(x, y);
  tft.setTextColor(color, matrixBgColor);
  tft.write(ch);
}

// Clear a rain character
void clearRainChar(int col, int row) {
  // If the row is out of bounds, return
  if (row < 0 || row >= NUM_ROWS) return;

  // Calculate the x and y
  int x = col * CHAR_W;
  int y = row * CHAR_H;

  // Check if the rain cell overlaps the overlay text box
  if (isOverlayArea(col, y, CHAR_H)) return;

  // Clear the rain character
  tft.fillRect(x, y, CHAR_W, CHAR_H, matrixBgColor);
}

// Update the matrix rain
void updateMatrixRain() {
  // Get the current time
  uint32_t now = millis();

  // Set the text size to the small text size
  tft.setTextSize(TEXT_SCALE);
  tft.setTextWrap(false);

  // Update each column
  for (int col = 0; col < NUM_COLS; col++) {
    // Get the column state
    ColumnState &c = columns[col];

    // Calculate the elapsed time since the last update
    uint32_t elapsed = now - c.lastUpdateMs;
    if (elapsed < c.intervalMs) {
      // Not time to advance this column yet, continue
      continue;
    }

    // Update the last update time
    c.lastUpdateMs += c.intervalMs;

    // If the last update time is greater than the interval, set it to the current time
    if (now - c.lastUpdateMs >= c.intervalMs) {
      c.lastUpdateMs = now;
    }

    // Get the previous head row
    int prevHead = c.headRow;
    c.headRow++;

    // Get the tail length
    int tailLen = c.tailLength;

    // When the entire stream (head + tail) has passed off-screen, respawn
    if (c.headRow >= NUM_ROWS + tailLen) {
      // Reset this column with a new tail length and speed
      resetColumn(col);
      // Set the head row to a random row above the screen
      c.headRow = random(-NUM_ROWS, 0);
      // Use the new tail length
      tailLen = c.tailLength;
    }

    // Erase the tail end cell
    clearRainChar(col, c.headRow - tailLen);

    // Draw the new head (white)
    if (c.headRow >= 0 && c.headRow < NUM_ROWS) {
      char ch = randomGlyph();
      // Set the glyph for the head
      glyphs[col][c.headRow] = ch;
      // Draw the head
      drawRainChar(col, c.headRow, ch, matrixHeadColor);
    }

    // The previous head becomes bright trail
    if (prevHead >= 0 && prevHead < NUM_ROWS) {
      char ch = glyphs[col][prevHead];
      drawRainChar(col, prevHead, ch, matrixTrailBright);
    }

    // Calculate the bright region length 
    int brightDist = tailLen / 5;      // ~20%
    if (brightDist < 1) brightDist = 1;
    if (brightDist >= tailLen) brightDist = tailLen - 1;

    // Calculate the dark region start distance
    int darkStartDist = (tailLen * 4) / 5;  // ~80%
    if (darkStartDist <= brightDist + 1) darkStartDist = brightDist + 2;
    if (darkStartDist >= tailLen) darkStartDist = tailLen - 1;

    // Cell leaving the bright zone becomes a dim trail
    int dimRow = c.headRow - (brightDist + 1);
    if (dimRow >= 0 && dimRow < NUM_ROWS) {
      int dist = c.headRow - dimRow;
      if (dist < tailLen && dist >= 0 && dist < darkStartDist) {
        drawRainChar(col, dimRow, glyphs[col][dimRow], matrixTrailDim);
      }
    }

    // Cell entering the last 20% of the tail becomes a dark trail
    int darkRow = c.headRow - darkStartDist;
    if (darkRow >= 0 && darkRow < NUM_ROWS) {
      int dist = c.headRow - darkRow;
      if (dist < tailLen && dist >= darkStartDist) {
        drawRainChar(col, darkRow, glyphs[col][darkRow], matrixTrailDark);
      }
    }
  }
}


// Handle touch
void handleTouch() {
  // If no touch detected, return
  // Return if no touch detected
  if (!ctp.touched()) {
    return;
  }

  // Get the current time
  uint32_t now = millis();

  // Get the raw touch coordinates and corner case handling
  TS_Point p = ctp.getPoint();

  // If the touch coordinates are 0, return
  if (p.x == 0 && p.y == 0) {
    return;
  }

  // Map touch coordinates to the screen coordinates
  int touchX = map(p.x, 0, 240, 240, 0); 
  int touchY = map(p.y, 0, 320, 320, 0);

  // Check if the touch is in the settings toggle region and if the last settings toggle press was less than the debounce time
  if (isSettingsToggleTouch(touchX, touchY)) {
    // If the last settings toggle press was less than the debounce time, return
    if (now - lastSettingsToggleMs > SETTINGS_TOGGLE_DEBOUNCE_MS) {
      // If the settings menu is not active, enter the settings menu
      if (!settingsActive) {
        enterSettingsMenu();
      } else {
        exitSettingsMenu();
      }
      lastSettingsToggleMs = now;
    }
    return;
  }

  // If the settings menu is active, handle the settings touch
  if (settingsActive) {
    handleSettingsTouch(touchX, touchY);
    return;
  }

  // Toggle color scheme if touch is in the color toggle region and if the last color toggle press was less than the debounce time
  if (isColorToggleTouch(touchX, touchY)) {
    if (now - lastColorToggleMs > COLOR_TOGGLE_DEBOUNCE_MS) {
      cycleColorScheme();
      lastColorToggleMs = now;
    }
    return;
  }

  // Update the time text from the clock
  updateTimeTextFromClock();
  startTimeOverlay(now);
}

// Update the time overlay
void updateTimeOverlay() {
  // If the overlay is not active, return
  if (!overlay.active) return;

  // Get the current time
  uint32_t now = millis();

  // If the current time is greater than the end time, deactivate the overlay and set the needs draw flag to false
  if (now > overlay.endMs) {
    overlay.active = false;
    overlay.needsDraw = false;
    return;
  }

  // If the needs draw flag is false, return
  if (!overlay.needsDraw) {
    return;
  }

  // Set the needs draw flag to false
  overlay.needsDraw = false;

  // Recalculate the Y position for text specifically since overlayAreaY includes padding
  int vPad = (1 * TIME_TEXT_SIZE) + 2;
  int textY = overlayAreaY + vPad;

  // Draw the time text in the center
  tft.setTextSize(TIME_TEXT_SIZE);
  tft.setTextWrap(false);
  tft.setCursor(overlayAreaX, textY);
  tft.setTextColor(matrixTrailBright, matrixBgColor);
  tft.print(timeText);
}

// Start the time overlay
void startTimeOverlay(uint32_t now) {
  // If the overlay is not active, fill the overlay area with the background color
  if (!overlay.active) {
    tft.fillRect(overlayAreaX, overlayAreaY, overlayAreaW, overlayAreaH, matrixBgColor);
  }

  // Update the overlay state
  overlay.active = true;
  overlay.startMs = now;
  overlay.endMs = now + TIME_OVERLAY_DURATION_MS;
  overlay.needsDraw = true;
}

// Update the time text from the clock
void updateTimeTextFromClock() {
  // Get the current hour and minute
  int hour = rtc.getHour(true);
  int minute = rtc.getMinute();
  if (hour == 0) {
    hour = 12;
  }

  // Format the time text
  snprintf(timeText, sizeof(timeText), "%02d:%02d", hour, minute);
}

// Check if the minute has changed and update the time text if it has
void checkMinuteTick() {
  // Get the current minute
  int currentMinute = rtc.getMinute();

  // If the current minute is the same as the last displayed minute, return
  if (currentMinute == lastDisplayedMinute) {
    return;
  }

  // Update the last displayed minute
  lastDisplayedMinute = currentMinute;

  // Update the time text from the clock
  updateTimeTextFromClock();

  // Start the time overlay
  startTimeOverlay(millis());
}
