/**
 ************************************************************************************************************
 *  \file               app.c
 *  \brief
 *  \author             caipiblack
 *  \version            1.0
 *  \date               01/06/2024
 *  \copyright          
 ************************************************************************************************************
 */
/************************************************************************************************************
 * Standard included files
 ************************************************************************************************************/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/************************************************************************************************************
 * Project included files
 ************************************************************************************************************/
// uShell
#include "sys_command_line.h"
// CANopen Stack
#include "CO_app_STM32.h"
#include "OD.h"
// App includes
#include "Inc/app.h"
#include "main.h"
/************************************************************************************************************
 * Local define
 ************************************************************************************************************/
#define SENSOR_RESET_TIMEOUT_MS  (5000)
// The NodeID in CANopen can be 1..127, but as we reserve the NodeID 1 for the controller
// We configure the minimal value as 2
#define NODE_ID_MIN              (2)
#define NODE_ID_MAX              (127)
#define DEFAULT_CAN_ID           (NODE_ID_MIN)
/************************************************************************************************************
 * Local Types
 ************************************************************************************************************/
typedef enum {
	SENSOR_STATE_IDLE = 0,
	SENSOR_STATE_MOUVEMENT = 1,
	SENSOR_STATE_VIBRATION = 2,
	SENSOR_STATE_MOUVEMENT_AND_VIBRATION = 3,
} SensorStates_t;

#pragma pack(push, 1)
typedef struct {
	uint8_t u8CanId;
	uint8_t au8Reserved[63];
} Configuration_t;
#pragma pack(pop)
/************************************************************************************************************
 * Local data
 ************************************************************************************************************/
uint8_t g_u8GlobalState = SENSOR_STATE_IDLE;
uint8_t g_u8PreviousState = SENSOR_STATE_IDLE;
uint32_t g_u32MouvementTriggeredTick = 0;
uint32_t g_u32VibrationTriggeredTick = 0;
Configuration_t g_xConfiguration;
CANopenNodeSTM32 g_xCanOpenNodeSTM32;
/************************************************************************************************************
 * Constant local data
 ************************************************************************************************************/
const char cli_display_help[] = "display informations about the module.";
const char cli_restore_help[] = "Restore the default configuration.";
const char cli_set_node_id_help[] = "Change the node-id value.";
const char cli_store_config_help[] = "Store the configuration in flash.";
const char cli_load_config_help[] = "Load the configuration from flash.";
/************************************************************************************************************
 * Constant exported data
 ************************************************************************************************************/

/************************************************************************************************************
 * Exported data
 ************************************************************************************************************/

/************************************************************************************************************
 * Local macros
 ************************************************************************************************************/

/************************************************************************************************************
 * Local function prototypes
 ************************************************************************************************************/
static uint8_t CliDisplay(int argc, char *argv[]);
static uint8_t CliRestoreConfiguration(int argc, char *argv[]);
static uint8_t CliStoreConfig(int argc, char *argv[]);
static uint8_t ClitLoadConfig(int argc, char *argv[]);
static uint8_t CliSetNodeId(int argc, char *argv[]);
static void DisplayConfiguration(Configuration_t *config,
		CANopenNodeSTM32 *canOpenNodeSTM32);
static void RestoreFactoryDefault(Configuration_t *config);
static void LoadConfiguration(Configuration_t *config);
static int32_t StoreConfiguration(Configuration_t *config);
static int32_t CheckConfiguration(Configuration_t *config);
/************************************************************************************************************
 * Exported functions declaration
 ************************************************************************************************************/
