#define LV_USE_PRIVATE_API 1

#include <Tactility/app/App.h>
#include <Tactility/app/AppManifest.h>
#include <Tactility/app/AppContext.h>
#include <Tactility/settings/Time.h>
#include <Tactility/Preferences.h>
#include <lvgl.h>
#include <ctime>
#include <cmath>
#include <chrono>

#ifdef ESP_PLATFORM
#include "Tactility/Timer.h"
#include "Tactility/lvgl/LvglSync.h"
#include "esp_sntp.h"
#endif

#include <Tactility/lvgl/Toolbar.h>

using namespace tt::app;

namespace tt::app::clock {

class ClockApp : public App {
private:
    struct AppWrapper {
        ClockApp* app;
        AppWrapper(ClockApp* app) : app(app) {}
    };

    lv_obj_t* toolbar;
    lv_obj_t* clock_container;
    lv_obj_t* time_label; // Digital
    lv_obj_t* clock_face; // Analog
    lv_obj_t* hour_hand;
    lv_obj_t* minute_hand;
    lv_obj_t* second_hand;
    lv_point_precise_t hour_points[2];
    lv_point_precise_t minute_points[2];
    lv_point_precise_t second_points[2];
    lv_timer_t* update_timer = nullptr;
    lv_obj_t* wifi_label;
    lv_obj_t* wifi_button;
    lv_obj_t* toggle_btn;
    lv_obj_t* date_label;
#ifdef ESP_PLATFORM
    std::unique_ptr<Timer> timer; // Only declare timer for ESP
    bool last_sync_status; // Track previous sync status to detect changes
#endif
    bool is_analog;
    AppContext* context;
    // No need for current angle state with lv_line hands

    static void update_timer_cb(lv_timer_t* timer) {
        ClockApp* app = static_cast<ClockApp*>(lv_timer_get_user_data(timer));
        app->update_time_display();
    }

#ifdef ESP_PLATFORM
    static void timer_callback(std::shared_ptr<void> appWrapper) {
        auto* app = std::static_pointer_cast<AppWrapper>(appWrapper)->app;
        app->update_time_and_check_sync();
    }
#endif

    static void toggle_mode_cb(lv_event_t* e) {
        ClockApp* app = static_cast<ClockApp*>(lv_event_get_user_data(e));
        app->toggle_mode();
    }

    static void wifi_connect_cb(lv_event_t* e) {
        tt::app::start("WifiManage");
    }

    void load_mode() {
        tt::Preferences prefs("clock_settings");
        is_analog = false;
        prefs.optBool("is_analog", is_analog);
    }

    void save_mode() {
        tt::Preferences prefs("clock_settings");
        prefs.putBool("is_analog", is_analog);
    }

    void toggle_mode() {
        is_analog = !is_analog;
        save_mode();
        TT_LOG_I("Clock", "Toggling mode to: %s", is_analog ? "analog" : "digital");
        redraw_clock();
        // Force immediate screen update
        lv_refr_now(NULL);
    }

#ifdef ESP_PLATFORM
    bool is_time_synced() {
        return sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    }

    void update_time_and_check_sync() {
        // Use FreeRTOS ticks instead of chrono (50ms = 50 ticks at 1000Hz)
        auto lock = lvgl::getSyncLock()->asScopedLock();
        if (!lock.lock(pdMS_TO_TICKS(50))) {
            TT_LOG_W("Clock", "LVGL lock timeout in update_time_and_check_sync - skipping update");
            return; // Skip this update cycle rather than hang
        }

        bool current_sync_status = is_time_synced();
        
        // If sync status changed, redraw the entire clock
        if (current_sync_status != last_sync_status) {
            last_sync_status = current_sync_status;
            redraw_clock();
            return;
        }

        // If not synced, just update the wifi label
        if (!current_sync_status) {
            if (wifi_label && lv_obj_is_valid(wifi_label)) {
                lv_label_set_text(wifi_label, "No Wi-Fi - Time not synced");
            }
            return;
        }

        // Update the actual time display
        update_time_display();
    }
#else
    bool is_time_synced() {
        return true; // Simulator assumes synced
    }

    void update_time_and_check_sync() {
        // No-op for simulator; static message handled in redraw_clock
    }
#endif

