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
#include <inttypes.h>
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

typedef enum {
	CONTROLLER_STATE_IDLE,
	CONTROLLER_STATE_ARMED,
	CONTROLLER_STATE_TEMPO,
	CONTROLLER_STATE_ALARM,
} ControllerStates_t;

typedef enum {
	BUZZER_DISABLED = 0,
	BUZZER_BEEP_ON_TEMPO = 1,
	BUZZER_BEEP_ON_ALARM = 2,
	BUZZER_BEEP_ALL = 3
} BuzzerConfig_t;

typedef enum {
	LED_DISABLED = 0,
	LED_ENABLE_ON_DETECTION = 1,
	LED_BLINK_ON_ARMED = 2,
	LED_BLINK_ALL = 3
} LedConfig_t;

#pragma pack(push, 1)
// Size must be multiple of 8 (64 bits)
typedef struct {
	uint8_t u8CanId;
	uint8_t u8BuzzerConfig; // BuzzerConfig_t
	uint8_t u8LedConfig; // LedConfig_t
	uint8_t au8Reserved[5];
} Configuration_t;
#pragma pack(pop)

typedef struct {
	uint8_t u8Enabled;
	uint8_t u8CurrentState;
	uint32_t u32NextSwitchTick;
} BuzzerWorkingStruct_t;
/************************************************************************************************************
 * Local data
 ************************************************************************************************************/
uint8_t g_u8GlobalState = SENSOR_STATE_IDLE;
uint8_t g_u8PreviousState = SENSOR_STATE_IDLE;
uint8_t g_u8ControllerState = 0;
uint32_t g_u32MouvementTriggeredTick = 0;
uint32_t g_u32VibrationTriggeredTick = 0;
Configuration_t g_xConfiguration;
CANopenNodeSTM32 g_xCanOpenNodeSTM32;
TIM_HandleTypeDef *g_pxPwmTimer;
BuzzerWorkingStruct_t buzzerWorkingStruct;
BuzzerWorkingStruct_t ledWorkingStruct;
/************************************************************************************************************
 * Constant local data
 ************************************************************************************************************/
const char cli_display_help[] = "display informations about the module.";
const char cli_restore_help[] = "Restore the default configuration.";
const char cli_set_node_id_help[] = "Change the node-id value.";
const char cli_set_buzzer_config_help[] = "Change the buzzer configuration.";
const char cli_set_led_config_help[] = "Change the LED configuration.";
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
static uint8_t CliSetBuzzerConfig(int argc, char *argv[]);
static uint8_t CliSetLedConfig(int argc, char *argv[]);
static void DisplayConfiguration(Configuration_t *config,
		CANopenNodeSTM32 *canOpenNodeSTM32);
static void RestoreFactoryDefault(Configuration_t *config);
static void LoadConfiguration(Configuration_t *config);
static int32_t StoreConfiguration(Configuration_t *config);
static int32_t CheckConfiguration(Configuration_t *config);
static void vProcessBuzzerOrLed(uint32_t u32CurrentTicks,
		uint32_t u32HighDuration, uint32_t u32LowDuration,
		uint8_t u8BuzzerOrLed);
static void vChangeBuzzerOrLedState(uint8_t u8State, uint8_t u8BuzzerOrLed);
/************************************************************************************************************
 * Exported functions declaration
 ************************************************************************************************************/
