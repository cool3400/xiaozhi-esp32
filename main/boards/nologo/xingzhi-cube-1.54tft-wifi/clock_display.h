#ifndef _CLOCK_DISPLAY_H_
#define _CLOCK_DISPLAY_H_

#include "display/lcd_display.h"
#include "display/lvgl_display/lvgl_theme.h"
#include "assets/lang_config.h"
#include "application.h"
#include "device_state.h"
#include <esp_log.h>
#include <lvgl.h>
#include <time.h>

// 大号时间数字字体 (84px, 本板型目录 clock_font.c 内联)
LV_FONT_DECLARE(clock_font);
// 回退字体: 编译进固件的 30px 字体 (主题中文 cbin 拿不到时用)
LV_FONT_DECLARE(font_noto_sans_basic_30_4);

// 待机大时钟: 待机(idle)时隐藏表情, 显示大字体 HH:MM + 日期(MM/DD 周X);
// 其它状态(聆听/思考/说话)恢复正常表情界面。
class ClockDisplay : public SpiLcdDisplay {
private:
    lv_obj_t* clock_box_ = nullptr;      // 覆盖层容器
    lv_obj_t* clock_time_ = nullptr;     // 大号时间 HH:MM (84px)
    lv_obj_t* clock_date_ = nullptr;     // 日期: MM/DD 周X (主题中文字体)
    bool ui_ready_ = false;
    bool clock_visible_ = false;
    int last_min_ = -1;
    int last_day_ = -1;
    static const char* weekdays[7];

    // 取主题的中文字体 (官方 assets 里的 cbin, 能显示中文)
    const lv_font_t* GetDateFont() {
        auto& mgr = LvglThemeManager::GetInstance();
        LvglTheme* theme = mgr.GetTheme("light");
        if (theme == nullptr) theme = mgr.GetTheme("dark");
        if (theme != nullptr) {
            auto tf = theme->text_font();
            if (tf != nullptr && tf->font() != nullptr) {
                return tf->font();
            }
        }
        return &font_noto_sans_basic_30_4;  // 回退
    }

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
        lv_obj_set_style_bg_color(clock_box_, lv_color_hex(0x050914), 0);
        lv_obj_set_style_border_width(clock_box_, 0, 0);
        lv_obj_set_style_pad_all(clock_box_, 0, 0);
        lv_obj_set_style_radius(clock_box_, 0, 0);
        lv_obj_set_scrollbar_mode(clock_box_, LV_SCROLLBAR_MODE_OFF);

        // 大号时间 (84px 数字): 居中略偏下
        clock_time_ = lv_label_create(clock_box_);
        lv_obj_set_style_text_font(clock_time_, &clock_font, 0);
        lv_obj_set_style_text_color(clock_time_, lv_color_hex(0x4FC3F7), 0);
        lv_obj_align(clock_time_, LV_ALIGN_CENTER, 0, 14);
        lv_label_set_text(clock_time_, "00:00");

        // 日期行 (主题中文字体): 时间上方, MM/DD 周X
        clock_date_ = lv_label_create(clock_box_);
        lv_obj_set_style_text_font(clock_date_, GetDateFont(), 0);
        lv_obj_set_style_text_color(clock_date_, lv_color_hex(0xB0BEC5), 0);
        lv_obj_align(clock_date_, LV_ALIGN_CENTER, 0, -52);
        lv_label_set_text(clock_date_, "");

        lv_obj_move_to_index(clock_box_, -1);
        lv_obj_add_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
        ui_ready_ = true;
        ESP_LOGI("ClockDisplay", "Clock overlay UI ready");
    }

    void SetClockVisible(bool visible) {
        if (!ui_ready_ || clock_visible_ == visible) return;
        clock_visible_ = visible;
        DisplayLockGuard lock(this);
        if (visible) {
            lv_obj_remove_flag(clock_box_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_to_index(clock_box_, -1);
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
        // 先让父类更新原生状态栏逻辑
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
            char buf[32];
            snprintf(buf, sizeof(buf), "%02d/%02d %s", t->tm_mon + 1, t->tm_mday,
                     weekdays[t->tm_wday]);
            lv_label_set_text(clock_date_, buf);
        }
    }
};

// tm_wday: 0=周日 ... 6=周六
const char* ClockDisplay::weekdays[7] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

#endif // _CLOCK_DISPLAY_H_
