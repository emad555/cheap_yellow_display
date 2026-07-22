/*
 * FLAPPY BIRD for ESP32 CYD (ESP32-2432S028)
 * 320×240 landscape | TFT_eSPI + XPT2046_Touchscreen
 *
 * Tap anywhere to flap!
 * Fly through the gaps without hitting the pipes, ground, or ceiling.
 * Each gap you pass scores one point.
 *
 * Drawing strategy (memory-safe, no full-screen sprite):
 *   - Pipes: shift left 2 px/frame; erase the revealed right-edge strip with sky colour
 *   - Bird: a 24×218 px sprite column restores the background behind the bird each frame
 *
 * Libraries (Arduino Library Manager):
 *   - TFT_eSPI           (Bodmer)   — configure with CYD User_Setup.h
 *   - XPT2046_Touchscreen (PaulStoffregen)
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

/***** 1) DISPLAY *****/
TFT_eSPI    tft     = TFT_eSPI();
TFT_eSprite birdSpr = TFT_eSprite(&tft);  // narrow column sprite for bird area

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

/***** 3) LAYOUT & GAME CONSTANTS *****/
#define GROUND_H     22
#define PLAY_H       (SCREEN_H - GROUND_H)    // 218 — playfield height

// Bird (fixed horizontal position, only Y changes)
#define BIRD_X       60
#define BIRD_R       9
#define SPR_W        24                        // bird sprite column width
#define SPR_X        (BIRD_X - SPR_W / 2)     // sprite left edge = 48

// Pipes
#define PIPE_W       28
#define PIPE_CAP_EX   2    // cap widens each side beyond body
#define PIPE_CAP_H    7
#define PIPE_GAP     70    // vertical opening height
#define PIPE_SPEED    2    // px per frame leftward movement
#define PIPE_COUNT    3
#define PIPE_SPACING 115

#define PIPE_GAP_MIN  30
#define PIPE_GAP_MAX  (PLAY_H - PIPE_GAP - PIPE_GAP_MIN)   // 118

#define FRAME_MS     33    // ~30 fps

// Colours
#define C_SKY        0x867B   // light sky blue
#define C_GND_GRASS  0x0560   // bright green
#define C_GND_DIRT   0x8400   // dark brown
#define C_PIPE_BODY  0x07E0   // TFT_GREEN
#define C_PIPE_CAP   0x02E0   // slightly darker green
#define C_BIRD       TFT_YELLOW
#define C_BEAK       0xFCC0   // orange
#define C_EYE_W      TFT_WHITE
#define C_PUPIL      TFT_BLACK
#define C_SCORE      TFT_WHITE
#define C_SCORE_SHD  0x528A   // shadow colour

// Physics
#define GRAVITY      0.25f
#define JUMP_VEL    -5.0f
#define MAX_VEL      7.0f

/***** 4) GAME STATE *****/
struct Pipe { int x, gapY; bool scored; };

Pipe          pipes[PIPE_COUNT];
float         birdY     = PLAY_H / 2.0f;
float         birdVY    = 0.0f;
int           score     = 0;
int           hiScore   = 0;

enum GameState { S_INTRO, S_PLAYING, S_DEAD };
GameState     gState       = S_INTRO;
unsigned long lastFrame    = 0;
bool          wasTouch     = false;
bool          pendingFlap  = false;  // latches a tap until the next game frame

/***** 5) GROUND *****/
void drawGround() {
  tft.fillRect(0, PLAY_H,     SCREEN_W, 4,            C_GND_GRASS);
  tft.fillRect(0, PLAY_H + 4, SCREEN_W, GROUND_H - 4, C_GND_DIRT);
}

/***** 6) PIPES *****/
void drawPipe(int i) {
  int x  = pipes[i].x;
  int gy = pipes[i].gapY;
  int topH = gy;
  int botY = gy + PIPE_GAP;
  int botH = PLAY_H - botY;

  // Top pipe
  if (topH > PIPE_CAP_H)
    tft.fillRect(x, 0, PIPE_W, topH - PIPE_CAP_H, C_PIPE_BODY);
  tft.fillRect(x - PIPE_CAP_EX, topH - PIPE_CAP_H, PIPE_W + PIPE_CAP_EX * 2, PIPE_CAP_H, C_PIPE_CAP);

  // Bottom pipe
  if (botH > 0) {
    tft.fillRect(x - PIPE_CAP_EX, botY, PIPE_W + PIPE_CAP_EX * 2, PIPE_CAP_H, C_PIPE_CAP);
    if (botH > PIPE_CAP_H)
      tft.fillRect(x, botY + PIPE_CAP_H, PIPE_W, botH - PIPE_CAP_H, C_PIPE_BODY);
  }
}

// Erase the sky strip revealed as the pipe moved one step to the left
void eraseRightEdge(int i) {
  int x = pipes[i].x + PIPE_W + PIPE_CAP_EX;
  if (x < SCREEN_W)
    tft.fillRect(x, 0, PIPE_SPEED + 1, PLAY_H, C_SKY);
}

