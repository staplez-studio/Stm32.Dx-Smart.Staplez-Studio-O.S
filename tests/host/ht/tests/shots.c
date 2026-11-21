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


#include "calendar_app.h"
#include "timezone_app.h"
#include "time_manager.h"
#include "main_screen.h"
static int shot_n=0;
static void snap(const char *name){ char p[96]; snprintf(p,sizeof p,"/tmp/wsim/out/%02d_%s.bin",shot_n++,name); FILE*f=fopen(p,"wb"); fwrite(sh1106_buffer,1,1024,f); fclose(f); }
static uint32_t T=100000;
int main(void){
  mmap((void*)0x40000000UL,0x30000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  mmap((void*)0xE0000000UL,0x100000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  memset(eeprom,0xFF,sizeof eeprom); tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; ds3231_present=true;

  /* --- November (standard time) --- */
  rtc_store_std(2026,11,7,22,42,37); DS3231_GetTime();
  Main_Screen_Init(); current_state=STATE_MAIN_SCREEN; T+=500; Main_Screen_Process(0,T); snap("main_standard_time");
  Calendar_App_Enter(); current_state=STATE_CALENDAR_APP; T+=500; Calendar_App_Process(0,T); snap("calendar_nov2026");
  /* --- July (daylight time) --- */
  rtc_store_std(2027,7,14,9,30,5); DS3231_GetTime();
  T+=500; Main_Screen_Process(0,T); snap("main_daylight_time");
  Calendar_App_Enter(); T+=500; Calendar_App_Process(0,T); snap("calendar_jul2027_dst");
  /* a genuine six-row month: January 2028 starts on a Saturday and has 31 days */
  Calendar_App_Enter(); for(int i=0;i<6;i++) Calendar_App_Process('6',T+=10); snap("calendar_six_rows_jan2028");
  /* set time and zone screens */
  current_state=STATE_SET_TIME_APP; Time_Manager_Enter(); T+=500; Time_Manager_Process(0,T); for(int i=0;i<7;i++) Time_Manager_Process('6',T+=10); Time_Manager_Process(0,T+=100); snap("set_time_zone_button");
  Timezone_App_Enter(); current_state=STATE_TIMEZONE_APP; T+=500; Timezone_App_Process(0,T); snap("zone_default");
  Timezone_App_Process('2',T+=10); Timezone_App_Process('2',T+=10); Timezone_App_Process('2',T+=10); Timezone_App_Process('6',T+=10); Timezone_App_Process('2',T+=10); snap("zone_edited");
  /* Wi-Fi: scan list, connected */
  reset_world: ;
  esp_ready_at=0; esp_echo=true; esp_link=false; esp_reset_outq(); USART2_ClearRX(); WiFi_Manager_Init(); current_state=STATE_WIFI_APP;
  WiFi_Manager_Process(0,T+=600); snap("wifi_before_scan");
  esp_sync(6); ESP01S_ScanNetworks(); wifi_footer_focus=0; WiFi_Manager_Process(0,T+=100); snap("wifi_scan_results");
  wifi_footer_focus=0; wifi_selected_index=0; WiFi_Manager_Process('#',T+=100);          /* open the password screen */
  { const char *pw="MyHomeWifi#2027!"; for(const char*p=pw;*p;p++){ int g=0; char cur=next_char('\0',1); int pr=1; while(cur!=*p&&g++<200){cur=next_char(cur,1);pr++;} for(int i=0;i<pr;i++) WiFi_Manager_Process('2',T+=10); WiFi_Manager_Process('#',T+=10);} }
  snap("wifi_password_typing");
  for(int i=0;i<10;i++){ WiFi_Manager_Process('2',T+=10); WiFi_Manager_Process('#',T+=10);} snap("wifi_password_long_scrolled");
  /* connected */
  esp_link=true; snprintf(esp_ssid,sizeof esp_ssid,"HomeNet"); wifi_connected=true; esp01s_state=ESP01S_STATE_GOT_IP; snprintf(esp01s_ip_address,sizeof esp01s_ip_address,"192.168.1.57"); snprintf(esp01s_status_msg,sizeof esp01s_status_msg,"TIME SET 09:30A");
  current_state=STATE_WIFI_APP; wifi_footer_focus=2; WiFi_Manager_Process(0,T+=600); snap("wifi_connected_sync_focus");
  /* weather */
  current_state=STATE_WEATHER_APP; wx_have=false; wifi_connected=false; T+=3000; Weather_App_Process(0,T); snap("weather_no_data_offline");
  wifi_connected=true; esp_ready_at=0; esp_link=true; esp01s_state=ESP01S_STATE_GOT_IP;
  static const char *bodies[]={"+2\xC2\xB0""C;80%;Light rain;Toronto, Canada\n","-12\xC2\xB0""C;71%;Light snow;Ottawa, Canada\n","+24\xC2\xB0""C;40%;Sunny;Madrid, Spain\n","+8\xC2\xB0""C;93%;Thunderstorm with rain;Vancouver\n"};
  static const char *names[]={"weather_rain","weather_snow","weather_sunny","weather_storm_long_text"};
  for(int i=0;i<4;i++){ snprintf(esp_http_body,sizeof esp_http_body,"%s",bodies[i]); wx_have=false; T+=3000; Weather_App_Process(0,T); T+=100; Weather_App_Process(0,T); snap(names[i]); }
  return 0; }
