// #define LV_USE_PRIVATE_API 1

#include <lvgl.h>

#include <tt_hal.h>
#include <tt_app.h>
#include <tt_kernel.h>
#include <tt_lvgl.h>
#include <tt_lvgl_toolbar.h>
#include <tt_lock.h>
#include <tt_preferences.h>
#include <tt_time.h>
#include <tt_timer.h>

#include <esp_log.h>
#include "esp_sntp.h"
#include <cmath>
#include <time.h>

constexpr auto *TAG = "ClockApp";

// Helper to get toolbar height based on UI scale
static int getToolbarHeight(UiScale uiScale) {
    if (uiScale == UiScaleSmallest) {
        return 22;
    } else {
        return 40;
    }
}

// Global state variables
static lv_obj_t *toolbar;
static lv_obj_t *clock_container;
static lv_obj_t *time_label; // Digital
static lv_obj_t *clock_face; // Analog
static lv_obj_t *hour_hand;
static lv_obj_t *minute_hand;
static lv_obj_t *second_hand;
static lv_point_precise_t hour_points[2];
static lv_point_precise_t minute_points[2];
static lv_point_precise_t second_points[2];
static lv_timer_t *update_timer = nullptr;
static lv_obj_t *wifi_label;
static lv_obj_t *wifi_button;
static lv_obj_t *toggle_btn;
static lv_obj_t *date_label;
static TimerHandle_t sync_check_timer = nullptr;
static bool last_sync_status;
static bool is_analog;
static AppHandle app_handle;
static LockHandle lvgl_mutex;
static bool needs_redraw = false; // Flag for deferred redraws

struct AppWrapper {
  void *app;
  AppWrapper(void *app) : app(app) {}
};

// Forward declarations
static void update_time_display();
static void check_sync_status();
static void toggle_mode();
static void redraw_clock();

// Static callback functions
static void update_timer_cb(lv_timer_t *timer) { 
  update_time_display(); 
}

static void sync_check_callback(void *context) { 
  check_sync_status(); 
}

static void toggle_mode_cb(lv_event_t *e) { 
  toggle_mode(); 
}

static void wifi_connect_cb(lv_event_t *e) { 
  tt_app_start("WifiManage"); 
}

static void load_mode() {
  PreferencesHandle prefs = tt_preferences_alloc("clock_settings");
  bool temp;
  if (tt_preferences_opt_bool(prefs, "is_analog", &temp)) {
    is_analog = temp;
  } else {
    is_analog = false;
  }
  tt_preferences_free(prefs);
}

static void save_mode() {
  PreferencesHandle prefs = tt_preferences_alloc("clock_settings");
  tt_preferences_put_bool(prefs, "is_analog", is_analog);
  tt_preferences_free(prefs);
}

static void toggle_mode() {
  is_analog = !is_analog;
  save_mode();
  ESP_LOGI("Clock", "Toggling mode to: %s", is_analog ? "analog" : "digital");
  redraw_clock();
}

// Check time sync by verifying year > 1970
static bool is_time_synced() {
  time_t now;
  struct tm timeinfo;
  ::time(&now);
  localtime_r(&now, &timeinfo);
  return (timeinfo.tm_year + 1900) > 1970;
}

// Lightweight sync status check (called from FreeRTOS timer)
static void check_sync_status() {
  bool current_sync_status = is_time_synced();

  // If sync status changed, flag for redraw
  if (current_sync_status != last_sync_status) {
    last_sync_status = current_sync_status;
    needs_redraw = true;
  }
}

// Deferred redraw check (called from LVGL timer)
static void check_and_redraw() {
  if (needs_redraw) {
    needs_redraw = false;
    redraw_clock();
  }
}