    void update_time_display() {
        time_t now;
        struct tm timeinfo;
        ::time(&now);
        localtime_r(&now, &timeinfo);

        if (is_analog && clock_face && lv_obj_is_valid(clock_face)) {
            lv_coord_t clock_size = lv_obj_get_width(clock_face);
            lv_coord_t center_x = clock_size / 2;
            lv_coord_t center_y = clock_size / 2;
            
            // Scale hand lengths based on clock size
            lv_coord_t hour_length = clock_size * 0.25;
            lv_coord_t minute_length = clock_size * 0.35;
            lv_coord_t second_length = clock_size * 0.4;

            float hour_angle = (timeinfo.tm_hour % 12 + timeinfo.tm_min / 60.0f) * 30.0f - 90;
            float minute_angle = timeinfo.tm_min * 6.0f - 90;
            float second_angle = timeinfo.tm_sec * 6.0f - 90;

            if (hour_hand && lv_obj_is_valid(hour_hand)) {
                this->hour_points[0].x = center_x; 
                this->hour_points[0].y = center_y;
                this->hour_points[1].x = center_x + (lv_coord_t)(hour_length * cos(hour_angle * M_PI / 180));
                this->hour_points[1].y = center_y + (lv_coord_t)(hour_length * sin(hour_angle * M_PI / 180));
                // Re-set the points to trigger LVGL's internal update
                lv_line_set_points(hour_hand, this->hour_points, 2);
            }
            if (minute_hand && lv_obj_is_valid(minute_hand)) {
                this->minute_points[0].x = center_x; 
                this->minute_points[0].y = center_y;
                this->minute_points[1].x = center_x + (lv_coord_t)(minute_length * cos(minute_angle * M_PI / 180));
                this->minute_points[1].y = center_y + (lv_coord_t)(minute_length * sin(minute_angle * M_PI / 180));
                // Re-set the points to trigger LVGL's internal update
                lv_line_set_points(minute_hand, this->minute_points, 2);
            }
            if (second_hand && lv_obj_is_valid(second_hand)) {
                this->second_points[0].x = center_x; 
                this->second_points[0].y = center_y;
                this->second_points[1].x = center_x + (lv_coord_t)(second_length * cos(second_angle * M_PI / 180));
                this->second_points[1].y = center_y + (lv_coord_t)(second_length * sin(second_angle * M_PI / 180));
                // Re-set the points to trigger LVGL's internal update
                lv_line_set_points(second_hand, this->second_points, 2);
            }
            if (date_label && lv_obj_is_valid(date_label)) {
                char date_str[16];
                strftime(date_str, sizeof(date_str), "%m/%d", &timeinfo);
                lv_label_set_text(date_label, date_str);
            }
        } else if (!is_analog && time_label && lv_obj_is_valid(time_label)) {
            char time_str[16];
            if (tt::settings::isTimeFormat24Hour()) {
                strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
            } else {
                strftime(time_str, sizeof(time_str), "%I:%M:%S %p", &timeinfo);
            }
            lv_label_set_text(time_label, time_str);
        }
    }

