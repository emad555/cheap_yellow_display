/***************************************************
 * 2-City Weather Station + XPT2046 Touch
 * Adapted from Random Nerd Tutorials examples
 ***************************************************/

#include <lvgl.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <XPT2046_Touchscreen.h>    // by PaulStoffregen
#include "weather_images.h"         // Must contain your weather icons
#include <WiFiManager.h>

/***** 1) NETWORK CREDENTIALS *****/
const char* ssid     = "emad";
const char* password = "04112023";

/***** 2) DISPLAY/TFT CONFIG *****/
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))
static uint32_t draw_buf[DRAW_BUF_SIZE / 4];

/***** 3) TOUCHSCREEN PINS (XPT2046) *****/
// Adjust these to match your hardware
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// Create the SPI instance for the touchscreen
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

/***** 4) WEATHER-RELATED SETTINGS *****/
// 0 for Fahrenheit, 1 for Celsius
#define TEMP_CELSIUS 1

#if TEMP_CELSIUS
  String temperature_unit = "";
  const char degree_symbol[] = "\u00B0C";
#else
  String temperature_unit = "&temperature_unit=fahrenheit";
  const char degree_symbol[] = "\u00B0F";
#endif

// We'll create a struct for each city's data
typedef struct {
  String cityName;
  String latitude;
  String longitude;
  String timeZone;

  // Fetched weather data
  String current_date;
  String last_weather_update;
  String temperature;
  String humidity;
  int    is_day;
  int    weather_code;
  String weather_description;

  // LVGL objects
  lv_obj_t* img_weather_icon;
  lv_obj_t* label_date;
  lv_obj_t* label_temperature;
  lv_obj_t* label_humidity;
  lv_obj_t* label_description;
  lv_obj_t* label_time_location;
} CityData;

// Create two CityData instances for Dubai & Abu Dhabi
CityData cityDubai = {
  "Dubai",
  "25.2048",     // approximate lat
  "55.2708",     // approximate lon
  "Asia/Dubai"
};

CityData cityAbuDhabi = {
  "Abu Dhabi",
  "24.4539", 
  "54.3773",
  "Asia/Dubai"
};

CityData cityAmman = {
  "Amman",
  "31.9539",     // Latitude
  "35.9106",     // Longitude
  "Asia/Amman"
};

// Create CityData instances for Muscat, Doha, and Riyadh
CityData cityMuscat = {
  "Muscat",
  "23.5859",  // Latitude
  "58.4059",  // Longitude
  "Asia/Muscat"
};

CityData cityDoha = {
  "Doha",
  "25.276987", 
  "51.520008",
  "Asia/Qatar"
};

CityData cityRiyadh = {
  "Riyadh",
  "24.7136", 
  "46.6753",
  "Asia/Riyadh"
};




// Forward declarations
void get_weather_data(CityData& city);
void get_weather_description(CityData& city);
static void timer_cb(lv_timer_t * timer);
void create_weather_tab(lv_obj_t * parent, CityData& city);

/***************************************************
 * 5) TOUCH INPUT READING FOR LVGL
 ***************************************************/
static void touchscreen_read(lv_indev_t * indev, lv_indev_data_t * data) {
  // If the touchscreen reports being touched...
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();
    
    // Adjust these map() ranges as needed:
    // - "p.x" & "p.y" min/max depend on your raw calibration.
    // - The final range must match your screen dimensions (240x320).
    // You might need to invert them depending on orientation.
    int x = map(p.x, 200, 3700, 0, SCREEN_WIDTH);
    int y = map(p.y, 240, 3800, 0, SCREEN_HEIGHT);

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;

    // For debugging:
    // Serial.printf("Touch: X=%d, Y=%d, Z=%d\n", x, y, p.z);
  } 
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

/***************************************************
 * 6) WEATHER DESCRIPTION
 ***************************************************/
