/*  CYD app launcher — 2 pages with smooth swipe.
    PC: python pc_listener.py COM3  (Serial Monitor closed)
*/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "icons.h"

TFT_eSPI    tft    = TFT_eSPI();
TFT_eSprite rowSpr = TFT_eSprite(&tft);  // 320×ROW_H row buffer — one SPI burst per row

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define HEADER_H   48
#define FOOTER_H   24
#define GRID_TOP   HEADER_H
#define GRID_COLS  3
#define GRID_ROWS  2
#define GRID_H     (SCREEN_HEIGHT - HEADER_H - FOOTER_H)
#define COL_W      (SCREEN_WIDTH / GRID_COLS)
#define ROW_H      (GRID_H / GRID_ROWS)
#define LABEL_SPACE 16

#define PAGE_COUNT     2
#define APPS_PER_PAGE  6
#define SWIPE_MIN      40   // release: must move this far to change page
#define TAP_MAX_MOVE   24   // release: within this = tap, not swipe
#define SWIPE_LIVE_MIN 22   // during drag: start live preview (above touch noise)
#define SLIDE_ANIM_STEPS 16   // frames for snap-to-page finish
#define LIVE_DRAG_THRESH  3   // px finger must move before redraw

#define COLOR_HEADER   0x1082
#define COLOR_DIVIDER  0xC618
#define COLOR_FOOTER   0xEF7D
#define COLOR_READY    0x07E0
#define COLOR_PAGE_ON  0x1082
#define COLOR_PAGE_OFF 0xC618

unsigned long lastCommandMs = 0;
const unsigned long COMMAND_COOLDOWN_MS = 700;
char statusLine[28] = "Ready";
int  currentPage    = 0;

struct AppTile {
  const char    *command;
  const char    *label;
  int            col, row;
  const uint16_t *icon;
  uint16_t       pressTint;
};

const AppTile PAGE_LAUNCHER[] = {
  {"OPEN_FUSION360", "Fusion 360",  0, 0, icon_fusion360, 0xFE59},
  {"OPEN_EUFYMAKE",  "eufyMake",    1, 0, icon_eufymake,  0xAF5F},
  {"OPEN_STEAM",     "Steam",       2, 0, icon_steam,     0x2D7F},
  {"OPEN_CHROME",    "Chrome",      0, 1, icon_chrome,    0x2F13},
  {"OPEN_CURSOR",    "Cursor",      1, 1, icon_cursor,    0x4A69},
  {"OPEN_ARDUINO",   "Arduino IDE", 2, 1, icon_arduino,   0x0A6B},
};

const AppTile PAGE_OFFICE[] = {
  {"OPEN_WORD",        "Word",        0, 0, icon_word,       0x4AB5},
  {"OPEN_EXCEL",       "Excel",       1, 0, icon_excel,      0x3D0A},
  {"OPEN_POWERPOINT",  "PowerPoint",  2, 0, icon_powerpoint, 0xFA9A},
  {"OPEN_OUTLOOK",     "Outlook",     0, 1, icon_outlook,    0x5B6D},
  {"OPEN_ONENOTE",     "OneNote",     1, 1, icon_onenote,    0x6B5D},
  {"OPEN_TEAMS",       "Teams",       2, 1, icon_teams,      0x5BEF},
};

const AppTile *const PAGES[]       = {PAGE_LAUNCHER, PAGE_OFFICE};
const char    *const PAGE_TITLES[] = {"Apps & tools", "Microsoft Office"};

int mapTouchX(int r) { return map(r, 200, 3700, 1, SCREEN_WIDTH);  }
int mapTouchY(int r) { return map(r, 240, 3800, 1, SCREEN_HEIGHT); }

bool readTouchPoint(int &x, int &y) {
  if (!touchscreen.tirqTouched() || !touchscreen.touched()) return false;
  TS_Point p = touchscreen.getPoint();
  x = mapTouchX(p.x);
  y = mapTouchY(p.y);
  return true;
}

bool inGridArea(int y) { return y >= GRID_TOP && y < SCREEN_HEIGHT - FOOTER_H; }

void drawAccentStripe() {
  int y = HEADER_H - 5, seg = SCREEN_WIDTH / 4;
  tft.fillRect(0,       y, seg,                   3, TFT_RED);
  tft.fillRect(seg,     y, seg,                   3, 0xFD20);
  tft.fillRect(seg*2,   y, seg,                   3, TFT_GREEN);
  tft.fillRect(seg*3,   y, SCREEN_WIDTH - seg*3,  3, TFT_BLUE);
}

