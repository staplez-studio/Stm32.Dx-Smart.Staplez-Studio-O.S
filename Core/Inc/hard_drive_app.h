#ifndef HARD_DRIVE_APP_H
#define HARD_DRIVE_APP_H
#include <stdint.h>

void Hard_Drive_App_Init(void);
void Hard_Drive_App_Process(char key, uint32_t current_tick);   /* key: '2' up '8' down '4' left '6' right '#' play '*' gear */

#endif
