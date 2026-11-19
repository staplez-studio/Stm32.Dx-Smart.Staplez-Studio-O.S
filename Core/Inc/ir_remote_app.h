/* USER CODE BEGIN Header */
/**
  ********************************----------------------------------------------
  * @file    ir_remote_app.h
  * @brief   IR Remote Control UI and Real-Time Hardware Signal Receiver (PA12).
  ********************************----------------------------------------------
  */
/* USER CODE END Header */

#ifndef __IR_REMOTE_APP_H__
#define __IR_REMOTE_APP_H__

#include <stdint.h>

void Ir_Remote_App_Init(void);
void Ir_Remote_App_Process(char key, uint32_t current_tick);
void IR_Remote_EXTI_Callback(void);
void IR_Remote_Update(void);

const char* IR_Get_Last_Hex_Str(void);
const char* IR_Get_Last_Button_Name(void);
const char* IR_Get_Last_Action_Desc(void);
void IR_Simulate_Button_Press(uint8_t index);
uint8_t IR_Is_New_Frame_Ready(void);
void IR_Remote_Check_Timeout(void);

#endif /* __IR_REMOTE_APP_H__ */