void drawHeader() {
  tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_H, COLOR_HEADER);
  drawAccentStripe();
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, COLOR_HEADER);
  tft.setTextSize(2);
  tft.drawString("Launchpad", SCREEN_WIDTH / 2, 8);
  tft.setTextSize(1);
  tft.setTextColor(0xBDF7, COLOR_HEADER);
  tft.drawString(PAGE_TITLES[currentPage], SCREEN_WIDTH / 2, 26);
  tft.drawString("Swipe left / right",    SCREEN_WIDTH / 2, 36);
}

void drawPageDots() {
  int y = SCREEN_HEIGHT - FOOTER_H / 2;
  tft.fillCircle(SCREEN_WIDTH - 34, y, 4, currentPage == 0 ? COLOR_PAGE_ON : COLOR_PAGE_OFF);
  tft.fillCircle(SCREEN_WIDTH - 16, y, 4, currentPage == 1 ? COLOR_PAGE_ON : COLOR_PAGE_OFF);
}

void drawFooter() {
  int y = SCREEN_HEIGHT - FOOTER_H;
  tft.fillRect(0, y, SCREEN_WIDTH, FOOTER_H, COLOR_FOOTER);
  tft.drawFastHLine(0, y, SCREEN_WIDTH, COLOR_DIVIDER);
  tft.fillCircle(12, y + FOOTER_H / 2, 4, COLOR_READY);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_DARKGREY, COLOR_FOOTER);
  tft.setTextSize(1);
  tft.drawString(statusLine, 22, y + FOOTER_H / 2);
  drawPageDots();
}

void setStatus(const char *msg) {
  strncpy(statusLine, msg, sizeof(statusLine) - 1);
  statusLine[sizeof(statusLine) - 1] = '\0';
  drawFooter();
}

void blitIcon(int sx, int sy, const uint16_t *pgmIcon) {
  // Sprite pushImage has no transparency key — skip ICON_TRANSP per pixel.
  for (int py = 0; py < ICON_H; py++) {
    for (int px = 0; px < ICON_W; px++) {
      uint16_t p = pgm_read_word(pgmIcon + py * ICON_W + px);
      if (p != ICON_TRANSP)
        rowSpr.drawPixel(sx + px, sy + py, p);
    }
  }
}

void drawRowFrame(int row,
                  int fromPage, int fromOff,
                  int toPage,   int toOff,
                  int pressIdx = -1)
{
  rowSpr.fillSprite(TFT_WHITE);   // clean slate — icons float on white, no cell boxes

  // ── fromPage tiles ──
  const AppTile *from = PAGES[fromPage];
  for (int i = 0; i < APPS_PER_PAGE; i++) {
    if (from[i].row != row) continue;
    int tx = from[i].col * COL_W + fromOff;
    if (tx + COL_W <= 0 || tx >= SCREEN_WIDTH) continue;

    bool pressed = (i == pressIdx);
    if (pressed)
      rowSpr.fillRect(tx, 0, COL_W, ROW_H, from[i].pressTint);

    blitIcon(tx + (COL_W - ICON_W) / 2,
             (ROW_H - ICON_H - LABEL_SPACE) / 2,
             from[i].icon);

    rowSpr.setTextDatum(TC_DATUM);
    rowSpr.setTextColor(pressed ? TFT_NAVY : TFT_DARKGREY,
                        pressed ? from[i].pressTint : TFT_WHITE);
    rowSpr.setTextSize(1);
    rowSpr.drawString(from[i].label, tx + COL_W / 2, ROW_H - LABEL_SPACE + 2);
  }

  // ── toPage tiles (only during animation) ──
  if (toPage >= 0) {
    const AppTile *to = PAGES[toPage];
    for (int i = 0; i < APPS_PER_PAGE; i++) {
      if (to[i].row != row) continue;
      int tx = to[i].col * COL_W + toOff;
      if (tx + COL_W <= 0 || tx >= SCREEN_WIDTH) continue;

      blitIcon(tx + (COL_W - ICON_W) / 2,
               (ROW_H - ICON_H - LABEL_SPACE) / 2,
               to[i].icon);

      rowSpr.setTextDatum(TC_DATUM);
      rowSpr.setTextColor(TFT_DARKGREY, TFT_WHITE);
      rowSpr.setTextSize(1);
      rowSpr.drawString(to[i].label, tx + COL_W / 2, ROW_H - LABEL_SPACE + 2);
    }
  }

  rowSpr.pushSprite(0, GRID_TOP + row * ROW_H);
}

void drawGridFrame(int fromPage, int fromOff, int toPage, int toOff, int pressIdx = -1) {
  for (int r = 0; r < GRID_ROWS; r++)
    drawRowFrame(r, fromPage, fromOff, toPage, toOff, pressIdx);
}

