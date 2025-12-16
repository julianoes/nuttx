/****************************************************************************
 * boards/arm/stm32h7/kakuteh7v2/include/board.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __BOARDS_ARM_STM32H7_KAKUTEH7V2_INCLUDE_BOARD_H
#define __BOARDS_ARM_STM32H7_KAKUTEH7V2_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#endif

/* Do not include STM32 H7 header files here */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clocking *****************************************************************/

/* The Holybro Kakute H7 V2 board provides the following clock sources:
 *
 *   X1:  8 MHz HSE crystal oscillator
 *   X2:  32.768 KHz crystal for LSE
 *
 * So we have these clock source available within the STM32
 *
 *   HSI: 16 MHz RC factory-trimmed
 *   LSI: 32 KHz RC
 *   HSE: 8 MHz crystal
 *   LSE: 32.768 kHz
 */

#define STM32_BOARD_XTAL        8000000ul

#define STM32_HSI_FREQUENCY     16000000ul
#define STM32_LSI_FREQUENCY     32000
#define STM32_HSE_FREQUENCY     STM32_BOARD_XTAL
#define STM32_LSE_FREQUENCY     32768

/* Main PLL Configuration.
 *
 * PLL source is HSE = 8,000,000
 *
 * When STM32_HSE_FREQUENCY / PLLM <= 2MHz VCOL must be selected.
 * VCOH otherwise.
 *
 * PLL_VCOx = (STM32_HSE_FREQUENCY / PLLM) * PLLN
 * Subject to:
 *
 *     1 <= PLLM <= 63
 *     4 <= PLLN <= 512
 *   150 MHz <= PLL_VCOL <= 420MHz
 *   192 MHz <= PLL_VCOH <= 836MHz
 *
 * SYSCLK  = PLL_VCO / PLLP
 * CPUCLK  = SYSCLK / D1CPRE
 * Subject to
 *
 *   PLLP1   = {2, 4, 6, 8, ..., 128}
 *   PLLP2,3 = {2, 3, 4, ..., 128}
 *   CPUCLK <= 480 MHz
 */

#define STM32_BOARD_USEHSE

#define STM32_PLLCFG_PLLSRC      RCC_PLLCKSELR_PLLSRC_HSE

/* PLL1, wide 4 - 8 MHz input, enable DIVP, DIVQ, DIVR
 *
 *   PLL1_VCO = (8 MHz / 1) * 120 = 960 MHz
 *
 *   PLL1P = PLL1_VCO/2  = 960 MHz / 2 = 480 MHz
 *   PLL1Q = PLL1_VCO/5  = 960 MHz / 5 = 192 MHz (SPI123 clock, max 200 MHz)
 *   PLL1R = PLL1_VCO/4  = 960 MHz / 4 = 240 MHz
 */

#define STM32_PLLCFG_PLL1CFG     (RCC_PLLCFGR_PLL1VCOSEL_WIDE | \
                                  RCC_PLLCFGR_PLL1RGE_4_8_MHZ | \
                                  RCC_PLLCFGR_DIVP1EN | \
                                  RCC_PLLCFGR_DIVQ1EN | \
                                  RCC_PLLCFGR_DIVR1EN)

#define STM32_VCO1_FREQUENCY     ((STM32_HSE_FREQUENCY / 1) * 120)
#define STM32_PLL1P_FREQUENCY    (STM32_VCO1_FREQUENCY / 2)
#define STM32_PLL1Q_FREQUENCY    (STM32_VCO1_FREQUENCY / 5)
#define STM32_PLL1R_FREQUENCY    (STM32_VCO1_FREQUENCY / 4)

#define STM32_PLLCFG_PLL1M       RCC_PLLCKSELR_DIVM1(1)
#define STM32_PLLCFG_PLL1N       RCC_PLL1DIVR_N1(120)
#define STM32_PLLCFG_PLL1P       RCC_PLL1DIVR_P1(2)
#define STM32_PLLCFG_PLL1Q       RCC_PLL1DIVR_Q1(5)
#define STM32_PLLCFG_PLL1R       RCC_PLL1DIVR_R1(4)

