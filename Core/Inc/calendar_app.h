#ifndef CALENDAR_APP_H
#define CALENDAR_APP_H
#include <stdint.h>
void Calendar_App_Enter(void);                              /* shows the current month */
void Calendar_App_Process(char key, uint32_t current_tick); /* LEFT/RIGHT month, UP/DOWN year, PLAY today, GEAR back */
#endif
