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

#ifdef CONFIG_MTD_W25N
#include <nuttx/spi/spi.h>
#include <nuttx/mtd/mtd.h>
#include "stm32_spi.h"
#endif

#include "kakuteh7v2.h"

#include "stm32_gpio.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_MTD_W25N
/****************************************************************************
 * Name: stm32_w25n_initialize
 *
 * Description:
 *   Initialize the W25N01GV NAND flash and register it as an MTD device.
 *
 ****************************************************************************/

static int stm32_w25n_initialize(void)
{
  FAR struct spi_dev_s *spi;
  FAR struct mtd_dev_s *mtd;
  int ret;

  spi = stm32_spibus_initialize(1);
  if (!spi)
    {
      syslog(LOG_ERR, "W25N: Failed to initialize SPI1\n");
      return -ENODEV;
    }

  mtd = w25n_initialize(spi, 0);
  if (!mtd)
    {
      syslog(LOG_ERR, "W25N: Failed to initialize MTD driver\n");
      return -ENODEV;
    }

  /* Register MTD character device */

  ret = register_mtddriver("/dev/mtd0", mtd, 0755, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "W25N: Failed to register MTD driver: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "W25N: MTD registered at /dev/mtd0\n");
  return OK;
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

#ifdef CONFIG_MTD_W25N
  /* Initialize W25N01GV NAND flash MTD driver */

  ret = stm32_w25n_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: stm32_w25n_initialize failed: %d\n", ret);
    }
#endif

  return OK;
}
