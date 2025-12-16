/****************************************************************************
 * boards/arm/stm32h7/kakuteh7v2/src/stm32_autoleds.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <debug.h>

#include <sys/param.h>

#include <nuttx/board.h>
#include <arch/board/board.h>

#include "stm32_gpio.h"
#include "kakuteh7v2.h"

#ifdef CONFIG_ARCH_LEDS

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void set_led(bool v)
{
  ledinfo("Turn LED %s\n", v ? "on" : "off");

  /* LED is active low (open-drain), so invert the logic:
   * Write 0 to turn ON, write 1 to turn OFF
   */

  stm32_gpiowrite(GPIO_LED_RED, !v);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_autoled_initialize
 ****************************************************************************/

void board_autoled_initialize(void)
{
  /* Configure LED GPIO for output */

  stm32_configgpio(GPIO_LED_RED);
}

/****************************************************************************
 * Name: board_autoled_on
 ****************************************************************************/

void board_autoled_on(int led)
{
  ledinfo("board_autoled_on(%d)\n", led);

  switch (led)
    {
    case LED_STARTED:
    case LED_HEAPALLOCATE:
    case LED_IRQSENABLED:

      /* Keep LED off during boot sequence */

      break;

    case LED_STACKCREATED:

      /* Turn LED on when idle stack is created (boot complete) */

      set_led(true);
      break;

    case LED_PANIC:

      /* For panic state, the LED is blinking */

      set_led(true);
      break;
    }
}

/****************************************************************************
 * Name: board_autoled_off
 ****************************************************************************/

void board_autoled_off(int led)
{
  switch (led)
    {
    case LED_PANIC:

      /* For panic state, the LED is blinking */

      set_led(false);
      break;
    }
}

#endif /* CONFIG_ARCH_LEDS */
