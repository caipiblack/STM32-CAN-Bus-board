/**
 ************************************************************************************************************
 *  \file               app.h
 *  \brief
 *  \author             caipiblack
 *  \version            1.0
 *  \date               01/06/2024
 *  \copyright          
 ************************************************************************************************************
 */

#ifndef APP_INC_APP_H_
#define APP_INC_APP_H_

/************************************************************************************************************
 * Standard included files
 ************************************************************************************************************/

/************************************************************************************************************
 * Project included files
 ************************************************************************************************************/

/************************************************************************************************************
 * Exported define
 ************************************************************************************************************/
#define ADDR_FLASH_PAGE_127    			((uint32_t)0x0803F800) /* Base @ of Page 127, 2 Kbytes */
#define FLASH_USER_START_ADDR   		ADDR_FLASH_PAGE_127   /* Start @ of user Flash area */
#define FLASH_USER_END_ADDR     		ADDR_FLASH_PAGE_127 + FLASH_PAGE_SIZE - 1   /* End @ of user Flash area */
#define FLASH_ROW_SIZE          		1 // We data is read by 64bits (8bytes), so: sizeof(Configuration_t/8)
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

#endif /* APP_INC_APP_H_ */
