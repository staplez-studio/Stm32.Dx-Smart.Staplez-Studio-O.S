/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    wifi_manager.h
  * @brief   ESP8266 / ESP32 AT-command Wi-Fi driver & manager for STM32F103 (ESP-01S, ESP32-CAM with AT firmware, ...).
  *          Implements strict step-by-step Espressif AT Command State Machine:
  *          1. Module Sync (AT)
  *          2. Echo Disable (ATE0)
  *          3. Station Mode Configuration (AT+CWMODE=1)
  *          4. DHCP Client Activation (AT+CWDHCP=1,1)
  *          5. Single-Connection Mode (AT+CIPMUX=0)
  *          6. 2.4GHz AP Scanner (AT+CWLAP)
  *          7. 4-Way WPA/WPA2 Join Sequence (AT+CWJAP)
  *          8. IP & MAC Query (AT+CIFSR)
  *          9. Clean Disconnect / Sleep (AT+CWQAP)
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __WIFI_MANAGER_H__
#define __WIFI_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>

#define ESP01S_MAX_SSID_LEN     32
#define ESP01S_MAX_PWD_LEN      32
#define ESP01S_MAX_SCAN_APS     16
#define ESP01S_IP_STR_LEN       18

typedef enum {
    ESP01S_STATE_IDLE = 0,
    ESP01S_STATE_SYNCING,
    ESP01S_STATE_READY,
    ESP01S_STATE_SCANNING,
    ESP01S_STATE_CONNECTING,
    ESP01S_STATE_CONNECTED,
    ESP01S_STATE_GOT_IP,
    ESP01S_STATE_ERR_TIMEOUT,
    ESP01S_STATE_ERR_WRONG_PWD,
    ESP01S_STATE_ERR_NOT_FOUND,
    ESP01S_STATE_ERR_NO_MODULE
} ESP01S_State_t;

typedef struct {
    char ssid[ESP01S_MAX_SSID_LEN];
    int8_t rssi;          // dBm (-30 to -95)
    uint8_t encryption;   // 0 = OPEN, 2 = WPA, 3 = WPA2, 4 = WPA/WPA2
} WiFi_AP_Entry_t;

extern bool wifi_connected;
extern ESP01S_State_t esp01s_state;
extern char esp01s_ip_address[ESP01S_IP_STR_LEN];
extern char esp01s_active_ssid[ESP01S_MAX_SSID_LEN];
extern char esp01s_status_msg[48];
extern uint8_t esp01s_scanned_count;
extern WiFi_AP_Entry_t esp01s_scanned_aps[ESP01S_MAX_SCAN_APS];

void WiFi_Manager_Init(void);
void WiFi_Manager_Process(char key, uint32_t current_tick);

/* Call every main-loop pass: starts the module a couple of seconds after power-up and auto-joins the
 * saved network (see wifi_config.h). */
void WiFi_Manager_Background(uint32_t current_tick);
void WiFi_Manager_Digit(char digit);   /* '1'..'9' from the remote while the password editor is open */

/* AT protocol interface (works with ESP8266 AT and ESP32 AT firmware) */
bool ESP01S_SyncAndInit(void);
bool ESP01S_ScanNetworks(void);
bool ESP01S_ConnectAP(const char *ssid, const char *password);
bool ESP01S_Disconnect(void);
bool ESP01S_QueryIP(void);
bool ESP01S_SyncTime(void);   /* network time -> DS3231 (needs wifi_connected) */

/* Plain HTTP GET over TCP. `request` is the complete request text. Returns the number of bytes of
 * response (headers + body) written to `out`, or 0 on failure. */
bool ESP01S_EnsureReady(void);   /* start / find the module if that has not happened yet (used by the Hard Drive app) */
uint16_t ESP01S_HttpGet(const char *host, uint16_t port, const char *request,
                        char *out, uint16_t cap, uint32_t timeout_ms);

/* Saved network (AT24C32 EEPROM) */
bool WiFi_Creds_Load(void);
bool WiFi_Creds_Save(const char *ssid, const char *password);
void WiFi_Creds_Clear(void);

#endif /* __WIFI_MANAGER_H__ */
