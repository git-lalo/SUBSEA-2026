/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Thruster 12V Controller - UiS Subsea 2025/2026
  * @description    : Receives CAN ID 33 (ROVCMD) with 8x int8 thruster values
  *                   Converts to PWM (1100-1900µs) for ESCs
  *                   Sends heartbeat on CAN ID 158 (HB12VTHR)
  ******************************************************************************
  * Hardware: STM32G431KB / STM32G431RB
  * CAN Bitrate: 500 kbps
  * PWM Frequency: 50 Hz (20ms period) for ESC control
  * 
  * Pin Configuration (adjust in STM32CubeMX):
  *   - PA11/PA12: FDCAN1 RX/TX
  *   - TIM1 CH1-4: PWM outputs for thrusters 1-4
  *   - TIM2 CH1-4: PWM outputs for thrusters 5-8
  ******************************************************************************
  */

#include "main.h"
#include <string.h>
#include <stdbool.h>

/* ==================== CONFIGURATION ==================== */
#define THRUSTER_COUNT      8
#define PWM_NEUTRAL         1500    // Neutral PWM in µs (ESC arm position)
#define PWM_MIN             1100    // Minimum PWM in µs (full reverse)
#define PWM_MAX             1900    // Maximum PWM in µs (full forward)
#define INPUT_MIN           -100    // Minimum input value
#define INPUT_MAX           100     // Maximum input value

#define CAN_ID_ROVCMD       33      // CAN ID for thruster commands
#define CAN_ID_HEARTBEAT_RX 126     // CAN ID for heartbeat request
#define CAN_ID_HEARTBEAT_TX 158     // CAN ID for heartbeat response (HB12VTHR)

#define HEARTBEAT_TIMEOUT_MS 1000   // Safety timeout - stop if no commands
#define LED_BLINK_OK_MS      500    // LED blink when OK
#define LED_BLINK_ERROR_MS   100    // LED blink when error

/* ==================== VARIABLES ==================== */
FDCAN_HandleTypeDef hfdcan1;
TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

FDCAN_FilterTypeDef sFilterConfig;
FDCAN_TxHeaderTypeDef TxHeader;
FDCAN_RxHeaderTypeDef RxHeader;
uint8_t TxData[8] = {0};
uint8_t RxData[8] = {0};

// Thruster values (-100 to 100)
volatile int8_t thrusterValues[THRUSTER_COUNT] = {0};
volatile uint32_t lastCommandTime = 0;
volatile bool canConnected = false;
volatile bool armed = false;

/* ==================== FUNCTION PROTOTYPES ==================== */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_FDCAN1_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);

void CAN_Init(void);
void CAN_SendHeartbeat(void);
uint16_t ConvertToPWM(int8_t value);
void UpdatePWMOutputs(void);
void SafetyCheck(void);
void SetAllThrustersNeutral(void);
void SetLED(bool state);

/* ==================== PWM CONVERSION ==================== */
/**
 * @brief Convert thruster value (-100 to 100) to PWM microseconds (1100-1900)
 * @param value: Input value from -100 to 100
 * @return PWM value in microseconds
 */
uint16_t ConvertToPWM(int8_t value) {
    // Clamp input to valid range
    if (value < INPUT_MIN) value = INPUT_MIN;
    if (value > INPUT_MAX) value = INPUT_MAX;
    
    // Linear mapping: -100 -> 1100, 0 -> 1500, 100 -> 1900
    // PWM = 1500 + (value * 4)
    int16_t pwm = PWM_NEUTRAL + (value * 4);
    
    // Safety clamp
    if (pwm < PWM_MIN) pwm = PWM_MIN;
    if (pwm > PWM_MAX) pwm = PWM_MAX;
    
    return (uint16_t)pwm;
}

/* ==================== PWM OUTPUT ==================== */
/**
 * @brief Update all PWM outputs based on thruster values
 * Timer CCR values are in timer ticks, not microseconds
 * With 50Hz PWM and 1MHz timer clock: 1µs = 1 tick
 */
