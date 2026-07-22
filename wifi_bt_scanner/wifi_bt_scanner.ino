/*
 * WiFi & BLE SCANNER for ESP32 CYD (ESP32-2432S028)
 *
 * REQUIRED Arduino IDE setting (sketch is too large for default partition):
 *   Tools -> Partition Scheme -> "Minimal SPIFFS (1.9MB APP with OTA)"
 *   or "Huge APP (3MB No OTA/1MB SPIFFS)"
 *
 * Set ENABLE_BLE to 0 for WiFi-only build (fits default partition).
 */
#define ENABLE_BLE 1

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFi.h>
#if ENABLE_BLE
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#endif

TFT_eSPI tft = TFT_eSPI();
#define W 320
#define H 240

#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define HDR_H 28
#define FTR_H 28
#define SCW 24
#define LW (W - SCW)
#define LH (H - HDR_H - FTR_H)
#define IH 24
#define PAGE (LH / IH)

struct WiFiNet {
  char ssid[33];
  char bssid[18];
  int32_t rssi;
  uint8_t channel;
  bool open;
};

#if ENABLE_BLE
struct BLEDev {
  char name[24];
  char mac[18];
  int rssi;
};
#endif

#define MAX_WIFI 20
#if ENABLE_BLE
#define MAX_BLE 20
BLEDev bleDevs[MAX_BLE];
int bleCount = 0;
bool bleReady = false;
#endif

WiFiNet wifiNets[MAX_WIFI];
int wifiCount = 0;

enum State { S_HOME, S_WIFI_LIST, S_WIFI_DETAIL, S_CONNECTED
#if ENABLE_BLE
  , S_BLE_LIST, S_BLE_DETAIL
#endif
};
State state = S_HOME;
int sel = -1;
int scroll = 0;
bool wasTouch = false;

#if ENABLE_BLE
class BLECb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice d) override {
    if (bleCount >= MAX_BLE) return;
    const char* mac = d.getAddress().toString().c_str();
    for (int i = 0; i < bleCount; i++)
      if (strcmp(bleDevs[i].mac, mac) == 0) return;
    if (d.haveName() && d.getName().length())
      strlcpy(bleDevs[bleCount].name, d.getName().c_str(), 24);
    else
      strcpy(bleDevs[bleCount].name, "(unknown)");
    strlcpy(bleDevs[bleCount].mac, mac, 18);
    bleDevs[bleCount].rssi = d.getRSSI();
    bleCount++;
  }
};
static BLECb bleCb;
#endif