/***** 7) BIRD SPRITE COLUMN *****/
// Restores pipe pixels behind the bird, then draws the bird on top.
void drawBirdColumn() {
  int by = constrain((int)birdY, BIRD_R, PLAY_H - BIRD_R - 1);

  birdSpr.fillSprite(C_SKY);

  // Restore any pipe sections that overlap this sprite column
  for (int i = 0; i < PIPE_COUNT; i++) {
    int pL = pipes[i].x - PIPE_CAP_EX;
    int pR = pipes[i].x + PIPE_W + PIPE_CAP_EX;
    int sR = SPR_X + SPR_W;

    if (pL >= sR || pR <= SPR_X) continue;  // no overlap

    int gy   = pipes[i].gapY;
    int topH = gy;
    int botY = gy + PIPE_GAP;
    int botH = PLAY_H - botY;

    int lx = max(pL, SPR_X) - SPR_X;
    int rx = min(pR, sR)    - SPR_X;
    int w  = rx - lx;
    if (w <= 0) continue;

    // Top pipe body + cap
    if (topH > PIPE_CAP_H)
      birdSpr.fillRect(lx, 0, w, topH - PIPE_CAP_H, C_PIPE_BODY);
    if (topH > 0)
      birdSpr.fillRect(lx, max(0, topH - PIPE_CAP_H), w, min(PIPE_CAP_H, topH), C_PIPE_CAP);

    // Bottom pipe cap + body
    if (botH > 0) {
      birdSpr.fillRect(lx, botY, w, min(PIPE_CAP_H, botH), C_PIPE_CAP);
      if (botH > PIPE_CAP_H)
        birdSpr.fillRect(lx, botY + PIPE_CAP_H, w, botH - PIPE_CAP_H, C_PIPE_BODY);
    }
  }

  // Bird body
  int sx = SPR_W / 2;
  birdSpr.fillCircle(sx, by, BIRD_R, C_BIRD);

  // Beak
  birdSpr.fillTriangle(sx + BIRD_R - 2, by - 2,
                       sx + BIRD_R - 2, by + 2,
                       sx + BIRD_R + 5, by,
                       C_BEAK);
  // Eye
  birdSpr.fillCircle(sx + 4, by - 3, 3, C_EYE_W);
  birdSpr.fillCircle(sx + 5, by - 3, 1, C_PUPIL);

  birdSpr.pushSprite(SPR_X, 0);
}

/***** 8) SCORE DISPLAY *****/
void clearScoreArea() {
  tft.fillRect(SCREEN_W / 2 - 56, 2, 112, 26, C_SKY);
}

void drawScore() {
  char buf[8];
  itoa(score, buf, 10);
  int sw = strlen(buf) * 18;
  int sx = SCREEN_W / 2 - sw / 2;

  tft.setTextSize(3);
  tft.setTextColor(C_SCORE_SHD);
  tft.setCursor(sx + 1, 5);
  tft.print(buf);
  tft.setTextColor(C_SCORE);
  tft.setCursor(sx, 4);
  tft.print(buf);
}

/***** 9) SCREENS *****/
void drawBackground() {
  tft.fillRect(0, 0, SCREEN_W, PLAY_H, C_SKY);
  drawGround();
}

void drawIntro() {
  drawBackground();

  // Decorative pipe pair on the right
  tft.fillRect(258, 0,   PIPE_W, 80, C_PIPE_BODY);
  tft.fillRect(256, 74,  PIPE_W + PIPE_CAP_EX * 2, PIPE_CAP_H, C_PIPE_CAP);
  tft.fillRect(258, 155, PIPE_W, PLAY_H - 155, C_PIPE_BODY);
  tft.fillRect(256, 149, PIPE_W + PIPE_CAP_EX * 2, PIPE_CAP_H, C_PIPE_CAP);

  // Title panel
  tft.fillRoundRect(16, 50, 220, 64, 10, TFT_NAVY);
  tft.drawRoundRect(16, 50, 220, 64, 10, TFT_WHITE);

  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(30, 60);
  tft.print("FLAPPY  BIRD");

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(30, 88);
  tft.print("for ESP32 CYD");

  // Static bird illustration
  tft.fillCircle(BIRD_X, 140, BIRD_R, C_BIRD);
  tft.fillTriangle(BIRD_X + BIRD_R - 2, 138,
                   BIRD_X + BIRD_R - 2, 142,
                   BIRD_X + BIRD_R + 5, 140, C_BEAK);
  tft.fillCircle(BIRD_X + 4, 137, 3, C_EYE_W);
  tft.fillCircle(BIRD_X + 5, 137, 1, C_PUPIL);

  // Prompt
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(34, 172);
  tft.print("TAP TO PLAY!");

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(34, 198);
  tft.print("Tap anywhere to flap.");
}