void get_weather_description(CityData& city) {
  int code = city.weather_code;
  switch (code) {
    case 0:
      if(city.is_day==1) { lv_img_set_src(city.img_weather_icon, &image_weather_sun); }
      else { lv_img_set_src(city.img_weather_icon, &image_weather_night); }
      city.weather_description = "CLEAR SKY";
      break;
    case 1:
      if(city.is_day==1) { lv_img_set_src(city.img_weather_icon, &image_weather_sun); }
      else { lv_img_set_src(city.img_weather_icon, &image_weather_night); }
      city.weather_description = "MAINLY CLEAR";
      break;
    case 2:
      lv_img_set_src(city.img_weather_icon, &image_weather_cloud);
      city.weather_description = "PARTLY CLOUDY";
      break;
    case 3:
      lv_img_set_src(city.img_weather_icon, &image_weather_cloud);
      city.weather_description = "OVERCAST";
      break;
    case 45:
      lv_img_set_src(city.img_weather_icon, &image_weather_cloud);
      city.weather_description = "FOG";
      break;
    case 48:
      lv_img_set_src(city.img_weather_icon, &image_weather_cloud);
      city.weather_description = "DEPOSITING RIME FOG";
      break;
    case 51:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "DRIZZLE LIGHT";
      break;
    case 53:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "DRIZZLE MODERATE";
      break;
    case 55:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "DRIZZLE DENSE";
      break;
    case 56:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "FREEZING DRIZZLE LIGHT";
      break;
    case 57:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "FREEZING DRIZZLE DENSE";
      break;
    case 61:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "RAIN SLIGHT";
      break;
    case 63:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "RAIN MODERATE";
      break;
    case 65:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "RAIN HEAVY";
      break;
    case 66:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "FREEZING RAIN LIGHT";
      break;
    case 67:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "FREEZING RAIN HEAVY";
      break;
    case 71:
      lv_img_set_src(city.img_weather_icon, &image_weather_snow);
      city.weather_description = "SNOW SLIGHT";
      break;
    case 73:
      lv_img_set_src(city.img_weather_icon, &image_weather_snow);
      city.weather_description = "SNOW MODERATE";
      break;
    case 75:
      lv_img_set_src(city.img_weather_icon, &image_weather_snow);
      city.weather_description = "SNOW HEAVY";
      break;
    case 77:
      lv_img_set_src(city.img_weather_icon, &image_weather_snow);
      city.weather_description = "SNOW GRAINS";
      break;
    case 80:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "RAIN SHOWERS SLIGHT";
      break;
    case 81:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "RAIN SHOWERS MODERATE";
      break;
    case 82:
      lv_img_set_src(city.img_weather_icon, &image_weather_rain);
      city.weather_description = "RAIN SHOWERS VIOLENT";
      break;
    case 85:
      lv_img_set_src(city.img_weather_icon, &image_weather_snow);
      city.weather_description = "SNOW SHOWERS SLIGHT";
      break;
    case 86:
      lv_img_set_src(city.img_weather_icon, &image_weather_snow);
      city.weather_description = "SNOW SHOWERS HEAVY";
      break;
    case 95:
      lv_img_set_src(city.img_weather_icon, &image_weather_thunder);
      city.weather_description = "THUNDERSTORM";
      break;
    case 96:
      lv_img_set_src(city.img_weather_icon, &image_weather_thunder);
      city.weather_description = "THUNDERSTORM SLIGHT HAIL";
      break;
    case 99:
      lv_img_set_src(city.img_weather_icon, &image_weather_thunder);
      city.weather_description = "THUNDERSTORM HEAVY HAIL";
      break;
    default:
      city.weather_description = "UNKNOWN WEATHER CODE";
      break;
  }
}

/***************************************************
 * 7) FETCH WEATHER DATA FROM OPEN-METEO
 ***************************************************/