void header(const char* t, bool back = false) {
  tft.fillRect(0, 0, W, HDR_H, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(2);
  if (back) { tft.setCursor(4, 7); tft.print("<"); tft.setCursor(20, 7); }
  else tft.setCursor(6, 7);
  tft.print(t);
}

void footer(const char* b) {
  tft.fillRect(0, H - FTR_H, W, FTR_H, 0x2104);
  tft.fillRoundRect(4, H - FTR_H + 4, 72, FTR_H - 8, 4, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextSize(1);
  tft.setCursor(12, H - FTR_H + 10);
  tft.print(b);
}

void truncS(const char* s, char* d, int n) {
  if ((int)strlen(s) <= n) { strcpy(d, s); return; }
  strncpy(d, s, n - 1);
  d[n - 1] = 0;
}

bool hit(int tx, int ty, int x, int y, int ww, int hh) {
  return tx >= x && tx < x + ww && ty >= y && ty < y + hh;
}

bool tap(int& tx, int& ty) {
  bool on = touchscreen.tirqTouched() && touchscreen.touched();
  bool n = on && !wasTouch;
  wasTouch = on;
  if (n) {
    TS_Point p = touchscreen.getPoint();
    tx = map(p.x, 200, 3700, 0, W);
    ty = map(p.y, 240, 3800, 0, H);
  }
  return n;
}

void sortWifi() {
  for (int i = 0; i < wifiCount - 1; i++)
    for (int j = 0; j < wifiCount - i - 1; j++)
      if (wifiNets[j].rssi < wifiNets[j + 1].rssi) {
        WiFiNet t = wifiNets[j];
        wifiNets[j] = wifiNets[j + 1];
        wifiNets[j + 1] = t;
      }
}

#if ENABLE_BLE
void sortBle() {
  for (int i = 0; i < bleCount - 1; i++)
    for (int j = 0; j < bleCount - i - 1; j++)
      if (bleDevs[j].rssi < bleDevs[j + 1].rssi) {
        BLEDev t = bleDevs[j];
        bleDevs[j] = bleDevs[j + 1];
        bleDevs[j + 1] = t;
      }
}
#endif

void drawScroll(int total) {
  tft.fillRect(LW, HDR_H, SCW, LH, 0x1082);
  tft.setTextSize(2);
  tft.setTextColor(scroll > 0 ? TFT_WHITE : 0x7BEF, 0x1082);
  tft.setCursor(LW + 5, HDR_H + LH / 2 - 24);
  tft.print("^");
  tft.setTextColor(scroll + PAGE < total ? TFT_WHITE : 0x7BEF, 0x1082);
  tft.setCursor(LW + 5, HDR_H + LH / 2 + 4);
  tft.print("v");
}

void drawHome() {
  state = S_HOME;
  tft.fillScreen(TFT_BLACK);
  header("WiFi/BLE Scanner");

  tft.fillRoundRect(8, 50, 144, 64, 8, TFT_NAVY);
  tft.setTextColor(TFT_CYAN); tft.setTextSize(2);
  tft.setCursor(50, 60); tft.print("WiFi");
  tft.setTextSize(1); tft.setTextColor(TFT_WHITE);
  tft.setCursor(20, 88); tft.print("Scan networks");

#if ENABLE_BLE
  tft.fillRoundRect(168, 50, 144, 64, 8, 0x0818);
  tft.setTextColor(TFT_CYAN); tft.setTextSize(2);
  tft.setCursor(210, 60); tft.print("BLE");
  tft.setTextSize(1); tft.setTextColor(TFT_WHITE);
  tft.setCursor(180, 88); tft.print("Scan BLE 5s");
#endif

  tft.setTextColor(0x7BEF); tft.setTextSize(1);
  tft.setCursor(8, 140); tft.print("MAC: ");
  tft.print(WiFi.macAddress().c_str());
#if !ENABLE_BLE
  tft.setCursor(8, 156); tft.print("BLE disabled - set ENABLE_BLE 1");
#endif
}

void drawWiFiList() {
  state = S_WIFI_LIST;
  sortWifi();
  tft.fillScreen(TFT_BLACK);
  char t[20];
  snprintf(t, 20, "WiFi %d", wifiCount);
  header(t, true);
  drawScroll(wifiCount);
  footer("RESCAN");

  tft.fillRect(0, HDR_H, LW, LH, TFT_BLACK);
  int end = min(scroll + PAGE, wifiCount);
  for (int i = scroll; i < end; i++) {
    int y = HDR_H + (i - scroll) * IH;
    uint16_t bg = ((i - scroll) & 1) ? 0x0841 : TFT_BLACK;
    tft.fillRect(0, y, LW, IH, bg);
    char s[22];
    truncS(wifiNets[i].ssid, s, 20);
    tft.setTextColor(TFT_WHITE, bg); tft.setTextSize(1);
    tft.setCursor(4, y + 4); tft.print(s);
    tft.setTextColor(0x7BEF, bg);
    tft.setCursor(4, y + 14);
    char sub[20];
    snprintf(sub, 20, "ch%d %ddBm", wifiNets[i].channel, wifiNets[i].rssi);
    tft.print(sub);
    tft.setCursor(LW - 36, y + 8);
    tft.setTextColor(wifiNets[i].open ? TFT_GREEN : TFT_YELLOW, bg);
    tft.print(wifiNets[i].open ? "OPEN" : "LOCK");
  }
}

#if ENABLE_BLE
void drawBleList() {
  state = S_BLE_LIST;
  sortBle();
  tft.fillScreen(TFT_BLACK);
  char t[20];
  snprintf(t, 20, "BLE %d", bleCount);
  header(t, true);
  drawScroll(bleCount);
  footer("RESCAN");

  tft.fillRect(0, HDR_H, LW, LH, TFT_BLACK);
  int end = min(scroll + PAGE, bleCount);
  for (int i = scroll; i < end; i++) {
    int y = HDR_H + (i - scroll) * IH;
    uint16_t bg = ((i - scroll) & 1) ? 0x0841 : TFT_BLACK;
    tft.fillRect(0, y, LW, IH, bg);
    char s[22];
    truncS(bleDevs[i].name, s, 20);
    tft.setTextColor(TFT_CYAN, bg); tft.setTextSize(1);
    tft.setCursor(4, y + 4); tft.print(s);
    tft.setTextColor(0x7BEF, bg);
    tft.setCursor(4, y + 14);
    char sub[24];
    snprintf(sub, 24, "%s %ddBm", bleDevs[i].mac, bleDevs[i].rssi);
    tft.print(sub);
  }
}
#endif

void drawWiFiDetail(int i) {
  state = S_WIFI_DETAIL;
  sel = i;
  WiFiNet& n = wifiNets[i];
  tft.fillScreen(TFT_BLACK);
  header("Network", true);

  int y = HDR_H + 8;
  auto line = [&](const char* k, const char* v) {
    tft.setTextColor(0x7BEF); tft.setTextSize(1);
    tft.setCursor(8, y); tft.print(k);
    tft.setTextColor(TFT_WHITE); tft.setCursor(72, y); tft.print(v);
    y += 16;
  };

  line("SSID:", n.ssid);
  line("BSSID:", n.bssid);
  char tmp[24];
  snprintf(tmp, 24, "%d", n.channel);
  line("Ch:", tmp);
  snprintf(tmp, 24, "%d dBm", n.rssi);
  line("RSSI:", tmp);
  line("Sec:", n.open ? "Open" : "Locked");

  if (n.open) {
    tft.fillRoundRect(8, y + 8, 120, 24, 4, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(18, y + 16);
    tft.print("CONNECT");
  }
}

#if ENABLE_BLE
void drawBleDetail(int i) {
  state = S_BLE_DETAIL;
  sel = i;
  BLEDev& d = bleDevs[i];
  tft.fillScreen(TFT_BLACK);
  header("BLE Device", true);
  int y = HDR_H + 8;
  tft.setTextColor(0x7BEF); tft.setTextSize(1);
  tft.setCursor(8, y); tft.print("Name:"); tft.setTextColor(TFT_CYAN);
  tft.setCursor(72, y); tft.print(d.name); y += 16;
  tft.setTextColor(0x7BEF); tft.setCursor(8, y); tft.print("MAC:");
  tft.setTextColor(TFT_WHITE); tft.setCursor(72, y); tft.print(d.mac); y += 16;
  char tmp[16]; snprintf(tmp, 16, "%d dBm", d.rssi);
  tft.setTextColor(0x7BEF); tft.setCursor(8, y); tft.print("RSSI:");
  tft.setTextColor(TFT_WHITE); tft.setCursor(72, y); tft.print(tmp);
}
#endif

void drawConnected() {
  state = S_CONNECTED;
  tft.fillScreen(TFT_BLACK);
  header("Connected", true);
  int y = HDR_H + 8;
  auto line = [&](const char* k, const char* v) {
    tft.setTextColor(0x7BEF); tft.setTextSize(1);
    tft.setCursor(8, y); tft.print(k);
    tft.setTextColor(TFT_WHITE); tft.setCursor(72, y); tft.print(v);
    y += 16;
  };
  line("SSID:", WiFi.SSID().c_str());
  line("IP:", WiFi.localIP().toString().c_str());
  line("GW:", WiFi.gatewayIP().toString().c_str());
  line("DNS:", WiFi.dnsIP().toString().c_str());
  char tmp[16]; snprintf(tmp, 16, "%d dBm", WiFi.RSSI());
  line("RSSI:", tmp);
  tft.fillRoundRect(8, y + 8, 100, 24, 4, 0x4000);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(16, y + 16);
  tft.print("DISCONNECT");
}

void scanWiFi() {
  wifiCount = 0;
  tft.fillScreen(TFT_BLACK);
  header("WiFi Scan");
  tft.setTextColor(TFT_YELLOW); tft.setTextSize(2);
  tft.setCursor(70, 100); tft.print("Scanning...");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  int n = WiFi.scanNetworks(false, true);
  n = min(n, MAX_WIFI);
  for (int i = 0; i < n; i++) {
    strlcpy(wifiNets[i].ssid, WiFi.SSID(i).length() ? WiFi.SSID(i).c_str() : "<hidden>", 33);
    strlcpy(wifiNets[i].bssid, WiFi.BSSIDstr(i).c_str(), 18);
    wifiNets[i].rssi = WiFi.RSSI(i);
    wifiNets[i].channel = WiFi.channel(i);
    wifiNets[i].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  wifiCount = n;
  WiFi.scanDelete();
  scroll = 0;
  drawWiFiList();
}

#if ENABLE_BLE
void scanBle() {
  bleCount = 0;
  tft.fillScreen(TFT_BLACK);
  header("BLE Scan");
  tft.setTextColor(TFT_YELLOW); tft.setTextSize(2);
  tft.setCursor(70, 100); tft.print("Scanning...");

  if (!bleReady) {
    BLEDevice::init("");
    bleReady = true;
  }
  BLEScan* s = BLEDevice::getScan();
  s->setAdvertisedDeviceCallbacks(&bleCb, false);
  s->setActiveScan(true);
  s->setInterval(100);
  s->setWindow(99);
  s->start(5, false);
  s->clearResults();

  scroll = 0;
  drawBleList();
}
#endif

void connectWiFi(int i) {
  WiFiNet& n = wifiNets[i];
  tft.fillScreen(TFT_BLACK);
  header("Connecting");
  tft.setTextColor(TFT_WHITE); tft.setTextSize(1);
  tft.setCursor(8, 50); tft.print(n.ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(n.ssid);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(200);

  if (WiFi.status() == WL_CONNECTED) drawConnected();
  else {
    WiFi.disconnect(true);
    drawWiFiDetail(i);
  }
}

int listIdx(int ty, int count) {
  if (ty < HDR_H || ty >= H - FTR_H) return -1;
  int idx = scroll + (ty - HDR_H) / IH;
  return idx < count ? idx : -1;
}

void setup() {
  Serial.begin(115200);
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(3);
  tft.init();
  tft.setRotation(3);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  drawHome();
}

void loop() {
  int tx = 0, ty = 0;
  if (!tap(tx, ty)) return;
  wasTouch = false;

  switch (state) {
    case S_HOME:
      if (hit(tx, ty, 8, 50, 144, 64)) scanWiFi();
#if ENABLE_BLE
      if (hit(tx, ty, 168, 50, 144, 64)) scanBle();
#endif
      break;

    case S_WIFI_LIST:
      if (ty < HDR_H) { scroll = 0; drawHome(); break; }
      if (hit(tx, ty, 4, H - FTR_H + 4, 72, FTR_H - 8)) { scanWiFi(); break; }
      if (tx >= LW) {
        int mid = HDR_H + LH / 2;
        if (ty < mid && scroll > 0) scroll--;
        else if (ty >= mid && scroll + PAGE < wifiCount) scroll++;
        drawWiFiList();
        break;
      }
      { int idx = listIdx(ty, wifiCount); if (idx >= 0) drawWiFiDetail(idx); }
      break;

#if ENABLE_BLE
    case S_BLE_LIST:
      if (ty < HDR_H) { scroll = 0; drawHome(); break; }
      if (hit(tx, ty, 4, H - FTR_H + 4, 72, FTR_H - 8)) { scanBle(); break; }
      if (tx >= LW) {
        int mid = HDR_H + LH / 2;
        if (ty < mid && scroll > 0) scroll--;
        else if (ty >= mid && scroll + PAGE < bleCount) scroll++;
        drawBleList();
        break;
      }
      { int idx = listIdx(ty, bleCount); if (idx >= 0) drawBleDetail(idx); }
      break;

    case S_BLE_DETAIL:
      if (ty < HDR_H) drawBleList();
      break;
#endif

    case S_WIFI_DETAIL:
      if (ty < HDR_H) { drawWiFiList(); break; }
      if (sel >= 0 && wifiNets[sel].open && hit(tx, ty, 8, HDR_H + 96, 120, 24))
        connectWiFi(sel);
      break;

    case S_CONNECTED:
      if (ty < HDR_H || hit(tx, ty, 8, HDR_H + 96, 100, 24)) {
        WiFi.disconnect(true);
        delay(100);
        scanWiFi();
      }
      break;
  }
}