/* PLL2, wide 4 - 8 MHz input, enable DIVP, DIVQ, DIVR
 *
 *   PLL2_VCO = (8 MHz / 1) * 75 = 600 MHz
 *
 *   PLL2P = PLL2_VCO/8  = 600 MHz / 8  = 75 MHz
 *   PLL2Q = PLL2_VCO/40 = 600 MHz / 40 = 15 MHz
 *   PLL2R = PLL2_VCO/3  = 600 MHz / 3  = 200 MHz
 */

#define STM32_PLLCFG_PLL2CFG (RCC_PLLCFGR_PLL2VCOSEL_WIDE | \
                              RCC_PLLCFGR_PLL2RGE_4_8_MHZ | \
                              RCC_PLLCFGR_DIVP2EN | \
                              RCC_PLLCFGR_DIVQ2EN | \
                              RCC_PLLCFGR_DIVR2EN)

#define STM32_VCO2_FREQUENCY     ((STM32_HSE_FREQUENCY / 1) * 75)
#define STM32_PLL2P_FREQUENCY    (STM32_VCO2_FREQUENCY / 8)
#define STM32_PLL2Q_FREQUENCY    (STM32_VCO2_FREQUENCY / 40)
#define STM32_PLL2R_FREQUENCY    (STM32_VCO2_FREQUENCY / 3)

#define STM32_PLLCFG_PLL2M       RCC_PLLCKSELR_DIVM2(1)
#define STM32_PLLCFG_PLL2N       RCC_PLL2DIVR_N2(75)
#define STM32_PLLCFG_PLL2P       RCC_PLL2DIVR_P2(8)
#define STM32_PLLCFG_PLL2Q       RCC_PLL2DIVR_Q2(40)
#define STM32_PLLCFG_PLL2R       RCC_PLL2DIVR_R2(3)

/* PLL3 - 8 MHz input, enable DIVP, DIVQ, DIVR
 *
 *   PLL3_VCO = (8 MHz / 1) * 100 = 800 MHz
 *
 *   PLL3P = PLL3_VCO/2  = 800 MHz / 2  = 400 MHz
 *   PLL3Q = PLL3_VCO/8  = 800 MHz / 8  = 100 MHz
 *   PLL3R = PLL3_VCO/20 = 800 MHz / 20 = 40 MHz
 */

#define STM32_PLLCFG_PLL3CFG (RCC_PLLCFGR_PLL3VCOSEL_WIDE | \
                              RCC_PLLCFGR_PLL3RGE_4_8_MHZ | \
                              RCC_PLLCFGR_DIVP3EN | \
                              RCC_PLLCFGR_DIVQ3EN | \
                              RCC_PLLCFGR_DIVR3EN)

#define STM32_PLLCFG_PLL3M       RCC_PLLCKSELR_DIVM3(1)
#define STM32_PLLCFG_PLL3N       RCC_PLL3DIVR_N3(100)
#define STM32_PLLCFG_PLL3P       RCC_PLL3DIVR_P3(2)
#define STM32_PLLCFG_PLL3Q       RCC_PLL3DIVR_Q3(8)
#define STM32_PLLCFG_PLL3R       RCC_PLL3DIVR_R3(20)

#define STM32_VCO3_FREQUENCY     ((STM32_HSE_FREQUENCY / 1) * 100)
#define STM32_PLL3P_FREQUENCY    (STM32_VCO3_FREQUENCY / 2)
#define STM32_PLL3Q_FREQUENCY    (STM32_VCO3_FREQUENCY / 8)
#define STM32_PLL3R_FREQUENCY    (STM32_VCO3_FREQUENCY / 20)

/* SYSCLK = PLL1P = 480 MHz
 * CPUCLK = SYSCLK / 1 = 480 MHz
 */

#define STM32_RCC_D1CFGR_D1CPRE  (RCC_D1CFGR_D1CPRE_SYSCLK)
#define STM32_SYSCLK_FREQUENCY   (STM32_PLL1P_FREQUENCY)
#define STM32_CPUCLK_FREQUENCY   (STM32_SYSCLK_FREQUENCY / 1)

/* Configure Clock Assignments */