void drawCurrentPage() {
  drawGridFrame(currentPage, 0, -1, 0);
}

void drawSlideFrame(int fromPage, int toPage, int dragDx) {
  dragDx = constrain(dragDx, -SCREEN_WIDTH, SCREEN_WIDTH);
  int toOff = (dragDx <= 0) ? SCREEN_WIDTH + dragDx : -SCREEN_WIDTH + dragDx;
  drawGridFrame(fromPage, dragDx, toPage, toOff);
}

void drawScreen() {
  tft.fillScreen(TFT_WHITE);
  drawHeader();
  drawCurrentPage();
  drawFooter();
}

int tileAt(int tx, int ty, int page) {
  if (!inGridArea(ty)) return -1;
  int col = tx / COL_W;
  int row = (ty - GRID_TOP) / ROW_H;
  if (col < 0 || col >= GRID_COLS || row < 0 || row >= GRID_ROWS) return -1;
  for (int i = 0; i < APPS_PER_PAGE; i++)
    if (PAGES[page][i].col == col && PAGES[page][i].row == row) return i;
  return -1;
}

void sendCommand(int index) {
  if (millis() - lastCommandMs < COMMAND_COOLDOWN_MS) return;
  lastCommandMs = millis();

  const AppTile &app = PAGES[currentPage][index];
  char msg[28];
  snprintf(msg, sizeof(msg), "Opening %s...", app.label);
  setStatus(msg);

  Serial.println(app.command);

  // Press highlight — single row redraw, one SPI burst
  drawRowFrame(app.row, currentPage, 0, -1, 0, index);
  delay(90);
  drawRowFrame(app.row, currentPage, 0, -1, 0);

  setStatus("Ready");
}

int swipeTargetPage(int dx) {
  if (dx < 0 && currentPage < PAGE_COUNT - 1) return currentPage + 1;
  if (dx > 0 && currentPage > 0)              return currentPage - 1;
  return -1;
}

void finishPageChange(int toPage, int fromDx) {
  int fromPage = currentPage;
  if (toPage == fromPage) { drawCurrentPage(); return; }

  int endDx = (toPage > fromPage) ? -SCREEN_WIDTH : SCREEN_WIDTH;

  // Snap animation — no added delay; hardware SPI speed limits the frame rate
  for (int step = 1; step <= SLIDE_ANIM_STEPS; step++) {
    int dx = fromDx + ((endDx - fromDx) * step) / SLIDE_ANIM_STEPS;
    drawSlideFrame(fromPage, toPage, dx);
  }

  currentPage = toPage;
  drawHeader();
  drawCurrentPage();
  drawFooter();
}

void handleTouch() {
  int startX, startY;
  if (!readTouchPoint(startX, startY)) return;

  int lastX = startX, lastY = startY;
  int lastDx = 0, lastDrawnDx = 0;
  bool wasSwiping = false;

  while (touchscreen.touched()) {
    if (readTouchPoint(lastX, lastY)) {
      int dx = lastX - startX;
      int dy = lastY - startY;
      lastDx = dx;

      if (inGridArea(startY) && abs(dx) > SWIPE_LIVE_MIN && abs(dx) > abs(dy)) {
        int tp = swipeTargetPage(dx);
        if (tp >= 0 && abs(dx - lastDrawnDx) >= LIVE_DRAG_THRESH) {
          drawSlideFrame(currentPage, tp, dx);
          lastDrawnDx = dx;
          wasSwiping  = true;
        }
      }
    }
    delay(6);
  }

  int dx = lastX - startX;
  int dy = lastY - startY;

  // Tap first — small movement on release always opens the icon, never changes page
  if (abs(dx) <= TAP_MAX_MOVE && abs(dy) <= TAP_MAX_MOVE) {
    int idx = tileAt(startX, startY, currentPage);
    if (idx >= 0) sendCommand(idx);
    return;
  }

  // Page change only when the finger clearly swiped horizontally
  if (abs(dx) >= SWIPE_MIN && abs(dx) > abs(dy)) {
    int toPage = swipeTargetPage(dx);
    if (toPage >= 0) finishPageChange(toPage, lastDx);
    return;
  }

  // Started a slide preview but didn't swipe far enough — snap back
  if (wasSwiping) drawCurrentPage();
}

void setup() {
  Serial.begin(115200);

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(3);

  tft.init();
  tft.setRotation(3);

  // Row sprite: 320 × ROW_H ≈ 54 KB — reused for every row draw
  rowSpr.setColorDepth(16);
  rowSpr.createSprite(SCREEN_WIDTH, ROW_H);

  drawScreen();
}

void loop() {
  if (touchscreen.tirqTouched() && touchscreen.touched())
    handleTouch();
  delay(12);
}
