/*
 * SIMON SAYS for ESP32 CYD (ESP32-2432S028)
 * 320×240 landscape | TFT_eSPI + XPT2046_Touchscreen
 *
 * Four large colored buttons fill the screen.
 * Watch the sequence light up — then tap the same colors in order.
 * Each round adds one more step. How far can you go?
 *
 * Libraries (install via Arduino Library Manager):
 *   - TFT_eSPI           (Bodmer)
 *   - XPT2046_Touchscreen (PaulStoffregen)
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

/***** 3) LAYOUT — 4 buttons in a 2×2 grid *****/
#define HEADER_H  30
#define GAP        5

// Each button takes half the available width/height minus gaps
#define BTN_W  ((SCREEN_W - GAP * 3) / 2)           // ≈152 px
#define BTN_H  ((SCREEN_H - HEADER_H - GAP * 3) / 2) // ≈ 97 px
#define RADIUS  10

// Button index layout:
//   0=RED (top-left)   1=GREEN (top-right)
//   2=BLUE (bot-left)  3=YELLOW (bot-right)
struct Btn {
  int      x, y;
  uint16_t bright;   // lit colour
  uint16_t dim;      // unlit colour
  const char* name;
};

const Btn BTN[4] = {
  { GAP,           HEADER_H + GAP,             TFT_RED,    0x6000, "RED"    },
  { GAP * 2 + BTN_W, HEADER_H + GAP,           TFT_GREEN,  0x0300, "GREEN"  },
  { GAP,           HEADER_H + GAP * 2 + BTN_H, TFT_BLUE,   0x0018, "BLUE"   },
  { GAP * 2 + BTN_W, HEADER_H + GAP * 2 + BTN_H, TFT_YELLOW, 0x5340, "YELLOW" },
};

/***** 4) GAME STATE *****/
#define MAX_SEQ 100

int  seq[MAX_SEQ];   // generated colour sequence (0-3)
int  seqLen    = 0;
int  gameRound = 0;
int  hiRound   = 0;
int  playerPos = 0;  // how far through the sequence the player has tapped

enum GameState { S_INTRO, S_SHOWING, S_PLAYER, S_GAMEOVER };
GameState gState = S_INTRO;

// Non-blocking show-sequence state
int           showIdx   = 0;
bool          showLit   = false;
unsigned long showTimer = 0;

// Touch edge-detection
bool          wasTouch      = false;
unsigned long inputAllowAt  = 0;   // millis() timestamp after which S_PLAYER accepts input

/***** 5) SHOW TIMING — faster each 5 rounds *****/
int showOnMs() {
  if (gameRound <= 5)  return 500;
  if (gameRound <= 10) return 380;
  if (gameRound <= 15) return 280;
  return 200;
}
int showOffMs() {
  if (gameRound <= 5)  return 200;
  if (gameRound <= 10) return 150;
  return 100;
}

/***** 6) DRAWING *****/
void drawBtn(int i, bool lit) {
  uint16_t col = lit ? BTN[i].bright : BTN[i].dim;
  tft.fillRoundRect(BTN[i].x, BTN[i].y, BTN_W, BTN_H, RADIUS, col);

  if (!lit) {
    // Label on dim button
    tft.setTextColor(TFT_DARKGREY);
    tft.setTextSize(2);
    int lw = strlen(BTN[i].name) * 12;
    tft.setCursor(BTN[i].x + (BTN_W - lw) / 2,
                  BTN[i].y + BTN_H / 2 - 8);
    tft.print(BTN[i].name);
  }
}

void drawAllButtons(int litIdx = -1) {
  for (int i = 0; i < 4; i++) drawBtn(i, i == litIdx);
}

void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_NAVY);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setCursor(4, 7);
  tft.print("Round:");
  tft.print(gameRound);
  tft.setCursor(188, 7);
  tft.print("Best:");
  tft.print(hiRound);
}

void drawHeaderMsg(const char* msg, uint16_t col = TFT_WHITE) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_NAVY);
  tft.setTextSize(2);
  tft.setTextColor(col, TFT_NAVY);
  int lw = strlen(msg) * 12;
  tft.setCursor((SCREEN_W - lw) / 2, 7);
  tft.print(msg);
}

/***** 7) INTRO SCREEN *****/
void drawIntro() {
  // Coloured quadrants as background
  tft.fillRect(0,            0,            SCREEN_W / 2, SCREEN_H / 2, 0x6000);
  tft.fillRect(SCREEN_W / 2, 0,            SCREEN_W / 2, SCREEN_H / 2, 0x0300);
  tft.fillRect(0,            SCREEN_H / 2, SCREEN_W / 2, SCREEN_H / 2, 0x0018);
  tft.fillRect(SCREEN_W / 2, SCREEN_H / 2, SCREEN_W / 2, SCREEN_H / 2, 0x5340);

  // Title card
  tft.fillRoundRect(44, 56, 232, 58, 12, TFT_BLACK);
  tft.drawRoundRect(44, 56, 232, 58, 12, TFT_WHITE);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(3);
  tft.setCursor(56, 68);
  tft.print("SIMON SAYS");

  // Sub-text
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(68, 128);
  tft.print("Watch the colors, then tap");
  tft.setCursor(68, 144);
  tft.print("them back in the same order.");

  // Prompt
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(80, 184);
  tft.print("TAP TO START");
}

