/* End-to-end simulation of the Wi-Fi / clock / weather code against simulated hardware.
 * The REAL firmware sources are used (wifi_manager.c, weather_app.c, usart.c ring, staplez_studio.c RTC code). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdbool.h>
#include "main.h"
#include "staplez_studio.h"
#include "usart.h"
#include "clock_zone.h"
#include "wifi_net.h"
#include "wifi_config.h"
#include "sh1106.h"
#include "wifi_manager.c"     /* included so tests can read its static state */
#include "weather_app.c"
#include "calendar_app.c"
#include "ir_remote_app.h"
#include "main_screen.h"
#include "app_menu.h"
#include "alarm_manager.h"
#include "timezone_app.h"
#include "time_manager.h"
#include "alarm_manager.h"

uint32_t SystemCoreClock = 72000000U;
void HAL_IncTick(void){}
void Error_Handler(void){ fprintf(stderr,"Error_Handler\n"); exit(3); }

/* ---------------- simulated time ---------------- */
static uint32_t g_tick = 0;
static void esp_tick(uint32_t now);

/* ---------------- simulated EEPROM (AT24C32 @0x57, 32-byte pages, 5 ms write cycle) and DS3231 @0x68 ---------------- */
static uint8_t eeprom[4096]; static uint32_t ee_busy_until = 0; static int ee_nack_count = 0, ee_wrap_events = 0;
static uint8_t rtc[19];
static int bcd(int v){ return (v/10)<<4 | (v%10); }
static int unbcd(int b){ return (b>>4)*10 + (b&15); }
static void rtc_store_std(int Y,int M,int D,int h24,int mi,int s){ int h12=h24%12; if(!h12)h12=12; rtc[0]=bcd(s); rtc[1]=bcd(mi); rtc[2]=0x40|(h24>=12?0x20:0)|bcd(h12); rtc[3]=1; rtc[4]=bcd(D); rtc[5]=bcd(M); rtc[6]=bcd(Y%100); }
static void rtc_read_std(int*Y,int*M,int*D,int*h24,int*mi,int*s){ *s=unbcd(rtc[0]&0x7F); *mi=unbcd(rtc[1]); int h12=unbcd(rtc[2]&0x1F); int pm=(rtc[2]&0x20)!=0; *h24=(h12%12)+(pm?12:0); *D=unbcd(rtc[4]); *M=unbcd(rtc[5]&0x1F); *Y=2000+unbcd(rtc[6]); }

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef*h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef*h,uint16_t a,uint32_t t,uint32_t o){(void)h;(void)t;(void)o; return (a==(0x68<<1)||a==(0x57<<1))?HAL_OK:HAL_ERROR;}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef*h,uint16_t a,uint8_t*d,uint16_t n,uint32_t o){(void)h;(void)d;(void)n;(void)o; return a==0x78?HAL_OK:HAL_ERROR;}
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef*h,uint16_t a,uint8_t*d,uint16_t n,uint32_t o){(void)h;(void)a;memset(d,0,n);(void)o;return HAL_ERROR;}
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef*h,uint16_t a,uint16_t m,uint16_t s,uint8_t*d,uint16_t n,uint32_t o){
  (void)h;(void)s;(void)o;
  if(a==0x78) return HAL_OK;                              /* OLED */
  if(a==(0x68<<1)){ for(int i=0;i<n&&m+i<19;i++) rtc[m+i]=d[i]; return HAL_OK; }
  if(a==(0x57<<1)){
    if(g_tick < ee_busy_until){ ee_nack_count++; return HAL_ERROR; }   /* still in the internal write cycle */
    uint16_t page=m/32;
    for(int i=0;i<n;i++){ uint16_t addr=m+i; if(addr/32!=page){ ee_wrap_events++; addr=(uint16_t)(page*32+(addr%32)); } eeprom[addr]=d[i]; }
    ee_busy_until = g_tick + 5; return HAL_OK; }
  return HAL_ERROR; }
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef*h,uint16_t a,uint16_t m,uint16_t s,uint8_t*d,uint16_t n,uint32_t o){
  (void)h;(void)s;(void)o;
  if(a==(0x68<<1)){ for(int i=0;i<n;i++) d[i]=(m+i<19)?rtc[m+i]:0; return HAL_OK; }
  if(a==(0x57<<1)){ if(g_tick < ee_busy_until){ ee_nack_count++; return HAL_ERROR; } for(int i=0;i<n;i++) d[i]=eeprom[m+i]; return HAL_OK; }
  return HAL_ERROR; }