void get_weather_data(CityData& city) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Example API call for open-meteo
    String url = 
      "http://api.open-meteo.com/v1/forecast?latitude=" + city.latitude +
      "&longitude=" + city.longitude +
      "&current=temperature_2m,relative_humidity_2m,is_day,weather_code" +
      temperature_unit + 
      "&timezone=" + city.timeZone + 
      "&forecast_days=1";

    http.begin(url);
    int httpCode = http.GET(); // Make the GET request

    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      // Parse JSON
      DynamicJsonDocument doc(1536);
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        const char* datetime = doc["current"]["time"];
        city.temperature = String(doc["current"]["temperature_2m"].as<float>(), 1);
        city.humidity    = String(doc["current"]["relative_humidity_2m"].as<int>());
        city.is_day      = doc["current"]["is_day"].as<int>();
        city.weather_code= doc["current"]["weather_code"].as<int>();

        String datetime_str = String(datetime);
        int splitIndex = datetime_str.indexOf('T');
        city.current_date        = datetime_str.substring(0, splitIndex);
        city.last_weather_update = datetime_str.substring(splitIndex + 1, splitIndex + 9); // HH:MM:SS
      } 
      else {
        Serial.println("JSON parse fail: " + String(error.c_str()));
      }
    }
    else {
      Serial.println("GET request failed with code " + String(httpCode));
    }
    http.end();
  } else {
    Serial.println("Not connected to Wi-Fi");
  }
}

/***************************************************
 * 8) CREATE WEATHER TAB
 ***************************************************/
