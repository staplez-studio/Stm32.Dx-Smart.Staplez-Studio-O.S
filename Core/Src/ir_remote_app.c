/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    ir_remote_app.c
  * @brief   IR Remote Control UI and Real-Time Signal Processing for Staplez Studio.
  *          Signal Input on PA12 (Active-LOW IR Receiver VS1838B / TSOP4838).
  *          Uses DWT cycle counter for microsecond-accurate edge timing.
  *          Sole input controller for menus, Set Alarm, Set Time, WiFi, Fan,
  *          Sound, and Entertainment apps.
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#include "ir_remote_app.h"
#include "calendar_app.h"
#include "timezone_app.h"
#include "staplez_studio.h"
#include "app_menu.h"
#include "wifi_manager.h"
#include "time_manager.h"
#include "alarm_manager.h"
#include "entertainment_app.h"
#include "hard_drive_app.h"
#include "weather_app.h"
#include "fan_app.h"
#include "sound_app.h"
#include "main_screen.h"
#include "sh1106.h"
#include "ui_draw.h"
#include "weather_icons.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t code;
    const char *name;
    const char *hex_str;
    const char *action;
} IrButtonDef_t;

static const IrButtonDef_t KNOWN_REMOTE_BUTTONS[] = {
    { 0xFFA25D, "POWER", "0xFFA25D", "TOGGLE FAN POWER" },
    { 0xFFE21D, "FUNC",  "0xFFE21D", "APP MENU" },
    { 0xFF629D, "MODE",  "0xFF629D", "APP MENU" },
    { 0xFF22DD, "MUTE",  "0xFF22DD", "MUTE AUDIO" },
    { 0xF2F0,   "UP",    "0xF2F0",   "NAVIGATE UP / PREV" },
    { 0xF2E0,   "DOWN",  "0xF2E0",   "NAVIGATE DOWN / NEXT" },
    { 0x72C8,   "LEFT",  "0x72C8",   "NAVIGATE LEFT" },
    { 0x72D8,   "RIGHT", "0x72D8",   "NAVIGATE RIGHT" },
    { 0x7158,   "PLAY",  "0x7158",   "OPEN APP MENU / PLAY" },
    { 0x7198,   "GEAR",  "0x7198",   "SELECT GEAR / BACK" },
    { 0xFFC23D, "PREV",  "0xFFC23D", "PREV TRACK" },
    { 0xFFE01F, "NEXT",  "0xFFE01F", "NEXT TRACK" },
    { 0xFFA857, "EQ",    "0xFFA857", "TOGGLE EQ BAND" },
    { 0xFF906F, "VOL-",  "0xFF906F", "VOLUME DOWN" },
    { 0xFF6897, "VOL+",  "0xFF6897", "VOLUME UP" },
    { 0xFF30CF, "1",     "0xFF30CF", "WIFI APP" },
    { 0xFF18E7, "2",     "0xFF18E7", "SET TIME APP" },
    { 0xFF7A85, "3",     "0xFF7A85", "ALARM APP" },
    { 0xFF10EF, "4",     "0xFF10EF", "FAN APP" },
    { 0xFF38C7, "5",     "0xFF38C7", "SOUND APP" },
    { 0xFF5AA5, "6",     "0xFF5AA5", "IR REMOTE APP" },
    { 0xFF42BD, "7",     "0xFF42BD", "ENTERTAINMENT" },
    { 0xFF4AB5, "8",     "0xFF4AB5", "WEATHER APP" },
    { 0xFF52AD, "9",     "0xFF52AD", "SCREEN SHIFT" }
};
#define NUM_KNOWN_BUTTONS (sizeof(KNOWN_REMOTE_BUTTONS) / sizeof(KNOWN_REMOTE_BUTTONS[0]))

static volatile uint32_t ir_last_edge_tick = 0;
static volatile uint8_t  ir_bit_count = 0;
static volatile uint32_t ir_raw_code = 0;
static volatile uint32_t ir_decoded_hex = 0x7158;
static volatile uint8_t  ir_new_frame_ready = 0;