/* ---------------- simulated ESP8266 AT module ---------------- */
typedef struct { const char *ssid; const char *pwd; int ecn; int rssi; } ap_t;
static const ap_t APS[] = {
  {"HomeNet",           "Pa$$w0rd!,\"x\\y", 3, -48},
  {"HomeNet",           "Pa$$w0rd!,\"x\\y", 3, -71},      /* same name on a weaker access point */
  {"Cafe, Free Wifi",   "",                 0, -77},      /* open network, comma in the name */
  {"Neighbour",         "secret123",        4, -62},
  {"",                  "x",                3, -55},      /* hidden */
  {"Garage",            "g",                3, -90},
};
#define NAPS (int)(sizeof(APS)/sizeof(APS[0]))
static uint32_t esp_ready_at = 1500;        /* module answers only after this tick (0xFFFFFFFF = dead) */
static bool esp_echo = true, esp_link = false, esp_http_ok = true; static char esp_ssid[40];
static int sntp_calls = 0; static wnet_time_t esp_utc = {2026,11,10,2,15,42,2};
static char esp_http_body[256] = "+2\xC2\xB0""C;80%;Light rain;Toronto, Canada\n"; static int esp_http_status = 200; static bool esp_tcp_refuse = false;
static int cmds_seen = 0; static char cmd_log[64][96];

typedef struct { uint32_t due; char data[900]; uint16_t len, pos; } outq_t;
static outq_t outq[48]; static int oq_head = 0, oq_tail = 0;
static void esp_send_at(uint32_t delay, const char *s, uint16_t len){ outq_t *q=&outq[oq_tail%48]; oq_tail++; q->due=g_tick+delay; q->len=len; q->pos=0; memcpy(q->data,s,len); }
static void esp_say(uint32_t delay, const char *s){ esp_send_at(delay,s,(uint16_t)strlen(s)); }
static void esp_reset_outq(void){ oq_head=oq_tail=0; }

static char rxline[512]; static int rxn = 0; static int data_mode = 0; static char databuf[512];
static void esp_rx_byte_to_mcu(uint8_t b){ USART2->SR |= (1U<<5); USART2->DR = b; USART2_RX_IRQHandler(); }

static void unescape(const char *in, char *out){ while(*in){ if(*in=='\\' && in[1]){ in++; } *out++=*in++; } *out=0; }

static void esp_http_reply(void){
  char resp[700]; int n = snprintf(resp,sizeof resp,"HTTP/1.0 %d X\r\nServer: nginx\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", esp_http_status,(int)strlen(esp_http_body),esp_http_body);
  int off=0, chunk=0; uint32_t d=350;
  while(off<n){ int take = (chunk==0)?180:(n-off>90?90:n-off); if(take>n-off) take=n-off; char hdr[40]; int hl=snprintf(hdr,sizeof hdr,"\r\n+IPD,%d:",take); char blk[900]; memcpy(blk,hdr,hl); memcpy(blk+hl,resp+off,take); esp_send_at(d,blk,(uint16_t)(hl+take)); off+=take; chunk++; d+=60; }
  esp_say(d+40,"CLOSED\r\n");
}