    void update_toggle_button_visibility() {
#ifdef ESP_PLATFORM
        bool should_show = is_time_synced();
#else
        bool should_show = true; // Always show in simulator
#endif
        
        if (toggle_btn) {
            if (should_show) {
                lv_obj_clear_flag(toggle_btn, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(toggle_btn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // (Removed: animate_hand, not needed for lv_line hands)

    void get_display_metrics(lv_coord_t* width, lv_coord_t* height, bool* is_small) {
        *width = lv_obj_get_width(clock_container);
        *height = lv_obj_get_height(clock_container);
        *is_small = (*width < 240 || *height < 180); // CardKB is around 240x135
    }

    void create_wifi_prompt() {
        lv_coord_t width, height;
        bool is_small;
        get_display_metrics(&width, &height, &is_small);

        // Create a card-style container for the WiFi prompt
        lv_obj_t* card = lv_obj_create(clock_container);
        lv_obj_set_size(card, LV_PCT(90), LV_SIZE_CONTENT);
        lv_obj_center(card);
        lv_obj_set_style_radius(card, is_small ? 8 : 16, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_10, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0x666666), 0);
        lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
        lv_obj_set_style_pad_all(card, is_small ? 12 : 20, 0);

        // WiFi icon (using symbols)
        lv_obj_t* icon = lv_label_create(card);
        lv_label_set_text(icon, LV_SYMBOL_WIFI);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_18, 0); // Use 18 instead of 20/32
        lv_obj_set_style_text_color(icon, lv_color_hex(0xFF9500), 0); // Orange color

        // Title
        wifi_label = lv_label_create(card);
        lv_label_set_text(wifi_label, "Time Not Synced");
        lv_obj_align_to(wifi_label, icon, LV_ALIGN_OUT_BOTTOM_MID, 0, is_small ? 8 : 12);
        lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_CENTER, 0);

        // Subtitle
        lv_obj_t* subtitle = lv_label_create(card);
        lv_label_set_text(subtitle, "Connect to Wi-Fi to sync time");
        lv_obj_align_to(subtitle, wifi_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 4);
        lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);

        // Connect button
        wifi_button = lv_btn_create(card);
        lv_obj_set_size(wifi_button, LV_PCT(80), is_small ? 28 : 36);
        lv_obj_align_to(wifi_button, subtitle, LV_ALIGN_OUT_BOTTOM_MID, 0, is_small ? 12 : 16);
        lv_obj_set_style_radius(wifi_button, is_small ? 6 : 8, 0);
        lv_obj_set_style_bg_color(wifi_button, lv_color_hex(0x007BFF), 0); // Blue color

        lv_obj_t* btn_label = lv_label_create(wifi_button);
        lv_label_set_text(btn_label, "Connect to Wi-Fi");
        lv_obj_center(btn_label);
        lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);

        lv_obj_add_event_cb(wifi_button, wifi_connect_cb, LV_EVENT_CLICKED, context);
    }