static char ir_last_hex_str[16] = "0x7158";
static char ir_last_btn_name[16] = "PLAY";
static char ir_last_action_desc[32] = "OPEN APP MENU / PLAY";

static uint8_t ir_selected_item = 0;
static uint32_t ir_state_timer = 0;

void Ir_Remote_App_Init(void)
{
    /* Enable Cortex-M3 DWT Cycle Counter for microsecond edge timing */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0;

    ir_selected_item = 0;
    ir_state_timer = 0;
    ir_bit_count = 0;
    ir_raw_code = 0;
    ir_decoded_hex = 0x7158;
    ir_new_frame_ready = 0;
    snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0x7158");
    snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "PLAY");
    snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "OPEN APP MENU / PLAY");
}

const char* IR_Get_Last_Hex_Str(void)
{
    return ir_last_hex_str;
}

const char* IR_Get_Last_Button_Name(void)
{
    return ir_last_btn_name;
}

const char* IR_Get_Last_Action_Desc(void)
{
    return ir_last_action_desc;
}

void IR_Simulate_Button_Press(uint8_t index)
{
    if (index >= NUM_KNOWN_BUTTONS) index = 0;
    ir_decoded_hex = KNOWN_REMOTE_BUTTONS[index].code;
    ir_new_frame_ready = 1;
}

uint8_t IR_Is_New_Frame_Ready(void)
{
    return ir_new_frame_ready;
}

/**
 * EXTI falling edge callback for PA12 (IR Receiver)
 * Measures pulse widths using Cortex-M3 DWT cycle counter.
 */
void IR_Remote_EXTI_Callback(void)
{
    uint32_t now = DWT->CYCCNT;
    uint32_t cycles_per_us = SystemCoreClock / 1000000U;
    if (cycles_per_us == 0) cycles_per_us = 72;
    uint32_t diff_us = (now - ir_last_edge_tick) / cycles_per_us;
    ir_last_edge_tick = now;

    // Idle gap (> 25ms) starts a clean frame
    if (diff_us > 25000) {
        ir_bit_count = 0;
        ir_raw_code = 0;
        return;
    }

    // NEC Repeat code: 9ms LOW + 2.25ms HIGH = ~11.25ms when bit_count == 0
    if (diff_us >= 9500 && diff_us < 12000 && ir_bit_count == 0 && ir_decoded_hex != 0) {
        ir_new_frame_ready = 1;
        return;
    }

    // Leader pulse: 4ms to 17ms resets bit accumulator for new command
    if (diff_us >= 4000 && diff_us <= 17000) {
        ir_bit_count = 0;
        ir_raw_code = 0;
        return;
    }

    // Data bit:
    // Logical 0: 560us LOW + 560us HIGH = ~1120us (500us .. 1550us)
    // Logical 1: 560us LOW + 1690us HIGH = ~2250us (1550us .. 3200us)
    if (ir_bit_count < 32) {
        if (diff_us >= 500 && diff_us <= 1550) {
            ir_raw_code = (ir_raw_code << 1);
            ir_bit_count++;
        } else if (diff_us > 1550 && diff_us <= 3200) {
            ir_raw_code = (ir_raw_code << 1) | 1U;
            ir_bit_count++;
        } else {
            return;
        }

        if (ir_bit_count == 32) {
            ir_decoded_hex = ir_raw_code;
            ir_new_frame_ready = 1;
            ir_bit_count = 0;
            ir_raw_code = 0;
        }
    }
}

/**
 * Helper to match 15/16-bit IR command across all possible byte alignments
 */