void create_weather_tab(lv_obj_t * parent, CityData& city) {
  // Weather icon
  city.img_weather_icon = lv_img_create(parent);
  lv_obj_align(city.img_weather_icon, LV_ALIGN_CENTER, -80, -20);

  // Date label
  city.label_date = lv_label_create(parent);
  lv_label_set_text(city.label_date, "----");
  lv_obj_align(city.label_date, LV_ALIGN_CENTER, 70, -70);
  lv_obj_set_style_text_font(city.label_date, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(city.label_date, lv_palette_main(LV_PALETTE_TEAL), 0);

  // Temperature icon & label
  lv_obj_t * icon_temp = lv_img_create(parent);
  lv_img_set_src(icon_temp, &image_weather_temperature);
  lv_obj_align(icon_temp, LV_ALIGN_CENTER, 30, -25);

  city.label_temperature = lv_label_create(parent);
  lv_label_set_text(city.label_temperature, "--");
  lv_obj_align(city.label_temperature, LV_ALIGN_CENTER, 70, -25);
  lv_obj_set_style_text_font(city.label_temperature, &lv_font_montserrat_18, 0);

  // Humidity icon & label
  lv_obj_t * icon_hum = lv_img_create(parent);
  lv_img_set_src(icon_hum, &image_weather_humidity);
  lv_obj_align(icon_hum, LV_ALIGN_CENTER, 30, 10);

  city.label_humidity = lv_label_create(parent);
  lv_label_set_text(city.label_humidity, "--");
  lv_obj_align(city.label_humidity, LV_ALIGN_CENTER, 70, 10);
  lv_obj_set_style_text_font(city.label_humidity, &lv_font_montserrat_18, 0);

  // Description label
  city.label_description = lv_label_create(parent);
  lv_label_set_text(city.label_description, "----");
  lv_obj_align(city.label_description, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_text_font(city.label_description, &lv_font_montserrat_18, 0);

  // Time + location label
  city.label_time_location = lv_label_create(parent);
  lv_label_set_text(city.label_time_location, "--");
  lv_obj_align(city.label_time_location, LV_ALIGN_BOTTOM_MID, 0, -3);
  lv_obj_set_style_text_font(city.label_time_location, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(city.label_time_location, lv_palette_main(LV_PALETTE_GREY), 0);

  // Fetch initial data
  get_weather_data(city);
  get_weather_description(city);

  // Update UI
  lv_label_set_text(city.label_date, city.current_date.c_str());
  lv_label_set_text(city.label_temperature, String("      " + city.temperature + degree_symbol).c_str());
  lv_label_set_text(city.label_humidity, String("   " + city.humidity + "%").c_str());
  lv_label_set_text(city.label_description, city.weather_description.c_str());
  lv_label_set_text(city.label_time_location,
    String("Last Update: " + city.last_weather_update + " | " + city.cityName).c_str());
}

/***************************************************
 * 9) CREATE THE TABVIEW (OLD-STYLE SINGLE PARAM)
 ***************************************************/
lv_obj_t* create_tabview(lv_obj_t* parent) {
  lv_obj_t * tv = lv_tabview_create(parent);

  lv_obj_t* tab_btns = lv_tabview_get_tab_btns(tv);
  lv_obj_set_height(tab_btns, 28);
  lv_obj_set_style_pad_ver(tab_btns, 2, 0);
  lv_obj_set_style_pad_hor(tab_btns, 2, 0);
  lv_obj_set_style_pad_gap(tab_btns, 1, 0);

  // Dubai Tab
  lv_obj_t* tab_dubai = lv_tabview_add_tab(tv, "Dubai");
  create_weather_tab(tab_dubai, cityDubai);

  // Abu Dhabi Tab
  lv_obj_t* tab_abu = lv_tabview_add_tab(tv, "Abu Dhabi");
  create_weather_tab(tab_abu, cityAbuDhabi);

  // Muscat Tab
  lv_obj_t* tab_muscat = lv_tabview_add_tab(tv, "Muscat");
  create_weather_tab(tab_muscat, cityMuscat);

  // Doha Tab
  lv_obj_t* tab_doha = lv_tabview_add_tab(tv, "Doha");
  create_weather_tab(tab_doha, cityDoha);

  // Riyadh Tab
  lv_obj_t* tab_riyadh = lv_tabview_add_tab(tv, "Riyadh");
  create_weather_tab(tab_riyadh, cityRiyadh);

  // Amman Tab
  lv_obj_t* tab_amman = lv_tabview_add_tab(tv, "Amman");
  create_weather_tab(tab_amman, cityAmman);

  return tv;
}




/***************************************************
 * 10) TIMER TO PERIODICALLY UPDATE
 ***************************************************/
static void timer_cb(lv_timer_t * timer) {
  LV_UNUSED(timer);

  // Dubai
  get_weather_data(cityDubai);
  get_weather_description(cityDubai);
  lv_label_set_text(cityDubai.label_date, cityDubai.current_date.c_str());
  lv_label_set_text(cityDubai.label_temperature, String("      " + cityDubai.temperature + degree_symbol).c_str());
  lv_label_set_text(cityDubai.label_humidity, String("   " + cityDubai.humidity + "%").c_str());
  lv_label_set_text(cityDubai.label_description, cityDubai.weather_description.c_str());
  lv_label_set_text(cityDubai.label_time_location, String("Last Update: " + cityDubai.last_weather_update + " | " + cityDubai.cityName).c_str());

  // Abu Dhabi
  get_weather_data(cityAbuDhabi);
  get_weather_description(cityAbuDhabi);
  lv_label_set_text(cityAbuDhabi.label_date, cityAbuDhabi.current_date.c_str());
  lv_label_set_text(cityAbuDhabi.label_temperature, String("      " + cityAbuDhabi.temperature + degree_symbol).c_str());
  lv_label_set_text(cityAbuDhabi.label_humidity, String("   " + cityAbuDhabi.humidity + "%").c_str());
  lv_label_set_text(cityAbuDhabi.label_description, cityAbuDhabi.weather_description.c_str());
  lv_label_set_text(cityAbuDhabi.label_time_location, String("Last Update: " + cityAbuDhabi.last_weather_update + " | " + cityAbuDhabi.cityName).c_str());

  // Muscat
  get_weather_data(cityMuscat);
  get_weather_description(cityMuscat);
  lv_label_set_text(cityMuscat.label_date, cityMuscat.current_date.c_str());
  lv_label_set_text(cityMuscat.label_temperature, String("      " + cityMuscat.temperature + degree_symbol).c_str());
  lv_label_set_text(cityMuscat.label_humidity, String("   " + cityMuscat.humidity + "%").c_str());
  lv_label_set_text(cityMuscat.label_description, cityMuscat.weather_description.c_str());
  lv_label_set_text(cityMuscat.label_time_location, String("Last Update: " + cityMuscat.last_weather_update + " | " + cityMuscat.cityName).c_str());

  // Doha
  get_weather_data(cityDoha);
  get_weather_description(cityDoha);
  lv_label_set_text(cityDoha.label_date, cityDoha.current_date.c_str());
  lv_label_set_text(cityDoha.label_temperature, String("      " + cityDoha.temperature + degree_symbol).c_str());
  lv_label_set_text(cityDoha.label_humidity, String("   " + cityDoha.humidity + "%").c_str());
  lv_label_set_text(cityDoha.label_description, cityDoha.weather_description.c_str());
  lv_label_set_text(cityDoha.label_time_location, String("Last Update: " + cityDoha.last_weather_update + " | " + cityDoha.cityName).c_str());

  // Riyadh
  get_weather_data(cityRiyadh);
  get_weather_description(cityRiyadh);
  lv_label_set_text(cityRiyadh.label_date, cityRiyadh.current_date.c_str());
  lv_label_set_text(cityRiyadh.label_temperature, String("      " + cityRiyadh.temperature + degree_symbol).c_str());
  lv_label_set_text(cityRiyadh.label_humidity, String("   " + cityRiyadh.humidity + "%").c_str());
  lv_label_set_text(cityRiyadh.label_description, cityRiyadh.weather_description.c_str());
  lv_label_set_text(cityRiyadh.label_time_location, String("Last Update: " + cityRiyadh.last_weather_update + " | " + cityRiyadh.cityName).c_str());

  // Amman
  get_weather_data(cityAmman);
  get_weather_description(cityAmman);
  lv_label_set_text(cityAmman.label_date, cityAmman.current_date.c_str());
  lv_label_set_text(cityAmman.label_temperature, String("      " + cityAmman.temperature + degree_symbol).c_str());
  lv_label_set_text(cityAmman.label_humidity, String("   " + cityAmman.humidity + "%").c_str());
  lv_label_set_text(cityAmman.label_description, cityAmman.weather_description.c_str());
  lv_label_set_text(cityAmman.label_time_location, String("Last Update: " + cityAmman.last_weather_update + " | " + cityAmman.cityName).c_str());
}


/***************************************************
 * 11) SETUP
 ***************************************************/
void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFiManager wm;

  // AutoConnect will launch a portal if no Wi-Fi is saved
  if (!wm.autoConnect("ESP32-Weather", "12345678")) { 
    Serial.println("Failed to connect to Wi-Fi, restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("Connected to Wi-Fi!");
  Serial.println(WiFi.localIP()); // Print ESP32 IP address

  // Initialize LVGL
  lv_init();

  // Initialize the XPT2046 touchscreen SPI
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(2);

  // Create the display in LVGL
  lv_display_t * disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_920);

  // Register the touch input device in LVGL
  lv_indev_t * indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);

  // Create the tabview for weather
  create_tabview(lv_scr_act());

  // Start a timer to update weather every 10 minutes
  lv_timer_t* timer = lv_timer_create(timer_cb, 600000, NULL);
  lv_timer_ready(timer);
}

/***************************************************
 * 12) LOOP
 ***************************************************/
void loop() {
  lv_task_handler();  // Let LVGL process
  lv_tick_inc(5);     // 5 ms
  delay(5);
}
