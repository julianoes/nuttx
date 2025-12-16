/****************************************************************************
 * boards/arm/stm32h7/kakuteh7v2/src/stm32_bringup.c
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

#include <sys/types.h>
#include <syslog.h>
#include <errno.h>

#include <arch/board/board.h>

#include <nuttx/fs/fs.h>

#ifdef CONFIG_STM32H7_SPI1
#include <nuttx/spi/spi.h>
#include "stm32_spi.h"
#endif

#include "kakuteh7v2.h"

#include "stm32_gpio.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_STM32H7_SPI1
/****************************************************************************
 * Name: w25n_jedec_id_test
 *
 * Description:
 *   Read and verify the JEDEC ID from the W25N01GV NAND flash chip.
 *   Expected: Manufacturer=0xEF (Winbond), Device=0xAA21
 *
 ****************************************************************************/

static void w25n_jedec_id_test(void)
{
  struct spi_dev_s *spi;
  uint8_t manufacturer;
  uint8_t device_hi;
  uint8_t device_lo;

  spi = stm32_spibus_initialize(1);
  if (!spi)
    {
      syslog(LOG_ERR, "W25N: Failed to initialize SPI1\n");
      return;
    }

  SPI_LOCK(spi, true);
  SPI_SETFREQUENCY(spi, 1000000);  /* 1 MHz for safety */
  SPI_SETMODE(spi, SPIDEV_MODE0);
  SPI_SETBITS(spi, 8);

  SPI_SELECT(spi, SPIDEV_FLASH(0), true);
  SPI_SEND(spi, 0x9f);             /* JEDEC ID command */
  SPI_SEND(spi, 0x00);             /* Dummy byte */
  manufacturer = SPI_SEND(spi, 0x00);
  device_hi = SPI_SEND(spi, 0x00);
  device_lo = SPI_SEND(spi, 0x00);
  SPI_SELECT(spi, SPIDEV_FLASH(0), false);

  SPI_LOCK(spi, false);

  syslog(LOG_INFO, "W25N JEDEC ID: Mfg=0x%02x, Dev=0x%02x%02x\n",
         manufacturer, device_hi, device_lo);

  if (manufacturer == 0xef && device_hi == 0xaa && device_lo == 0x21)
    {
      syslog(LOG_INFO, "W25N01GV detected successfully!\n");
    }
  else
    {
      syslog(LOG_WARNING, "W25N: Unexpected ID (expected EF AA21)\n");
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: stm32_bringup
 *
 * Description:
 *   Perform architecture-specific initialization
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=y :
 *     Called from board_late_initialize().
 *
 *   CONFIG_BOARD_LATE_INITIALIZE=n && CONFIG_BOARDCTL=y &&
 *   CONFIG_NSH_ARCHINIT:
 *     Called from the NSH library
 *
 ****************************************************************************/

int stm32_bringup(void)
{
  int ret = OK;

  UNUSED(ret);

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */

  ret = nx_mount(NULL, STM32_PROCFS_MOUNTPOINT, "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: Failed to mount the PROC filesystem: %d\n",  ret);
    }
#endif /* CONFIG_FS_PROCFS */

#ifdef CONFIG_FS_TMPFS
  /* Mount the tmpfs file system */

  ret = nx_mount(NULL, "/tmp", "tmpfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: Failed to mount tmpfs at /tmp: %d\n", ret);
    }
#endif /* CONFIG_FS_TMPFS */

#ifdef CONFIG_STM32H7_SPI1
  /* Test W25N01GV NAND flash JEDEC ID */

  w25n_jedec_id_test();
#endif

  return OK;
}