static uint8_t ir_match_cmd(uint32_t code, uint16_t target)
{
    uint16_t c0 = (uint16_t)(code & 0xFFFF);
    uint16_t c8 = (uint16_t)((code >> 8) & 0xFFFF);
    uint16_t c16 = (uint16_t)((code >> 16) & 0xFFFF);
    uint16_t t15 = target & 0x7FFF;

    if (code == target || c0 == target || c8 == target || c16 == target) return 1;
    if ((c0 & 0x7FFF) == t15 || (c8 & 0x7FFF) == t15 || (c16 & 0x7FFF) == t15) return 1;
    return 0;
}

/**
 * Internal helper to check for IR frame timeout (idle gap > 25ms).
 * Called by main loop and buzzer tone generator to ensure responsiveness.
 */
void IR_Remote_Check_Timeout(void)
{
    if (ir_bit_count >= 12) {
        uint32_t now = DWT->CYCCNT;
        uint32_t cycles_per_us = SystemCoreClock / 1000000U;
        if (cycles_per_us == 0) cycles_per_us = 72;
        if (((now - ir_last_edge_tick) / cycles_per_us) > 25000) {
            ir_decoded_hex = ir_raw_code;
            ir_new_frame_ready = 1;
            ir_bit_count = 0;
            ir_raw_code = 0;
        }
    }
}

/**
 * Main OS process loop update for IR.
 * Recognizes received IR codes, updates hex strings, and triggers actions across all apps.
 */