/* AHB clock (HCLK) is SYSCLK/2 (240 MHz max)
 * HCLK1 = HCLK2 = HCLK3 = HCLK4
 */

#define STM32_RCC_D1CFGR_HPRE   RCC_D1CFGR_HPRE_SYSCLKd2        /* HCLK  = SYSCLK / 2 */
#define STM32_ACLK_FREQUENCY    (STM32_SYSCLK_FREQUENCY / 2)    /* ACLK in D1, HCLK3 in D1 */
#define STM32_HCLK_FREQUENCY    (STM32_SYSCLK_FREQUENCY / 2)    /* HCLK in D2, HCLK4 in D3 */

/* APB1 clock (PCLK1) is HCLK/2 (120 MHz) */

#define STM32_RCC_D2CFGR_D2PPRE1  RCC_D2CFGR_D2PPRE1_HCLKd2       /* PCLK1 = HCLK / 2 */
#define STM32_PCLK1_FREQUENCY     (STM32_HCLK_FREQUENCY/2)

/* APB2 clock (PCLK2) is HCLK/2 (120 MHz) */

#define STM32_RCC_D2CFGR_D2PPRE2  RCC_D2CFGR_D2PPRE2_HCLKd2       /* PCLK2 = HCLK / 2 */
#define STM32_PCLK2_FREQUENCY     (STM32_HCLK_FREQUENCY/2)

/* APB3 clock (PCLK3) is HCLK/2 (120 MHz) */

#define STM32_RCC_D1CFGR_D1PPRE   RCC_D1CFGR_D1PPRE_HCLKd2        /* PCLK3 = HCLK / 2 */
#define STM32_PCLK3_FREQUENCY     (STM32_HCLK_FREQUENCY/2)

/* APB4 clock (PCLK4) is HCLK/2 (120 MHz) */

#define STM32_RCC_D3CFGR_D3PPRE   RCC_D3CFGR_D3PPRE_HCLKd2       /* PCLK4 = HCLK / 2 */
#define STM32_PCLK4_FREQUENCY     (STM32_HCLK_FREQUENCY/2)

/* Timer clock frequencies */

/* Timers driven from APB1 will be twice PCLK1 */

#define STM32_APB1_TIM2_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM3_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM4_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM5_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM6_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM7_CLKIN   (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM12_CLKIN  (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM13_CLKIN  (2*STM32_PCLK1_FREQUENCY)
#define STM32_APB1_TIM14_CLKIN  (2*STM32_PCLK1_FREQUENCY)

/* Timers driven from APB2 will be twice PCLK2 */

#define STM32_APB2_TIM1_CLKIN   (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM8_CLKIN   (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM15_CLKIN  (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM16_CLKIN  (2*STM32_PCLK2_FREQUENCY)
#define STM32_APB2_TIM17_CLKIN  (2*STM32_PCLK2_FREQUENCY)

/* Kernel Clock Configuration
 *
 * Note: look at Table 54 in ST Manual
 */

/* I2C123 clock source - HSI */

#define STM32_RCC_D2CCIP2R_I2C123SRC RCC_D2CCIP2R_I2C123SEL_HSI

/* I2C4 clock source - HSI */

#define STM32_RCC_D3CCIPR_I2C4SRC    RCC_D3CCIPR_I2C4SEL_HSI

/* SPI123 clock source - PLL1Q */

#define STM32_RCC_D2CCIP1R_SPI123SRC RCC_D2CCIP1R_SPI123SEL_PLL1

/* SPI45 clock source - APB (PCLK2?) */

#define STM32_RCC_D2CCIP1R_SPI45SRC  RCC_D2CCIP1R_SPI45SEL_PLL2

/* SPI6 clock source - APB (PCLK4) */

#define STM32_RCC_D3CCIPR_SPI6SRC    RCC_D3CCIPR_SPI6SEL_PCLK4

/* USB 1 and 2 clock source - HSI48 */

#define STM32_RCC_D2CCIP2R_USBSRC    RCC_D2CCIP2R_USBSEL_HSI48

/* ADC 1 2 3 clock source - pll2_pclk */

#define STM32_RCC_D3CCIPR_ADCSRC     RCC_D3CCIPR_ADCSEL_PLL2