void APP_Init(CAN_HandleTypeDef *hCan, TIM_HandleTypeDef *hTim,
		void (*hCanHWInitFunction)(), UART_HandleTypeDef *hUart) {
	CLI_INIT(hUart, USART2_IRQn);
	CLI_ADD_CMD("display", cli_display_help, CliDisplay);
	CLI_ADD_CMD("restore", cli_restore_help, CliRestoreConfiguration);
	CLI_ADD_CMD("store-config", cli_store_config_help, CliStoreConfig);
	CLI_ADD_CMD("load-config", cli_load_config_help, ClitLoadConfig);
	CLI_ADD_CMD("set-node-id", cli_set_node_id_help, CliSetNodeId);

	// Load the configuration from NVS
	LoadConfiguration(&g_xConfiguration);

	// CANopen Stack
	g_xCanOpenNodeSTM32.CANHandle = hCan;
	g_xCanOpenNodeSTM32.HWInitFunction = hCanHWInitFunction;
	g_xCanOpenNodeSTM32.timerHandle = hTim;
	g_xCanOpenNodeSTM32.desiredNodeID = g_xConfiguration.u8CanId;
	g_xCanOpenNodeSTM32.baudrate = 250;
	canopen_app_init(&g_xCanOpenNodeSTM32);
}

void APP_Start(void) {
	// Configure default state
	OD_set_u8(OD_find(OD, 0x6000), 0X00, g_u8GlobalState, false);
}

void APP_ExecFromMainLoop(void) {
	// uShell
	CLI_RUN();
	// CANopen Stack
	canopen_app_process();
	// Application
	uint32_t u32CurrentTicks = HAL_GetTick();
	if (((g_u8GlobalState & SENSOR_STATE_MOUVEMENT) == SENSOR_STATE_MOUVEMENT)
			&& (u32CurrentTicks - g_u32MouvementTriggeredTick
					> SENSOR_RESET_TIMEOUT_MS)
			&& (HAL_GPIO_ReadPin(GPIO_Mouvement_GPIO_Port, GPIO_Mouvement_Pin)
					== GPIO_PIN_RESET)) {
		// Reset the sensor state if current state is cleared and
		// the time since the last trigger is greater than SENSOR_RESET_TIMEOUT_MS
		g_u8GlobalState &= ~SENSOR_STATE_MOUVEMENT;
		g_u32MouvementTriggeredTick = 0;
	}

	if (((g_u8GlobalState & SENSOR_STATE_VIBRATION) == SENSOR_STATE_VIBRATION)
			&& (u32CurrentTicks - g_u32VibrationTriggeredTick
					> SENSOR_RESET_TIMEOUT_MS)
			&& (HAL_GPIO_ReadPin(GPIO_Vibration_GPIO_Port, GPIO_Vibration_Pin)
					== GPIO_PIN_RESET)) {
		// Reset the sensor state if current state is cleared and
		// the time since the last trigger is greater than SENSOR_RESET_TIMEOUT_MS
		g_u8GlobalState &= ~SENSOR_STATE_VIBRATION;
		g_u32VibrationTriggeredTick = 0;
	}

	// Update the OD if the global state changes
	if (g_u8GlobalState != g_u8PreviousState) {
		g_u8PreviousState = g_u8GlobalState;
		OD_set_u8(OD_find(OD, 0x6000), 0X00, g_u8GlobalState, false);
		CO_TPDOsendRequest(&g_xCanOpenNodeSTM32.canOpenStack->TPDO[0]);
	}

	if (OD_PERSIST_COMM.x6001_LED > 0) {
		HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
	} else {
		HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
	}
}
/************************************************************************************************************
 * Local functions declaration
 ************************************************************************************************************/
static void DisplayConfiguration(Configuration_t *config,
		CANopenNodeSTM32 *canOpenNodeSTM32) {
	if (config != NULL) {
		printf("-------------- Parameters --------------\n");
		printf("  - Desired NodeID: %d\n", g_xConfiguration.u8CanId);
		if (canOpenNodeSTM32 != NULL) {
			printf("  - Active NodeID: %d\n", canOpenNodeSTM32->activeNodeID);
		}
		printf("----------------------------------------\n");
	}
}

static void RestoreFactoryDefault(Configuration_t *config) {
	if (config != NULL) {
		memset(config, 0x00, sizeof(Configuration_t));
		config->u8CanId = DEFAULT_CAN_ID;
	}
}