void IR_Remote_Update(void)
{
    IR_Remote_Check_Timeout();

    if (ir_new_frame_ready) {
        ir_new_frame_ready = 0;

        uint32_t code = ir_decoded_hex;
        uint32_t tick = HAL_GetTick();

        if (is_alarm_ringing) {
            Alarm_Dismiss();
            return;
        }

        if (current_state == STATE_SPLASH) {
            Buzzer_Stop();
            set_system_state(STATE_MAIN_SCREEN);
            Main_Screen_Init();
            Main_Screen_Render();
            return;
        }

        uint8_t is_up    = ir_match_cmd(code, 0xF2F0);
        uint8_t is_down  = ir_match_cmd(code, 0xF2E0);
        uint8_t is_left  = ir_match_cmd(code, 0x72C8) || code == 0xFFC23D || (code & 0xFFFF) == 0xC23D || code == 0xFF906F;
        uint8_t is_right = ir_match_cmd(code, 0x72D8) || code == 0xFFE01F || (code & 0xFFFF) == 0xE01F || code == 0xFF6897;
        uint8_t is_gear  = ir_match_cmd(code, 0x7198);
        uint8_t is_play  = ir_match_cmd(code, 0x7158);

        if (is_up) {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0xF2F0");
            snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "UP");
            snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "UP / VALUE +1");
        } else if (is_down) {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0xF2E0");
            snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "DOWN");
            snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "DOWN / VALUE -1");
        } else if (is_left) {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0x72C8");
            snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "LEFT");
            snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "NAVIGATE LEFT");
        } else if (is_right) {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0x72D8");
            snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "RIGHT");
            snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "NAVIGATE RIGHT");
        } else if (is_gear) {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0x7198");
            snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "GEAR");
            snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "SHIFT / BACK");
        } else if (is_play) {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0x7158");
            snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "PLAY");
            snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "SELECT / PLAY");
        } else if ((code & 0xFFFF0000) == 0 && code <= 0xFFFF) {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0x%04lX", (unsigned long)code);
        } else {
            snprintf(ir_last_hex_str, sizeof(ir_last_hex_str), "0x%06lX", (unsigned long)(code & 0xFFFFFF));
        }

        // 1. App Menu Navigation (STATE_ICON_MENU)
        if (current_state == STATE_ICON_MENU) {
            if (is_up) App_Menu_IR_Process(0xF2F0);
            else if (is_down) App_Menu_IR_Process(0xF2E0);
            else if (is_left) App_Menu_IR_Process(0x72C8);
            else if (is_right) App_Menu_IR_Process(0x72D8);
            else if (is_gear) App_Menu_IR_Process(0x7198);
            else if (is_play) App_Menu_IR_Process(0x7158);
            else App_Menu_IR_Process(code);
            return;
        }

        // 2. Main Clock Screen
        if (current_state == STATE_MAIN_SCREEN) {
            if (is_gear || is_up || is_down) {
                Main_Screen_Set_Gear_Selected(is_gear ? !Main_Screen_Is_Gear_Selected() : 1);
                Main_Screen_Render();
                return;
            } else if (code == 0xFFE21D || code == 0xFF629D) {
                /* FUNC / MODE: the app menu, straight away */
                Main_Screen_Set_Gear_Selected(0);
                set_system_state(STATE_ICON_MENU);
                App_Menu_Render();
                return;
            } else if (is_play) {
                if (Main_Screen_Is_Gear_Selected()) {
                    set_system_state(STATE_ICON_MENU);
                    Main_Screen_Set_Gear_Selected(0);
                    App_Menu_Render();
                } else {
                    /* PLAY on the clock opens the calendar */
                    Calendar_App_Enter();
                    set_system_state(STATE_CALENDAR_APP);
                    Calendar_App_Process(0, tick);
                }
                return;
            }
        }

        // 3. Sub-App Controls & Navigation (Set Alarm, Set Time, WiFi, Fan, Sound, Entertainment, IR, Weather)
        if (current_state != STATE_MAIN_SCREEN && current_state != STATE_ICON_MENU) {
            char mapped_key = 0;
            if (is_left)       mapped_key = '4';
            else if (is_right) mapped_key = '6';
            else if (is_up)    mapped_key = '2';
            else if (is_down)  mapped_key = '8';
            else if (is_play)  mapped_key = '#';
            else if (is_gear)  mapped_key = '*';

            if (mapped_key != 0) {
                if (current_state == STATE_ALARM_TIME_APP ||
                    current_state == STATE_ALARM_SINGLE_APP ||
                    current_state == STATE_ALARM_DOUBLE_APP) {
                    Alarm_Manager_Process(mapped_key, tick);
                } else if (current_state == STATE_SET_TIME_APP) {
                    Time_Manager_Process(mapped_key, tick);
                } else if (current_state == STATE_WIFI_APP || current_state == STATE_WIFI_PASSWORD) {
                    WiFi_Manager_Process(mapped_key, tick);
                } else if (current_state == STATE_FAN_APP) {
                    Fan_App_Process(mapped_key, tick);
                } else if (current_state == STATE_SOUND_APP) {
                    Sound_App_Process(mapped_key, tick);
                } else if (current_state == STATE_ENTERTAINMENT_APP) {
                    Entertainment_App_Process(mapped_key, tick);
                } else if (current_state == STATE_FILE_EXPLORER_APP) {
                    Hard_Drive_App_Process(mapped_key, tick);
                } else if (current_state == STATE_IR_REMOTE_APP) {
                    Ir_Remote_App_Process(mapped_key, tick);
                } else if (current_state == STATE_WEATHER_APP) {
                    Weather_App_Process(mapped_key, tick);
                } else if (current_state == STATE_CALENDAR_APP) {
                    Calendar_App_Process(mapped_key, tick);
                } else if (current_state == STATE_TIMEZONE_APP) {
                    Timezone_App_Process(mapped_key, tick);
                }
                return;
            }
        }

        // 3b. Wi-Fi password editor: the number buttons type digits (1-9) instead of jumping to an app
        if (current_state == STATE_WIFI_PASSWORD) {
            for (uint8_t i = 0; i < NUM_KNOWN_BUTTONS; i++) {
                const char *nm = KNOWN_REMOTE_BUTTONS[i].name;
                if (nm[0] >= '1' && nm[0] <= '9' && nm[1] == '\0' &&
                    (KNOWN_REMOTE_BUTTONS[i].code == code ||
                     (KNOWN_REMOTE_BUTTONS[i].code & 0xFFFF) == (code & 0xFFFF) ||
                     (KNOWN_REMOTE_BUTTONS[i].code & 0xFFFF) == ((code >> 8) & 0xFFFF))) {
                    WiFi_Manager_Digit(nm[0]);
                    return;
                }
            }
        }

        // 4. Global known buttons
        uint8_t matched = 0;
        for (uint8_t i = 0; i < NUM_KNOWN_BUTTONS; i++) {
            if (KNOWN_REMOTE_BUTTONS[i].code == code ||
               (KNOWN_REMOTE_BUTTONS[i].code & 0xFFFF) == (code & 0xFFFF) ||
               (KNOWN_REMOTE_BUTTONS[i].code & 0xFFFF) == ((code >> 8) & 0xFFFF)) {
                snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "%s", KNOWN_REMOTE_BUTTONS[i].name);
                snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "%s", KNOWN_REMOTE_BUTTONS[i].action);
                matched = 1;

                if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "POWER") == 0) {
                    Fan_App_Process('5', tick);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "VOL+") == 0) {
                    Sound_App_Process('2', tick);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "VOL-") == 0) {
                    Sound_App_Process('8', tick);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "MUTE") == 0) {
                    Sound_App_Process('#', tick);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "MODE") == 0 || strcmp(KNOWN_REMOTE_BUTTONS[i].name, "FUNC") == 0) {
                    set_system_state(STATE_ICON_MENU);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "1") == 0) {
                    set_system_state(STATE_WIFI_APP);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "2") == 0) {
                    set_system_state(STATE_SET_TIME_APP);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "3") == 0) {
                    set_system_state(STATE_ALARM_TIME_APP);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "4") == 0) {
                    set_system_state(STATE_FAN_APP);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "5") == 0) {
                    set_system_state(STATE_SOUND_APP);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "6") == 0) {
                    set_system_state(STATE_IR_REMOTE_APP);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "7") == 0) {
                    set_system_state(STATE_ENTERTAINMENT_APP);
                } else if (strcmp(KNOWN_REMOTE_BUTTONS[i].name, "8") == 0) {
                    set_system_state(STATE_WEATHER_APP);
                }
                break;
            }
        }

        if (!matched) {
            snprintf(ir_last_btn_name, sizeof(ir_last_btn_name), "REMOTE BTN");
            snprintf(ir_last_action_desc, sizeof(ir_last_action_desc), "RECEIVED IR");
        }
    }
}