// Update time display
static void update_time_display() {
  // First check if we need to redraw due to sync status change
  check_and_redraw();

  time_t now;
  struct tm timeinfo;
  ::time(&now);
  localtime_r(&now, &timeinfo);

  // If not synced, update wifi label
  if (!is_time_synced()) {
    if (wifi_label && lv_obj_is_valid(wifi_label)) {
      lv_label_set_text(wifi_label, "No Wi-Fi - Time not synced");
    }
    return;
  }

  if (is_analog && clock_face && lv_obj_is_valid(clock_face)) {
    lv_coord_t clock_size = lv_obj_get_width(clock_face);
    lv_coord_t center_x = clock_size / 2;
    lv_coord_t center_y = clock_size / 2;

    // Scale hand lengths based on clock size
    lv_coord_t hour_length = clock_size * 0.25;
    lv_coord_t minute_length = clock_size * 0.35;
    lv_coord_t second_length = clock_size * 0.4;

    float hour_angle =
        (timeinfo.tm_hour % 12 + timeinfo.tm_min / 60.0f) * 30.0f - 90;
    float minute_angle = timeinfo.tm_min * 6.0f - 90;
    float second_angle = timeinfo.tm_sec * 6.0f - 90;

    if (hour_hand && lv_obj_is_valid(hour_hand)) {
      hour_points[1].x =
          center_x + (lv_coord_t)(hour_length * cos(hour_angle * M_PI / 180));
      hour_points[1].y =
          center_y + (lv_coord_t)(hour_length * sin(hour_angle * M_PI / 180));
      lv_line_set_points(hour_hand, hour_points, 2);
    }
    if (minute_hand && lv_obj_is_valid(minute_hand)) {
      minute_points[1].x =
          center_x +
          (lv_coord_t)(minute_length * cos(minute_angle * M_PI / 180));
      minute_points[1].y =
          center_y +
          (lv_coord_t)(minute_length * sin(minute_angle * M_PI / 180));
      lv_line_set_points(minute_hand, minute_points, 2);
    }
    if (second_hand && lv_obj_is_valid(second_hand)) {
      second_points[1].x =
          center_x +
          (lv_coord_t)(second_length * cos(second_angle * M_PI / 180));
      second_points[1].y =
          center_y +
          (lv_coord_t)(second_length * sin(second_angle * M_PI / 180));
      lv_line_set_points(second_hand, second_points, 2);
    }
    if (date_label && lv_obj_is_valid(date_label)) {
      char date_str[16];
      strftime(date_str, sizeof(date_str), "%m/%d", &timeinfo);
      lv_label_set_text(date_label, date_str);
    }
  } else if (!is_analog && time_label && lv_obj_is_valid(time_label)) {
    char time_str[16];
    if (tt_timezone_is_format_24_hour()) {
      strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    } else {
      strftime(time_str, sizeof(time_str), "%I:%M:%S %p", &timeinfo);
    }
    lv_label_set_text(time_label, time_str);
  }
}