void APP_Init(CAN_HandleTypeDef *hCan, TIM_HandleTypeDef *hTim,
		void (*hCanHWInitFunction)(), UART_HandleTypeDef *hUart,
		TIM_HandleTypeDef *hTimPwm) {
	g_pxPwmTimer = hTimPwm;

	CLI_INIT(hUart, USART2_IRQn);
	CLI_ADD_CMD("display", cli_display_help, CliDisplay);
	CLI_ADD_CMD("restore", cli_restore_help, CliRestoreConfiguration);
	CLI_ADD_CMD("store-config", cli_store_config_help, CliStoreConfig);
	CLI_ADD_CMD("load-config", cli_load_config_help, ClitLoadConfig);
	CLI_ADD_CMD("set-node-id", cli_set_node_id_help, CliSetNodeId);
	CLI_ADD_CMD("set-buzzer-config", cli_set_buzzer_config_help,
			CliSetBuzzerConfig);
	CLI_ADD_CMD("set-led-config", cli_set_led_config_help, CliSetLedConfig);

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
	uint32_t u32CurrentTicks = HAL_GetTick();
	// uShell
	CLI_RUN();
	// CANopen Stack
	canopen_app_process();
	// Read OD variables
	if (OD_PERSIST_COMM.x6001_controllerState != g_u8ControllerState) {
		g_u8ControllerState = OD_PERSIST_COMM.x6001_controllerState;
		DBG("Controller state changed to: 0x%02x", g_u8ControllerState);
	}
	// Check motion detection sensor
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
	// Check vibration detection sensor
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

	if ((g_u8GlobalState != SENSOR_STATE_IDLE)
			&& ((g_xConfiguration.u8LedConfig & LED_ENABLE_ON_DETECTION)
					== LED_ENABLE_ON_DETECTION)) {
		// Led ON
		HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
	} else if ((g_u8ControllerState != CONTROLLER_STATE_IDLE)
			&& ((g_xConfiguration.u8LedConfig & LED_BLINK_ON_ARMED)
					== LED_BLINK_ON_ARMED)) {
		// Blink (Blink 100ms every 2s)
		vProcessBuzzerOrLed(u32CurrentTicks, 100, 2000, 0);
	} else {
		if (ledWorkingStruct.u8Enabled == 1) {
			ledWorkingStruct.u8Enabled = 0;
			ledWorkingStruct.u8CurrentState = 0;
			ledWorkingStruct.u32NextSwitchTick = 0;
			DBG("Led: Stop");
		}
		HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
	}

	if ((g_u8ControllerState == CONTROLLER_STATE_ALARM)
			&& ((g_xConfiguration.u8BuzzerConfig & BUZZER_BEEP_ON_ALARM)
					== BUZZER_BEEP_ON_ALARM)) {
		// Beep for alarm. (Beep 100ms every 0.5s)
		vProcessBuzzerOrLed(u32CurrentTicks, 100, 500, 1);
	} else if ((g_u8ControllerState == CONTROLLER_STATE_TEMPO)
			&& ((g_xConfiguration.u8BuzzerConfig & BUZZER_BEEP_ON_TEMPO)
					== BUZZER_BEEP_ON_TEMPO)) {
		// Beep for tempo. (Beep 100ms every 2s)
		vProcessBuzzerOrLed(u32CurrentTicks, 100, 2000, 1);
	} else {
		if (buzzerWorkingStruct.u8Enabled == 1) {
			buzzerWorkingStruct.u8Enabled = 0;
			buzzerWorkingStruct.u8CurrentState = 0;
			buzzerWorkingStruct.u32NextSwitchTick = 0;
			DBG("Buzzer: Stop");
			HAL_TIM_PWM_Stop(g_pxPwmTimer, TIM_CHANNEL_1);
		}
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
		printf("  - Buzzer configuration: %d\n",
				g_xConfiguration.u8BuzzerConfig);
		printf("  - LED configuration: %d\n", g_xConfiguration.u8LedConfig);
		printf("---------------- Status ----------------\n");
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
		config->u8BuzzerConfig = BUZZER_DISABLED;
		config->u8LedConfig = LED_DISABLED;
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

	if (config->u8BuzzerConfig > BUZZER_BEEP_ALL) {
		return EXIT_FAILURE;
	}

	if (config->u8LedConfig > LED_BLINK_ALL) {
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

static uint8_t CliSetBuzzerConfig(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: \"%s {config}\".\n", argv[0]);
		printf("  - config:\n");
		printf("       - 0: Disabled\n");
		printf("       - 1: Beep on TEMPO\n");
		printf("       - 2: Beep on ALARM\n");
		printf("       - 3: Beep on TEMPO or ALARM\n");
		NL1();
		return EXIT_FAILURE;
	}

	uint8_t u8Value = atoi(argv[1]);
	if (u8Value > BUZZER_BEEP_ALL) {
		return EXIT_FAILURE;
	}

	g_xConfiguration.u8BuzzerConfig = u8Value;

	return EXIT_SUCCESS;
}

static uint8_t CliSetLedConfig(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: \"%s {config}\".\n", argv[0]);
		printf("  - config:\n");
		printf("       - 0: Disabled\n");
		printf("       - 1: Blink on detection\n");
		printf("       - 2: Blink when the system is armed\n");
		printf("       - 3: Blink on detection or when the system is armed\n");
		NL1();
		return EXIT_FAILURE;
	}

	uint8_t u8Value = atoi(argv[1]);
	if (u8Value > LED_BLINK_ALL) {
		return EXIT_FAILURE;
	}

	g_xConfiguration.u8LedConfig = u8Value;

	return EXIT_SUCCESS;
}

static void vProcessBuzzerOrLed(uint32_t u32CurrentTicks,
		uint32_t u32HighDuration, uint32_t u32LowDuration,
		uint8_t u8BuzzerOrLed) {
	BuzzerWorkingStruct_t *pWorkingStruct =
			(u8BuzzerOrLed == 1) ? &buzzerWorkingStruct : &ledWorkingStruct;
	const char *pcPeripheralName = (u8BuzzerOrLed == 1) ? "Buzzer" : "Led";

	if (pWorkingStruct->u8Enabled == 0) {
		// We have to start the buzzer, we start HIGH
		pWorkingStruct->u32NextSwitchTick = u32HighDuration + u32CurrentTicks;
		pWorkingStruct->u8CurrentState = 1;
		pWorkingStruct->u8Enabled = 1;
		// Start the timer
		DBG("%s: Start (HighT=%" PRIu32 ", LowT=%" PRIu32 ")", pcPeripheralName,
				u32HighDuration, u32LowDuration);
		vChangeBuzzerOrLedState(1, u8BuzzerOrLed);
	} else {
		// The buzzer is already started, detect if we have to change the state
		if (u32CurrentTicks > pWorkingStruct->u32NextSwitchTick) {
			// Switch the state
			if (pWorkingStruct->u8CurrentState == 1) {
				pWorkingStruct->u32NextSwitchTick = u32LowDuration
						+ u32CurrentTicks;
				pWorkingStruct->u8CurrentState = 0;
				vChangeBuzzerOrLedState(0, u8BuzzerOrLed);
				DBG("%s: Switch OFF", pcPeripheralName);
			} else {
				pWorkingStruct->u32NextSwitchTick = u32HighDuration
						+ u32CurrentTicks;
				pWorkingStruct->u8CurrentState = 1;
				vChangeBuzzerOrLedState(1, u8BuzzerOrLed);
				DBG("%s: Switch ON", pcPeripheralName);
			}
		}
	}
}

static void vChangeBuzzerOrLedState(uint8_t u8State, uint8_t u8BuzzerOrLed) {
	if (u8State) {
		if (u8BuzzerOrLed == 1) {
			HAL_TIM_PWM_Start(g_pxPwmTimer, TIM_CHANNEL_1);
		} else {
			HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_SET);
		}
	} else {
		if (u8BuzzerOrLed == 1) {
			HAL_TIM_PWM_Stop(g_pxPwmTimer, TIM_CHANNEL_1);
		} else {
			HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);
		}
	}
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
