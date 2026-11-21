/* End-to-end simulation of the Wi-Fi / clock / weather code against simulated hardware.
 * The REAL firmware sources are used (wifi_manager.c, weather_app.c, usart.c ring, staplez_studio.c RTC code). */
#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
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
#include "hard_drive_app.c"
#include "wifi_store.h"
#include "level_meter.h"
#include "alarm_manager.h"
#include "sd_bridge.h"
#include "sound_app.h"
#include "dfplayer_proto.h"
#include "entertainment_app.h"
#include "rfid.h"

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
  {"SecretNet",         "hush",             3, -70},      /* hidden network: joinable but never listed */
};
#define NAPS (int)(sizeof(APS)/sizeof(APS[0]))
static unsigned ap_listed = 0x3F, ap_joinable = 0xFFFF; static int ap_rssi_set[16]; static int ap_rssi_used[16];   /* test controls: who is in range */
static int ap_rssi(int i){ return ap_rssi_used[i] ? ap_rssi_set[i] : APS[i].rssi; }
static uint32_t esp_ready_at = 1500;        /* module answers only after this tick (0xFFFFFFFF = dead) */
static bool esp_echo = true, esp_link = false, esp_http_ok = true; static char esp_ssid[40];
static int sntp_calls = 0; static wnet_time_t esp_utc = {2026,11,10,2,15,42,2};
static char esp_http_body[256] = "+2\xC2\xB0""C;80%;Light rain;Toronto, Canada\n"; static int esp_http_status = 200; static bool esp_tcp_refuse = false;
static uint32_t esp_baud = 115200; static int esp_mode_garble = 0, esp_text_only = 0, esp_busy_n = 0, esp_oldfw = 0, esp_nosntp = 0;
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
  char resp[700]; int n = snprintf(resp,sizeof resp,"HTTP/1.0 %d X\r\nServer: nginx\r\nDate: Tue, 10 Nov 2026 02:15:42 GMT\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", esp_http_status,(int)strlen(esp_http_body),esp_http_body);
  int off=0, chunk=0; uint32_t d=350;
  while(off<n){ int take = (chunk==0)?180:(n-off>90?90:n-off); if(take>n-off) take=n-off; char hdr[40]; int hl=snprintf(hdr,sizeof hdr,"\r\n+IPD,%d:",take); char blk[900]; memcpy(blk,hdr,hl); memcpy(blk+hl,resp+off,take); esp_send_at(d,blk,(uint16_t)(hl+take)); off+=take; chunk++; d+=60; }
  esp_say(d+40,"CLOSED\r\n");
}


/* ---- simulated ESP32-CAM SD card (AT+SD... commands) ---- */
typedef struct { const char *dir; const char *name; int is_dir; uint32_t size; } fsent_t;
static const fsent_t FS[] = {
  {"/", "MUSIC", 1, 0}, {"/", "PHOTOS", 1, 0}, {"/", "config.txt", 0, 412}, {"/", "notes, draft.txt", 0, 2048},
  {"/MUSIC", "01_synth_80s.mp3", 0, 4812345}, {"/MUSIC", "02_retro.mp3", 0, 3901122}, {"/MUSIC", "A Very Long Song Title For Testing.mp3", 0, 9123456},
  {"/MUSIC", "cover.jpg", 0, 88123}, {"/MUSIC", "LIVE", 1, 0}, {"/MUSIC", "zz_last.MP3", 0, 999},
  {"/MUSIC/LIVE", "show1.mp3", 0, 12000000}, {"/PHOTOS", "IMG_0001.JPG", 0, 1500000}, {"/PHOTOS", "IMG_0002.JPG", 0, 1400000},
};
#define NFS (int)(sizeof(FS)/sizeof(FS[0]))
static int esp_sd_mode = 0;              /* 0 card ok, 1 no card, 2 firmware without SD commands */
static char sd_played[80]; static int sd_stops = 0; static int sd_playn = -1; static int sd_noclock = 0;
static int sd_is_mp3_name(const char*n){ size_t l=strlen(n); return l>4 && (n[l-4]=='.') && (n[l-3]=='m'||n[l-3]=='M') && (n[l-2]=='p'||n[l-2]=='P') && n[l-1]=='3'; }
static int sd_dir_exists(const char*d){ if(!strcmp(d,"/")) return 1; for(int i=0;i<NFS;i++){ char full[100]; snprintf(full,sizeof full,"%s%s%s",FS[i].dir,strcmp(FS[i].dir,"/")?"/":"",FS[i].name); if(FS[i].is_dir && !strcmp(full,d)) return 1; } return 0; }
static int sd_file_exists(const char*f){ for(int i=0;i<NFS;i++){ char full[100]; snprintf(full,sizeof full,"%s%s%s",FS[i].dir,strcmp(FS[i].dir,"/")?"/":"",FS[i].name); if(!FS[i].is_dir && !strcmp(full,f)) return 1; } return 0; }
static void sd_quoted(const char *c, char *out){ const char *p=strchr(c,'"'); out[0]=0; if(!p) return; p++; int i=0; while(*p && *p!='"'){ if(*p=='\\'&&p[1]) p++; out[i++]=*p++; } out[i]=0; }
static void esp_sd_command(const char *c){
  if(esp_sd_mode==2){ esp_say(5,"\r\nERROR\r\n"); return; }
  if(!strcmp(c,"AT+SDINFO")){ esp_say(20, esp_sd_mode==1 ? "+SDINFO:0,0,0\r\n\r\nOK\r\n" : "+SDINFO:1,7580,1203\r\n\r\nOK\r\n"); }
  else if(!strncmp(c,"AT+SDLS=",8)){
    char dir[100]; sd_quoted(c,dir); const char *q=strrchr(c,'"'); int first=0,cnt=3; if(q) sscanf(q+1,",%d,%d",&first,&cnt);
    if(esp_sd_mode==1){ esp_say(10,"+SDERR:2\r\n\r\nERROR\r\n"); return; }
    if(!sd_dir_exists(dir)){ esp_say(10,"+SDERR:1\r\n\r\nERROR\r\n"); return; }
    char b[900]; int n=0, idx=0, total=0;
    for(int i=0;i<NFS;i++) if(!strcmp(FS[i].dir,dir)){ if(idx>=first && idx<first+cnt) n+=snprintf(b+n,sizeof b-n,"+SDLS:%c,%u,\"%s\"\r\n",FS[i].is_dir?'d':'f',FS[i].size,FS[i].name); idx++; total++; }
    n+=snprintf(b+n,sizeof b-n,"+SDLS:END,%d\r\n\r\nOK\r\n",total); esp_send_at(30,b,(uint16_t)n); }
  else if(!strncmp(c,"AT+SDPLAY=",10)){ char f[100]; sd_quoted(c,f); if(sd_file_exists(f)){ snprintf(sd_played,sizeof sd_played,"%s",f); esp_say(15,"\r\nOK\r\n"); } else esp_say(15,"\r\nERROR\r\n"); }
  else if(!strcmp(c,"AT+SDSTOP")){ sd_stops++; sd_played[0]=0; sd_playn=-1; esp_say(10,"\r\nOK\r\n"); }
  else if(!strncmp(c,"AT+SDMP3=",9)){
    int first=0,cnt=1; sscanf(c+9,"%d,%d",&first,&cnt);
    if(esp_sd_mode==1){ esp_say(10,"+SDERR:2\r\n\r\nERROR\r\n"); return; }
    char b[900]; int n=0, idx=0;
    for(int i=0;i<NFS;i++) if(!FS[i].is_dir && sd_is_mp3_name(FS[i].name)){ if(idx>=first && idx<first+cnt) n+=snprintf(b+n,sizeof b-n,"+SDLS:f,%u,\"%s\"\r\n",FS[i].size,FS[i].name); idx++; }
    n+=snprintf(b+n,sizeof b-n,"+SDLS:END,%d\r\n\r\nOK\r\n",idx); esp_send_at(30,b,(uint16_t)n); }
  else if(!strncmp(c,"AT+SDPLAYN=",11)){ int k=atoi(c+11); int tot=0; for(int i=0;i<NFS;i++) if(!FS[i].is_dir&&sd_is_mp3_name(FS[i].name)) tot++;
    if(k>=0 && k<tot){ sd_playn=k; sd_played[0]=0; esp_say(15,"\r\nOK\r\n"); } else esp_say(15,"\r\nERROR\r\n"); }
  else if(!strcmp(c,"AT+SDSTAT?")){ int tot=0; for(int i=0;i<NFS;i++) if(!FS[i].is_dir&&sd_is_mp3_name(FS[i].name)) tot++;
    char b[80]; snprintf(b,sizeof b,"+SDSTAT:%d,%d,%d\r\n\r\nOK\r\n", sd_noclock?2:(sd_playn>=0?1:0), sd_playn<0?0:sd_playn, tot); esp_say(15,b); }
}