void Ir_Remote_App_Process(char key, uint32_t current_tick)
{
    if (key != 0) {
        if (key == '*' || key == '#') {
            set_system_state(STATE_ICON_MENU);
            return;
        }
    }

    if ((current_tick - ir_state_timer) >= 50) {
        ir_state_timer = current_tick;
        sh1106_clear();

        // Matching emulator renderIrRemoteApp:
        sh1106_drawString(4, 2, "IR REMOTE CONTROL", 1, SH1106_WHITE);
        sh1106_drawHLine(0, 12, 128, SH1106_WHITE);

        char hex_buf[32];
        snprintf(hex_buf, sizeof(hex_buf), "HEX: %s", ir_last_hex_str);
        sh1106_drawString(4, 16, hex_buf, 1, SH1106_WHITE);

        char btn_buf[32];
        snprintf(btn_buf, sizeof(btn_buf), "BTN: %s", ir_last_btn_name);
        sh1106_drawString(4, 27, btn_buf, 1, SH1106_WHITE);

        char map_buf[48];
        snprintf(map_buf, sizeof(map_buf), "MAP: %.15s", ir_last_action_desc);
        sh1106_drawString(4, 38, map_buf, 1, SH1106_WHITE);

        sh1106_drawRect(4, 48, 28, 12, SH1106_WHITE);
        if (ir_selected_item == 0) {
            sh1106_drawRect(2, 46, 32, 16, SH1106_WHITE);
        }
        sh1106_drawString(6, 50, "BACK", 1, SH1106_WHITE);
        sh1106_drawString(40, 50, "PLAY:MENU", 1, SH1106_WHITE);

        sh1106_update();
    }
}
