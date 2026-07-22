/*
 * CYD TV — MJPEG stream viewer for ESP32 CYD (ESP32-2432S028)
 * 320×240 landscape | TFT_eSPI + XPT2046 + WiFi
 *
 * Plays MJPEG video streams over HTTP (IP cameras, ffmpeg servers, etc.)
 * Does NOT support YouTube, Netflix, H.264, or RTSP.
 *
 * Arduino IDE:
 *   Board: ESP32 Dev Module
 *   Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA)
 *
 * Libraries:
 *   TFT_eSPI, XPT2046_Touchscreen, WiFiManager, JPEGDecoder (Bodmer)
 */

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <JPEGDecoder.h>
#include <SD.h>

TFT_eSPI tft = TFT_eSPI();

#define SCREEN_W 320
#define SCREEN_H 240
#define HEADER_H 24

// Colours copied from the Flappy Bird project
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

#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SD_CS    5
#define SD_MOSI  23
#define SD_MISO  19
#define SD_SCK   18

SPIClass sdSPI = SPIClass(HSPI);

#define JPEG_MAX    65536  // 64 KB max frame

struct Channel {
  const char* name;
  const char* url;
};

// Edit these URLs for your own cameras / ffmpeg MJPEG server
Channel channels[] = {
  { "Demo Cam",   "http://pendelcam.kip.uni-heidelberg.de/mjpg/video.mjpg" },
  { "Local Cam",  "http://192.168.1.231:8080/video" },
  { "PC Stream",  "http://192.168.1.50:8080/?action=stream" },
};
#define CH_COUNT (sizeof(channels) / sizeof(channels[0]))

uint8_t  jpegBuf[JPEG_MAX];
int      jpegLen = 0;

enum State { S_MENU, S_CONNECT, S_PLAY, S_ERROR };
State state = S_MENU;
int   activeCh = 0;
bool  stopPlay = false;
bool  wasTouch = false;

unsigned long lastFpsMs = 0;
int fpsCount = 0;
int fpsShow = 0;
int failShow = 0;

void headerBar(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_NAVY);
  tft.setTextColor(C_EYE_W, TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(4, 8);
  tft.print(title);
}

bool getTap(int& tx, int& ty) {
  bool on = touchscreen.tirqTouched() && touchscreen.touched();
  bool n = on && !wasTouch;
  wasTouch = on;
  if (n) {
    TS_Point p = touchscreen.getPoint();
    tx = map(p.x, 200, 3700, 0, SCREEN_W);
    ty = map(p.y, 240, 3800, 0, SCREEN_H);
  }
  return n;
}

bool hit(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

#define SD_BTN_Y    (SCREEN_H - 86)
#define WIFI_BTN_Y  (SCREEN_H - 54)
#define WIFI_BTN_H  28
#define SD_BTN_H    28
#define CH_BTN_H    28
#define CH_BTN_STEP 32

void drawWifiStatus() {
  tft.setTextSize(1);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(C_PIPE_BODY);
    tft.setCursor(8, HEADER_H + 4);
    tft.print("WiFi: ");
    tft.print(WiFi.SSID());
    tft.setCursor(8, HEADER_H + 14);
    tft.setTextColor(C_SCORE_SHD);
    tft.print("IP: ");
    tft.print(WiFi.localIP());
  } else {
    tft.setTextColor(TFT_RED);
    tft.setCursor(8, HEADER_H + 4);
    tft.print("WiFi: NOT connected");
    tft.setTextColor(C_BIRD);
    tft.setCursor(8, HEADER_H + 14);
    tft.print("Tap WiFi Setup below");
  }
}

void drawMenu() {
  state = S_MENU;
  stopPlay = false;
  tft.fillScreen(C_SKY);
  headerBar("CYD TV");
  drawWifiStatus();

  int y = HEADER_H + 30;
  for (int i = 0; i < (int)CH_COUNT; i++) {
    uint16_t bg = (i == activeCh) ? C_PIPE_CAP : C_PIPE_BODY;
    tft.fillRoundRect(8, y, SCREEN_W - 16, CH_BTN_H, 6, bg);
    tft.setTextColor(C_EYE_W, bg);
    tft.setTextSize(2);
    tft.setCursor(16, y + 8);
    tft.print(channels[i].name);
    y += CH_BTN_STEP;
  }

  tft.fillRoundRect(8, SD_BTN_Y, SCREEN_W - 16, SD_BTN_H, 6, C_PIPE_CAP);
  tft.drawRoundRect(8, SD_BTN_Y, SCREEN_W - 16, SD_BTN_H, 6, C_EYE_W);
  tft.setTextColor(C_EYE_W, C_PIPE_CAP);
  tft.setTextSize(2);
  tft.setCursor(62, SD_BTN_Y + 8);
  tft.print("SD Video");

  tft.fillRoundRect(8, WIFI_BTN_Y, SCREEN_W - 16, WIFI_BTN_H, 6, C_GND_DIRT);
  tft.drawRoundRect(8, WIFI_BTN_Y, SCREEN_W - 16, WIFI_BTN_H, 6, C_BIRD);
  tft.setTextColor(C_BIRD, C_GND_DIRT);
  tft.setTextSize(2);
  tft.setCursor(52, WIFI_BTN_Y + 8);
  tft.print("WiFi Setup");

  tft.setTextColor(C_SCORE_SHD);
  tft.setTextSize(1);
  tft.setCursor(8, SCREEN_H - 18);
  tft.print("Tap channel to play");
}

