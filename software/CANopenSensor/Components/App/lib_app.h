/**
 ************************************************************************************************************
 *  \file               lib_app.h
 *  \brief
 *  \author             caipiblack
 *  \version            1.0
 *  \date               01/06/2024
 *  \copyright          
 ************************************************************************************************************
 */

#ifndef APP_INC_LIB_APP_H_
#define APP_INC_LIB_APP_H_

/************************************************************************************************************
 * Standard included files
 ************************************************************************************************************/

/************************************************************************************************************
 * Project included files
 ************************************************************************************************************/
#include "stm32l4xx_hal.h"
/************************************************************************************************************
 * Exported define
 ************************************************************************************************************/

/************************************************************************************************************
 * Exported types
 ************************************************************************************************************/

/************************************************************************************************************
 * Exported Constant data
 ************************************************************************************************************/

/************************************************************************************************************
 * Exported data
 ************************************************************************************************************/

/************************************************************************************************************
 * Exported functions declaration
 ************************************************************************************************************/
void APP_Init(CAN_HandleTypeDef *hCan, TIM_HandleTypeDef *hTim,
		void (*hCanHWInitFunction)(), UART_HandleTypeDef *hUart,
		TIM_HandleTypeDef *hTimPwm);
void APP_Start(void);
void APP_ExecFromMainLoop(void);
/************************************************************************************************************
 * Exported macros
 ************************************************************************************************************/

#endif /* APP_INC_LIB_APP_H_ */