    void create_analog_clock() {
        lv_coord_t width, height;
        bool is_small;
        get_display_metrics(&width, &height, &is_small);

        // Calculate optimal clock size (leave margins)
        lv_coord_t max_size = LV_MIN(width * 0.85, height * 0.75);
        lv_coord_t clock_size = LV_MAX(max_size, is_small ? 120 : 200);

        // Create clock face background
        clock_face = lv_obj_create(clock_container);
        lv_obj_set_size(clock_face, clock_size, clock_size);
        lv_obj_center(clock_face);
        lv_obj_set_style_radius(clock_face, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(clock_face, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(clock_face, LV_OPA_10, 0);
        lv_obj_set_style_border_width(clock_face, is_small ? 2 : 3, 0);
        lv_obj_set_style_border_color(clock_face, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_set_style_border_opa(clock_face, LV_OPA_50, 0);
        lv_obj_set_style_pad_all(clock_face, 0, 0); // Remove padding for accurate centering
        lv_obj_clear_flag(clock_face, LV_OBJ_FLAG_SCROLLABLE); // Prevent scrolling

        // Add hour markers using lv_line for proper alignment
        lv_coord_t center_x = clock_size / 2;
        lv_coord_t center_y = clock_size / 2;
        for (int i = 0; i < 12; i++) {
            float angle = i * 30.0f * M_PI / 180.0f;
            lv_coord_t marker_length = (i % 3 == 0) ? (clock_size / 8) : (clock_size / 14);
            lv_coord_t marker_width = (i % 3 == 0) ? (is_small ? 3 : 4) : (is_small ? 1 : 2);
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
            lv_obj_t* marker = lv_line_create(clock_face);
            lv_line_set_points(marker, marker_points, 2);
            lv_obj_set_style_line_width(marker, marker_width, 0);
            lv_obj_set_style_line_color(marker, lv_color_hex(0x999999), 0);
            lv_obj_set_style_line_rounded(marker, true, 0);
        }

        // Create clock hands using lv_line
        lv_coord_t hour_length = clock_size * 0.25;
        lv_coord_t minute_length = clock_size * 0.35;
        lv_coord_t second_length = clock_size * 0.4;

        this->hour_points[0].x = center_x; this->hour_points[0].y = center_y;
        this->hour_points[1].x = center_x; this->hour_points[1].y = center_y - hour_length;
        hour_hand = lv_line_create(clock_face);
        lv_line_set_points_mutable(hour_hand, this->hour_points, 2);
        lv_obj_set_style_line_width(hour_hand, is_small ? 4 : 6, 0);
        lv_obj_set_style_line_color(hour_hand, lv_color_white(), 0);
        lv_obj_set_style_line_opa(hour_hand, LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(hour_hand, true, 0);

        this->minute_points[0].x = center_x; this->minute_points[0].y = center_y;
        this->minute_points[1].x = center_x; this->minute_points[1].y = center_y - minute_length;
        minute_hand = lv_line_create(clock_face);
        lv_line_set_points_mutable(minute_hand, this->minute_points, 2);
        lv_obj_set_style_line_width(minute_hand, is_small ? 3 : 4, 0);
        lv_obj_set_style_line_color(minute_hand, lv_color_white(), 0);
        lv_obj_set_style_line_opa(minute_hand, LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(minute_hand, true, 0);

        this->second_points[0].x = center_x; this->second_points[0].y = center_y;
        this->second_points[1].x = center_x; this->second_points[1].y = center_y - second_length;
        second_hand = lv_line_create(clock_face);
        lv_line_set_points_mutable(second_hand, this->second_points, 2);
        lv_obj_set_style_line_width(second_hand, 2, 0);
        lv_obj_set_style_line_color(second_hand, lv_color_hex(0xFF0000), 0);
        lv_obj_set_style_line_opa(second_hand, LV_OPA_COVER, 0);
        lv_obj_set_style_line_rounded(second_hand, true, 0);

        // Center dot
        lv_obj_t* center = lv_obj_create(clock_face);
        lv_obj_set_size(center, is_small ? 8 : 12, is_small ? 8 : 12);
        lv_obj_center(center);
        lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(center, lv_color_white(), 0);
        lv_obj_set_style_border_width(center, 0, 0);

        // Date label - position below center to avoid hand overlap
        date_label = lv_label_create(clock_face);
        lv_obj_align(date_label, LV_ALIGN_BOTTOM_MID, 0, -15);
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(date_label, lv_color_hex(0xAAAAAA), 0);
        
        // Initialize hand positions with current time
        update_time_display();
        
        // Force a redraw to ensure hands are visible immediately
        lv_obj_invalidate(clock_face);
    }

    void create_digital_clock() {
        lv_coord_t width, height;
        bool is_small;
        get_display_metrics(&width, &height, &is_small);

        // Create main time display
        time_label = lv_label_create(clock_container);
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, is_small ? -25 : -35);
        lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);

        // Use larger fonts for better visibility
        const lv_font_t* time_font;
        if (is_small) {
            time_font = &lv_font_montserrat_32;
        } else {
            time_font = &lv_font_montserrat_48;
        }
        lv_obj_set_style_text_font(time_label, time_font, 0);

        // Enhanced styling with borders and better contrast
        lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
        lv_obj_set_style_bg_color(time_label, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(time_label, LV_OPA_30, 0);
        lv_obj_set_style_radius(time_label, is_small ? 12 : 16, 0);
        lv_obj_set_style_pad_all(time_label, is_small ? 20 : 28, 0);
        lv_obj_set_style_border_width(time_label, 2, 0);
        lv_obj_set_style_border_color(time_label, lv_color_hex(0x444444), 0);
        lv_obj_set_style_border_opa(time_label, LV_OPA_50, 0);

        // Create date display
        date_label = lv_label_create(clock_container);
        lv_obj_align_to(date_label, time_label, LV_ALIGN_OUT_BOTTOM_MID, 0, is_small ? 12 : 16);
        lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(date_label, is_small ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);
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

    void redraw_clock() {
#ifdef ESP_PLATFORM
        auto lock = lvgl::getSyncLock()->asScopedLock();
        if (!lock.lock(lvgl::defaultLockTime)) {
            TT_LOG_E("Clock", "LVGL lock failed in redraw_clock");
            return;
        }
#endif

        // Clear the clock container
        lv_obj_clean(clock_container);
        time_label = nullptr;
        clock_face = hour_hand = minute_hand = second_hand = nullptr;
        wifi_label = nullptr;
        wifi_button = nullptr;
        date_label = nullptr;

        // Update toggle button visibility
        update_toggle_button_visibility();

#ifdef ESP_PLATFORM
        if (!is_time_synced()) {
            create_wifi_prompt();
        } else if (is_analog) {
            create_analog_clock();
        } else {
            create_digital_clock();
        }
#else
        if (is_analog) {
            create_analog_clock();
        } else {
            create_digital_clock();
        }
#endif
        
        // Force complete redraw with multiple invalidation strategies
        lv_obj_invalidate(clock_container);
        lv_obj_t* parent = lv_obj_get_parent(clock_container);
        if (parent) {
            lv_obj_invalidate(parent);
        }
        // Mark all children as needing redraw
        if (is_analog && clock_face) {
            lv_obj_invalidate(clock_face);
            if (hour_hand) lv_obj_invalidate(hour_hand);
            if (minute_hand) lv_obj_invalidate(minute_hand);
            if (second_hand) lv_obj_invalidate(second_hand);
        } else if (!is_analog && time_label) {
            lv_obj_invalidate(time_label);
        }
    }

public:
    void onShow(AppContext& app_context, lv_obj_t* parent) override {
        context = &app_context;
        
        // Create toolbar
        toolbar = tt::lvgl::toolbar_create(parent, app_context);
        lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

        // Create toggle button with better styling and responsive sizing
        toggle_btn = lv_btn_create(toolbar);
        lv_obj_set_height(toggle_btn, LV_PCT(80)); // Use percentage for better scaling
        lv_obj_set_style_radius(toggle_btn, 6, 0);
        lv_obj_set_style_bg_color(toggle_btn, lv_color_hex(0x007BFF), 0);
        lv_obj_set_style_bg_opa(toggle_btn, LV_OPA_80, 0);
        
        lv_obj_t* toggle_label = lv_label_create(toggle_btn);
        lv_label_set_text(toggle_label, LV_SYMBOL_REFRESH " Mode");
        lv_obj_center(toggle_label);
        
        // Position with better responsive alignment
        lv_obj_align(toggle_btn, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_add_event_cb(toggle_btn, toggle_mode_cb, LV_EVENT_CLICKED, this);

        // Create clock container
        clock_container = lv_obj_create(parent);
        lv_obj_set_size(clock_container, LV_PCT(100), LV_PCT(80));
        lv_obj_align_to(clock_container, toolbar, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
        lv_obj_set_style_border_width(clock_container, 0, 0);
        lv_obj_set_style_pad_all(clock_container, 10, 0);
        lv_obj_clear_flag(clock_container, LV_OBJ_FLAG_SCROLLABLE); // Prevent scrolling

        // Load settings and initialize
        load_mode();
        
#ifdef ESP_PLATFORM
        last_sync_status = is_time_synced();
#endif

        redraw_clock();

        // Start timer - this will handle both sync checking and time updates
#ifdef ESP_PLATFORM
        auto wrapper = std::make_shared<AppWrapper>(this);
        timer = std::make_unique<Timer>(Timer::Type::Periodic, [wrapper]() { timer_callback(wrapper); });
        timer->start(1000);
        TT_LOG_I("Clock", "Timer started in onShow");
#endif
#ifndef ESP_PLATFORM
        update_timer = lv_timer_create([](lv_timer_t* timer) {
            ClockApp* app = static_cast<ClockApp*>(lv_timer_get_user_data(timer));
            app->update_time_display();
        }, 1000, this);
#endif
    }

    void onHide(AppContext& app_context) override {
#ifdef ESP_PLATFORM
        // Stop timer first to prevent any callbacks during cleanup
        if (timer) {
            timer->stop();
            TT_LOG_I("Clock", "Timer stopped in onHide");
        }
        
        // Clear object pointers to prevent stale access
        time_label = nullptr;
        clock_face = hour_hand = minute_hand = second_hand = nullptr;
        wifi_label = nullptr;
        wifi_button = nullptr;
        toggle_btn = nullptr;
        clock_container = nullptr;
        toolbar = nullptr;
        date_label = nullptr;
#endif
#ifndef ESP_PLATFORM
        if (update_timer) {
            lv_timer_del(update_timer);
            update_timer = nullptr;
        }
#endif
    }
};

AppManifest clock_manifest = {
    .appId = "Clock",
    .appName = "Clock",
    .createApp = create<ClockApp>
};

} // namespace tt::app::clock