void drawGameOver() {
  int cx = 60, cy = 62, cw = 200, ch = 116;
  tft.fillRoundRect(cx, cy, cw, ch, 12, TFT_NAVY);
  tft.drawRoundRect(cx, cy, cw, ch, 12, TFT_WHITE);

  tft.setTextColor(TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(cx + 18, cy + 10);
  tft.print("GAME  OVER");

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(1);
  tft.setCursor(cx + 14, cy + 46);
  tft.print("Score:  ");
  tft.print(score);
  tft.setCursor(cx + 14, cy + 62);
  tft.print("Best:   ");
  tft.print(hiScore);

  if (score > 0 && score == hiScore) {
    tft.setTextColor(TFT_YELLOW);
    tft.setCursor(cx + 14, cy + 80);
    tft.print("New personal best!");
  }

  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(cx + 30, cy + 98);
  tft.print("Tap to retry");
}

/***** 10) PIPE MANAGEMENT *****/
int randomGapY() {
  return random(PIPE_GAP_MIN, PIPE_GAP_MAX);
}

void initPipes() {
  for (int i = 0; i < PIPE_COUNT; i++) {
    pipes[i].x      = SCREEN_W + 30 + i * PIPE_SPACING;
    pipes[i].gapY   = randomGapY();
    pipes[i].scored = false;
  }
}

void respawnPipe(int i) {
  int maxX = 0;
  for (int j = 0; j < PIPE_COUNT; j++)
    if (pipes[j].x > maxX) maxX = pipes[j].x;
  pipes[i].x      = maxX + PIPE_SPACING;
  pipes[i].gapY   = randomGapY();
  pipes[i].scored = false;
}

/***** 11) COLLISION *****/
bool checkCollision() {
  int by = (int)birdY;

  if (by + BIRD_R >= PLAY_H) return true;
  if (by - BIRD_R <= 0)      return true;

  for (int i = 0; i < PIPE_COUNT; i++) {
    if (BIRD_X + BIRD_R > pipes[i].x &&
        BIRD_X - BIRD_R < pipes[i].x + PIPE_W) {
      if (by - BIRD_R < pipes[i].gapY)               return true;
      if (by + BIRD_R > pipes[i].gapY + PIPE_GAP)    return true;
    }
  }
  return false;
}

/***** 12) TOUCH *****/
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

/***** 13) GAME RESET *****/
void resetGame() {
  birdY       = PLAY_H * 0.42f;
  birdVY      = -2.5f;
  score       = 0;
  wasTouch    = false;
  pendingFlap = false;

  initPipes();
  drawBackground();
  for (int i = 0; i < PIPE_COUNT; i++) drawPipe(i);
  drawBirdColumn();
  drawScore();

  gState    = S_PLAYING;
  lastFrame = millis();
}

/***** 14) GAME STEP *****/
void gameStep(bool flap) {
  // Physics
  if (flap) birdVY = JUMP_VEL;
  birdVY += GRAVITY;
  if (birdVY > MAX_VEL) birdVY = MAX_VEL;
  birdY  += birdVY;

  // Pipes
  for (int i = 0; i < PIPE_COUNT; i++) {
    pipes[i].x -= PIPE_SPEED;

    // Score: bird just cleared this pipe
    if (!pipes[i].scored && pipes[i].x + PIPE_W < BIRD_X) {
      pipes[i].scored = true;
      score++;
      clearScoreArea();
      drawScore();
    }

    // Recycle pipe when it exits left
    if (pipes[i].x + PIPE_W + PIPE_CAP_EX < 0)
      respawnPipe(i);

    eraseRightEdge(i);
    drawPipe(i);
  }

  // Bird (drawn last so it appears on top of pipes)
  drawBirdColumn();

  // Collision
  if (checkCollision()) {
    if (score > hiScore) hiScore = score;
    gState = S_DEAD;
    delay(200);          // brief pause before overlay so player sees the crash
    wasTouch = false;
    drawGameOver();
  }
}

/***** 15) SETUP & LOOP *****/
void setup() {
  Serial.begin(115200);

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(3);

  tft.init();
  tft.setRotation(3);

  // Bird sprite: 24 × 218 px × 2 bytes = ~10.5 KB — comfortably fits in heap
  birdSpr.setColorDepth(16);
  birdSpr.createSprite(SPR_W, PLAY_H);

  randomSeed(analogRead(34));
  drawIntro();
}

void loop() {
  int tx = 0, ty = 0;
  bool tapped = getTap(tx, ty);

  // Latch any tap so it isn't missed between game frames
  if (tapped) pendingFlap = true;

  switch (gState) {

    case S_INTRO:
      if (tapped) { wasTouch = false; pendingFlap = false; resetGame(); }
      break;

    case S_PLAYING: {
      unsigned long now = millis();
      if (now - lastFrame >= FRAME_MS) {
        lastFrame = now;
        gameStep(pendingFlap);
        pendingFlap = false;   // clear after the frame consumed it
      }
      break;
    }

    case S_DEAD:
      if (tapped) { wasTouch = false; pendingFlap = false; resetGame(); }
      break;
  }
  // No delay() here — keeping the loop tight maximises touch poll rate
}
