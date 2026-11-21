#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "clock_zone.h"
/* usage: z OFFSET_MIN RULE   then reads "Y M D h m s" UTC lines; prints local time + dst flag.
 * the clock holds STANDARD time, so: utc -> std (store) -> local (display) */
int main(int argc,char**argv){
  tz_config_t cfg={ (int16_t)atoi(argv[1]), (uint8_t)atoi(argv[2]) };
  int Y,M,D,h,m,s;
  int mode = argc>3 ? atoi(argv[3]) : 0;
  while(scanf("%d %d %d %d %d %d",&Y,&M,&D,&h,&m,&s)==6){
    wnet_time_t u={Y,M,D,h,m,s,0}, std, loc, back; bool dst;
    tz_utc_to_std(&cfg,&u,&std);
    tz_std_to_local(&cfg,&std,&loc,&dst);
    if(mode==0){ printf("%04d-%02d-%02d %02d:%02d:%02d w%d %d\n",loc.year,loc.month,loc.day,loc.hour,loc.minute,loc.second,loc.wday,dst); }
    else { /* round trip: local -> std must equal the std we started from, except in the repeated hour */
      tz_local_to_std(&cfg,&loc,&back);
      printf("%d\n", wnet_to_epoch(&back)==wnet_to_epoch(&std) ? 0 : 1); }
  }
  return 0;
}