void drawStatus(const char* line1, const char* line2 = "") {
  tft.fillScreen(C_SKY);
  headerBar("CYD TV");
  tft.setTextColor(C_BIRD);
  tft.setTextSize(2);
  tft.setCursor(40, 90);
  tft.print(line1);
  if (line2[0]) {
    tft.setTextColor(C_EYE_W);
    tft.setTextSize(1);
    tft.setCursor(20, 120);
    tft.print(line2);
  }
}

void drawError(const char* title, const char* msg) {
  state = S_ERROR;
  tft.fillScreen(C_SKY);
  headerBar("Error");
  tft.setTextColor(TFT_RED);
  tft.setTextSize(2);
  tft.setCursor(24, 80);
  tft.print(title);
  tft.setTextColor(C_EYE_W);
  tft.setTextSize(1);
  tft.setCursor(8, 110);
  tft.print(msg);
  tft.setTextColor(C_BIRD);
  tft.setCursor(8, SCREEN_H - 20);
  tft.print("Tap to return to menu");
}

bool findJpegStart() {
  for (int i = 0; i < jpegLen - 1; i++) {
    if (jpegBuf[i] == 0xFF && jpegBuf[i + 1] == 0xD8) {
      if (i > 0) {
        memmove(jpegBuf, jpegBuf + i, jpegLen - i);
        jpegLen -= i;
      }
      return jpegLen >= 100;
    }
  }
  return false;
}

bool drawJpegFrame() {
  if (!findJpegStart()) return false;

  JpegDec.abort();
  if (JpegDec.decodeArray(jpegBuf, (uint32_t)jpegLen) == 0) return false;

  int x = 0;
  int y = HEADER_H;
  uint16_t mcuW = JpegDec.MCUWidth;
  uint16_t mcuH = JpegDec.MCUHeight;

  while (JpegDec.read())
    tft.pushImage(x + JpegDec.MCUx * mcuW, y + JpegDec.MCUy * mcuH,
                  mcuW, mcuH, JpegDec.pImage, true);
  return true;
}

void showFrame() {
  if (jpegLen < 100) return;
  if (drawJpegFrame()) {
    failShow = 0;
    fpsCount++;
    if (millis() - lastFpsMs >= 1000) {
      fpsShow = fpsCount;
      fpsCount = 0;
      lastFpsMs = millis();
      drawHud();
    }
  } else {
    failShow++;
  }
}

bool readNextFrame(WiFiClient& client, bool jpegDirect, bool multipart,
                   const String& marker, String& line) {
  if (jpegDirect) return readRawJpegFrame(client);
  if (multipart)  return readMultipartJpegFrame(client, marker, line);
  return readRawJpegFrame(client);
}

bool readRawJpegFrame(WiFiClient& client) {
  jpegLen = 0;
  uint8_t prev = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 10000 && !stopPlay) {
    while (client.available()) {
      uint8_t b = client.read();
      if (jpegLen < JPEG_MAX) jpegBuf[jpegLen++] = b;
      if (prev == 0xFF && b == 0xD9) return true;
      prev = b;
      t0 = millis();
    }
    delay(1);
  }
  return false;
}

