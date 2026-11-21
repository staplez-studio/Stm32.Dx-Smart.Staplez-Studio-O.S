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
#include "wifi_manager.c"
#include "sh1106.h"     /* included so tests can read its static state */
#include "weather_app.c"
#include "hard_drive_app.c"
#include "wifi_store.h"
#include "level_meter.h"
#include "entertainment_app.h"
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
  {"ThisIsAVeryLongNetworkName32ch","",   0, -30},
};
#define NAPS (int)(sizeof(APS)/sizeof(APS[0]))
static uint32_t esp_ready_at = 1500;        /* module answers only after this tick (0xFFFFFFFF = dead) */
static bool esp_echo = true, esp_link = false, esp_http_ok = true; static char esp_ssid[40];
static int sntp_calls = 0; static wnet_time_t esp_utc = {2026,11,10,2,15,42,2};
static char esp_http_body[256] = "-12\xC2\xB0""C;100%;Light freezing drizzle and mist;Llanfairpwllgwyngyll, United Kingdom\n"; static int esp_http_status = 200; static bool esp_tcp_refuse = false;
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


/* ===================== UI lint: records every draw call and checks the finished frame ===================== */
#include "ui_draw.h"
#include "font5x7.h"
#include "tim.h"
#include "gpio.h"
#include "ir_remote_app.h"
#include "main_screen.h"
#include "app_menu.h"
typedef struct { int x,y,w,h; char kind; char txt[40]; } item_t;   /* T text, I icon, R outline box, F filled box */
static item_t it[3000]; static int nit=0;
static int inhibit=0;
static void add(char k,int x,int y,int w,int h,const char*t){ if(inhibit&&k!='I') return; if(nit<3000){ it[nit]=(item_t){x,y,w,h,k,{0}}; if(t) snprintf(it[nit].txt,40,"%s",t); nit++; } }
void __wrap_sh1106_clear(void){ nit=0; __real_sh1106_clear(); }
void __wrap_sh1106_drawString(uint8_t x,uint8_t y,const char*s,uint8_t sc,uint8_t col){
  if(s) for(int i=0;s[i];i++){ char c=s[i]; if(c<32||c>126) continue; int x0=9,x1=-1,y0=9,y1=-1; for(int cc=0;cc<5;cc++) for(int r=0;r<7;r++) if((font5x7[c-32][cc]>>r)&1){ if(cc<x0)x0=cc; if(cc>x1)x1=cc; if(r<y0)y0=r; if(r>y1)y1=r; }
    if(x1>=0){ char b[40]; snprintf(b,40,"%s",s); add('T',x+i*6*sc+x0*sc,y+y0*sc,(x1-x0+1)*sc,(y1-y0+1)*sc,b); } }
  __real_sh1106_drawString(x,y,s,sc,col); }
void __wrap_sh1106_drawChar(uint8_t x,uint8_t y,char c,uint8_t sc,uint8_t col){ char b[2]={c,0};
  if(c>=32&&c<=126){ int x0=9,x1=-1,y0=9,y1=-1; for(int cc=0;cc<5;cc++) for(int r=0;r<7;r++) if((font5x7[c-32][cc]>>r)&1){ if(cc<x0)x0=cc; if(cc>x1)x1=cc; if(r<y0)y0=r; if(r>y1)y1=r; }
    if(x1>=0) add('T',x+x0*sc,y+y0*sc,(x1-x0+1)*sc,(y1-y0+1)*sc,b); }
  __real_sh1106_drawChar(x,y,c,sc,col); }