void UpdatePWMOutputs(void) {
    if (!armed) {
        SetAllThrustersNeutral();
        return;
    }
    
    // TIM1: Thrusters 1-4 (CH1-CH4)
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ConvertToPWM(thrusterValues[0]));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, ConvertToPWM(thrusterValues[1]));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, ConvertToPWM(thrusterValues[2]));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, ConvertToPWM(thrusterValues[3]));
    
    // TIM2: Thrusters 5-8 (CH1-CH4)
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, ConvertToPWM(thrusterValues[4]));
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, ConvertToPWM(thrusterValues[5]));
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, ConvertToPWM(thrusterValues[6]));
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, ConvertToPWM(thrusterValues[7]));
}

/**
 * @brief Set all thrusters to neutral (1500µs) for safety
 */
void SetAllThrustersNeutral(void) {
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, PWM_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, PWM_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, PWM_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, PWM_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, PWM_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, PWM_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, PWM_NEUTRAL);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, PWM_NEUTRAL);
}

/* ==================== CAN COMMUNICATION ==================== */
/**
 * @brief Initialize CAN bus with filter for ROVCMD and heartbeat
 */
void CAN_Init(void) {
    // Filter: Accept CAN IDs 32-63 (Reguleringskort range)
    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = 0x20;     // Base ID 32
    sFilterConfig.FilterID2 = 0xE0;     // Mask: accept 32-63
    
    // TX Header setup
    TxHeader.IdType = FDCAN_STANDARD_ID;
    TxHeader.TxFrameType = FDCAN_DATA_FRAME;
    TxHeader.DataLength = FDCAN_DLC_BYTES_8;
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker = 0;
    
    HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig);
    
    // Also accept heartbeat request (ID 126)
    sFilterConfig.FilterIndex = 1;
    sFilterConfig.FilterID1 = 0x7E;     // ID 126
    sFilterConfig.FilterID2 = 0xFF;     // Exact match
    HAL_FDCAN_ConfigFilter(&hfdcan1, &sFilterConfig);
    
    HAL_FDCAN_Start(&hfdcan1);
    HAL_FDCAN_ActivateNotification(&hfdcan1, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0);
}

/**
 * @brief Send heartbeat response on CAN ID 158
 */
void CAN_SendHeartbeat(void) {
    memcpy(TxData, "polo!\n", 6);
    TxData[6] = armed ? 1 : 0;
    TxData[7] = 0;
    
    TxHeader.Identifier = CAN_ID_HEARTBEAT_TX;
    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &TxHeader, TxData);
}

/**
 * @brief CAN RX Callback - Process incoming messages
 */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs) {
    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET) {
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &RxHeader, RxData) != HAL_OK) {
            Error_Handler();
        }
        
        if (HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK) {
            Error_Handler();
        }
        
        switch (RxHeader.Identifier) {
            case CAN_ID_ROVCMD:
                // Received thruster command: 8x int8 values (-100 to 100)
                for (int i = 0; i < THRUSTER_COUNT; i++) {
                    thrusterValues[i] = (int8_t)RxData[i];
                }
                lastCommandTime = HAL_GetTick();
                canConnected = true;
                
                // Arm ESCs after first valid command
                if (!armed) {
                    armed = true;
                }
                break;
                
            case CAN_ID_HEARTBEAT_RX:
                // Heartbeat request - respond immediately
                CAN_SendHeartbeat();
                break;
                
            default:
                break;
        }
    }
}

/* ==================== SAFETY ==================== */
/**
 * @brief Safety check - disable thrusters if no commands received
 */