bool readMultipartJpegFrame(WiFiClient& client, const String& marker, String& line) {
  int contentLen = -1;

  while (true) {
    if (!readLine(client, line, 8000)) return false;
    if (line.length() == 0) break;
    if (line.startsWith("--")) continue;  // part boundary line
    if (line.startsWith("Content-Length:") || line.startsWith("content-length:"))
      contentLen = line.substring(15).toInt();
  }

  jpegLen = 0;

  if (contentLen > 0) {
    if (contentLen > JPEG_MAX) {
      skipBytes(client, contentLen);
      skipCrLf(client);
      return false;
    }
    if (!readBytes(client, jpegBuf, contentLen, 20000)) return false;
    jpegLen = contentLen;
    skipCrLf(client);
    return true;
  }

  // Fallback: read bytes until next boundary marker
  uint8_t win[128];
  int wlen = 0;
  while (!stopPlay) {
    if (!client.available()) {
      if (!client.connected()) return jpegLen > 100;
      delay(1);
      continue;
    }

    uint8_t b = client.read();
    if (jpegLen < JPEG_MAX) jpegBuf[jpegLen++] = b;
    win[wlen % 128] = b;
    wlen++;

    if (marker.length() > 0 && wlen >= (int)marker.length()) {
      bool match = true;
      for (int i = 0; i < (int)marker.length(); i++) {
        if (win[(wlen - marker.length() + i) % 128] != marker[i]) {
          match = false;
          break;
        }
      }
      if (match) {
        jpegLen -= marker.length();
        while (jpegLen > 0 && (jpegBuf[jpegLen - 1] == '\r' || jpegBuf[jpegLen - 1] == '\n'))
          jpegLen--;
        return jpegLen > 100;
      }
    }
  }
  return false;
}

void drawHud() {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_NAVY);
  tft.setTextColor(C_EYE_W, TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(4, 8);
  tft.print(fpsShow);
  tft.print(" fps");
  if (failShow > 10) {
    tft.setTextColor(TFT_RED, TFT_NAVY);
    tft.setCursor(70, 8);
    tft.print("decode err");
  }
}

bool readLine(WiFiClient& client, String& line, uint32_t timeoutMs) {
  line = "";
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        if (line.endsWith("\r")) line.remove(line.length() - 1);
        return true;
      }
      line += c;
      t0 = millis();
    }
    if (stopPlay) return false;
    delay(1);
  }
  return false;
}

bool readBytes(WiFiClient& client, uint8_t* dst, int need, uint32_t timeoutMs) {
  int got = 0;
  uint32_t t0 = millis();
  while (got < need && millis() - t0 < timeoutMs) {
    int n = client.read(dst + got, need - got);
    if (n > 0) {
      got += n;
      t0 = millis();
    } else if (stopPlay) {
      return false;
    } else if (!client.connected() && !client.available()) {
      break;
    } else {
      delay(1);
    }
  }
  return got == need;
}

bool readBytes(File& file, uint8_t* dst, int need) {
  int got = 0;
  while (got < need && file.available() && !stopPlay) {
    int n = file.read(dst + got, need - got);
    if (n > 0) got += n;
    else delay(1);
  }
  return got == need;
}

void skipCrLf(WiFiClient& client) {
  while (client.available()) {
    int p = client.peek();
    if (p == '\r' || p == '\n') client.read();
    else break;
  }
}

bool skipBytes(WiFiClient& client, int need) {
  uint8_t dump[512];
  while (need > 0 && !stopPlay) {
    int chunk = need > 512 ? 512 : need;
    int n = client.read(dump, chunk);
    if (n > 0) need -= n;
    else if (!client.connected() && !client.available()) return false;
  }
  return true;
}

bool extractBoundary(const String& ct, String& boundary) {
  int i = ct.indexOf("boundary=");
  if (i < 0) return false;
  boundary = ct.substring(i + 9);
  boundary.trim();
  if (boundary.startsWith("\"")) {
    int e = boundary.indexOf('"', 1);
    if (e > 1) boundary = boundary.substring(1, e);
  }
  return boundary.length() > 0;
}

bool playMjpeg(const char* url) {
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(8000);
  if (!http.begin(url)) {
    drawError("STREAM FAILED", "Bad URL");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    drawError("STREAM FAILED", "HTTP error");
    return false;
  }

  String contentType = http.header("Content-Type");
  Serial.print("Content-Type: ");
  Serial.println(contentType);

  if (contentType.indexOf("text/html") >= 0) {
    http.end();
    drawError("STREAM FAILED", "Wrong URL - add /video");
    return false;
  }

  String boundary;
  bool multipart = extractBoundary(contentType, boundary);
  bool jpegDirect = (contentType.indexOf("image/jpeg") >= 0);

  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    http.end();
    drawError("STREAM FAILED", "No stream");
    return false;
  }

  state = S_PLAY;
  stopPlay = false;
  fpsCount = 0;
  fpsShow = 0;
  lastFpsMs = millis();
  tft.fillScreen(TFT_BLACK);
  drawHud();

  String marker = multipart ? String("--") + boundary : "";
  String line;

  while (!stopPlay && (stream->connected() || stream->available())) {
    int tx = 0, ty = 0;
    if (getTap(tx, ty)) {
      stopPlay = true;
      wasTouch = false;
      break;
    }

    if (!readNextFrame(*stream, jpegDirect, multipart, marker, line)) {
      if (!stream->connected() && stream->available() == 0) break;
      continue;
    }

    showFrame();
  }

  http.end();
  return !stopPlay;
}

