/****************************************************************************
 * include/nuttx/usb/ptp.h
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

#ifndef __INCLUDE_NUTTX_USB_PTP_H
#define __INCLUDE_NUTTX_USB_PTP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define USBPTP_SUBCLASS_STILL_IMAGE 0x01
#define USBPTP_PROTO_PIMA15740      0x01

/* IOCTL commands */
#define PTP_IOC_SENDREQ  _PTPIOC(0x0001)
#define PTP_IOC_GETDATA  _PTPIOC(0x0002)
#define PTP_IOC_GETEVENT _PTPIOC(0x0003)

/* PTP Container Types */
#define PTP_CONTAINER_TYPE_COMMAND   0x0001
#define PTP_CONTAINER_TYPE_DATA      0x0002
#define PTP_CONTAINER_TYPE_RESPONSE  0x0003
#define PTP_CONTAINER_TYPE_EVENT     0x0004

/* PTP Operation Codes */
#define PTP_OPCODE_GETDEVICEINFO    0x1001
#define PTP_OPCODE_OPENSESSION      0x1002
#define PTP_OPCODE_CLOSESESSION     0x1003
#define PTP_OPCODE_GETSTORAGEIDS    0x1004
#define PTP_OPCODE_GETNUMOBJECTS    0x1005
#define PTP_OPCODE_GETOBJECTHANDLES 0x1007
#define PTP_OPCODE_GETOBJECTINFO    0x1008
#define PTP_OPCODE_GETOBJECT        0x1009

/* PTP Response Codes */
#define PTP_RC_OK                   0x2001
#define PTP_RC_GENERAL_ERROR        0x2002
#define PTP_RC_SESSION_NOT_OPEN     0x2003
#define PTP_RC_INVALID_TRANSACTION  0x2004


struct ptp_container_s
{
  uint32_t length;    /* Length of container in bytes */
  uint16_t type;      /* Container type */
  uint16_t code;      /* Operation code */
  uint32_t trans_id;  /* Transaction ID */
  uint32_t params[5]; /* Operation parameters */
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Functions Definitions
 ****************************************************************************/

#endif /* __INCLUDE_NUTTX_USB_PTP_H */