void SafetyCheck(void) {
    uint32_t currentTime = HAL_GetTick();
    
    if (currentTime - lastCommandTime > HEARTBEAT_TIMEOUT_MS) {
        // No commands received - safety shutdown
        for (int i = 0; i < THRUSTER_COUNT; i++) {
            thrusterValues[i] = 0;
        }
        canConnected = false;
        // Keep armed but set neutral
        SetAllThrustersNeutral();
    }
}

/* ==================== LED ==================== */
void SetLED(bool state) {
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* ==================== MAIN ==================== */
int main(void) {
    HAL_Init();
    SystemClock_Config();
    
    MX_GPIO_Init();
    MX_FDCAN1_Init();
    MX_TIM1_Init();
    MX_TIM2_Init();
    
    // Initialize CAN
    CAN_Init();
    
    // Start PWM outputs
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
    
    // Set initial neutral position (ESCs will arm on 1500µs)
    SetAllThrustersNeutral();
    
    uint32_t lastLedToggle = 0;
    
    while (1) {
        // Update PWM outputs
        UpdatePWMOutputs();
        
        // Safety check
        SafetyCheck();
        
        // LED indication
        uint32_t blinkInterval = canConnected ? LED_BLINK_OK_MS : LED_BLINK_ERROR_MS;
        if (HAL_GetTick() - lastLedToggle > blinkInterval) {
            HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
            lastLedToggle = HAL_GetTick();
        }
        
        HAL_Delay(10);  // 100Hz update rate
    }
}

/* ==================== PERIPHERAL INIT (STM32CubeMX generated) ==================== */

/**
  * @brief FDCAN1 Initialization Function
  * @note  Configure for 500 kbps with 170 MHz clock
  */
static void MX_FDCAN1_Init(void) {
    hfdcan1.Instance = FDCAN1;
    hfdcan1.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission = ENABLE;
    hfdcan1.Init.TransmitPause = DISABLE;
    hfdcan1.Init.ProtocolException = DISABLE;
    // 500 kbps @ 170 MHz: Prescaler=20, TimeSeg1=14, TimeSeg2=2
    hfdcan1.Init.NominalPrescaler = 20;
    hfdcan1.Init.NominalSyncJumpWidth = 1;
    hfdcan1.Init.NominalTimeSeg1 = 14;
    hfdcan1.Init.NominalTimeSeg2 = 2;
    hfdcan1.Init.DataPrescaler = 20;
    hfdcan1.Init.DataSyncJumpWidth = 1;
    hfdcan1.Init.DataTimeSeg1 = 14;
    hfdcan1.Init.DataTimeSeg2 = 2;
    hfdcan1.Init.StdFiltersNbr = 2;
    hfdcan1.Init.ExtFiltersNbr = 0;
    hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    
    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) {
        Error_Handler();
    }
}

/**
  * @brief TIM1 Initialization - PWM for Thrusters 1-4
  * @note  50 Hz PWM (20ms period), 1µs resolution
  *        Timer clock = 170 MHz, Prescaler = 170-1, Period = 20000-1
  */
static void MX_TIM1_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = 170 - 1;           // 170 MHz / 170 = 1 MHz (1µs tick)
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = 20000 - 1;            // 20ms period = 50 Hz
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.RepetitionCounter = 0;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK) {
        Error_Handler();
    }
    
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = PWM_NEUTRAL;            // Initial neutral
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_4);
}

/**
  * @brief TIM2 Initialization - PWM for Thrusters 5-8
  */
static void MX_TIM2_Init(void) {
    TIM_OC_InitTypeDef sConfigOC = {0};
    
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 170 - 1;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 20000 - 1;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        Error_Handler();
    }
    
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = PWM_NEUTRAL;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2);
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3);
    HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4);
}

/**
  * @brief GPIO Initialization
  */
static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // LED pin (LD2)
    HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = LD2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);
}

/**
  * @brief System Clock Configuration (170 MHz)
  */
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
    
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }
    
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
        // Rapid LED blink to indicate error
        HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
        for(volatile int i = 0; i < 100000; i++);
    }
}
