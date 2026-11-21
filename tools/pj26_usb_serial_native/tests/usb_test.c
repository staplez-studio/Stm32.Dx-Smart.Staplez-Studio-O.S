#include "usb_proto.h"
#include <stdio.h>
#include <string.h>
static int fails, checks;
#define CHECK(c,m) do{checks++; if(!(c)){fails++; printf("FAIL: %s\n",m);} else printf("ok   %s\n",m);}while(0)
int main(void){
  usbp_t u; usbp_init(&u); const uint8_t *d; int n;
  uint8_t gd[8]={0x80,6,0,1,0,0,64,0};
  n=usbp_setup(&u,gd,&d); CHECK(n==18&&d[0]==18&&d[1]==1&&d[7]==64,"device descriptor (18 bytes, EP0 = 64)");
  gd[6]=8; n=usbp_setup(&u,gd,&d); CHECK(n==8,"host asking for 8 bytes only gets 8");
  uint8_t gc[8]={0x80,6,0,2,0,0,0xFF,0}; n=usbp_setup(&u,gc,&d);
  CHECK(n==75&&d[2]==75,"configuration descriptor length 75 = wTotalLength");
  int i=0,ifaces=0,eps=0; while(i<n){ if(d[i]==0) break; if(d[i+1]==4)ifaces++; if(d[i+1]==5)eps++; i+=d[i]; }
  CHECK(i==75&&ifaces==2&&eps==3,"descriptors chain exactly to the end: 2 interfaces, 3 endpoints");
  uint8_t gs[8]={0x80,6,2,3,9,4,0xFF,0}; n=usbp_setup(&u,gs,&d);
  CHECK(n>2&&d[0]==n&&d[1]==3&&d[2]=='P',"product string is UTF-16 and well formed");
  uint8_t g0[8]={0x80,6,0,3,0,0,0xFF,0}; n=usbp_setup(&u,g0,&d); CHECK(n==4&&d[2]==9,"language id 0x0409");
  uint8_t gq[8]={0x80,6,0,6,0,0,10,0}; CHECK(usbp_setup(&u,gq,&d)==-1,"device qualifier stalls (full-speed device)");
  uint8_t sa[8]={0,5,42,0,0,0,0,0}; CHECK(usbp_setup(&u,sa,&d)==0&&u.addr_pending&&u.new_addr==42,"SET_ADDRESS 42 pending until status stage");
  uint8_t sc[8]={0,9,1,0,0,0,0,0}; CHECK(usbp_setup(&u,sc,&d)==0&&u.configured&&u.cfg_changed,"SET_CONFIGURATION 1");
  uint8_t slc[8]={0x21,0x20,0,0,0,0,7,0}; CHECK(usbp_setup(&u,slc,&d)==0&&u.out_expect,"SET_LINE_CODING expects 7 data bytes");
  uint8_t lcd[7]={0x00,0xC2,0x01,0x00,0,0,8}; usbp_out_data(&u,lcd,7);   /* 115200 */
  CHECK(u.baud==115200&&u.bits==8&&u.line_changed,"line coding 115200 8N1 stored");
  uint8_t lcd2[7]={0x00,0x08,0x07,0x00,0,0,8}; u.out_expect=1; usbp_out_data(&u,lcd2,7); CHECK(u.baud==460800,"line coding 460800");
  uint8_t glc[8]={0xA1,0x21,0,0,0,0,7,0}; n=usbp_setup(&u,glc,&d); CHECK(n==7&&d[0]==0x00&&d[1]==0x08&&d[2]==0x07,"GET_LINE_CODING returns it");
  uint8_t cl[8]={0x21,0x22,3,0,0,0,0,0}; usbp_setup(&u,cl,&d); CHECK(u.dtr==1&&u.rts==1,"control lines: DTR+RTS");
  uint8_t io0,en;
  usbp_esp_pins(0,1,&io0,&en); CHECK(en==1&&io0==0,"esptool step 1 (RTS only): EN low = reset");
  usbp_esp_pins(1,0,&io0,&en); CHECK(en==0&&io0==1,"esptool step 2 (DTR only): EN released, GPIO0 low = boot loader");
  usbp_esp_pins(0,0,&io0,&en); CHECK(!en&&!io0,"idle: both released");
  usbp_esp_pins(1,1,&io0,&en); CHECK(!en&&!io0,"both asserted (port open): both released, no accidental reset");
  uint8_t bad[8]={0x40,0x77,0,0,0,0,0,0}; CHECK(usbp_setup(&u,bad,&d)==-1,"unknown vendor request stalls");
  printf("\n%d checks, %d failed\n",checks,fails); return fails!=0; }