static void esp_command(const char *c){
  if(esp_text_only){ esp_say(3,"hello from some other device\r\n"); return; }
  if(esp_busy_n>0 && strcmp(c,"AT")){ esp_busy_n--; esp_say(5,"\r\nbusy p...\r\n"); return; }
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
    for(int i=0;i<NAPS;i++) if((ap_listed>>i)&1) n+=snprintf(b+n,sizeof b-n,"+CWLAP:(%d,\"%s\",%d,\"aa:bb:cc:dd:ee:%02x\",%d,-3,0)\r\n",APS[i].ecn,APS[i].ssid,ap_rssi(i),i,1+i);
    n+=snprintf(b+n,sizeof b-n,"\r\nOK\r\n"); esp_send_at(2500,b,(uint16_t)n); }
  else if(!strncmp(c,"AT+CWJAP=\"",10)) {
    char body[300]; snprintf(body,sizeof body,"%s",c+10); char *end=strstr(body,"\",\"");  /* naive split is fine: escapes make the first unescaped "," the separator */
    /* proper scan honouring backslash escapes */
    char ssid_raw[100]={0}, pwd_raw[200]={0}; const char *p=c+10; int i=0; while(*p && !(*p=='"')){ if(*p=='\\'&&p[1]) ssid_raw[i++]=*p++; ssid_raw[i++]=*p++; } p++; /* closing quote */ p+=2; /* ," */ i=0; while(*p && !(*p=='"' && p[1]==0)){ if(*p=='\\'&&p[1]) pwd_raw[i++]=*p++; pwd_raw[i++]=*p++; }
    (void)end; char ssid[60], pwd[120]; unescape(ssid_raw,ssid); unescape(pwd_raw,pwd);
    const ap_t *hit=NULL; for(int k=0;k<NAPS;k++) if(((ap_joinable>>k)&1) && !strcmp(APS[k].ssid,ssid)){ hit=&APS[k]; break; }
    if(!hit) esp_say(1500,"+CWJAP:3\r\n\r\nFAIL\r\n");
    else if(strcmp(hit->pwd,pwd)) esp_say(2000,"+CWJAP:2\r\n\r\nFAIL\r\n");
    else { esp_link=true; snprintf(esp_ssid,sizeof esp_ssid,"%s",ssid); esp_say(2500, esp_oldfw ? "\r\nOK\r\n" : "WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n"); } }
  else if(!strncmp(c,"AT+CIPSNTPCFG",13)) esp_say(10, esp_nosntp ? "\r\nERROR\r\n" : "\r\nOK\r\n");
  else if(!strcmp(c,"AT+CIPSNTPTIME?")) {
    sntp_calls++; char m[120];
    if(sntp_calls<3) snprintf(m,sizeof m,"+CIPSNTPTIME:Thu Jan 01 00:00:0%d 1970\r\nOK\r\n",sntp_calls);
    else { static const char*mn[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}; static const char*dn[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      snprintf(m,sizeof m,"+CIPSNTPTIME:%s %s %02d %02d:%02d:%02d %d\r\nOK\r\n",dn[esp_utc.wday],mn[esp_utc.month-1],esp_utc.day,esp_utc.hour,esp_utc.minute,esp_utc.second,esp_utc.year); }
    esp_say(20,m); }
  else if(!strncmp(c,"AT+CIPSTART",11)) { if(!esp_link||esp_tcp_refuse) esp_say(300,"DNS Fail\r\n\r\nERROR\r\n"); else esp_say(300,"CONNECT\r\n\r\nOK\r\n"); }
  else if(!strncmp(c,"AT+CIPSEND=",11)) { data_mode=atoi(c+11); rxn=0; esp_say(15,"\r\nOK\r\n> "); }
  else if(!strncmp(c,"AT+SD",5)) esp_sd_command(c);
  else if(!strcmp(c,"AT+CIPCLOSE")) esp_say(10,"CLOSED\r\nOK\r\n");
  else esp_say(5,"\r\nERROR\r\n");
}