bool readSdJpegFrame(File& file) {
  jpegLen = 0;
  bool foundStart = false;
  uint8_t prev = 0;

  while (file.available() && !stopPlay) {
    uint8_t b = file.read();

    if (!foundStart) {
      if (prev == 0xFF && b == 0xD8) {
        jpegBuf[0] = 0xFF;
        jpegBuf[1] = 0xD8;
        jpegLen = 2;
        foundStart = true;
      }
      prev = b;
      continue;
    }

    if (jpegLen < JPEG_MAX) jpegBuf[jpegLen++] = b;
    if (prev == 0xFF && b == 0xD9) return true;
    prev = b;
  }

  return false;
}

bool playSdVideo(const char* path) {
  state = S_PLAY;
  stopPlay = false;
  fpsCount = 0;
  fpsShow = 0;
  failShow = 0;
  lastFpsMs = millis();

  drawStatus("Opening SD...", path);

  if (!SD.begin(SD_CS, sdSPI)) {
    drawError("SD FAILED", "Card mount failed");
    return false;
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    drawError("SD FAILED", "Missing /video.mjpg");
    return false;
  }

  tft.fillScreen(TFT_BLACK);
  drawHud();

  while (!stopPlay && file.available()) {
    int tx = 0, ty = 0;
    if (getTap(tx, ty)) {
      stopPlay = true;
      wasTouch = false;
      break;
    }

    if (!readSdJpegFrame(file)) {
      if (!file.available()) {
        file.seek(0);
        continue;
      }
      break;
    }
    showFrame();
  }

  file.close();
  return true;
}

void openWifiPortal() {
  tft.fillScreen(C_SKY);
  headerBar("WiFi Setup");
  tft.setTextColor(C_BIRD);
  tft.setTextSize(2);
  tft.setCursor(8, 40);
  tft.print("On your phone:");
  tft.setTextColor(C_EYE_W);
  tft.setTextSize(1);
  tft.setCursor(8, 68);
  tft.print("1. WiFi settings -> join CYD-TV");
  tft.setCursor(8, 84);
  tft.print("2. Browser opens, or type:");
  tft.setTextColor(C_PIPE_CAP);
  tft.setCursor(8, 100);
  tft.print("   192.168.4.1");
  tft.setTextColor(C_EYE_W);
  tft.setCursor(8, 120);
  tft.print("3. Pick home WiFi (2.4GHz)");
  tft.setCursor(8, 136);
  tft.print("4. Enter password + Save");
  tft.setTextColor(C_PIPE_BODY);
  tft.setCursor(8, 164);
  tft.print("Waiting for phone... (5 min)");

  WiFi.disconnect(true);
  delay(300);

  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.setCaptivePortalEnable(true);
  wm.startConfigPortal("CYD-TV");

  drawMenu();
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  drawStatus("WiFi setup", "Trying saved network...");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConfigPortalTimeout(120);
  wm.setCaptivePortalEnable(true);
  wm.autoConnect("CYD-TV");
  // If this fails, menu still opens — user can tap WiFi Setup
}

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);
  WiFi.setSleep(false);

  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(3);

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  tft.init();
  tft.setRotation(3);
  tft.setSwapBytes(true);

  drawStatus("Starting...");
  ensureWiFi();
  drawMenu();
}

void loop() {
  int tx = 0, ty = 0;
  bool tapped = getTap(tx, ty);

  switch (state) {
    case S_MENU:
      if (tapped) {
        wasTouch = false;
        if (hit(tx, ty, 8, WIFI_BTN_Y, SCREEN_W - 16, WIFI_BTN_H)) {
          openWifiPortal();
          break;
        }
        if (hit(tx, ty, 8, SD_BTN_Y, SCREEN_W - 16, SD_BTN_H)) {
          state = S_CONNECT;
          bool ok = playSdVideo("/video.mjpg");
          if (!ok && !stopPlay) drawError("SD FAILED", "Could not play /video.mjpg");
          else drawMenu();
          break;
        }
        int y0 = HEADER_H + 30;
        for (int i = 0; i < (int)CH_COUNT; i++) {
          if (hit(tx, ty, 8, y0 + i * CH_BTN_STEP, SCREEN_W - 16, CH_BTN_H)) {
            if (WiFi.status() != WL_CONNECTED) {
              openWifiPortal();
              break;
            }
            activeCh = i;
            state = S_CONNECT;
            drawStatus("Connecting...", channels[i].name);
            bool ok = playMjpeg(channels[i].url);
            if (!ok && !stopPlay) drawError("STREAM FAILED", "Could not play stream");
            else drawMenu();
            break;
          }
        }
      }
      break;

    case S_ERROR:
      if (tapped) {
        wasTouch = false;
        drawMenu();
      }
      break;

    default:
      break;
  }

  delay(5);
}
