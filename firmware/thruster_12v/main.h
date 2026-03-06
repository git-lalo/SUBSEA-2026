/**
  ******************************************************************************
  * @file    main.h
  * @brief   Header for main.c - Thruster 12V Controller
  ******************************************************************************
  */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32g4xx_hal.h"

/* Exported functions prototypes */
void Error_Handler(void);

/* Pin Definitions - Adjust based on your PCB */
#define LD2_Pin         GPIO_PIN_5
#define LD2_GPIO_Port   GPIOA

/* PWM Pin Definitions (example for STM32G431KB) */
// TIM1 - Thrusters 1-4
// CH1: PA8,  CH2: PA9,  CH3: PA10, CH4: PA11
// TIM2 - Thrusters 5-8  
// CH1: PA0,  CH2: PA1,  CH3: PB10, CH4: PB11

/* FDCAN Pin Definitions */
// FDCAN1 RX: PA11 (or PB8)
// FDCAN1 TX: PA12 (or PB9)

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
