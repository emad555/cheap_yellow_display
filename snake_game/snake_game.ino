/*
 * SNAKE for ESP32 CYD (ESP32-2432S028)
 * 320×240 landscape | TFT_eSPI + XPT2046_Touchscreen
 *
 * Controls:
 *   Tap anywhere on the screen. The direction from the snake's
 *   head to your tap point becomes the new direction.
 *
 * Libraries required (install via Arduino Library Manager):
 *   - TFT_eSPI  (Bodmer)         — configure with CYD User_Setup.h
 *   - XPT2046_Touchscreen        (PaulStoffregen)
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

/***** 1) DISPLAY *****/
TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W  320
#define SCREEN_H  240

/***** 2) TOUCHSCREEN PINS (XPT2046) *****/
#define XPT2046_IRQ   36
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_CLK   25
#define XPT2046_CS    33

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

/***** 3) GAME CONSTANTS *****/
#define HEADER_H  28          // pixels reserved for score bar
#define CELL      10          // grid cell size in pixels
#define COLS      (SCREEN_W / CELL)                   // 32
#define ROWS      ((SCREEN_H - HEADER_H) / CELL)      // 21
#define GRID_Y0   HEADER_H
#define MAX_LEN   (COLS * ROWS)                       // 672 — max snake length

// Colour palette
#define C_BG        TFT_BLACK
#define C_HEADER    TFT_NAVY
#define C_BORDER    0x2945     // dark slate
#define C_SNAKE_H   TFT_GREEN
#define C_SNAKE_B   0x0380     // darker green body
#define C_FOOD      TFT_RED
#define C_TEXT      TFT_WHITE

/***** 4) GAME STATE *****/
struct Cell { int8_t x, y; };

Cell  snake[MAX_LEN];
int   snakeLen  = 0;
int   snakeHead = 0;  // ring-buffer: most-recent head index

Cell  food;

enum Dir   { UP, DOWN, LEFT, RIGHT };
Dir   dir     = RIGHT;
Dir   nextDir = RIGHT;

int   score   = 0;
int   hiScore = 0;
int   stepMs  = 150;  // ms per game tick; decreases as score rises

enum GameState { S_INTRO, S_PLAYING, S_OVER };
GameState gState = S_INTRO;

unsigned long lastStep = 0;
bool          wasTouch = false;  // for tap edge-detection

/***** 5) RING-BUFFER HELPERS *****/
// snakeSeg(0) = tail, snakeSeg(snakeLen-1) = head
inline Cell snakeSeg(int i) {
  return snake[(snakeHead - snakeLen + 1 + i + MAX_LEN) % MAX_LEN];
}
inline Cell& snakeHeadCell() { return snake[snakeHead]; }

/***** 6) DRAWING UTILITIES *****/
void drawCell(int cx, int cy, uint16_t col) {
  int px = cx * CELL;
  int py = GRID_Y0 + cy * CELL;
  tft.fillRect(px + 1, py + 1, CELL - 2, CELL - 2, col);
}

void drawFood() {
  int px = food.x * CELL + CELL / 2;
  int py = GRID_Y0 + food.y * CELL + CELL / 2;
  tft.fillCircle(px, py, CELL / 2 - 1, C_FOOD);
}

void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, C_HEADER);
  tft.setTextSize(2);
  tft.setTextColor(C_TEXT, C_HEADER);
  tft.setCursor(4, 6);
  tft.print("SCORE:");
  tft.print(score);
  tft.setCursor(182, 6);
  tft.print("BEST:");
  tft.print(hiScore);
}

void drawGameArea() {
  tft.fillRect(0, GRID_Y0, SCREEN_W, SCREEN_H - GRID_Y0, C_BG);
  tft.drawRect(0, GRID_Y0, COLS * CELL, ROWS * CELL, C_BORDER);
}

void drawSnakeFull() {
  for (int i = 0; i < snakeLen; i++) {
    Cell c = snakeSeg(i);
    drawCell(c.x, c.y, (i == snakeLen - 1) ? C_SNAKE_H : C_SNAKE_B);
  }
}

void drawPlayScreen() {
  drawHeader();
  drawGameArea();
  drawSnakeFull();
  drawFood();
}

/***** 7) INTRO / GAME-OVER SCREENS *****/
void drawIntro() {
  tft.fillScreen(C_BG);

  // Title
  tft.setTextColor(C_SNAKE_H);
  tft.setTextSize(4);
  tft.setCursor(91, 42);
  tft.print("SNAKE");

  // Decorative dots
  for (int i = 0; i < 5; i++) {
    tft.fillCircle(55 + i * 15, 48, 4, C_SNAKE_B);
  }
  tft.fillCircle(55, 48, 5, C_SNAKE_H);  // "head"

  // Instructions
  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.setCursor(52, 116);
  tft.print("Tap relative to the snake head");
  tft.setCursor(52, 132);
  tft.print("to steer in that direction.");
  tft.setCursor(52, 152);
  tft.print("Eat red food to grow and score.");
  tft.setCursor(52, 168);
  tft.print("Don't hit the walls or yourself!");

  // Prompt
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(82, 196);
  tft.print("TAP TO START");
}

