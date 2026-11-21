#include <stdio.h>
#include <string.h>
#include "wifi_net.h"
int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("  [FAIL] %s\n",m); fails++; } }while(0)
int main(void){
  /* SNTP parsing */
  wnet_time_t t;
  CHECK(wnet_parse_sntp("+CIPSNTPTIME:Tue Nov 10 02:15:42 2026",&t) && t.year==2026&&t.month==11&&t.day==10&&t.hour==2&&t.minute==15&&t.second==42&&t.wday==2,"sntp normal (Tue)");
  CHECK(!wnet_parse_sntp("+CIPSNTPTIME:Thu Jan 01 00:00:00 1970",&t),"sntp 1970 = not synced yet");
  CHECK(!wnet_parse_sntp("+CIPSNTPTIME:garbage",&t),"sntp garbage");
  CHECK(!wnet_parse_sntp("OK",&t),"sntp unrelated line");
  CHECK(wnet_parse_sntp("+CIPSNTPTIME:Sun Mar  8 12:00:00 2026",&t) && t.day==8 && t.wday==0,"sntp padded day, Sunday");
  /* weather */
  wnet_weather_t w;
  CHECK(wnet_parse_weather("+2\xC2\xB0""C;80%;Light rain;Toronto, Canada",&w) && w.valid&&w.temp==2&&w.humidity==80&&!strcmp(w.cond,"LIGHT RAIN")&&!strcmp(w.loc,"TORONTO, CANADA"),"weather metric with degree sign");
  CHECK(wnet_parse_weather("-12\xC2\xB0""C;71%;Snow;Ottawa",&w) && w.temp==-12 && wnet_icon_for(w.cond)==WNET_ICON_SNOW,"weather negative + snow icon");
  CHECK(wnet_parse_weather("72\xC2\xB0""F;54%;Sunny;Miami",&w) && w.temp==72 && wnet_icon_for(w.cond)==WNET_ICON_SUN,"weather F sunny");
  CHECK(wnet_parse_weather("+0C;100%;Fog;X",&w) && w.temp==0 && w.humidity==100 && wnet_icon_for(w.cond)==WNET_ICON_FOG,"weather zero / 100%");
  CHECK(!wnet_parse_weather("Unknown location; please try ~ yes",&w),"weather: error text rejected");
  CHECK(!wnet_parse_weather("<html><body>503</body></html>",&w),"weather: html error rejected");
  CHECK(!wnet_parse_weather("",&w),"weather: empty rejected");
  CHECK(!wnet_parse_weather(NULL,&w),"weather: NULL rejected");
  CHECK(wnet_parse_weather("+5C;60%;Patchy rain nearby with thunder and a very long description;Some Very Long Location Name, Country",&w) && strlen(w.cond)<24 && strlen(w.loc)<24,"weather: long fields truncated safely");
  /* http */
  const char *r="HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n+2C;80%;Rain;X\n";
  CHECK(wnet_http_status(r)==200 && !strncmp(wnet_http_body(r),"+2C;",4),"http status/body");
  CHECK(wnet_http_status("HTTP/1.0 404 Not Found\r\n\r\n")==404,"http 404");
  CHECK(wnet_http_status("garbage")==0 && wnet_http_body("no blank line")==NULL,"http garbage");
  char e[40]; wnet_escape_at("a\"b,c\\d",e,sizeof e); CHECK(!strcmp(e,"a\\\"b\\,c\\\\d"),"AT string escaping");
  wnet_escape_at("0123456789012345678901234567890123456789",e,12); CHECK(strlen(e)<12,"escape respects buffer size");
  printf(fails?"UNIT: %d FAILURES\n":"UNIT: all passed\n",fails);
  return fails;
}