static void LoadConfiguration(Configuration_t *config) {
	uint32_t address = FLASH_USER_START_ADDR;
	uint64_t *config64 = (uint64_t*) config;

	for (int i = 0; i < FLASH_ROW_SIZE; i++) {
		uint64_t data64 = *(__IO uint64_t*) address;
		uint64_t *data = config64 + i;
		*data = data64;
		address = address + sizeof(uint64_t);
	}

	if (CheckConfiguration(config)) {
		DBG(
				"Invalid or no configuration in NVS, initializing default configuration..");
		RestoreFactoryDefault(config);
		StoreConfiguration(config);
	}
}

static int32_t StoreConfiguration(Configuration_t *config) {
	int32_t result = EXIT_SUCCESS;
	uint32_t PAGEError = 0;

	// STM32L432xx devices feature up to 256 Kbyte of embedded Flash memory available for
	// storing programs and data in single bank architecture. The Flash memory contains 128
	// pages of 2 Kbyte.

	HAL_FLASH_Unlock();
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR);
	static FLASH_EraseInitTypeDef EraseInitStruct;
	EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
	EraseInitStruct.Banks = FLASH_BANK_1;
	EraseInitStruct.Page = 127;
	EraseInitStruct.NbPages = 1;
	if (HAL_FLASHEx_Erase(&EraseInitStruct, &PAGEError) != HAL_OK) {
		result = EXIT_FAILURE;
		ERR("Failed to erase flash");
	} else {
		uint32_t offset = 0;
		uint64_t *data = (uint64_t*) config;
		uint32_t address = FLASH_USER_START_ADDR;
		for (int i = 0; i < FLASH_ROW_SIZE; i++) {
			if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
					address + offset, (uint64_t) *data++) != HAL_OK) {
				result = EXIT_FAILURE;
				ERR("Failed to write page!");
				break;
			}
			offset += sizeof(uint64_t);
		}
	}
	HAL_FLASH_Lock();

	if (result == EXIT_SUCCESS) {
		DBG("Configuration stored!");
	} else {
		ERR("Failed to store configuration!");
	}

	return result;
}

static int32_t CheckConfiguration(Configuration_t *config) {
	if (config->u8CanId < NODE_ID_MIN || config->u8CanId > NODE_ID_MAX) {
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static uint8_t CliDisplay(int argc, char *argv[]) {
	DisplayConfiguration(&g_xConfiguration, &g_xCanOpenNodeSTM32);
	return EXIT_SUCCESS;
}

static uint8_t CliRestoreConfiguration(int argc, char *argv[]) {
	RestoreFactoryDefault(&g_xConfiguration);
	return EXIT_SUCCESS;
}

static uint8_t CliStoreConfig(int argc, char *argv[]) {
	StoreConfiguration(&g_xConfiguration);
	return EXIT_SUCCESS;
}

static uint8_t ClitLoadConfig(int argc, char *argv[]) {
	LoadConfiguration(&g_xConfiguration);
	return EXIT_SUCCESS;
}

static uint8_t CliSetNodeId(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: \"%s {node-id}\".\n", argv[0]);
		printf("  - node-id: (%d-%d).\n", NODE_ID_MIN, NODE_ID_MAX);
		NL1();
		return EXIT_FAILURE;
	}

	uint8_t u8Id = atoi(argv[1]);
	if (u8Id < NODE_ID_MIN || u8Id > NODE_ID_MAX) {
		return EXIT_FAILURE;
	}

	g_xConfiguration.u8CanId = u8Id;

	return EXIT_SUCCESS;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	// Handle CANOpen app interrupts
	if (htim == canopenNodeSTM32->timerHandle) {
		canopen_app_interrupt();
	}
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	// We only read when the sensor is triggered! We automatically clear the triggered in main loop!
	switch (GPIO_Pin) {
	case GPIO_Mouvement_Pin:
		g_u8GlobalState |= SENSOR_STATE_MOUVEMENT;
		g_u32MouvementTriggeredTick = HAL_GetTick();
		break;
	case GPIO_Vibration_Pin:
		g_u8GlobalState |= SENSOR_STATE_VIBRATION;
		g_u32VibrationTriggeredTick = HAL_GetTick();
		break;
	}
}