void drawGameOver() {
  const int bx = 58, by = 76, bw = 204, bh = 90;
  tft.fillRect(bx, by, bw, bh, C_HEADER);
  tft.drawRect(bx, by, bw, bh, TFT_WHITE);

  tft.setTextColor(TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(bx + 22, by + 10);
  tft.print("GAME OVER");

  tft.setTextColor(C_TEXT);
  tft.setTextSize(1);
  tft.setCursor(bx + 14, by + 40);
  tft.print("Score: ");
  tft.print(score);
  if (score > 0 && score == hiScore) {
    tft.setTextColor(TFT_YELLOW);
    tft.print("  NEW BEST!");
  }

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(bx + 26, by + 64);
  tft.print("Tap to play again");
}

/***** 8) FOOD PLACEMENT *****/
void placeFood() {
  // Use a static grid to avoid large stack allocation
  static bool occupied[COLS][ROWS];
  memset(occupied, 0, sizeof(occupied));

  for (int i = 0; i < snakeLen; i++) {
    Cell c = snakeSeg(i);
    if (c.x >= 0 && c.x < COLS && c.y >= 0 && c.y < ROWS)
      occupied[c.x][c.y] = true;
  }

  int freeCount = COLS * ROWS - snakeLen;
  if (freeCount <= 0) return;  // board full — player wins edge case
  int pick = random(freeCount);

  for (int y = 0; y < ROWS; y++) {
    for (int x = 0; x < COLS; x++) {
      if (!occupied[x][y] && pick-- == 0) {
        food = { (int8_t)x, (int8_t)y };
        return;
      }
    }
  }
}

/***** 9) GAME RESET *****/
void resetGame() {
  snakeLen  = 4;
  snakeHead = snakeLen - 1;
  int startX = COLS / 2 - snakeLen / 2;
  int startY = ROWS / 2;
  for (int i = 0; i < snakeLen; i++)
    snake[i] = { (int8_t)(startX + i), (int8_t)startY };

  dir = nextDir = RIGHT;
  score  = 0;
  stepMs = 150;
  placeFood();
}

/***** 10) TOUCH *****/
// Returns true on a NEW tap (rising edge only); sets tx, ty in screen pixels.
bool getTap(int &tx, int &ty) {
  bool isTouched = touchscreen.tirqTouched() && touchscreen.touched();
  bool newTap    = isTouched && !wasTouch;
  wasTouch       = isTouched;
  if (newTap) {
    TS_Point p = touchscreen.getPoint();
    tx = map(p.x, 200, 3700, 0, SCREEN_W);
    ty = map(p.y, 240, 3800, 0, SCREEN_H);
  }
  return newTap;
}

/***** 11) GAME STEP *****/
void triggerGameOver() {
  if (score > hiScore) hiScore = score;
  gState = S_OVER;
  drawGameOver();
}

void stepGame() {
  dir = nextDir;

  Cell head    = snakeHeadCell();
  Cell newHead = head;
  switch (dir) {
    case UP:    newHead.y--; break;
    case DOWN:  newHead.y++; break;
    case LEFT:  newHead.x--; break;
    case RIGHT: newHead.x++; break;
  }

  // Wall collision
  if (newHead.x < 0 || newHead.x >= COLS ||
      newHead.y < 0 || newHead.y >= ROWS) {
    triggerGameOver();
    return;
  }

  // Self collision — skip tail (index 0) since it moves away this tick
  for (int i = 1; i < snakeLen; i++) {
    Cell c = snakeSeg(i);
    if (c.x == newHead.x && c.y == newHead.y) {
      triggerGameOver();
      return;
    }
  }

  bool ate = (newHead.x == food.x && newHead.y == food.y);

  // Erase tail unless growing
  if (!ate) {
    Cell tail = snakeSeg(0);
    drawCell(tail.x, tail.y, C_BG);
  }

  // Advance ring buffer
  snakeHead = (snakeHead + 1) % MAX_LEN;
  snake[snakeHead] = newHead;

  if (ate) {
    snakeLen++;
    score  += 10;
    stepMs  = max(60, stepMs - 2);  // gradually speed up
    placeFood();
    drawFood();
    drawHeader();  // refresh score
  }

  // Re-colour previous head as body, draw new head
  if (snakeLen > 1) {
    Cell prev = snakeSeg(snakeLen - 2);
    drawCell(prev.x, prev.y, C_SNAKE_B);
  }
  drawCell(newHead.x, newHead.y, C_SNAKE_H);
}

/***** 12) SETUP & LOOP *****/
void setup() {
  Serial.begin(115200);

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(3);

  tft.init();
  tft.setRotation(3);

  randomSeed(analogRead(34));
  drawIntro();
}

void loop() {
  int tx = 0, ty = 0;
  bool tapped = getTap(tx, ty);

  switch (gState) {

    case S_INTRO:
      if (tapped) {
        resetGame();
        drawPlayScreen();
        gState   = S_PLAYING;
        lastStep = millis();
      }
      break;

    case S_PLAYING:
      if (tapped) {
        // Determine desired direction from tap relative to snake head centre
        Cell head = snakeHeadCell();
        int hpx = head.x * CELL + CELL / 2;
        int hpy = GRID_Y0 + head.y * CELL + CELL / 2;
        int dx  = tx - hpx;
        int dy  = ty - hpy;
        // Use the dominant axis; prevent 180° reversal
        if (abs(dx) >= abs(dy)) {
          if (dx > 0 && dir != LEFT)  nextDir = RIGHT;
          if (dx < 0 && dir != RIGHT) nextDir = LEFT;
        } else {
          if (dy > 0 && dir != UP)   nextDir = DOWN;
          if (dy < 0 && dir != DOWN) nextDir = UP;
        }
      }
      if (millis() - lastStep >= (unsigned long)stepMs) {
        lastStep = millis();
        stepGame();
      }
      break;

    case S_OVER:
      if (tapped) {
        resetGame();
        drawPlayScreen();
        gState   = S_PLAYING;
        lastStep = millis();
      }
      break;
  }

  delay(5);
}
