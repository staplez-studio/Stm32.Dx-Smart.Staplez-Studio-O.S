#include <stdio.h>
#include "clock_zone.h"
int main(void){ for(int y=2000;y<=2099;y++) for(int m=1;m<=12;m++) printf("%d %d %d %d\n",y,m,tz_weekday(y,m,1),tz_days_in_month(y,m)); return 0; }
