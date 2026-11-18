#ifndef _CLOCK_DISPLAY_H_
#define _CLOCK_DISPLAY_H_

#include "display/lcd_display.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include <lvgl.h>
#include <time.h>

#define TAG "ClockDisplay"

// 大号数字字体 (由 CMakeLists 的 BUILTIN_TEXT_FONT 编译进固件)
LV_FONT_DECLARE(font_noto_sans_basic_30_4);

// 大号待机时钟: 待机(idle)时隐藏表情, 显示大字体 HH:MM + 日期;
// 其它状态(聆听/思考/说话)恢复正常表情界面。
class ClockDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* clock_box_ = nullptr;      // 覆盖层容器
    lv_obj_t* clock_time_ = nullptr;     // 大号时间 HH:MM
    lv_obj_t* clock_date_ = nullptr;     // 日期: MM/DD 周X
    bool ui_ready_ = false;
    bool clock_visible_ = false;
    int last_min_ = -1;
    int last_day_ = -1;
    static const char* weekdays[7];

    // 建立覆盖层 (等 SetupUI 完成后调用)
    void EnsureClockUi() {
        if (ui_ready_) return;
        if (!setup_ui_called_) return;
        auto screen = lv_screen_active();
        if (screen == nullptr) return;

        DisplayLockGuard lock(this);

        clock_box_ = lv_obj_create(screen);
        lv_obj_set_size(clock_box_, LV_HOR_RES, LV_VER_RES);
        lv_obj_align(clock_box_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_opa(clock_box_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(clock_box_, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(clock_box_, 0, 0);
        lv_obj_set_style_pad_all(clock_box_, 0, 0);
        lv_obj_set_style_radius(clock_box_, 0, 0);
        lv_obj_set_scrollbar_mode(clock_box_, LV_SCROLLBAR_MODE_OFF);

        // 大号时间
        clock_time_ = lv_label_create(clock_box_);
        lv_obj_set_style_text_font(clock_time_, &font_noto_sans_basic_30_4, 0);
        lv_obj_set_style_text_color(clock_time_, lv_color_hex(0x00E5FF), 0);
        lv_obj_align(clock_time_, LV_ALIGN_CENTER, 0, -12);
        lv_label_set_text(clock_time_, "00:00");

        // 日期行 (同字体, 通过较小颜色区分层级)
        clock_date_ = lv_label_create(clock_box_);
        lv_obj_set_style_text_font(clock_date_, &font_noto_sans_basic_30_4, 0);
        lv_obj_set_style_text_color(clock_date_, lv_color_hex(0xB0BEC5), 0);
        lv_obj_align(clock_date_, LV_ALIGN_CENTER, 0, 32);
        lv_label_set_text(clock_date_, "");

        lv_obj_move_foreground(clock_box_);
        lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
        ui_ready_ = true;
        ESP_LOGI(TAG, "Clock overlay UI ready");
    }

    void SetClockVisible(bool visible) {
        if (!ui_ready_ || clock_visible_ == visible) return;
        clock_visible_ = visible;
        DisplayLockGuard lock(this);
        if (visible) {
            lv_obj_remove_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(clock_box_);
            last_min_ = -1;  // 强制刷新
        } else {
            lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
        }
    }

public:
    ClockDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                 int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {}

    void UpdateStatusBar(bool update_all = false) override {
        // 先让父类更新原生状态栏/时钟逻辑
        LvglDisplay::UpdateStatusBar(update_all);
        EnsureClockUi();
        if (!ui_ready_) return;

        auto& app = Application::GetInstance();
        bool idle = (app.GetDeviceState() == kDeviceStateIdle);

        if (!idle) {
            SetClockVisible(false);
            return;
        }

        SetClockVisible(true);

        time_t now = time(NULL);
        struct tm* t = localtime(&now);
        if (t == nullptr || t->tm_year < 2025 - 1900) return;  // 未校时

        int cur_min = t->tm_hour * 60 + t->tm_min;
        int cur_day = t->tm_mon * 100 + t->tm_mday;

        DisplayLockGuard lock(this);
        if (cur_min != last_min_) {
            last_min_ = cur_min;
            char buf[16];
            snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
            lv_label_set_text(clock_time_, buf);
        }
        if (cur_day != last_day_) {
            last_day_ = cur_day;
            char buf[24];
            snprintf(buf, sizeof(buf), "%02d/%02d %s", t->tm_mon + 1, t->tm_mday,
                     weekdays[t->tm_wday]);
            lv_label_set_text(clock_date_, buf);
        }
    }
};

const char* ClockDisplay::weekdays[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

#endif // _CLOCK_DISPLAY_H_