void __wrap_sh1106_drawRect(uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint8_t col){ if(w>=3&&h>=3) add('R',x,y,w,h,NULL); __real_sh1106_drawRect(x,y,w,h,col); }
void __wrap_sh1106_fillRect(uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint8_t col){ if(col==SH1106_WHITE&&w>=3&&h>=3) add('F',x,y,w,h,NULL); __real_sh1106_fillRect(x,y,w,h,col); }
static void ink(const uint16_t*ic,int rows,int x,int y){ int x0=99,x1=-1,y0=99,y1=-1; for(int r=0;r<rows;r++) for(int c=0;c<16;c++) if((ic[r]>>(15-c))&1){ if(c<x0)x0=c; if(c>x1)x1=c; if(r<y0)y0=r; if(r>y1)y1=r; } if(x1>=0) add('I',x+x0,y+y0,x1-x0+1,y1-y0+1,NULL); }
void __wrap_draw_16x16_icon(uint8_t x,uint8_t y,const uint16_t*ic){ if(ic) ink(ic,16,x,y); __real_draw_16x16_icon(x,y,ic); }
void __wrap_draw_16x32_icon(uint8_t x,uint8_t y,const uint16_t*ic){ if(ic) ink(ic,32,x,y); __real_draw_16x32_icon(x,y,ic); }
void __wrap_draw_8x8_icon(uint8_t x,uint8_t y,const uint8_t*ic){ if(ic){ int x0=9,x1=-1,y0=9,y1=-1; for(int r=0;r<8;r++) for(int c=0;c<8;c++) if((ic[r]>>(7-c))&1){ if(c<x0)x0=c; if(c>x1)x1=c; if(r<y0)y0=r; if(r>y1)y1=r; } if(x1>=0) add('I',x+x0,y+y0,x1-x0+1,y1-y0+1,NULL);} __real_draw_8x8_icon(x,y,ic); }
void __wrap_Weather_DrawIcon16(uint8_t x,uint8_t y,wnet_icon_t k){ add('I',x,y,16,16,NULL); inhibit=1; __real_Weather_DrawIcon16(x,y,k); inhibit=0; }
#define OV(a,b) ((a)->x < (b)->x+(b)->w && (b)->x < (a)->x+(a)->w && (a)->y < (b)->y+(b)->h && (b)->y < (a)->y+(a)->h)
static int vcount=0, frames=0; static char seen[600][160]; static int nseen=0;
static const char* sname(void){ static const char*n[]={"SPLASH","MAIN","MENU","FAN","SOUND","WEATHER","WIFI","WIFIPWD","SETTIME","ENT","FILES","SYMBOLS","ALARMT","ALARM1","ALARM2","MOVER","IR","CAL","TZ"}; return current_state<19?n[current_state]:"?"; }
static void report(const char*msg){ char k[160]; snprintf(k,sizeof k,"%s|%s",sname(),msg); for(int i=0;i<nseen;i++) if(!strcmp(seen[i],k)) return; if(nseen<600) snprintf(seen[nseen++],160,"%s",k); vcount++; }
static void check_frame(void){
  frames++;
  for(int i=0;i<nit;i++){ item_t*a=&it[i]; char m[160];
    if(a->kind=='T'||a->kind=='I'){
      if(a->x<0||a->y<0||a->x+a->w>128||a->y+a->h>64){ snprintf(m,sizeof m,"%s '%s' at %d,%d %dx%d leaves the screen",a->kind=='T'?"text":"icon",a->txt,a->x,a->y,a->w,a->h); report(m); }
      for(int j=0;j<nit;j++){ item_t*b=&it[j]; if(b->kind!='R'&&b->kind!='F') continue; if(!OV(a,b)) continue;
        int inside = a->x>=b->x+1 && a->y>=b->y+1 && a->x+a->w<=b->x+b->w-1 && a->y+a->h<=b->y+b->h-1;
        if(b->kind=='F') inside = a->x>=b->x && a->y>=b->y && a->x+a->w<=b->x+b->w && a->y+a->h<=b->y+b->h;
        if(!inside){ snprintf(m,sizeof m,"%s '%s' (%d,%d %dx%d) crosses box (%d,%d %dx%d)",a->kind=='T'?"text":"icon",a->txt,a->x,a->y,a->w,a->h,b->x,b->y,b->w,b->h); report(m); } }
      for(int j=i+1;j<nit;j++){ item_t*b=&it[j]; if(b->kind!='T'&&b->kind!='I') continue; if(!OV(a,b)) continue;
        if(a->kind=='T'&&b->kind=='T'&&a->x==b->x&&a->y==b->y) continue;      /* same text re-drawn (highlight) */
        snprintf(m,sizeof m,"%s '%s' (%d,%d %dx%d) overlaps %s '%s' (%d,%d %dx%d)",a->kind=='T'?"text":"icon",a->txt,a->x,a->y,a->w,a->h,b->kind=='T'?"text":"icon",b->txt,b->x,b->y,b->w,b->h); report(m); }
    } }
}
HAL_StatusTypeDef __wrap_sh1106_update(void){ check_frame(); return HAL_OK; }