static void esp_command(const char *c){
  if(cmds_seen<64) snprintf(cmd_log[cmds_seen],96,"%s",c); cmds_seen++;
  if(esp_echo){ char e[200]; snprintf(e,sizeof e,"%s\r\n",c); esp_say(2,e); }
  if(!strcmp(c,"AT")) { esp_say(5,"\r\nOK\r\n"); }
  else if(!strcmp(c,"ATE0")) { esp_say(5,"\r\nOK\r\n"); esp_echo=false; }
  else if(!strncmp(c,"AT+CWMODE",9)||!strncmp(c,"AT+CWAUTOCONN",13)||!strncmp(c,"AT+CWDHCP",9)||!strncmp(c,"AT+CIPMUX",9)) esp_say(8,"\r\nOK\r\n");
  else if(!strcmp(c,"AT+CIFSR")) esp_say(10, esp_link ? "+CIFSR:STAIP,\"192.168.1.57\"\r\n+CIFSR:STAMAC,\"aa:bb:cc:dd:ee:ff\"\r\n\r\nOK\r\n" : "+CIFSR:STAIP,\"0.0.0.0\"\r\n+CIFSR:STAMAC,\"aa:bb:cc:dd:ee:ff\"\r\n\r\nOK\r\n");
  else if(!strcmp(c,"AT+CWJAP?")) { if(esp_link){ char m[120]; snprintf(m,sizeof m,"+CWJAP:\"%s\",\"aa:bb:cc:dd:ee:00\",6,-48\r\n\r\nOK\r\n",esp_ssid); esp_say(10,m);} else esp_say(10,"No AP\r\n\r\nOK\r\n"); }
  else if(!strcmp(c,"AT+CWQAP")) { esp_link=false; esp_say(20,"WIFI DISCONNECT\r\n\r\nOK\r\n"); }
  else if(!strcmp(c,"AT+CWLAP")) {
    char b[900]; int n=0;
    for(int i=0;i<NAPS;i++) n+=snprintf(b+n,sizeof b-n,"+CWLAP:(%d,\"%s\",%d,\"aa:bb:cc:dd:ee:%02x\",%d,-3,0)\r\n",APS[i].ecn,APS[i].ssid,APS[i].rssi,i,1+i);
    n+=snprintf(b+n,sizeof b-n,"\r\nOK\r\n"); esp_send_at(2500,b,(uint16_t)n); }
  else if(!strncmp(c,"AT+CWJAP=\"",10)) {
    char body[300]; snprintf(body,sizeof body,"%s",c+10); char *end=strstr(body,"\",\"");  /* naive split is fine: escapes make the first unescaped "," the separator */
    /* proper scan honouring backslash escapes */
    char ssid_raw[100]={0}, pwd_raw[200]={0}; const char *p=c+10; int i=0; while(*p && !(*p=='"')){ if(*p=='\\'&&p[1]) ssid_raw[i++]=*p++; ssid_raw[i++]=*p++; } p++; /* closing quote */ p+=2; /* ," */ i=0; while(*p && !(*p=='"' && p[1]==0)){ if(*p=='\\'&&p[1]) pwd_raw[i++]=*p++; pwd_raw[i++]=*p++; }
    (void)end; char ssid[60], pwd[120]; unescape(ssid_raw,ssid); unescape(pwd_raw,pwd);
    const ap_t *hit=NULL; for(int k=0;k<NAPS;k++) if(!strcmp(APS[k].ssid,ssid)){ hit=&APS[k]; break; }
    if(!hit) esp_say(1500,"+CWJAP:3\r\n\r\nFAIL\r\n");
    else if(strcmp(hit->pwd,pwd)) esp_say(2000,"+CWJAP:2\r\n\r\nFAIL\r\n");
    else { esp_link=true; snprintf(esp_ssid,sizeof esp_ssid,"%s",ssid); esp_say(2500,"WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n"); } }
  else if(!strncmp(c,"AT+CIPSNTPCFG",13)) esp_say(10,"\r\nOK\r\n");
  else if(!strcmp(c,"AT+CIPSNTPTIME?")) {
    sntp_calls++; char m[120];
    if(sntp_calls<3) snprintf(m,sizeof m,"+CIPSNTPTIME:Thu Jan 01 00:00:0%d 1970\r\nOK\r\n",sntp_calls);
    else { static const char*mn[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; static const char*dn[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      snprintf(m,sizeof m,"+CIPSNTPTIME:%s %s %02d %02d:%02d:%02d %d\r\nOK\r\n",dn[esp_utc.wday],mn[esp_utc.month-1],esp_utc.day,esp_utc.hour,esp_utc.minute,esp_utc.second,esp_utc.year); }
    esp_say(20,m); }
  else if(!strncmp(c,"AT+CIPSTART",11)) { if(!esp_link||esp_tcp_refuse) esp_say(300,"DNS Fail\r\n\r\nERROR\r\n"); else esp_say(300,"CONNECT\r\n\r\nOK\r\n"); }
  else if(!strncmp(c,"AT+CIPSEND=",11)) { data_mode=atoi(c+11); rxn=0; esp_say(15,"\r\nOK\r\n> "); }
  else if(!strcmp(c,"AT+CIPCLOSE")) esp_say(10,"CLOSED\r\nOK\r\n");
  else esp_say(5,"\r\nERROR\r\n");
}

static void esp_from_mcu(uint8_t b){
  if(g_tick < esp_ready_at) return;                      /* module not up yet: ignores everything */
  if(data_mode){ databuf[rxn++]=(char)b; if(rxn>=data_mode){ databuf[rxn]=0; data_mode=0; rxn=0;
      esp_say(10,"\r\nRecv bytes\r\n\r\nSEND OK\r\n"); esp_http_reply(); } return; }
  if(b=='\r') return;
  if(b=='\n'){ rxline[rxn]=0; if(rxn>0) esp_command(rxline); rxn=0; return; }
  if(rxn<(int)sizeof(rxline)-1) rxline[rxn++]=(char)b;
}

static void esp_tick(uint32_t now){
  /* release up to 11 bytes per millisecond (115200 baud) from the head item once it is due */
  int budget=11;
  while(budget>0 && oq_head<oq_tail){ outq_t *q=&outq[oq_head%48]; if(q->due>now) break; esp_rx_byte_to_mcu((uint8_t)q->data[q->pos++]); budget--; if(q->pos>=q->len) oq_head++; }
}
uint32_t HAL_GetTick(void){ g_tick++; esp_tick(g_tick); return g_tick; }
void HAL_Delay(uint32_t ms){ uint32_t end=g_tick+ms; while(g_tick<end) HAL_GetTick(); }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *h){ (void)h; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *h,const uint8_t*d,uint16_t n,uint32_t t){ (void)h;(void)t; for(int i=0;i<n;i++) esp_from_mcu(d[i]); g_tick += 1 + n/12; return HAL_OK; }


static void pump(uint32_t ms){ HAL_Delay(ms); }
/* buttons, in the order of KNOWN_REMOTE_BUTTONS in ir_remote_app.c */
enum { B_POWER=0,B_FUNC,B_MODE,B_MUTE,B_UP,B_DOWN,B_LEFT,B_RIGHT,B_PLAY,B_GEAR,B_PREV,B_NEXT,B_EQ,B_VOLM,B_VOLP,B_D1,B_D2,B_D3,B_D4,B_D5,B_D6,B_D7,B_D8,B_D9 };
static void press_btn(int b){ IR_Simulate_Button_Press((uint8_t)b); IR_Remote_Update(); pump(5); }
static int fails=0, checks=0;
#define CHECK(c,msg) do{ checks++; int r_=(c); printf("  [%s] %s\n", r_?"PASS":"FAIL", msg); if(!r_) fails++; }while(0)
static void world(void){ esp_ready_at=0; esp_echo=true; esp_link=false; esp_reset_outq(); USART2_ClearRX(); WiFi_Manager_Init(); wifi_connected=false; sntp_calls=0; cmds_seen=0; esp_tcp_refuse=false; }

int main(void){
  setvbuf(stdout,NULL,_IONBF,0);
  mmap((void*)0x40000000UL,0x30000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  mmap((void*)0xE0000000UL,0x100000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  memset(eeprom,0xFF,sizeof eeprom); tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; tz_saved=false; ds3231_present=true;
  rtc_store_std(2026,11,7,22,42,37); DS3231_GetTime(); world();
  Main_Screen_Init(); App_Menu_Init(); Time_Manager_Enter();

  printf("R1  main clock\n");
  current_state=STATE_MAIN_SCREEN; Main_Screen_Set_Gear_Selected(0);
  press_btn(B_PLAY);  CHECK(current_state==STATE_CALENDAR_APP,"PLAY on the clock opens the CALENDAR");
  press_btn(B_GEAR);  CHECK(current_state==STATE_MAIN_SCREEN,"GEAR leaves the calendar");
  press_btn(B_FUNC);  CHECK(current_state==STATE_ICON_MENU,"FUNC opens the APP MENU (it was hijacked by the calendar change before this fix)");
  current_state=STATE_MAIN_SCREEN; Main_Screen_Set_Gear_Selected(0);
  press_btn(B_MODE);  CHECK(current_state==STATE_ICON_MENU,"MODE opens the APP MENU");
  current_state=STATE_MAIN_SCREEN; Main_Screen_Set_Gear_Selected(0);
  press_btn(B_DOWN);  CHECK(Main_Screen_Is_Gear_Selected(),"DOWN selects the gear icon"); press_btn(B_PLAY); CHECK(current_state==STATE_ICON_MENU,"then PLAY opens the menu (unchanged)");

  printf("R2  calendar: every control\n");
  current_state=STATE_MAIN_SCREEN; Main_Screen_Set_Gear_Selected(0); press_btn(B_PLAY);
  { uint8_t m0=view_month; uint16_t y0=view_year;
    press_btn(B_RIGHT); CHECK(view_month==(m0%12)+1,"RIGHT = next month");
    press_btn(B_LEFT);  CHECK(view_month==m0,"LEFT = previous month");
    press_btn(B_NEXT);  CHECK(view_month==(m0%12)+1,"NEXT button also = next month"); press_btn(B_PREV); CHECK(view_month==m0,"PREV button also = previous month");
    press_btn(B_VOLP);  CHECK(view_month==(m0%12)+1,"VOL+ also = next month"); press_btn(B_VOLM); CHECK(view_month==m0,"VOL- also = previous month");
    press_btn(B_UP);    CHECK(view_year==y0+1,"UP = next year"); press_btn(B_DOWN); CHECK(view_year==y0,"DOWN = previous year");
    press_btn(B_RIGHT); press_btn(B_UP); press_btn(B_PLAY); CHECK(view_month==m0 && view_year==y0 && current_state==STATE_CALENDAR_APP,"PLAY = back to today's month");
    for(int i=0;i<14;i++) press_btn(B_RIGHT); CHECK(view_year==y0+2 && view_month==1,"14 months on from Nov 2026 is Jan 2028: months roll over into the next year");
    press_btn(B_FUNC); CHECK(current_state==STATE_ICON_MENU,"FUNC jumps to the menu from the calendar");
    current_state=STATE_CALENDAR_APP; press_btn(B_D4); CHECK(current_state==STATE_FAN_APP,"number button 4 jumps to the Fan app (global shortcut)"); }

  printf("R3  Set Time > zone > save, with the remote only\n");
  current_state=STATE_MAIN_SCREEN; press_btn(B_D2); CHECK(current_state==STATE_SET_TIME_APP,"number 2 opens Set Time"); Time_Manager_Enter();
  for(int i=0;i<7;i++) press_btn(B_RIGHT);  press_btn(B_PLAY);
  CHECK(current_state==STATE_TIMEZONE_APP,"RIGHT x7 reaches the new 'zone' button; PLAY opens the Time Zone screen");
  press_btn(B_GEAR); CHECK(current_state==STATE_SET_TIME_APP,"GEAR backs out to Set Time");
  press_btn(B_PLAY); CHECK(current_state==STATE_TIMEZONE_APP,"back on Set Time the cursor is still on 'zone', so PLAY re-opens it");
  for(int i=0;i<4;i++) press_btn(B_UP);     /* offset +60 min */
  press_btn(B_RIGHT); press_btn(B_UP);      /* DST rule -> next */
  press_btn(B_RIGHT);                       /* SAVE */
  press_btn(B_PLAY);
  CHECK(current_state==STATE_SET_TIME_APP,"SAVE returns to Set Time");
  CHECK(tz_config.std_offset_min==-240 && tz_config.rule==TZ_DST_EU && tz_saved,"offset +1 h and the next rule were applied with UP presses");
  { uint8_t *b=&eeprom[EEPROM_TZ_ADDR]; CHECK(b[0]==0xB3 && b[1]==TZ_DST_EU,"and written to the EEPROM"); }
  tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; TZ_Save(); pump(20);
  /* cancel path: RIGHT x8 = cancel */
  Time_Manager_Enter(); for(int i=0;i<8;i++) press_btn(B_RIGHT); press_btn(B_PLAY); CHECK(current_state==STATE_ICON_MENU,"cancel button still works (unchanged)");

  printf("R4  Wi-Fi with the remote only\n");
  world(); current_state=STATE_MAIN_SCREEN; press_btn(B_D1); CHECK(current_state==STATE_WIFI_APP,"number 1 opens Wi-Fi");
  esp_sync(6); press_btn(B_RIGHT); press_btn(B_PLAY); CHECK(esp01s_scanned_count==4,"RIGHT to SCAN, PLAY: scan lists the real networks");
  press_btn(B_LEFT); CHECK(wifi_footer_focus==0,"LEFT returns to the list"); press_btn(B_DOWN); press_btn(B_UP);
  press_btn(B_PLAY); CHECK(current_state==STATE_WIFI_PASSWORD && !strcmp(target_ssid,"HomeNet"),"PLAY on a network opens the password editor");
  press_btn(B_D3); press_btn(B_D1); press_btn(B_D9);
  CHECK(current_state==STATE_WIFI_PASSWORD && !strcmp(pwd_buffer,"319"),"number buttons TYPE digits in the editor (before the fix they jumped to other apps)");
  press_btn(B_GEAR); CHECK(!strcmp(pwd_buffer,"31"),"GEAR = backspace"); 
  press_btn(B_UP); CHECK(pwd_len==3,"UP adds/changes a character");
  press_btn(B_LEFT); press_btn(B_D7); CHECK(pwd_buffer[1]=='7' ,"cursor moved with LEFT; a digit overwrites there");
  { const char *good="Pa$$w0rd!,\"x\\y"; snprintf(pwd_buffer,sizeof pwd_buffer,"%s",good); pwd_len=(uint8_t)strlen(good); pwd_cursor=pwd_len; pwd_focus=0; }
  press_btn(B_RIGHT); CHECK(pwd_focus==1,"RIGHT past the last character selects CONNECT"); press_btn(B_PLAY);
  CHECK(wifi_connected && current_state==STATE_WIFI_APP,"PLAY connects and returns to the network list");
  tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; TZ_Save(); pump(20);
  rtc_store_std(2026,1,1,6,0,0); esp_utc=(wnet_time_t){2026,11,10,2,15,42,2}; sntp_calls=0;
  press_btn(B_RIGHT); press_btn(B_RIGHT); press_btn(B_PLAY);     /* SCAN -> SYNC */
  { int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(Y==2026&&M==11&&D==9&&h==21,"RIGHT RIGHT PLAY on SYNC sets time and date from the network"); }
  press_btn(B_RIGHT); press_btn(B_PLAY); CHECK(!wifi_connected,"RIGHT PLAY on DISC disconnects");
  press_btn(B_RIGHT); press_btn(B_PLAY); CHECK(current_state==STATE_ICON_MENU,"RIGHT PLAY on BACK returns to the menu");
  press_btn(B_GEAR);  /* whatever the menu does with GEAR */
  current_state=STATE_WIFI_APP; wifi_footer_focus=0; press_btn(B_GEAR); CHECK(wifi_footer_focus==4,"GEAR jumps to BACK on the Wi-Fi screen"); press_btn(B_PLAY); CHECK(current_state==STATE_ICON_MENU,"and PLAY leaves");

  printf("R5  weather with the remote only\n");
  world(); esp_link=true; wifi_connected=true; esp01s_state=ESP01S_STATE_GOT_IP; wx_have=false;
  snprintf(esp_http_body,sizeof esp_http_body,"-3\xC2\xB0""C;90%%;Snow;Ottawa, Canada\n");
  current_state=STATE_MAIN_SCREEN; press_btn(B_D8); CHECK(current_state==STATE_WEATHER_APP,"number 8 opens Weather (it did nothing before this fix)");
  T_dummy: ;
  press_btn(B_PLAY); CHECK(wx_have && wx.temp==-3 && !strcmp(wx.cond,"SNOW"),"PLAY on the weather screen fetches it");
  press_btn(B_GEAR); CHECK(current_state==STATE_ICON_MENU,"GEAR leaves");

  printf("R6  alarm\n");
  current_state=STATE_CALENDAR_APP; is_alarm_ringing=1; press_btn(B_GEAR);
  CHECK(is_alarm_ringing==0 && current_state==STATE_CALENDAR_APP,"any button silences a ringing alarm and does nothing else");

  printf("\n%d checks, %d failed -> %s\n",checks,fails,fails?"FAILURES":"ALL PASSED");
  return fails?1:0;
}