/* FDCAN 1 2 clock source, use STM32_HSE_FREQUENCY  */

#define STM32_RCC_D2CCIP1R_FDCANSEL  RCC_D2CCIP1R_FDCANSEL_HSE

/* FLASH wait states
 *
 *  ------------ ---------- -----------
 *  Vcore        MAX ACLK   WAIT STATES
 *  ------------ ---------- -----------
 *  1.15-1.26 V     70 MHz    0
 *  (VOS1 level)   140 MHz    1
 *                 210 MHz    2
 *  1.05-1.15 V     55 MHz    0
 *  (VOS2 level)   110 MHz    1
 *                 165 MHz    2
 *                 220 MHz    3
 *  0.95-1.05 V     45 MHz    0
 *  (VOS3 level)    90 MHz    1
 *                 135 MHz    2
 *                 180 MHz    3
 *                 225 MHz    4
 *  ------------ ---------- -----------
 */

#define BOARD_FLASH_WAITSTATES 4

/* LED definitions **********************************************************/

/* The Kakute H7 V2 has 1 red LED on PC2 (active low, open-drain).
 *
 * If CONFIG_ARCH_LEDS is not defined, then the user can control the LEDs in
 * any way.
 * The following definitions are used to access individual LEDs.
 */

/* LED index values for use with board_userled() */

#define BOARD_LED1        0
#define BOARD_NLEDS       1

/* LED bits for use with board_userled_all() */

#define BOARD_LED1_BIT    (1 << BOARD_LED1)

/* If CONFIG_ARCH_LEDS is defined, the usage by the board port is defined in
 * include/board.h and src/stm32_leds.c.
 * The LEDs are used to encode OS-related events as follows:
 *
 *
 *   SYMBOL                     Meaning                      LED state
 *                                                           Red
 *   ----------------------  --------------------------  -------------
 */

#define LED_STARTED        0 /* NuttX has been started   OFF           */
#define LED_HEAPALLOCATE   1 /* Heap has been allocated  OFF           */
#define LED_IRQSENABLED    2 /* Interrupts enabled       OFF           */
#define LED_STACKCREATED   3 /* Idle stack created       ON            */
#define LED_INIRQ          4 /* In an interrupt          N/C           */
#define LED_SIGNAL         5 /* In a signal handler      N/C           */
#define LED_ASSERTION      6 /* An assertion failed      N/C           */
#define LED_PANIC          7 /* The system has crashed   Blink         */
#define LED_IDLE           8 /* MCU is is sleep mode     N/C           */

/* Thus if the Red LED is statically on, NuttX has successfully booted and
 * is, apparently, running normally.  If the Red LED is flashing at
 * approximately 2Hz, then a fatal error has been detected and the system
 * has halted.
 */

/* Alternate function pin selections ****************************************/

/* USART3 (Serial Console) - PD8 TX, PD9 RX */

#define GPIO_USART3_RX   (GPIO_USART3_RX_3 | GPIO_SPEED_100MHz)  /* PD9 */
#define GPIO_USART3_TX   (GPIO_USART3_TX_3 | GPIO_SPEED_100MHz)  /* PD8 */

/* SPI1 - W25N01GV NAND Flash */

#define GPIO_SPI1_SCK    (GPIO_SPI1_SCK_1 | GPIO_SPEED_50MHz)    /* PA5 */
#define GPIO_SPI1_MISO   (GPIO_SPI1_MISO_1 | GPIO_SPEED_50MHz)   /* PA6 */
#define GPIO_SPI1_MOSI   (GPIO_SPI1_MOSI_1 | GPIO_SPEED_50MHz)   /* PA7 */

/* OTGFS */

#define GPIO_OTGFS_DM  (GPIO_OTGFS_DM_0|GPIO_SPEED_100MHz) /* PA11 */
#define GPIO_OTGFS_DP  (GPIO_OTGFS_DP_0|GPIO_SPEED_100MHz) /* PA12 */
#define GPIO_OTGFS_ID  (GPIO_OTGFS_ID_0|GPIO_SPEED_100MHz) /* PA10 */

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __BOARDS_ARM_STM32H7_KAKUTEH7V2_INCLUDE_BOARD_H */