static int lm_cnt=0; static uint16_t lm_hook(uint8_t ch){ lm_cnt++; return (uint16_t)(2048 + ((lm_cnt*7919u + ch*13u) % ((lm_cnt/4000)%2000+1)) ); }
static uint32_t rng=12345; static uint32_t rnd(void){ rng=rng*1664525u+1013904223u; return rng>>8; }
static void press_btn(int idx){ IR_Simulate_Button_Press((uint8_t)idx); }
int main(int argc,char**argv){
  mmap((void*)0x40000000UL,0x30000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  mmap((void*)0xE0000000UL,0x100000,PROT_READ|PROT_WRITE,MAP_FIXED_NOREPLACE|MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
  memset(eeprom,0xFF,sizeof eeprom); tz_config.std_offset_min=-300; tz_config.rule=TZ_DST_US; rtc_store_std(2026,11,19,13,15,0);
  long steps = argc>1? atol(argv[1]) : 60000; if(argc>2) rng=(uint32_t)atol(argv[2]);
  esp_ready_at=0; LevelMeter_SetSampleHook(lm_hook);
  MX_GPIO_Init(); MX_TIM1_Init();
  Staplez_Init();
  WiFi_Creds_Save("ThisIsAVeryLongNetworkName32ch","");WiFi_Creds_Save("Neighbour","secret123");WiFi_Creds_Save("Cafe, Free Wifi","");WiFi_Creds_Save("Garage","g");WiFi_Creds_Save("HomeNet","Pa$$w0rd!,\"x\\y"); TZ_Save(); tz_saved=true;
  g_tick+=3000; for(int k=0;k<40;k++){ g_tick+=100; Staplez_Process(); }

  static const int APPKEYS[]={15,16,17,18,19,20,21,22,23, 1,2,8,9};  /* digits 1..9 jump straight into apps */
  static long stateframes[19];

  /* directed walk of the MP3 player screen with both sources and every cam condition */
  { for(int src=0;src<2;src++) for(int mode=0;mode<3;mode++) for(int nc=0;nc<2;nc++){
      esp_sd_mode=mode; sd_noclock=nc; sd_playn=-1; Sound_SetInputSource((uint8_t)src); Entertainment_App_Init(); set_system_state(STATE_ENTERTAINMENT_APP);
      g_tick+=3000; Entertainment_App_Process('6',g_tick); Entertainment_App_Process('#',g_tick);
      const char *ks="6644##8#6#4#8";
      for(const char*c=ks;*c;c++){ g_tick+=2500; Entertainment_App_Process(*c,g_tick); g_tick+=2500; Entertainment_App_Process(0,g_tick); stateframes[current_state<19?current_state:0]++; if(current_state!=STATE_ENTERTAINMENT_APP){ set_system_state(STATE_ENTERTAINMENT_APP); } }
      Entertainment_App_Process('*',g_tick); }
    esp_sd_mode=0; sd_noclock=0; Sound_SetInputSource(0); set_system_state(STATE_MAIN_SCREEN); }

  /* directed walks of the Wi-Fi screens: scan list, SAVED page with 0..6 networks, options page, editors */
  { for(int nsaved=0;nsaved<=6;nsaved++){
      WiFi_Creds_Clear(); static const char *NM[]={"HomeNet","Neighbour","Cafe, Free Wifi","Garage","ThisIsAVeryLongNetworkName32ch","Net6"};
      for(int i=0;i<nsaved;i++) WStore_Save(NM[i],"pw");
      for(int conn=0;conn<2;conn++){
        set_system_state(STATE_MAIN_SCREEN); esp_link=false; wifi_connected=false; esp01s_active_ssid[0]=0;
        if(conn && nsaved){ snprintf(esp01s_active_ssid,sizeof esp01s_active_ssid,"%s",NM[0]); wifi_connected=true; snprintf(esp01s_ip_address,sizeof esp01s_ip_address,"192.168.100.200"); }
        set_system_state(STATE_WIFI_APP); g_tick+=3000; Staplez_Process();
        const char *seq="2828868#26886#88#2#4*#8886#8#8#8#8#8#8*#4";      /* list -> SAVED -> options -> back ... */
        for(const char*c=seq;*c;c++){ g_tick+=300; WiFi_Manager_Process(*c,g_tick); Staplez_Process(); stateframes[current_state<19?current_state:0]++; if(current_state!=STATE_WIFI_APP && current_state!=STATE_WIFI_PASSWORD){ set_system_state(STATE_WIFI_APP); } }
        /* SAVED page, every row, then options page of every network */
        for(int r=0;r<=nsaved;r++){ set_system_state(STATE_WIFI_APP); view=V_SAVED; sv_sel=(uint8_t)r; sv_top=(uint8_t)(r>3?r-3:0); sv_back=false; WiFi_Manager_Render(); Staplez_Process();
          if(r<nsaved){ open_action(NM[r],V_SAVED); for(int a=0;a<4;a++){ act_sel=(uint8_t)(a%act_n); act_back=(a==3); act_confirm=(a==2); WiFi_Manager_Render(); Staplez_Process(); } } }
        view=V_SAVED; sv_back=true; WiFi_Manager_Render();
        /* editors: name, long name, full password */
        view=V_LIST; name_open(); for(int i=0;i<32;i++) pwd_digit('W'); pwd_focus=1; WiFi_Manager_Render(); pwd_focus=2; WiFi_Manager_Render();
        pwd_open("ThisIsAVeryLongNetworkName32ch"); set_status("WRONG PASSWORD"); for(int i=0;i<63;i++) pwd_digit('m'); WiFi_Manager_Render(); pwd_focus=1; WiFi_Manager_Render();
        set_system_state(STATE_WIFI_APP); view=V_LIST;
      } }
    WiFi_Creds_Clear(); WiFi_Creds_Save("Neighbour","secret123");WiFi_Creds_Save("Cafe, Free Wifi","");WiFi_Creds_Save("Garage","g");WiFi_Creds_Save("HomeNet","Pa$$w0rd!,\"x\\y"); view=V_LIST; set_system_state(STATE_MAIN_SCREEN); }

  /* directed walks of the SD screens (random play rarely reaches them) */
  { const char *keys="8888#8#8#4*8#2222#4#"; int modes[3]={0,1,2};
    for(int m=0;m<3;m++){ esp_sd_mode=modes[m]; esp_link=false; set_system_state(STATE_FILE_EXPLORER_APP); Hard_Drive_App_Init();
      for(int rep=0;rep<3;rep++){ for(const char*c=keys;*c;c++){ g_tick+=300; Hard_Drive_App_Process(0,g_tick); Hard_Drive_App_Process(*c,g_tick); g_tick+=100; Staplez_Process(); if(current_state!=STATE_FILE_EXPLORER_APP){ set_system_state(STATE_FILE_EXPLORER_APP); Hard_Drive_App_Init(); } stateframes[current_state<19?current_state:0]++; } } }
    esp_sd_mode=0;
    for(int tracks=0;tracks<=9;tracks+=3){ Alarm_Manager_Init(); alarms[0].enabled=0; is_sdcard_inserted=1; set_system_state(STATE_ALARM_TIME_APP);
      for(int i=0;i<5;i++){ Alarm_Manager_Process('6',g_tick); Staplez_Process(); } Alarm_Manager_Process('#',g_tick);
      for(int i=0;i<tracks+2;i++){ Alarm_Manager_Process('8',g_tick); g_tick+=50; Staplez_Process(); stateframes[current_state<19?current_state:0]++; }
      for(int i=0;i<tracks+2;i++){ Alarm_Manager_Process('2',g_tick); g_tick+=50; Staplez_Process(); stateframes[current_state<19?current_state:0]++; } } }
  for(long n=0;n<steps;){
    /* one episode: jump into an app with its digit key, then mostly in-app keys */
    press_btn(APPKEYS[rnd()%9]); for(int k=0;k<3;k++){ g_tick+=50; Staplez_Process(); }
    int len=40+rnd()%60;
    for(int e=0;e<len;e++,n++){
      int idx; int r=rnd()%100;
      if(r<80) idx=4+(int)(rnd()%11);               /* UP DOWN LEFT RIGHT PLAY GEAR PREV NEXT EQ VOL- VOL+ */
      else if(r<90) idx=(int)(rnd()%24); else idx=(rnd()%2)?1:2;
      if(idx==0||idx==3) idx=7;
      press_btn(idx);
      for(int k=0;k<3;k++){ g_tick+=40+rnd()%40; Staplez_Process(); }
      stateframes[current_state<19?current_state:0]++;
    }
    g_tick+=3000;
  }
  printf("frames checked: %d, distinct problems: %d\n",frames,vcount);
  for(int i=0;i<nseen;i++) printf("  %s\n",seen[i]);
  printf("frames by screen:"); for(int i=0;i<19;i++) if(stateframes[i]) printf(" %d=%ld",i,stateframes[i]); printf("\n");
  return vcount?1:0;
}