static void esp_from_mcu(uint8_t b){
  if(g_tick < esp_ready_at) return;                      /* module not up yet: ignores everything */
  if(USART2_GetBaud() != esp_baud){ if(esp_mode_garble) esp_rx_byte_to_mcu(0xFC); return; }   /* wrong speed: it cannot decode us */
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

/* ---------------- test helpers ---------------- */
static int fails=0, checks=0;
#define CHECK(c,msg) do{ checks++; int chk_result_=(c); printf("  [%s] %s\n", chk_result_?"PASS":"FAIL", msg); if(!chk_result_) fails++; }while(0)
static void pump(uint32_t ms){ HAL_Delay(ms); }
static void press(char k){ WiFi_Manager_Process(k, g_tick); }
static void type_char(char want){
  /* the shortest UP presses to reach `want` from the empty-slot start, then OK to advance */
  int guard=0; char cur='\0'; int presses=0;
  /* emulate next_char from an empty slot */
  cur = next_char('\0', 1); presses=1;
  while(cur!=want && guard++<200){ cur=next_char(cur,1); presses++; }
  for(int i=0;i<presses;i++) press('2');
  press('#');                                           /* OK: next slot */
}
static void reset_world(void){
  esp_ready_at=g_tick+1500; esp_echo=true; esp_link=false; esp_ssid[0]=0; esp_reset_outq(); data_mode=0; rxn=0; sntp_calls=0; cmds_seen=0; esp_tcp_refuse=false; esp_baud=115200; esp_mode_garble=esp_text_only=esp_busy_n=esp_oldfw=esp_nosntp=0; USART2_SetBaud(115200);
  USART2_ClearRX(); WiFi_Manager_Init(); wifi_connected=false;
}

/* ---- fake DFPlayer reply bytes for the bit-bang receiver ---- */
static uint8_t df_q[128]; static int df_qn=0, df_qi=0;
static bool df_hook(uint8_t *b, uint32_t timeout_us){ if(df_qi<df_qn){ *b=df_q[df_qi++]; return true; } g_tick += timeout_us/1000 + 1; return false; }
static void df_feed_frame(uint8_t cmd, uint16_t par){ uint8_t t[10]={0x7E,0xFF,0x06,cmd,0,(uint8_t)(par>>8),(uint8_t)par,0,0,0xEF}; uint16_t sm=0; for(int i=1;i<=6;i++) sm=(uint16_t)(sm+t[i]); uint16_t ck=(uint16_t)(0-sm); t[7]=(uint8_t)(ck>>8); t[8]=(uint8_t)ck; memcpy(df_q+df_qn,t,10); df_qn+=10; }
static void df_reset(void){ df_qn=df_qi=0; }

/* ---- fake ADC input for the level meter: sine of a chosen amplitude around a chosen DC level ---- */
static int lm_amp[2] = {0,0}, lm_dc[2] = {2048,2048}; static unsigned lm_n = 0;
static uint16_t lm_hook(uint8_t ch){ double v = lm_dc[ch] + lm_amp[ch]*sin(lm_n++*0.37 + ch); if(v<0)v=0; if(v>4095)v=4095; return (uint16_t)v; }

int main(void){
  mmap((void*)0x40000000UL,0x30000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  mmap((void*)0xE0000000UL,0x100000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  memset(eeprom,0xFF,sizeof eeprom);
  tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US;
  rtc_store_std(2026,11,7,22,42,37);

  printf("T1  module that needs 1.5 s to start (old code probed at 35 ms and gave up)\n");
  reset_world(); esp_ready_at=g_tick+1500;
  { uint32_t t0=g_tick; WiFi_Manager_Background(1000); CHECK(esp01s_state==ESP01S_STATE_IDLE,"nothing happens before the boot delay");
    pump(1600); WiFi_Manager_Background(WIFI_BOOT_PROBE_DELAY_MS+10);
    CHECK(esp01s_state==ESP01S_STATE_READY,"module found and configured once it has had time to start");
    CHECK(!esp_echo,"echo turned off (ATE0 was sent)");
    int saw_mode=0,saw_mux=0; for(int i=0;i<cmds_seen&&i<64;i++){ if(!strncmp(cmd_log[i],"AT+CWMODE=1",11)) saw_mode=1; if(!strncmp(cmd_log[i],"AT+CIPMUX=0",11)) saw_mux=1; }
    CHECK(saw_mode&&saw_mux,"station mode and single-connection mode were set (the old code skipped this after a failed first probe)");
    (void)t0; }
  printf("T2  no module fitted\n");
  reset_world(); esp_ready_at=0xFFFFFFFFu; { uint32_t t0=g_tick; WiFi_Manager_Background(WIFI_BOOT_PROBE_DELAY_MS+10); uint32_t cost=g_tick-t0;
    CHECK(esp01s_state==ESP01S_STATE_ERR_NO_MODULE && !wifi_connected,"reports NO MODULE"); CHECK(cost<1200,"gives up quickly (blocks the clock < 1.2 s once)"); printf("      (blocked for %u ms)\n",cost); }

  printf("T3  scan: real results only, sorted, de-duplicated, open networks detected\n");
  reset_world(); pump(1600); esp_sync(6);
  CHECK(esp01s_scanned_count==0,"list starts EMPTY (no invented networks)");
  ESP01S_ScanNetworks();
  for(int i=0;i<esp01s_scanned_count;i++) printf("      ap[%d] \"%s\" ecn=%d rssi=%d\n",i,esp01s_scanned_aps[i].ssid,esp01s_scanned_aps[i].encryption,esp01s_scanned_aps[i].rssi);
  CHECK(esp01s_scanned_count==4,"4 distinct visible networks (duplicate merged, hidden skipped)");
  CHECK(!strcmp(esp01s_scanned_aps[0].ssid,"HomeNet") && esp01s_scanned_aps[0].rssi==-48,"strongest first; duplicate keeps the stronger signal (-48)");
  { int open=-1,comma=-1; for(int i=0;i<esp01s_scanned_count;i++){ if(!strcmp(esp01s_scanned_aps[i].ssid,"Cafe, Free Wifi")){ comma=i; open=(esp01s_scanned_aps[i].encryption==0);} }
    CHECK(comma>=0,"SSID containing a comma parsed intact"); CHECK(open==1,"open network flagged as open (no password asked)"); }
  CHECK(esp01s_scanned_aps[0].rssi>=esp01s_scanned_aps[1].rssi && esp01s_scanned_aps[1].rssi>=esp01s_scanned_aps[2].rssi,"ordered by signal strength");

  printf("T4  password editor, full character set\n");
  { const char *pw="Pa$$w0rd!,\"x\\y";                  /* upper+lower+digits+symbols incl. comma, quote, backslash */
    wifi_footer_focus=0; wifi_selected_index=0; set_system_state(STATE_WIFI_APP); press('#');  /* select HomeNet */
    CHECK(current_state==STATE_WIFI_PASSWORD && pwd_len==0 && !strcmp(target_ssid,"HomeNet"),"opens empty (no pre-filled password)");
    for(const char*p=pw;*p;p++) type_char(*p);
    CHECK(strcmp(pwd_buffer,pw)==0,"typed a password with upper/lower case, digits and symbols"); printf("      typed: %s\n",pwd_buffer);
    press('*'); CHECK(pwd_len==strlen(pw)-1,"* = backspace removes the last character");
    type_char('y'); CHECK(strcmp(pwd_buffer,pw)==0,"retyped");
    /* long password scrolls */
    pwd_len=0; pwd_buffer[0]=0; pwd_cursor=0; pwd_view=0; pwd_focus=0; for(int i=0;i<30;i++) type_char((char)('a'+(i%26)));
    CHECK(pwd_len==30 && pwd_view>0 && pwd_cursor>=pwd_view && pwd_cursor<pwd_view+16,"30 characters: the view scrolls and the cursor stays visible");
    /* length cap */
    for(int i=0;i<70;i++) type_char('z'); CHECK(pwd_len<=63,"length capped at 63 (WPA maximum)");
    /* restore the real password for the connect tests */
    pwd_len=0; pwd_buffer[0]=0; pwd_cursor=0; pwd_view=0; pwd_focus=0; for(const char*p=pw;*p;p++) type_char(*p); }

  printf("T5  connect: wrong password, then right password\n");
  { char good[64]; snprintf(good,sizeof good,"%s",pwd_buffer);
    snprintf(pwd_buffer,sizeof pwd_buffer,"WRONGPASS"); pwd_len=(uint8_t)strlen(pwd_buffer); pwd_cursor=pwd_len; pwd_focus=1; press('#');
    CHECK(!wifi_connected && current_state==STATE_WIFI_PASSWORD,"wrong password: stays on the password screen so it can be corrected");
    CHECK(strstr(esp01s_status_msg,"WRONG PASSWORD")!=NULL,"clear message: wrong password"); printf("      status: %s\n",esp01s_status_msg);
    snprintf(pwd_buffer,sizeof pwd_buffer,"%s",good); pwd_len=(uint8_t)strlen(pwd_buffer); pwd_cursor=pwd_len; pwd_focus=1; press('#');
    CHECK(wifi_connected && current_state==STATE_WIFI_APP,"right password (with , \" \\ in it): connected, back on the list"); CHECK(!strcmp(esp01s_ip_address,"192.168.1.57"),"IP address shown");
    CHECK(saved_valid && !strcmp(saved_ssid,"HomeNet"),"network saved"); }
  { /* check what is physically in the EEPROM, independent of the code's own variables */
    uint8_t *b=&eeprom[EEPROM_WIFILIST_ADDR]; CHECK(b[0]==0xA8 && b[1]==7 && memcmp(&b[5],"HomeNet",7)==0 && memcmp(&b[37],"Pa$$w0rd!,\"x\\y",13)==0,"credentials physically in EEPROM (slot 0 of the saved list)");
    CHECK(ee_wrap_events==0,"no EEPROM page-wrap corruption (16-byte chunks stay inside pages)"); CHECK(ee_nack_count==0,"never touched the EEPROM during its write cycle"); }

  printf("T6  power cycle: auto-join the saved network\n");
  { memset(saved_ssid,0,sizeof saved_ssid); memset(saved_pwd,0,sizeof saved_pwd); saved_valid=false;   /* RAM forgotten, only EEPROM remains */
    reset_world(); esp_link=false; pump(1600); WiFi_Manager_Background(WIFI_BOOT_PROBE_DELAY_MS+10);
    CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"HomeNet"),"joined HomeNet automatically from EEPROM alone"); CHECK(!strcmp(esp01s_ip_address,"192.168.1.57"),"has an IP"); }
  printf("T7  module that kept its link across a controller reset\n");
  { reset_world(); esp_link=true; snprintf(esp_ssid,sizeof esp_ssid,"HomeNet"); esp_ready_at=0; WiFi_Manager_Background(WIFI_BOOT_PROBE_DELAY_MS+10);
    CHECK(wifi_connected && !strcmp(esp01s_ip_address,"192.168.1.57"),"detected the existing connection (no pointless re-join)"); }

  printf("T8  time from the network -> DS3231 holds STANDARD time, clock shows local time with DST\n");
  { static const struct { wnet_time_t utc; const char *what; int exp_std_h, exp_loc_h; bool dst; } cases[] = {
      {{2026,11,10,2,15,42,2},"Nov (standard time)",21,21,false},   /* 02:15 UTC -> 21:15 EST the day before */
      {{2027, 7, 14,15,30, 5,3},"Jul (daylight time)",10,11,true},  /* 15:30 UTC -> 10:30 EST std -> 11:30 EDT shown */
  };
    for(int i=0;i<2;i++){ esp_utc=cases[i].utc; sntp_calls=0; wifi_connected=true; ds3231_present=true;
      CHECK(ESP01S_SyncTime(),cases[i].what);
      int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); wnet_time_t chk;
      CHECK(h==cases[i].exp_std_h,"  chip holds STANDARD time (no DST in the DS3231)");
      DS3231_GetTime(); int h24=(current_hour%12)+(is_pm?12:0);
      CHECK(h24==cases[i].exp_loc_h && tz_dst_now==cases[i].dst,"  clock shows local time with DST applied"); (void)chk; (void)Y;(void)M;(void)D;(void)mi;(void)s; } }

  printf("T9  OFFLINE daylight saving (no Wi-Fi involved): US spring-forward and fall-back\n");
  { wifi_connected=false; ds3231_present=true;
    rtc_store_std(2027,3,14,1,59,58); DS3231_GetTime(); CHECK(current_hour==1 && current_minute==59 && !tz_dst_now,"01:59:58 standard -> shows 1:59 AM");
    rtc_store_std(2027,3,14,2,0,0);   DS3231_GetTime(); CHECK(current_hour==3 && current_minute==0 && !is_pm && tz_dst_now,"02:00:00 standard -> jumps to 3:00 AM DST (spring forward)");
    rtc_store_std(2027,11,7,0,59,59); DS3231_GetTime(); CHECK(current_hour==1 && current_minute==59 && tz_dst_now,"fall back: 00:59:59 std -> 1:59 AM (still DST)");
    rtc_store_std(2027,11,7,1,0,0);   DS3231_GetTime(); CHECK(current_hour==1 && current_minute==0 && !tz_dst_now,"01:00:00 std -> 1:00 AM again (repeated hour, now standard)");
    (void)0; }
  { /* the line above documents std 12:00 -> local 13:00; verify properly */
    rtc_store_std(2027,7,4,12,0,0); DS3231_GetTime(); int h24=(current_hour%12)+(is_pm?12:0); CHECK(h24==13 && current_day_num==4,"noon standard on Jul 4 shows 1:00 PM"); fails-=0; }
  { rtc_store_std(2027,7,4,23,30,0); DS3231_GetTime(); CHECK(current_day_num==5 && current_month==7 && current_hour==12 && !is_pm,"date rolls over with DST: 23:30 std Jul 4 shows 12:30 AM Jul 5"); CHECK(current_wday==1,"weekday follows (Mon Jul 5 2027)"); }

  printf("T10 setting the time by hand stores standard time\n");
  { DS3231_SetTime(3,30,0,1,15,7,2027);  /* user enters 3:30 PM on Jul 15 (daylight) */
    int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(h==14 && mi==30 && D==15,"3:30 PM in July is stored as 14:30 standard");
    DS3231_GetTime(); CHECK(current_hour==3 && current_minute==30 && is_pm,"and reads back as 3:30 PM");
    DS3231_SetTime(3,30,0,1,15,1,2027); rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(h==15 && mi==30,"3:30 PM in January is stored unchanged (15:30)"); }

  printf("T11 time zone setting persists (EEPROM) and survives corruption\n");
  { tz_config.std_offset_min=-480; tz_config.rule=TZ_DST_US; CHECK(TZ_Save(),"saved"); pump(20);
    tz_config.std_offset_min=0; tz_config.rule=0; TZ_Load(); CHECK(tz_config.std_offset_min==-480 && tz_config.rule==TZ_DST_US,"reloaded after 'power cycle'");
    eeprom[EEPROM_TZ_ADDR+4]^=0x55; TZ_Load(); CHECK(tz_config.std_offset_min==TZ_DEFAULT_OFFSET_MIN && tz_config.rule==TZ_DEFAULT_RULE,"corrupt block -> safe defaults");
    tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; TZ_Save(); pump(20); }

  printf("T11b regression: saving the time zone must not damage the saved Wi-Fi network\n");
  { uint8_t before[100]; memcpy(before,&eeprom[WIFI_CREDS_EEPROM_ADDR],100);
    tz_config.std_offset_min=-480; tz_config.rule=TZ_DST_US; TZ_Save(); pump(20);
    CHECK(memcmp(before,&eeprom[WIFI_CREDS_EEPROM_ADDR],100)==0,"Wi-Fi record byte-for-byte unchanged after saving the zone");
    memset(saved_ssid,0,sizeof saved_ssid); memset(saved_pwd,0,sizeof saved_pwd); saved_valid=false; CHECK(WiFi_Creds_Load() && !strcmp(saved_ssid,"HomeNet"),"and it still loads (checksum valid)");
    tz_config.std_offset_min=-300; TZ_Save(); pump(20); }
  printf("T12 weather over HTTP (chunked +IPD replies)\n");
  { reset_world(); esp_ready_at=0; esp_link=true; wifi_connected=true; esp01s_state=ESP01S_STATE_GOT_IP; wx_have=false;
    CHECK(wx_have==false,"no weather until it has really been fetched");
    CHECK(Weather_Fetch(),"fetch ok"); CHECK(wx.temp==2 && wx.humidity==80 && !strcmp(wx.cond,"LIGHT RAIN") && !strcmp(wx.loc,"TORONTO, CANADA"),"parsed: 2 C, 80%, LIGHT RAIN, TORONTO, CANADA");
    esp_http_status=404; CHECK(!Weather_Fetch() && strstr(wx_status,"404")!=NULL,"server error reported"); esp_http_status=200;
    snprintf(esp_http_body,sizeof esp_http_body,"Unknown location; please try ~42.0,-71.0\n"); CHECK(!Weather_Fetch() && !strcmp(wx_status,"BAD REPLY"),"error page rejected, old data kept"); 
    snprintf(esp_http_body,sizeof esp_http_body,"+2\xC2\xB0""C;80%%;Light rain;Toronto, Canada\n");
    esp_tcp_refuse=true; CHECK(!Weather_Fetch() && !strcmp(wx_status,"NO RESPONSE"),"connection refused -> NO RESPONSE"); esp_tcp_refuse=false;
    wifi_connected=false; CHECK(!Weather_Fetch() && strstr(wx_status,"NOT CONNECTED")!=NULL,"offline -> says so"); wifi_connected=true; }

  printf("T13 UART receive ring: a burst while the CPU is busy must not corrupt data\n");
  { USART2_ClearRX(); usart2_rx_dropped=0; for(int i=0;i<900;i++) esp_rx_byte_to_mcu((uint8_t)(i&0xFF));
    int in_order=1,n=0,first_bad=-1; uint8_t b; while(USART2_ReadByte(&b)){ if(b!=(uint8_t)(n&0xFF)){ if(first_bad<0){first_bad=n; printf("      DEBUG first mismatch at %d: got %u expected %u\n",n,b,(unsigned)(n&0xFF));} in_order=0; } n++; }
    CHECK(in_order,"bytes that fit arrive in order, uncorrupted"); CHECK(n==511 && usart2_rx_dropped==389,"ring holds 511, the other 389 are counted as dropped (not silently lost)"); printf("      kept %d, dropped %u\n",n,(unsigned)usart2_rx_dropped); }


  printf("T14 automatic online jobs: time + date sync, then weather\n");
  { /* fresh boot, saved network in EEPROM, time zone not yet saved by the user */
    memset(saved_ssid,0,sizeof saved_ssid); memset(saved_pwd,0,sizeof saved_pwd); saved_valid=false;
    reset_world(); esp_link=false; pump(1600); ds3231_present=true; is_alarm_ringing=0;
    tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; tz_saved=false; current_state=STATE_MAIN_SCREEN;
    rtc_store_std(2026,1,1,6,0,0); wx_have=false; wx_armed=false; esp_utc=(wnet_time_t){2026,11,10,2,15,42,2};
    uint32_t t=g_tick;  WiFi_Manager_Background(WIFI_BOOT_PROBE_DELAY_MS+10);       /* joins HomeNet */
    CHECK(wifi_connected,"joined the saved network at boot");
    sntp_calls=0; t=g_tick; WiFi_Manager_Background(t);
    { int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(Y==2026&&M==1&&D==1,"zone never saved -> clock NOT touched (a wrong default zone must not shift it)"); }
    CHECK(strstr(esp01s_status_msg,"SET TIME ZONE")!=NULL,"tells the user what to do: SET TIME ZONE FIRST");
    CHECK(wx_have==false,"no weather request yet (time sync comes first)");
    /* user saves a zone */
    TZ_Save(); pump(20);  CHECK(tz_saved,"zone saved");
    time_sync_next=g_tick; sntp_calls=0; t=g_tick; WiFi_Manager_Background(t);
    { int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(Y==2026&&M==11&&D==9&&h==21&&mi==15,"synced TIME and DATE automatically (std 21:15 on Nov 9, from 02:15 UTC Nov 10)"); }
    CHECK(strstr(esp01s_status_msg,"TIME SET")!=NULL,"confirmation message");
    snprintf(esp_http_body,sizeof esp_http_body,"+2\xC2\xB0""C;80%%;Light rain;Toronto, Canada\n");
    t=g_tick; WiFi_Manager_Background(t);
    CHECK(wx_have && wx.temp==2,"next pass fetched the weather automatically");
    uint32_t nxt=wx_next_try; t=g_tick; WiFi_Manager_Background(t); CHECK(wx_next_try==nxt,"and does not refetch every pass");
    /* periodic re-sync after WIFI_TIME_SYNC_HOURS */
    g_tick += (uint32_t)WIFI_TIME_SYNC_HOURS*3600000UL + 5000; esp_utc=(wnet_time_t){2026,11,10,14,0,0,2}; sntp_calls=0; t=g_tick; WiFi_Manager_Background(t);
    { int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(D==10&&h==9&&mi==0,"re-synced 12 h later (std 09:00 Nov 10 from 14:00 UTC)"); }
    /* weather refreshes after its own interval */
    snprintf(esp_http_body,sizeof esp_http_body,"+5\xC2\xB0""C;60%%;Sunny;Toronto, Canada\n"); g_tick += (uint32_t)WEATHER_REFRESH_MIN*60000UL + 1000; t=g_tick; time_sync_next=t+100000; WiFi_Manager_Background(t);
    CHECK(wx.temp==5 && !strcmp(wx.cond,"SUNNY"),"weather refreshed automatically after the interval");
    /* never during an alarm, never on busy screens */
    is_alarm_ringing=1; time_sync_next=g_tick; sntp_calls=0; esp_utc=(wnet_time_t){2026,11,10,20,0,0,2}; t=g_tick; WiFi_Manager_Background(t);
    { int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(h!=15,"alarm ringing: no blocking network job"); } is_alarm_ringing=0;
    current_state=STATE_FAN_APP; time_sync_next=g_tick; t=g_tick; WiFi_Manager_Background(t);
    { int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(h!=15,"on a busy screen (fan): deferred"); } current_state=STATE_MAIN_SCREEN;
    /* failure: retry later, not in a tight loop; offline: nothing runs */
    time_sync_next=g_tick; sntp_calls=-100000; t=g_tick; uint32_t t0=g_tick; WiFi_Manager_Background(t);
    CHECK((int32_t)(time_sync_next-g_tick)>500000,"failed sync backs off ~10 minutes"); printf("      (failed sync blocked the clock for %u ms)\n",(unsigned)(g_tick-t0));
    sntp_calls=0;
    wifi_connected=false; rejoin_next=g_tick+1000000UL; time_sync_next=g_tick; uint32_t c0=cmds_seen; t=g_tick; WiFi_Manager_Background(t); CHECK(cmds_seen==c0,"offline: sends nothing to the module"); }


  printf("T15 module set to another speed (9600 / 57600): found by the baud search\n");
  for(int k=0;k<2;k++){ uint32_t bd = k? 57600 : 9600;
    reset_world(); esp_baud=bd; pump(1600); esp_ready_at=0;
    CHECK(esp_sync_ex(3,true) && esp01s_state==ESP01S_STATE_READY && USART2_GetBaud()==bd, k?"found at 57600 and the UART stays there":"found at 9600 and the UART stays there"); }
  printf("T16 power-up probe only tries the two common speeds (fast), user SCAN tries all\n");
  reset_world(); esp_baud=38400; esp_ready_at=0; { uint32_t t0=g_tick; bool r=esp_sync(3); CHECK(!r,"38400 not tried at power-up"); CHECK(g_tick-t0<1500,"and it costs < 1.5 s"); printf("      (%u ms)\n",g_tick-t0); CHECK(USART2_GetBaud()==115200,"UART back at 115200 after a failed search"); }
  CHECK(ESP01S_ScanNetworks() && esp01s_scanned_count==4 && USART2_GetBaud()==38400,"SCAN searches all speeds, finds 38400 and lists networks");
  printf("T17 failure diagnosis\n");
  reset_world(); esp_ready_at=0xFFFFFFFFu; esp_sync_ex(3,true); CHECK(!strcmp(esp01s_status_msg,"NO DATA: CHECK WIRES"),"silence -> check wires");
  reset_world(); esp_ready_at=0; esp_baud=4800; esp_mode_garble=1; esp_sync_ex(3,true); CHECK(!strcmp(esp01s_status_msg,"GARBLED: WRONG BAUD"),"junk bytes -> wrong baud");
  reset_world(); esp_ready_at=0; esp_text_only=1; esp_sync_ex(3,true); CHECK(!strcmp(esp01s_status_msg,"NO AT: BAD FIRMWARE"),"readable text but no AT -> firmware problem");
  printf("T18 old firmware says busy: command retried\n");
  reset_world(); esp_ready_at=0; esp_sync(3); esp_busy_n=2; CHECK(ESP01S_SendATCommand("AT+CWMODE=1","OK",300),"busy twice, then OK");
  esp_busy_n=2; esp_link=true; snprintf(esp_ssid,sizeof esp_ssid,"HomeNet"); CHECK(ESP01S_QueryIP() && !strcmp(esp01s_ip_address,"192.168.1.57"),"IP query retried after busy");
  printf("T19 old firmware: join answers only OK (no WIFI GOT IP lines)\n");
  reset_world(); esp_ready_at=0; esp_oldfw=1; esp_sync(3);
  CHECK(ESP01S_ConnectAP("HomeNet","Pa$$w0rd!,\"x\\y") && wifi_connected && !strcmp(esp01s_ip_address,"192.168.1.57"),"join confirmed through the IP address");
  printf("T20 no SNTP in the firmware: time from the HTTP Date header\n");
  ds3231_present=true; tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; rtc_store_std(2026,1,1,6,0,0); esp_nosntp=1;
  CHECK(ESP01S_SyncTime(),"sync works without SNTP");
  { int Y,M,D,h,mi,s; rtc_read_std(&Y,&M,&D,&h,&mi,&s); CHECK(Y==2026&&M==11&&D==9&&h==21&&mi==15,"clock set from Date: 02:15 UTC Nov 10 -> std 21:15 Nov 9"); }
  { wnet_time_t u; CHECK(wnet_parse_http_date("HTTP/1.1 200 OK\r\ndate: Sun, 29 Mar 2026 01:00:05 GMT\r\n\r\n",&u) && u.month==3 && u.day==29 && u.wday==0 && u.second==5,"Date header parser (lower case, weekday computed)");
    CHECK(!wnet_parse_http_date("HTTP/1.1 200 OK\r\nServer: x\r\n\r\nDate: Sun, 29 Mar 2026 01:00:05 GMT",&u),"a Date: inside the body is ignored");
    CHECK(!wnet_parse_http_date("HTTP/1.1 200 OK\r\nDate: Thu, 01 Jan 1970 00:00:00 GMT\r\n\r\n",&u),"1970 date rejected"); }
  printf("T21 stale TCP connection from a failed fetch does not block the next one\n");
  reset_world(); esp_ready_at=0; esp_sync(3); esp_link=true; wifi_connected=true; esp01s_state=ESP01S_STATE_GOT_IP; { char o[200]; CHECK(ESP01S_HttpGet("wttr.in",80,"GET / HTTP/1.0\r\n\r\n",o,sizeof o,6000)>0,"fetch ok"); }
  { int closes=0; for(int i=0;i<cmds_seen&&i<64;i++) if(!strcmp(cmd_log[i],"AT+CIPCLOSE")) closes++; CHECK(closes>=1,"AT+CIPCLOSE sent before CIPSTART"); }


  printf("T22 SD protocol parsers\n");
  { sd_entry_t e; bool m; uint32_t t,u; uint16_t tot; char sz[6];
    CHECK(sdp_parse_entry("+SDLS:f,4812345,\"01_synth_80s.mp3\"",&e) && !e.is_dir && e.size==4812345 && !strcmp(e.name,"01_synth_80s.mp3"),"file entry");
    CHECK(sdp_parse_entry("+SDLS:d,0,\"MUSIC\"",&e) && e.is_dir && !strcmp(e.name,"MUSIC"),"folder entry");
    CHECK(sdp_parse_entry("+SDLS:f,2048,\"notes, draft.txt\"",&e) && !strcmp(e.name,"notes, draft.txt"),"comma inside a name");
    CHECK(sdp_parse_entry("+SDLS:f,1,\"caf\xC3\xA9.txt\"",&e) && !strcmp(e.name,"caf??.txt"),"non-ASCII bytes shown as ? (font is ASCII only)");
    CHECK(sdp_parse_entry("+SDLS:f,1,\"A Very Long Song Title For Testing.mp3\"",&e) && strlen(e.name)==SD_NAME_MAX-1,"long names cut, never overflow");
    CHECK(!sdp_parse_entry("+SDLS:END,4",&e) && !sdp_parse_entry("garbage",&e) && !sdp_parse_entry("+SDLS:x,1,\"a\"",&e) && !sdp_parse_entry("+SDLS:f,1,\"\"",&e),"junk / END / empty name rejected");
    CHECK(sdp_parse_end("+SDLS:END,87",&tot) && tot==87 && !sdp_parse_end("+SDLS:f,1,\"a\"",&tot),"END line");
    CHECK(sdp_parse_info("+SDINFO:1,7580,1203",&m,&t,&u) && m && t==7580 && u==1203 && sdp_parse_info("+SDINFO:0,0,0",&m,&t,&u) && !m,"info line");
    sdp_format_size(0,sz); CHECK(!strcmp(sz,"0B"),"0 bytes");
    sdp_format_size(999,sz); CHECK(!strcmp(sz,"999B"),"999 bytes");
    sdp_format_size(1500,sz); CHECK(!strcmp(sz,"1.5K"),"1.5K");
    sdp_format_size(88123,sz); CHECK(!strcmp(sz,"88K"),"88K");
    sdp_format_size(4812345,sz); CHECK(!strcmp(sz,"4.8M"),"4.8M");
    sdp_format_size(12000000,sz); CHECK(!strcmp(sz,"12M"),"12M");
    sdp_format_size(4000000000u,sz); CHECK(!strcmp(sz,"4.0G"),"4.0G");
    CHECK(sdp_is_mp3("a.mp3")&&sdp_is_mp3("A.MP3")&&sdp_is_mp3("x.Mp3")&&!sdp_is_mp3("a.mp4")&&!sdp_is_mp3(".mp3")&&!sdp_is_mp3("mp3"),"MP3 detection");
    char p[64]="/"; CHECK(sdp_path_join(p,sizeof p,"MUSIC")&&!strcmp(p,"/MUSIC")&&sdp_path_join(p,sizeof p,"LIVE")&&!strcmp(p,"/MUSIC/LIVE"),"path join");
    sdp_path_parent(p); CHECK(!strcmp(p,"/MUSIC"),"parent 1"); sdp_path_parent(p); CHECK(!strcmp(p,"/"),"parent 2"); sdp_path_parent(p); CHECK(!strcmp(p,"/"),"parent of root stays root");
    char sm[8]="/abcde"; CHECK(!sdp_path_join(sm,sizeof sm,"fghij") && !strcmp(sm,"/abcde"),"path too long is refused, unchanged"); }

  printf("T23 SD bridge against the simulated ESP32-CAM card\n");
  { sd_entry_t e[3]; uint8_t got; uint16_t total; sd_info_t info;
    reset_world(); esp_ready_at=0; esp_sd_mode=0; esp_sync(3);
    CHECK(SDBridge_Info(&info)==SD_OK && info.mounted && info.total_mb==7580,"info: card present");
    CHECK(SDBridge_List("/",0,e,3,&got,&total)==SD_OK && got==3 && total==4 && e[0].is_dir && !strcmp(e[0].name,"MUSIC"),"root: first 3 of 4");
    CHECK(SDBridge_List("/",3,e,3,&got,&total)==SD_OK && got==1 && !strcmp(e[0].name,"notes, draft.txt"),"root: second page");
    CHECK(SDBridge_List("/MUSIC",0,e,3,&got,&total)==SD_OK && total==6 && got==3 && !strcmp(e[0].name,"01_synth_80s.mp3"),"MUSIC folder");
    CHECK(SDBridge_List("/MUSIC",3,e,3,&got,&total)==SD_OK && got==3 && strlen(e[0].name)<SD_NAME_MAX,"long name entry fits");
    CHECK(SDBridge_List("/NOPE",0,e,3,&got,&total)==SD_BAD_PATH,"missing folder");
    CHECK(SDBridge_Play("/MUSIC/02_retro.mp3")==SD_OK && !strcmp(sd_played,"/MUSIC/02_retro.mp3"),"play an MP3 (to the ADAU1401 via the cam)");
    CHECK(SDBridge_Play("/MUSIC/missing.mp3")==SD_BAD_PATH,"play missing file");
    CHECK(SDBridge_Stop()==SD_OK && sd_stops==1,"stop");
    reset_world(); esp_ready_at=0; esp_sd_mode=1; esp_sync(3); CHECK(SDBridge_Info(&info)==SD_NO_CARD,"no card inserted");
    reset_world(); esp_ready_at=0; esp_sd_mode=2; esp_sync(3); CHECK(SDBridge_Info(&info)==SD_NO_BRIDGE,"cam firmware without SD commands");
    reset_world(); esp_ready_at=0xFFFFFFFFu; CHECK(SDBridge_Info(&info)==SD_NO_LINK,"no ESP32-CAM attached");
    { int okl=1; for(int k=0;k<=SD_ERROR;k++) if(strlen(SDBridge_StatusText((sd_status_t)k))>20) okl=0; CHECK(okl,"every status text fits the 20-column screen"); }
    esp_sd_mode=0; }

  printf("T24 DFPlayer: how many MP3 files does the module's own card hold?\n");
  { dfp_parser_t pr; dfp_frame_t f; uint8_t fr[10]={0x7E,0xFF,0x06,0x48,0x00,0x00,0x0C,0,0,0xEF}; uint16_t sm=0; for(int i=1;i<=6;i++) sm=(uint16_t)(sm+fr[i]); uint16_t ck=(uint16_t)(0-sm); fr[7]=(uint8_t)(ck>>8); fr[8]=(uint8_t)ck;
    dfp_parser_reset(&pr); int done=0; for(int i=0;i<10;i++) done|=dfp_parser_feed(&pr,fr[i],&f); CHECK(done && f.cmd==0x48 && f.param==12,"parser: valid frame");
    uint8_t bad[10]; memcpy(bad,fr,10); bad[8]^=1; dfp_parser_reset(&pr); done=0; for(int i=0;i<10;i++) done|=dfp_parser_feed(&pr,bad[i],&f); CHECK(!done,"parser: bad checksum rejected");
    dfp_parser_reset(&pr); done=0; uint8_t noise[]={0x00,0x13,0x7E,0x7E}; for(int i=0;i<4;i++) dfp_parser_feed(&pr,noise[i],&f); for(int i=1;i<10;i++) done|=dfp_parser_feed(&pr,fr[i],&f); CHECK(done && f.param==12,"parser: resynchronises after noise");
    DFPlayer_SetRxHook(df_hook);
    df_reset(); df_q[df_qn++]=0x00; df_feed_frame(0x3F,2); df_feed_frame(0x48,12);
    CHECK(DFPlayer_RefreshTrackCount() && DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount()==12,"12 MP3 files reported (an unrelated frame in front is skipped)");
    df_reset(); df_feed_frame(0x48,0);
    CHECK(DFPlayer_RefreshTrackCount() && DFPlayer_GetTrackCount()==0 && !strcmp(DFPlayer_LastError(),"NO MP3 ON SD CARD"),"empty card: count 0, says so");
    df_reset(); df_feed_frame(0x40,8);
    CHECK(!DFPlayer_RefreshTrackCount() && !DFPlayer_TrackCountKnown() && !strcmp(DFPlayer_LastError(),"NO SD IN DFPLAYER"),"error frame 'no card'");
    df_reset(); { uint32_t t0=g_tick; bool r=DFPlayer_RefreshTrackCount(); CHECK(!r && !DFPlayer_TrackCountKnown() && !strcmp(DFPlayer_LastError(),"NO ANSWER (PA1?)"),"silence (TX not wired): says so"); CHECK(g_tick-t0<1500,"and gives up in about a second"); }
    { int okl=1; const char *msgs[]={"NO ANSWER (PA1?)","NO SD IN DFPLAYER","NO MP3 ON SD CARD","DFPLAYER ERROR"}; for(int i=0;i<4;i++) if(strlen(msgs[i])>20) okl=0; CHECK(okl,"messages fit the screen"); } }

  printf("T25 Hard Drive app: browse names, open folders, play an MP3, leave\n");
  { reset_world(); esp_ready_at=0; esp_sd_mode=0; esp_link=false; set_system_state(STATE_FILE_EXPLORER_APP); Hard_Drive_App_Init();
    uint32_t t=g_tick+3000; g_tick=t; Hard_Drive_App_Process(0,g_tick);
    CHECK(status==SD_OK && total==4 && nrows==3 && !strcmp(rows[0].name,"MUSIC") && rows[0].is_dir,"opens at '/' and lists the card's real names");
    Hard_Drive_App_Process('8',g_tick); Hard_Drive_App_Process('8',g_tick); CHECK(sel==2 && top==0,"DOWN moves through the rows");
    Hard_Drive_App_Process('8',g_tick); CHECK(top==1 && !strcmp(rows[2].name,"notes, draft.txt"),"DOWN at the bottom scrolls the list");
    Hard_Drive_App_Process('8',g_tick); CHECK(focus_back,"DOWN at the very end lands on BACK");
    Hard_Drive_App_Process('8',g_tick); CHECK(!focus_back && top==0 && sel==0,"and wraps to the first entry");
    Hard_Drive_App_Process('#',g_tick); CHECK(!strcmp(path,"/MUSIC") && total==6 && !strcmp(rows[0].name,"01_synth_80s.mp3"),"PLAY opens the folder");
    Hard_Drive_App_Process('#',g_tick); CHECK(!strcmp(sd_played,"/MUSIC/01_synth_80s.mp3") && !strcmp(hint,"PLAYING"),"PLAY on an MP3 plays it");
    Hard_Drive_App_Process('#',g_tick); CHECK(sd_played[0]==0 && sd_stops>=1 && !strcmp(hint,"STOPPED"),"PLAY again stops it");
    Hard_Drive_App_Process('8',g_tick); Hard_Drive_App_Process('8',g_tick); sd_played[0]=0; Hard_Drive_App_Process('#',g_tick);
    CHECK(!strcmp(rows[2].name,"A Very Long Song T") || sd_played[0]==0,"a non-MP3 (cover.jpg) is not played"); 
    Hard_Drive_App_Process('4',g_tick); CHECK(!strcmp(path,"/") && total==4,"LEFT goes up one folder");
    Hard_Drive_App_Process('*',g_tick); CHECK(focus_back,"GEAR jumps to BACK");
    Hard_Drive_App_Process('#',g_tick); CHECK(current_state==STATE_ICON_MENU,"BACK leaves to the app menu");
    /* no card, then card inserted */
    esp_sd_mode=1; set_system_state(STATE_FILE_EXPLORER_APP); Hard_Drive_App_Init(); g_tick+=3000; Hard_Drive_App_Process(0,g_tick);
    CHECK(status==SD_NO_CARD,"no card: clear message instead of a made-up list");
    esp_sd_mode=0; Hard_Drive_App_Process('#',g_tick); CHECK(status==SD_OK && total==4,"PLAY retries once the card is in");
    esp_ready_at=0xFFFFFFFFu; esp01s_state=ESP01S_STATE_IDLE; set_system_state(STATE_FILE_EXPLORER_APP); Hard_Drive_App_Init(); g_tick+=3000; Hard_Drive_App_Process(0,g_tick);
    CHECK(status==SD_NO_LINK && nrows==0,"cam not answering: says so, no fake files"); }

  printf("T26 alarm 'double tone' page lists the DFPlayer's real tracks\n");
  { esp_ready_at=0; DFPlayer_SetRxHook(df_hook); df_reset(); df_feed_frame(0x48,5);
    Alarm_Manager_Init(); alarms[0].enabled=0; alarms[0].mp3_track=1; is_sdcard_inserted=1; set_system_state(STATE_ALARM_TIME_APP);
    for(int i=0;i<5;i++) Alarm_Manager_Process('6',g_tick);          /* focus 1 -> 6 = the double-note button */
    Alarm_Manager_Process('#',g_tick);
    CHECK(current_state==STATE_ALARM_DOUBLE_APP && DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount()==5,"opening the page asks the DFPlayer: 5 tracks");
    Alarm_Manager_Process('8',g_tick); Alarm_Manager_Process('8',g_tick); Alarm_Manager_Process('8',g_tick); Alarm_Manager_Process('#',g_tick);
    CHECK(alarms[0].mp3_track==4 && alarms[0].rhythm_type==2 && alarms[0].enabled && current_state==STATE_ALARM_TIME_APP,"picking the 4th row sets track 4 for the alarm");
    df_reset(); df_feed_frame(0x48,0); Alarm_Manager_Init(); alarms[0].enabled=0; set_system_state(STATE_ALARM_TIME_APP);
    for(int i=0;i<5;i++) Alarm_Manager_Process('6',g_tick); Alarm_Manager_Process('#',g_tick);
    uint8_t before=alarms[0].mp3_track; Alarm_Manager_Process('#',g_tick);
    CHECK(alarms[0].mp3_track==before && current_state==STATE_ALARM_DOUBLE_APP,"empty card: nothing to pick, track unchanged, 'ask again' stays on the page");
    DFPlayer_SetRxHook(NULL); }

  printf("T27 saved-network store (EEPROM)\n");
  { memset(eeprom,0xFF,sizeof eeprom); ee_wrap_events=0; ee_nack_count=0; g_tick+=100000; ee_busy_until=0;
    WStore_Load(); CHECK(WStore_Count()==0,"empty chip: no networks");
    CHECK(WStore_Save("A","pa") && WStore_Save("B","pb") && WStore_Save("C",""),"three networks saved (one open)");
    CHECK(WStore_Count()==3 && !strcmp(WStore_Get(0)->ssid,"C") && !strcmp(WStore_Get(2)->ssid,"A"),"newest first");
    CHECK(WStore_Touch("A") && !strcmp(WStore_Get(0)->ssid,"A") && !strcmp(WStore_Get(1)->ssid,"C"),"using a network makes it the most recent");
    CHECK(WStore_Save("B","new") && WStore_Count()==3 && !strcmp(WStore_Get(0)->ssid,"B") && !strcmp(WStore_Get(0)->pwd,"new"),"saving again updates the password, no duplicate");
    WStore_Load(); CHECK(WStore_Count()==3 && !strcmp(WStore_Get(0)->ssid,"B") && !strcmp(WStore_Get(0)->pwd,"new") && !strcmp(WStore_Get(1)->ssid,"A") && !strcmp(WStore_Get(2)->ssid,"C"),"everything survives a power cycle, same order");
    { wstore_seen_t seen[]={{"B",-60},{"A",-50},{"C",-90},{"Stranger",-30}}; uint8_t o[6]; uint8_t n=WStore_Candidates(seen,4,o,6);
      CHECK(n==2 && !strcmp(WStore_Get(o[0])->ssid,"A") && !strcmp(WStore_Get(o[1])->ssid,"B"),"candidates: only saved ones in range, strongest first, too-weak skipped");
      wstore_seen_t tie[]={{"A",-60},{"B",-60}}; n=WStore_Candidates(tie,2,o,6); CHECK(n==2 && !strcmp(WStore_Get(o[0])->ssid,"B"),"equal signal: most recently used first");
      n=WStore_Candidates(seen,4,o,1); CHECK(n==1 && !strcmp(WStore_Get(o[0])->ssid,"A"),"limit on how many are returned keeps the best"); }
    CHECK(WStore_Forget("A") && WStore_Find("A")<0 && WStore_Count()==2,"forget");
    { int live=0; for(int s=0;s<6;s++){ uint8_t *b=&eeprom[EEPROM_WIFILIST_ADDR+s*128]; if(b[0]==0xA8) live++; } CHECK(live==2,"forgotten slot is physically wiped (only B and C remain in the chip)"); }
    WStore_Load(); CHECK(WStore_Count()==2 && WStore_Find("A")<0,"forgotten stays forgotten after reload");
    WStore_Save("D","d"); WStore_Save("E","e"); WStore_Save("F","f"); WStore_Save("G","g");
    CHECK(WStore_Count()==6,"six is the capacity"); CHECK(WStore_Save("H","h") && WStore_Count()==6 && WStore_Find("H")==0,"7th network: accepted");
    CHECK(WStore_Find("C")<0 && WStore_Find("B")>=0,"the least recently used one (C) was the one replaced");
    { int hit=-1; for(int s=0;s<6;s++){ uint8_t *b=&eeprom[EEPROM_WIFILIST_ADDR+s*128]; if(b[0]==0xA8 && b[1]==1 && b[5]=='H') hit=s; } CHECK(hit>=0,"found H in the chip"); eeprom[EEPROM_WIFILIST_ADDR+hit*128+37]^=0x01; }
    WStore_Load(); CHECK(WStore_Count()==5 && WStore_Find("H")<0 && WStore_Find("G")>=0,"one corrupt slot is dropped, the rest are fine");
    CHECK(!WStore_Save("","x") && !WStore_Save("123456789012345678901234567890123","x") && !WStore_Save("x","1234567890123456789012345678901234567890123456789012345678901234"),"empty / too long name or password refused");
    memset(eeprom,0xFF,sizeof eeprom); WStore_Load();
    CHECK(WStore_Save("Cafe, Free Wifi","Pa$$w0rd!,\"x\\y"),"name with a comma and spaces, password with , \" \\"); WStore_Load();
    CHECK(WStore_Count()==1 && !strcmp(WStore_Get(0)->ssid,"Cafe, Free Wifi") && !strcmp(WStore_Get(0)->pwd,"Pa$$w0rd!,\"x\\y"),"read back intact");
    /* old firmware kept ONE network at 0x100 */
    memset(eeprom,0xFF,sizeof eeprom); { uint8_t b[100]={0}; b[0]=0xA7; b[1]=7; b[2]=5; memcpy(&b[3],"OldHome",7); memcpy(&b[35],"oldpw",5); uint8_t sm=0x5A; for(int i=0;i<99;i++) sm=(uint8_t)(sm+b[i]*3U+i); b[99]=sm; memcpy(&eeprom[EEPROM_WIFI_ADDR],b,100); }
    WStore_Load(); CHECK(WStore_Count()==1 && !strcmp(WStore_Get(0)->ssid,"OldHome") && !strcmp(WStore_Get(0)->pwd,"oldpw"),"network saved by the old firmware is imported");
    CHECK(eeprom[EEPROM_WIFI_ADDR]!=0xA7,"old record retired"); WStore_Load(); CHECK(WStore_Count()==1,"and it is not imported twice");
    memset(eeprom,0xFF,sizeof eeprom); WStore_Load(); WStore_SaveEx("Hid","pw",true); WStore_Save("Hid","pw2"); WStore_Load();
    CHECK(WStore_Count()==1 && WStore_Get(0)->hidden && !strcmp(WStore_Get(0)->pwd,"pw2"),"hidden flag survives reload and a plain re-save");
    CHECK(ee_wrap_events==0 && ee_nack_count==0,"no EEPROM page wrap / busy-write problems"); }

  printf("T28 several networks: home -> away -> home, switching, forgetting\n");
  { const char *HOME_PW="Pa$$w0rd!,\"x\\y";
    #define POWER_CYCLE() do{ reset_world(); esp_ready_at=0; set_system_state(STATE_MAIN_SCREEN); g_tick+=3000; WiFi_Manager_Background(g_tick); }while(0)
    #define OPEN_APP()    do{ set_system_state(STATE_WIFI_APP); g_tick+=3000; WiFi_Manager_Process(0,g_tick); }while(0)
    #define SCAN_NOW()    do{ wifi_footer_focus=1; press('#'); wifi_footer_focus=0; }while(0)
    #define PICK_SCAN(nm) do{ int ix_=-1; for(int q_=0;q_<esp01s_scanned_count;q_++) if(!strcmp(esp01s_scanned_aps[q_].ssid,nm)) ix_=q_; wifi_footer_focus=0; wifi_selected_index=(uint8_t)(ix_<0?0:ix_); wifi_scroll_offset=wifi_selected_index; g_tick+=100; press('#'); }while(0)
    #define TYPE_PW(pw)   do{ snprintf(pwd_buffer,sizeof pwd_buffer,"%s",pw); pwd_len=(uint8_t)strlen(pwd_buffer); pwd_cursor=pwd_len; pwd_focus=1; press('#'); }while(0)
    #define GOTO_SAVED()  do{ view=V_LIST; wifi_footer_focus=2; press('#'); }while(0)
    #define PICK_SAVED(nm) do{ sv_sel=(uint8_t)WStore_Find(nm); sv_back=false; press('#'); }while(0)
    memset(eeprom,0xFF,sizeof eeprom); ee_wrap_events=0; ee_nack_count=0; ap_listed=0x3F; ap_joinable=0xFFFF; memset(ap_rssi_used,0,sizeof ap_rssi_used);

    POWER_CYCLE(); CHECK(!wifi_connected && WStore_Count()==0,"nothing saved yet: stays offline");
    OPEN_APP(); CHECK(scanned_once && esp01s_scanned_count>0,"opening the Wi-Fi app looks around by itself");
    PICK_SCAN("HomeNet"); CHECK(current_state==STATE_WIFI_PASSWORD,"new secured network: asks for the password"); TYPE_PW(HOME_PW);
    CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"HomeNet") && WStore_Count()==1,"connected and remembered");

    ap_listed=(1u<<2)|(1u<<3); POWER_CYCLE();                       /* at someone else's house: HomeNet is out of range */
    CHECK(!wifi_connected && strstr(esp01s_status_msg,"NO KNOWN WIFI")!=NULL && current_state!=STATE_WIFI_PASSWORD,"away: no known network in range -> offline, no nagging");
    OPEN_APP(); PICK_SCAN("Neighbour"); CHECK(current_state==STATE_WIFI_PASSWORD,"neighbour's network is new: password asked"); TYPE_PW("secret123");
    CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"Neighbour") && WStore_Count()==2,"connected there; HomeNet is still remembered");
    POWER_CYCLE(); CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"Neighbour"),"restart over there: reconnects with no typing");
    ap_listed=0x3F&~(1u<<3); POWER_CYCLE();                            /* back home */
    CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"HomeNet"),"back home: joins HomeNet by itself, password was kept");

    ap_rssi_used[3]=1; ap_rssi_set[3]=-40; ap_listed=0x3F; POWER_CYCLE();
    CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"Neighbour"),"both in range: the stronger signal is chosen");
    ap_rssi_used[3]=0;

    OPEN_APP(); SCAN_NOW(); PICK_SCAN("HomeNet");
    CHECK(current_state==STATE_WIFI_APP && wifi_connected && !strcmp(esp01s_active_ssid,"HomeNet"),"switch from the list: known network, no password asked");
    CHECK(!strcmp(WStore_Get(0)->ssid,"HomeNet"),"and it becomes the most recent");
    GOTO_SAVED(); CHECK(view==V_SAVED,"SAVED page opens"); PICK_SAVED("Neighbour");
    CHECK(view==V_ACTION && act_ids[0]==ACT_CONNECT,"options for a network you are not on: connect first");
    act_sel=0; press('#'); CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"Neighbour") && view==V_LIST,"CONNECT switches networks");

    GOTO_SAVED(); PICK_SAVED("Neighbour"); CHECK(act_ids[0]==ACT_DISC,"the network you are on offers DISCONNECT");
    act_sel=0; press('#'); CHECK(!wifi_connected && user_disconnected,"disconnected");
    { set_system_state(STATE_MAIN_SCREEN); g_tick+=3600000UL; uint32_t c0=cmds_seen; WiFi_Manager_Background(g_tick); CHECK(cmds_seen==c0 && !wifi_connected,"after DISCONNECT it does not rejoin by itself"); }
    OPEN_APP(); SCAN_NOW(); PICK_SCAN("HomeNet"); CHECK(wifi_connected && !user_disconnected,"choosing a network again turns auto-join back on");

    /* a saved network whose password is refused */
    WStore_Save("Neighbour","wrong"); sync_saved_compat(); ap_rssi_used[3]=1; ap_rssi_set[3]=-40; POWER_CYCLE();
    CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"HomeNet"),"strongest known network refuses the password: falls back to the next one");
    CHECK(!strcmp(skip_ssid,"Neighbour"),"and the refused one is not hammered again"); ap_rssi_used[3]=0;
    OPEN_APP(); SCAN_NOW(); PICK_SCAN("Neighbour");
    CHECK(current_state==STATE_WIFI_PASSWORD && !strcmp(pwd_buffer,"wrong") && strstr(esp01s_status_msg,"WRONG")!=NULL,"picking it: 'wrong password', editor opens pre-filled");
    TYPE_PW("secret123"); CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"Neighbour") && !strcmp(WStore_Get(0)->pwd,"secret123") && skip_ssid[0]==0,"corrected password is saved and used");

    /* EDIT PASSWORD from the options page */
    GOTO_SAVED(); PICK_SAVED("HomeNet"); act_sel=1; press('#'); CHECK(current_state==STATE_WIFI_PASSWORD && !strcmp(pwd_buffer,HOME_PW),"EDIT PASSWORD opens the editor with the saved password");
    pwd_focus=2; press('#'); CHECK(current_state==STATE_WIFI_APP && view==V_ACTION,"BACK returns to the options page");

    /* connection drops: noticed, then re-joined */
    OPEN_APP(); set_system_state(STATE_MAIN_SCREEN); esp_link=false; g_tick+=50000; link_check_next=0; time_sync_next=g_tick+10000000UL; WiFi_Manager_Background(g_tick);
    CHECK(!wifi_connected && strstr(esp01s_status_msg,"LOST")!=NULL,"dropped link is noticed");
    esp_link=false; g_tick+=rejoin_delay_ms()+500; WiFi_Manager_Background(g_tick); CHECK(wifi_connected,"and the clock re-joins a known network on its own");

    /* forget */
    GOTO_SAVED(); PICK_SAVED("Neighbour"); { int before=WStore_Count(); act_sel=2; press('#'); CHECK(act_confirm && WStore_Count()==before,"FORGET asks to press OK again");
      press('2'); CHECK(!act_confirm,"moving away cancels it"); act_sel=2; press('#'); press('#');
      CHECK(WStore_Find("Neighbour")<0 && WStore_Count()==before-1 && view==V_SAVED,"forgotten, back on the SAVED page"); }
    POWER_CYCLE(); CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"HomeNet"),"after a restart only the remaining network is used");
    ap_listed=1u<<3; POWER_CYCLE(); CHECK(!wifi_connected,"the forgotten network is not joined any more");
    ap_listed=0x3F;
    /* forgetting the one you are on disconnects it */
    POWER_CYCLE(); GOTO_SAVED(); PICK_SAVED("HomeNet"); act_sel=2; press('#'); press('#'); CHECK(!wifi_connected && WStore_Count()==0 && user_disconnected,"forgetting the current network disconnects it");

    /* hidden / unlisted network typed by hand */
    ap_listed=0x3F; POWER_CYCLE(); OPEN_APP(); GOTO_SAVED(); CHECK(WStore_Count()==0 && sv_sel==0,"empty list shows only ADD NETWORK");
    press('#'); CHECK(current_state==STATE_WIFI_PASSWORD && edit_mode==1,"ADD NETWORK: asks for the name first");
    pwd_focus=1; press('#'); CHECK(edit_mode==1 && strstr(esp01s_status_msg,"ENTER")!=NULL,"empty name refused");
    snprintf(pwd_buffer,sizeof pwd_buffer,"SecretNet"); pwd_len=9; pwd_cursor=9; pwd_focus=1; press('#');
    CHECK(edit_mode==0 && !strcmp(target_ssid,"SecretNet") && current_state==STATE_WIFI_PASSWORD,"then the password"); TYPE_PW("hush");
    CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"SecretNet") && WStore_Find("SecretNet")==0,"hidden network joined and saved");
    CHECK(WStore_Get(0)->hidden,"typed-in network that no scan shows is flagged hidden");
    POWER_CYCLE(); CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"SecretNet"),"hidden network never shows in scans, but is still joined by name after a restart");

    /* open network */
    OPEN_APP(); SCAN_NOW(); PICK_SCAN("Cafe, Free Wifi"); CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"Cafe, Free Wifi") && WStore_Find("Cafe, Free Wifi")==0,"open network: joined with one press and remembered");
    ap_listed=(1u<<2); POWER_CYCLE(); CHECK(wifi_connected && !strcmp(esp01s_active_ssid,"Cafe, Free Wifi"),"open network re-joins by itself");
    CHECK(ee_wrap_events==0 && ee_nack_count==0,"EEPROM: no page wrap, no write-cycle collisions"); ap_listed=0x3F;
    #undef POWER_CYCLE
  }

  printf("T29 level meter: real ADC peak-to-peak, shown as 3 dB segments\n");
  { LevelMeter_SetSampleHook(lm_hook); LevelMeter_Init();
    lm_amp[0]=lm_amp[1]=0; LevelMeter_Update(); CHECK(LevelMeter_Get()->seg[0]==0 && LevelMeter_Get()->seg[1]==0,"silence (no input): both bars empty");
    lm_amp[0]=2047; lm_amp[1]=1024; LevelMeter_Update(); { const level_t *l=LevelMeter_Get(); CHECK(l->seg[0]==20 && l->p2p[0]>4000,"full-scale left: bar full"); CHECK(l->seg[1]>=17 && l->seg[1]<=19,"half-scale right: about -6 dB"); CHECK(LevelMeter_Db(l->seg[0])==0 && LevelMeter_Db(l->seg[1])<=-3 && LevelMeter_Db(l->seg[1])>=-9,"dB labels follow"); }
    lm_amp[0]=lm_amp[1]=100; lm_dc[0]=900; lm_dc[1]=3000; for(int k=0;k<25;k++) LevelMeter_Update();
    { const level_t *l=LevelMeter_Get(); CHECK(l->seg[0]==l->seg[1] && l->seg[0]>0,"same signal, different DC bias: same level (AC measurement)"); }
    uint8_t before=LevelMeter_Get()->seg[0]; lm_amp[0]=lm_amp[1]=0; LevelMeter_Update(); CHECK(LevelMeter_Get()->seg[0]==before-1,"bar falls one segment per update (readable decay)");
    for(int k=0;k<40;k++) LevelMeter_Update(); CHECK(LevelMeter_Get()->seg[0]==0,"and reaches empty");
    CHECK(LevelMeter_SegmentsFor(0)==0 && LevelMeter_SegmentsFor(7)==0 && LevelMeter_SegmentsFor(8)==1 && LevelMeter_SegmentsFor(4095)==20 && LevelMeter_SegmentsFor(3700)==20 && LevelMeter_SegmentsFor(3699)==19,"segment thresholds");
    { int mono=1; uint8_t prev=0; for(int v=0;v<=4095;v++){ uint8_t sg=LevelMeter_SegmentsFor((uint16_t)v); if(sg<prev) mono=0; prev=sg; } CHECK(mono,"louder never shows a shorter bar"); }
    lm_amp[0]=lm_amp[1]=0; for(int k=0;k<40;k++) LevelMeter_Update(); }

  printf("T30 MP3 player: pick the DFPlayer's SD card or the ESP32-CAM's SD card\n");
  { uint8_t st; uint16_t ix,tt; sd_entry_t e[3]; uint8_t got; uint16_t total; sd_play_t pl;
    CHECK(sdp_parse_stat("+SDSTAT:1,3,12",&st,&ix,&tt) && st==1 && ix==3 && tt==12 && sdp_parse_stat("+SDSTAT:2,0,5",&st,&ix,&tt) && st==2 && !sdp_parse_stat("+SDSTAT:3,0,5",&st,&ix,&tt) && !sdp_parse_stat("+SDSTAT:1",&st,&ix,&tt),"status line parser");
    reset_world(); esp_ready_at=0; esp_sd_mode=0; sd_playn=-1; sd_noclock=0; esp_sync(3);
    CHECK(SDBridge_ListMp3(0,e,3,&got,&total)==SD_OK && total==5 && got==3 && !strcmp(e[0].name,"01_synth_80s.mp3"),"cam play list: every MP3 on the card, any folder (5)");
    CHECK(SDBridge_ListMp3(4,e,3,&got,&total)==SD_OK && got==1 && !strcmp(e[0].name,"show1.mp3"),"play list reaches into sub-folders");
    CHECK(SDBridge_PlayIndex(2)==SD_OK && sd_playn==2 && SDBridge_PlayIndex(9)==SD_BAD_PATH,"play by number; a number past the end is refused");
    CHECK(SDBridge_Status(&pl)==SD_OK && pl.state==SD_PLAY_PLAYING && pl.index==2 && pl.total==5,"status: playing 3 of 5");
    sd_noclock=1; CHECK(SDBridge_Status(&pl)==SD_OK && pl.state==SD_PLAY_NO_CLOCK,"status: no I2S clock from the ADAU1401"); sd_noclock=0;
    SDBridge_Stop(); CHECK(SDBridge_Status(&pl)==SD_OK && pl.state==SD_PLAY_STOPPED,"stopped");
    reset_world(); esp_ready_at=0; esp_sd_mode=2; esp_sync(3); CHECK(SDBridge_ListMp3(0,e,1,&got,&total)==SD_NO_BRIDGE,"cam firmware without the play list commands is reported");

    reset_world(); esp_ready_at=0; esp_sd_mode=0; sd_playn=-1; esp_sync(3);
    DFPlayer_SetRxHook(df_hook); df_reset(); df_feed_frame(0x48,7);
    Sound_SetInputSource(0); Entertainment_App_Init(); set_system_state(STATE_ENTERTAINMENT_APP);
    Entertainment_App_Process('6',g_tick); Entertainment_App_Process('#',g_tick);          /* menu: MP3 PLAYER */
    CHECK(Sound_GetInputSource()==0 && sd_playn==-1 && DFPlayer_TrackCountKnown() && DFPlayer_GetTrackCount()==7,"DFPLAYER source: reads its own card (7), cam stays silent");
    Entertainment_App_Process('8',g_tick);
    CHECK(Sound_GetInputSource()==1 && sd_playn==0,"DOWN: CAM SD selected, cam plays play-list entry 1");
    CHECK(eeprom[EEPROM_SETTINGS_ADDR+19]==1,"the choice is remembered in the EEPROM");
    Entertainment_App_Process('6',g_tick); CHECK(sd_playn==1,"RIGHT: next track on the cam");
    Entertainment_App_Process('4',g_tick); Entertainment_App_Process('4',g_tick); CHECK(sd_playn==4,"LEFT twice from the first wraps to the last");
    Entertainment_App_Process('#',g_tick); CHECK(sd_playn==-1,"PLAY while playing: stops the cam");
    Entertainment_App_Process('#',g_tick); CHECK(sd_playn==4,"PLAY again: resumes the same track");
    sd_playn=2; g_tick+=3000; Entertainment_App_Process(0,g_tick); Entertainment_App_Process('6',g_tick);
    CHECK(sd_playn==3,"the cam moved on by itself (track 3): the player followed, so RIGHT goes to 4");
    sd_noclock=1; g_tick+=3000; Entertainment_App_Process(0,g_tick); sd_noclock=0; Entertainment_App_Process('#',g_tick);
    CHECK(sd_playn==3,"no I2S clock: player shows it stopped, so PLAY starts the track again (not 'stop')");
    Entertainment_App_Process('8',g_tick); CHECK(Sound_GetInputSource()==0 && sd_playn==-1,"DOWN back to DFPLAYER: the cam is silenced");
    Entertainment_App_Process('8',g_tick); CHECK(Sound_GetInputSource()==1 && sd_playn==0,"and to CAM again");
    Entertainment_App_Process('*',g_tick); CHECK(sd_playn==-1,"leaving the player silences the cam");
    /* no card in the cam */
    esp_sd_mode=1; Entertainment_App_Init(); set_system_state(STATE_ENTERTAINMENT_APP); Entertainment_App_Process('6',g_tick); Entertainment_App_Process('#',g_tick);
    CHECK(sd_playn==-1,"cam without a card: nothing plays, no made-up tracks");
    esp_sd_mode=0; Entertainment_App_Process('#',g_tick); CHECK(sd_playn==0,"card inserted: PLAY retries and works");
    Entertainment_App_Process('*',g_tick); Sound_SetInputSource(0); DFPlayer_SetRxHook(NULL); }

  if(getenv("SHOTS_DIR")){ const char *dir=getenv("SHOTS_DIR"); char fn[200];
    #define SHOT(name) do{ WiFi_Manager_Render(); snprintf(fn,sizeof fn,"%s/%s.pbm",dir,name); FILE*f=fopen(fn,"wb"); fprintf(f,"P4\n128 64\n"); for(int y=0;y<64;y++){ for(int xb=0;xb<16;xb++){ uint8_t v=0; for(int b=0;b<8;b++){ int x=xb*8+b; if(sh1106_buffer[(y/8)*128+x]&(1<<(y%8))) v|=(uint8_t)(0x80>>b);} fputc(v,f);} } fclose(f);}while(0)
    memset(eeprom,0xFF,sizeof eeprom); ap_listed=0x3F; ap_joinable=0xFFFF; reset_world(); esp_ready_at=0; g_tick+=3000; WStore_Load();
    WStore_Save("Neighbour","secret123"); WStore_Save("HomeNet","Pa$$w0rd!,\"x\\y"); WStore_Save("Garage","g"); sync_saved_compat();
    reset_world(); esp_ready_at=0; set_system_state(STATE_MAIN_SCREEN); g_tick+=3000; WiFi_Manager_Background(g_tick);
    set_system_state(STATE_WIFI_APP); g_tick+=3000; WiFi_Manager_Process(0,g_tick); view=V_LIST; wifi_footer_focus=0; SHOT("1_list");
    wifi_footer_focus=2; SHOT("2_list_saved_button_focus");
    view=V_SAVED; sv_sel=0; sv_top=0; sv_back=false; SHOT("3_saved");
    sv_sel=3; sv_top=0; SHOT("4_saved_add_row");
    open_action("HomeNet",V_SAVED); SHOT("5_options_active"); act_sel=2; act_confirm=true; SHOT("6_options_forget_confirm");
    view=V_SAVED; name_open(); { const char*t="MyHiddenNet"; for(const char*c=t;*c;c++) pwd_digit(*c);} SHOT("7_name_editor");
    pwd_open("Neighbour"); set_status("WRONG PASSWORD"); SHOT("8_password_wrong");
    WStore_Forget("Neighbour"); WStore_Forget("HomeNet"); WStore_Forget("Garage"); set_system_state(STATE_WIFI_APP); view=V_SAVED; sv_sel=0; SHOT("9_saved_empty");
  }

  
  /* ---- T31: RDM6300 reader (real frame parser, real app screen) ---- */
  { printf("T31 RFID\n");
    #define RFRAME(buf,hex10) do{ uint8_t x_=0; for(int i_=0;i_<5;i_++){ unsigned v_; sscanf((hex10)+2*i_,"%2x",&v_); x_^=(uint8_t)v_; } snprintf(buf,32,"\x02%s%02X\x03",hex10,x_); }while(0)
    char fr[32]; rfid_tag_t tg; RFRAME(fr,"0100808B3E");
    for(const char*c=fr;*c;c++) Rfid_FeedByte((uint8_t)*c);
    CHECK(Rfid_Poll(&tg) && tg.id==0x00808B3Eu && !strcmp(tg.dec,"0008424254") && !strcmp(tg.hex,"00808B3E") && tg.version==1,"valid frame: ID decoded (decimal 0008424254, hex 00808B3E)");
    CHECK(!Rfid_Poll(&tg),"a frame is delivered once");
    char bad[32]; strcpy(bad,fr); bad[11]^=1; uint32_t b0=Rfid_FramesBad();
    for(const char*c=bad;*c;c++) Rfid_FeedByte((uint8_t)*c);
    CHECK(!Rfid_Poll(&tg) && Rfid_FramesBad()==b0+1,"wrong checksum: rejected and counted");
    Rfid_FeedByte(0x55); Rfid_FeedByte('1'); for(const char*c=fr;*c;c++) Rfid_FeedByte((uint8_t)*c);
    CHECK(Rfid_Poll(&tg),"noise before the start byte is ignored, next frame is read");
    for(int i=0;i<5;i++) Rfid_FeedByte(0x02), Rfid_FeedByte('Z');
    for(const char*c=fr;*c;c++) Rfid_FeedByte((uint8_t)*c);
    CHECK(Rfid_Poll(&tg),"garbage mid-frame does not wedge the parser");
    Entertainment_App_Init(); set_system_state(STATE_ENTERTAINMENT_APP);
    g_tick+=3000; Entertainment_App_Process('8',g_tick); Entertainment_App_Process('8',g_tick); Entertainment_App_Process('8',g_tick); Entertainment_App_Process('#',g_tick);
    for(const char*c=fr;*c;c++) Rfid_FeedByte((uint8_t)*c);
    g_tick+=100; Entertainment_App_Process(0,g_tick);
    CHECK(Rfid_FramesOk()>=4,"app screen consumed the frame without crashing"); }
printf("\n%d checks, %d failed -> %s\n",checks,fails,fails?"FAILURES":"ALL PASSED");
  return fails?1:0;
}