/***** 8) GAME-OVER OVERLAY *****/
void drawGameOver() {
  if (gameRound > hiRound) hiRound = gameRound;
  gState = S_GAMEOVER;

  // Darken all buttons
  for (int i = 0; i < 4; i++)
    tft.fillRoundRect(BTN[i].x, BTN[i].y, BTN_W, BTN_H, RADIUS, TFT_BLACK);

  // Card
  int cx = 48, cy = 64, cw = 224, ch = 112;
  tft.fillRoundRect(cx, cy, cw, ch, 14, TFT_NAVY);
  tft.drawRoundRect(cx, cy, cw, ch, 14, TFT_WHITE);

  tft.setTextColor(TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(cx + 20, cy + 12);
  tft.print("WRONG COLOR!");

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(cx + 14, cy + 46);
  tft.print("You reached round ");
  tft.print(gameRound);
  tft.print(".");

  if (gameRound == hiRound && gameRound > 1) {
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(cx + 14, cy + 64);
    tft.print("NEW BEST! Keep it up!");
  }

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(cx + 36, cy + 86);
  tft.print("Tap to try again");
}

/***** 9) WRONG-TAP FLASH *****/
void flashAllRed() {
  for (int f = 0; f < 3; f++) {
    for (int i = 0; i < 4; i++)
      tft.fillRoundRect(BTN[i].x, BTN[i].y, BTN_W, BTN_H, RADIUS, TFT_RED);
    delay(140);
    drawAllButtons();
    delay(90);
  }
}

/***** 10) SEQUENCE SHOW (non-blocking) *****/
void startShowSequence() {
  gState     = S_SHOWING;
  showIdx    = 0;
  showLit    = false;
  showTimer  = millis();
  drawHeader();
  drawHeaderMsg("WATCH...", TFT_CYAN);
  drawAllButtons();
}

void updateShowing() {
  unsigned long now = millis();

  if (!showLit) {
    // Waiting in gap before lighting next button
    if (now - showTimer >= (unsigned long)showOffMs()) {
      showLit = true;
      drawBtn(seq[showIdx], true);
      showTimer = now;
    }
  } else {
    // Button is lit — wait then turn off
    if (now - showTimer >= (unsigned long)showOnMs()) {
      drawBtn(seq[showIdx], false);
      showLit = false;
      showIdx++;
      showTimer = now;

      if (showIdx >= seqLen) {
        // All shown — player's turn
        playerPos    = 0;
        wasTouch     = false;          // discard any touch held during showing
        inputAllowAt = millis() + 400; // 400 ms grace before accepting taps
        gState       = S_PLAYER;
        drawHeaderMsg("YOUR TURN!", TFT_GREEN);
      }
    }
  }
}

/***** 11) TOUCH HELPER *****/
// Returns true on a rising-edge tap; sets tx/ty to screen coords.
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

int hitButton(int tx, int ty) {
  for (int i = 0; i < 4; i++) {
    if (tx >= BTN[i].x && tx < BTN[i].x + BTN_W &&
        ty >= BTN[i].y && ty < BTN[i].y + BTN_H)
      return i;
  }
  return -1;  // missed
}

/***** 12) ROUND MANAGEMENT *****/
void startNewRound() {
  gameRound++;
  seq[seqLen] = random(4);
  seqLen++;
  delay(600);       // let "CORRECT!" message linger
  wasTouch = false; // discard any touch accumulated during the delay
  startShowSequence();
}

void startGame() {
  gameRound = 0;
  seqLen    = 0;
  gState  = S_INTRO;  // will be overridden immediately

  // Draw the play field
  tft.fillScreen(TFT_BLACK);
  drawHeader();
  drawAllButtons();
  delay(400);
  wasTouch = false; // discard the tap that launched the game

  startNewRound();
}

/***** 13) SETUP & LOOP *****/
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
      if (tapped) startGame();
      break;

    case S_SHOWING:
      updateShowing();   // non-blocking timer-driven animation
      break;

    case S_PLAYER:
      if (tapped && millis() >= inputAllowAt) {
        int btn = hitButton(tx, ty);
        if (btn < 0) break;  // tapped the gap — ignore

        // Visual feedback: flash the tapped button
        drawBtn(btn, true);
        delay(120);
        drawBtn(btn, false);
        wasTouch = false;  // clear so next tap is always a fresh edge

        if (btn == seq[playerPos]) {
          // Correct tap
          playerPos++;
          if (playerPos >= seqLen) {
            // Whole sequence matched!
            drawHeaderMsg("CORRECT!", TFT_GREEN);
            startNewRound();
          }
        } else {
          // Wrong tap
          flashAllRed();
          drawGameOver();
        }
      }
      break;

    case S_GAMEOVER:
      if (tapped) { wasTouch = false; startGame(); }
      break;
  }

  delay(5);
}