static void update_toggle_button_visibility() {
  bool should_show = is_time_synced();

  if (toggle_btn) {
    if (should_show) {
      lv_obj_clear_flag(toggle_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(toggle_btn, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static void get_display_metrics(lv_coord_t *width, lv_coord_t *height,
                                bool *is_small) {
  *width = lv_obj_get_width(clock_container);
  *height = lv_obj_get_height(clock_container);
  *is_small = (*width < 240 || *height < 180);
}

static void create_wifi_prompt() {
  lv_coord_t width, height;
  bool is_small;
  get_display_metrics(&width, &height, &is_small);

  // Create a card-style container for the WiFi prompt
  lv_obj_t *card = lv_obj_create(clock_container);
  lv_obj_set_size(card, LV_PCT(90), LV_SIZE_CONTENT);
  lv_obj_set_style_radius(card, is_small ? 8 : 16, 0);
  lv_obj_set_layout(card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x666666), 0);
  lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
  lv_obj_set_style_pad_all(card, is_small ? 12 : 20, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // WiFi icon
  lv_obj_t *icon = lv_label_create(card);
  lv_label_set_text(icon, LV_SYMBOL_WIFI);
  lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_text_font(icon, lv_font_get_default(), 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(0xFF9500), 0);

  // Title
  wifi_label = lv_label_create(card);
  lv_label_set_text(wifi_label, "Time Not Synced");
  lv_obj_align_to(wifi_label, icon, LV_ALIGN_OUT_BOTTOM_MID, 0,
                  is_small ? 8 : 12);
  lv_obj_set_style_text_font(wifi_label, lv_font_get_default(), 0);
  lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_CENTER, 0);

  // Subtitle
  lv_obj_t *subtitle = lv_label_create(card);
  lv_label_set_text(subtitle, "Connect to Wi-Fi to sync time");
  lv_obj_align_to(subtitle, wifi_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
  lv_obj_set_style_text_font(subtitle, lv_font_get_default(), 0);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);

  // Connect button
  wifi_button = lv_btn_create(card);
  lv_obj_set_size(wifi_button, LV_PCT(80), is_small ? 28 : 36);
  lv_obj_align_to(wifi_button, subtitle, LV_ALIGN_OUT_BOTTOM_MID, 0,
                  is_small ? 12 : 16);
  lv_obj_set_style_radius(wifi_button, is_small ? 6 : 8, 0);
  lv_obj_set_style_bg_color(wifi_button, lv_color_hex(0x007BFF), 0);

  lv_obj_t *btn_label = lv_label_create(wifi_button);
  lv_label_set_text(btn_label, "Connect to Wi-Fi");
  lv_obj_center(btn_label);
  lv_obj_set_style_text_font(btn_label, lv_font_get_default(), 0);
  lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);

  lv_obj_add_event_cb(wifi_button, wifi_connect_cb, LV_EVENT_CLICKED,
                      app_handle);
}

static void create_analog_clock() {
  lv_coord_t width, height;
  bool is_small;
  get_display_metrics(&width, &height, &is_small);

  // Calculate optimal clock size
  lv_coord_t max_size = LV_MIN(width * 0.85, height * 0.75);
  lv_coord_t clock_size = LV_MAX(max_size, is_small ? 120 : 200);

  // Create clock face background
  clock_face = lv_obj_create(clock_container);
  lv_obj_set_size(clock_face, clock_size, clock_size);
  lv_obj_center(clock_face);
  lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(clock_face, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(clock_face, LV_OPA_10, 0);
  lv_obj_set_style_border_width(clock_face, is_small ? 2 : 3, 0);
  lv_obj_set_style_border_color(clock_face, lv_palette_main(LV_PALETTE_GREY), 0);
  lv_obj_set_style_border_opa(clock_face, LV_OPA_50, 0);
  lv_obj_set_style_pad_all(clock_face, 0, 0);
  lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE);

  lv_coord_t center_x = clock_size / 2;
  lv_coord_t center_y = clock_size / 2;

  // Add hour markers
  for (int i = 0; i < 12; i++) {
    float angle = i * 30.0f * M_PI / 180.0f;
    lv_coord_t marker_length =
        (i % 3 == 0) ? (clock_size / 8) : (clock_size / 14);
    lv_coord_t marker_width =
        (i % 3 == 0) ? (is_small ? 3 : 4) : (is_small ? 1 : 2);
    lv_coord_t r_outer = clock_size / 2 - 4;
    lv_coord_t r_inner = r_outer - marker_length;
    float x0 = center_x + r_inner * cos(angle - M_PI / 2);
    float y0 = center_y + r_inner * sin(angle - M_PI / 2);
    float x1 = center_x + r_outer * cos(angle - M_PI / 2);
    float y1 = center_y + r_outer * sin(angle - M_PI / 2);
    lv_point_precise_t marker_points[2];
    marker_points[0].x = x0;
    marker_points[0].y = y0;
    marker_points[1].x = x1;
    marker_points[1].y = y1;
    lv_obj_t *marker = lv_line_create(clock_face);
    lv_line_set_points(marker, marker_points, 2);
    lv_obj_set_style_line_width(marker, marker_width, 0);
    lv_obj_set_style_line_color(marker, lv_color_hex(0x999999), 0);
    lv_obj_set_style_line_rounded(marker, true, 0);
  }

  // Calculate hand lengths
  lv_coord_t hour_length = clock_size * 0.25;
  lv_coord_t minute_length = clock_size * 0.35;
  lv_coord_t second_length = clock_size * 0.4;

  // Initialize hour hand pointing up (12 o'clock position)
  hour_points[0].x = center_x;
  hour_points[0].y = center_y;
  hour_points[1].x = center_x;
  hour_points[1].y = center_y - hour_length;
  hour_hand = lv_line_create(clock_face);
  lv_line_set_points(hour_hand, hour_points, 2);
  lv_obj_set_style_line_width(hour_hand, is_small ? 4 : 6, 0);
  lv_obj_set_style_line_color(hour_hand, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_line_opa(hour_hand, LV_OPA_COVER, 0);
  lv_obj_set_style_line_rounded(hour_hand, true, 0);

  // Initialize minute hand pointing up
  minute_points[0].x = center_x;
  minute_points[0].y = center_y;
  minute_points[1].x = center_x;
  minute_points[1].y = center_y - minute_length;
  minute_hand = lv_line_create(clock_face);
  lv_line_set_points(minute_hand, minute_points, 2);
  lv_obj_set_style_line_width(minute_hand, is_small ? 3 : 4, 0);
  lv_obj_set_style_line_color(minute_hand, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_line_opa(minute_hand, LV_OPA_COVER, 0);
  lv_obj_set_style_line_rounded(minute_hand, true, 0);

  // Initialize second hand pointing up
  second_points[0].x = center_x;
  second_points[0].y = center_y;
  second_points[1].x = center_x;
  second_points[1].y = center_y - second_length;
  second_hand = lv_line_create(clock_face);
  lv_line_set_points(second_hand, second_points, 2);
  lv_obj_set_style_line_width(second_hand, 2, 0);
  lv_obj_set_style_line_color(second_hand, lv_color_hex(0xFF0000), 0);
  lv_obj_set_style_line_opa(second_hand, LV_OPA_COVER, 0);
  lv_obj_set_style_line_rounded(second_hand, true, 0);

  // Center dot
  lv_obj_t *center = lv_obj_create(clock_face);
  lv_obj_set_size(center, is_small ? 8 : 12, is_small ? 8 : 12);
  lv_obj_center(center);
  lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(center, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(center, 0, 0);

  // Date label
  date_label = lv_label_create(clock_face);
  lv_obj_align(date_label, LV_ALIGN_BOTTOM_MID, 0, -15);
  lv_obj_set_style_text_font(date_label, lv_font_get_default(), 0);
  lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);

  // Now update hands to actual time
  update_time_display();
}

static void create_digital_clock() {
  lv_coord_t width, height;
  bool is_small;
  get_display_metrics(&width, &height, &is_small);

  // Create main time display
  time_label = lv_label_create(clock_container);
  lv_obj_align(time_label, LV_ALIGN_CENTER, 0, is_small ? -25 : -35);
  lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);

  const lv_font_t *time_font = lv_font_get_default();
  lv_obj_set_style_text_font(time_label, time_font, 0);

  lv_obj_set_style_text_color(time_label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_color(time_label, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(time_label, LV_OPA_30, 0);
  lv_obj_set_style_radius(time_label, is_small ? 12 : 16, 0);
  lv_obj_set_style_pad_all(time_label, is_small ? 20 : 28, 0);
  lv_obj_set_style_border_width(time_label, 2, 0);
  lv_obj_set_style_border_color(time_label, lv_color_hex(0x444444), 0);
  lv_obj_set_style_border_opa(time_label, LV_OPA_50, 0);

  // Create date display
  date_label = lv_label_create(clock_container);
  lv_obj_align_to(date_label, time_label, LV_ALIGN_OUT_BOTTOM_MID, 0,
                  is_small ? 12 : 16);
  lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(date_label, lv_font_get_default(), 0);
  lv_obj_set_style_text_color(date_label, lv_color_hex(0xaaaaaa), 0);
  lv_obj_set_style_pad_all(date_label, is_small ? 8 : 10, 0);

  // Update date
  time_t now;
  struct tm timeinfo;
  ::time(&now);
  localtime_r(&now, &timeinfo);

  char date_str[64];
  if (is_small) {
    strftime(date_str, sizeof(date_str), "%m/%d/%Y", &timeinfo);
  } else {
    strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", &timeinfo);
  }
  lv_label_set_text(date_label, date_str);

  update_time_display();
}

static void redraw_clock() {
  // Clear the clock container
  lv_obj_clean(clock_container);
  time_label = nullptr;
  clock_face = hour_hand = minute_hand = second_hand = nullptr;
  wifi_label = nullptr;
  wifi_button = nullptr;
  date_label = nullptr;

  // Update toggle button visibility
  update_toggle_button_visibility();

  if (!is_time_synced()) {
    create_wifi_prompt();
  } else if (is_analog) {
    create_analog_clock();
  } else {
    create_digital_clock();
  }

  // Force invalidation
  lv_obj_invalidate(clock_container);
}

// C callback functions
extern "C" void onShow(void *app, void *data, lv_obj_t *parent) {
  app_handle = app;

  // Create toolbar
  toolbar = tt_lvgl_toolbar_create_for_app(parent, app_handle);
  lv_obj_align(toolbar, LV_ALIGN_TOP_LEFT, 0, 0);

  // Create toggle button
  toggle_btn = lv_btn_create(toolbar);
  lv_obj_set_height(toggle_btn, LV_PCT(80));
  lv_obj_set_style_radius(toggle_btn, 6, 0);
  lv_obj_set_style_bg_color(toggle_btn, lv_color_hex(0x007BFF), 0);
  lv_obj_set_style_bg_opa(toggle_btn, LV_OPA_80, 0);

  lv_obj_t* toggle_label = lv_label_create(toggle_btn);
  lv_label_set_text(toggle_label, LV_SYMBOL_REFRESH " Mode");
  lv_obj_center(toggle_label);

  lv_obj_align(toggle_btn, LV_ALIGN_RIGHT_MID, -8, 0);
  lv_obj_add_event_cb(toggle_btn, toggle_mode_cb, LV_EVENT_CLICKED, app_handle);

  // Load settings
  load_mode();
  last_sync_status = is_time_synced();
  needs_redraw = false;

  // Initialize LVGL mutex
  lvgl_mutex = tt_lock_alloc_mutex(MutexTypeRecursive);
  
  // Get UI scale and calculate layout
  UiScale uiScale = tt_hal_configuration_get_ui_scale();
  int toolbar_height = getToolbarHeight(uiScale);
  
  // Create clock container
  clock_container = lv_obj_create(parent);
  
  lv_coord_t parent_width = lv_obj_get_width(parent);
  lv_coord_t parent_height = lv_obj_get_height(parent);
  lv_coord_t container_height = parent_height - toolbar_height;
  
  lv_obj_set_size(clock_container, parent_width, container_height);
  lv_obj_align_to(clock_container, toolbar, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_border_width(clock_container, 0, 0);
  lv_obj_set_style_pad_all(clock_container, 10, 0);
  lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_set_layout(clock_container, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(clock_container, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(clock_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  redraw_clock();

  // Start LVGL timer for UI updates (runs in LVGL context)
  update_timer = lv_timer_create(update_timer_cb, 1000, nullptr);
  
  // Start FreeRTOS timer for sync checking (lightweight, runs every 5 seconds)
  sync_check_timer = (TimerHandle_t)tt_timer_alloc(TimerTypePeriodic, sync_check_callback, nullptr);
  tt_timer_start(sync_check_timer, 5000);
  
  ESP_LOGI("Clock", "Timers started in onShow");
}

extern "C" void onHide(void *app, void *data) {
  // Stop timers first
  if (update_timer) {
    lv_timer_del(update_timer);
    update_timer = nullptr;
  }
  
  if (sync_check_timer) {
    tt_timer_stop(sync_check_timer);
    tt_timer_free(sync_check_timer);
    sync_check_timer = nullptr;
    ESP_LOGI("Clock", "Timers stopped in onHide");
  }

  // Clean up mutex
  if (lvgl_mutex) {
    tt_lock_free(lvgl_mutex);
    lvgl_mutex = nullptr;
  }

  // Clear object pointers
  time_label = nullptr;
  clock_face = hour_hand = minute_hand = second_hand = nullptr;
  wifi_label = nullptr;
  wifi_button = nullptr;
  toggle_btn = nullptr;
  clock_container = nullptr;
  toolbar = nullptr;
  date_label = nullptr;
}

AppRegistration manifest = {
    .createData = nullptr,
    .destroyData = nullptr,
    .onCreate = nullptr,
    .onDestroy = nullptr,
    .onShow = onShow,
    .onHide = onHide,
    .onResult = nullptr,
};

extern "C" void app_main(void) { tt_app_register(manifest); }
