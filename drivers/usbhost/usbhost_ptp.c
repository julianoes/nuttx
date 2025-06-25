/****************************************************************************
 * drivers/usbhost/usbhost_ptp.c
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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <sys/ioctl.h>

#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/signal.h>
#include <nuttx/arch.h>
#include <nuttx/wqueue.h>
#include <nuttx/semaphore.h>

#include <nuttx/usb/usb.h>
#include <nuttx/usb/usbhost.h>
#include <nuttx/usb/ptp.h>
#include <nuttx/usb/usbhost_devaddr.h>


/* Don't compile if prerequisites are not met */

#if defined(CONFIG_USBHOST) && !defined(CONFIG_USBHOST_BULK_DISABLE)

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

#ifndef CONFIG_SCHED_WORKQUEUE
#  warning "Worker thread support is required (CONFIG_SCHED_WORKQUEUE)"
#endif

/* If the create() method is called by the USB host device driver from an
 * interrupt handler, then it will be unable to call kmm_malloc() in order to
 * allocate a new class instance.  If the create() method is called from the
 * interrupt level, then class instances must be pre-allocated.
 */

/* Driver support ***********************************************************/

/* This format is used to construct the /dev/ptp[n] device driver path.  It
 * defined here so that it will be used consistently in all places.
 */

#define DEV_NAME            "/dev/ptp%d"
#define DEV_NAMELEN         10


/****************************************************************************
 * Private Types
 ****************************************************************************/


/* This structure contains the internal, private state of the USB host PTP class */
struct usbhost_state_s
{
  /* This is the externally visible portion of the state */
  struct usbhost_class_s  usbclass;

  /* The remainder of the fields are provided to the PTP class */
  //char                    devname[DEV_NAMELEN];  /* Device path for registration */
  char                    ptpchar;      /* Character identifying the /dev/ptp[n] device */
  volatile bool           disconnected; /* TRUE: Device has been disconnected */
  uint8_t                 ifno;         /* Interface number */
  int16_t                 crefs;        /* Reference count on the driver instance */
  sem_t                   exclsem;      /* Used to maintain mutual exclusive access */
  struct work_s           work;         /* For interacting with the worker thread */
  usbhost_ep_t            bulkin;       /* Bulk IN endpoint */
  usbhost_ep_t            bulkout;      /* Bulk OUT endpoint */
  usbhost_ep_t            intin;        /* Interrupt IN endpoint for events */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Semaphores */

static int usbhost_takesem(FAR sem_t *sem);
#define usbhost_givesem(s) nxsem_post(s);

/* Memory allocation services */

static inline FAR struct usbhost_state_s *usbhost_allocclass(void);
static inline void usbhost_freeclass(FAR struct usbhost_state_s *usbclass);

/* Device name management */

static int usbhost_allocdevno(FAR struct usbhost_state_s *priv);
static void usbhost_freedevno(FAR struct usbhost_state_s *priv);
static inline void usbhost_mkdevname(FAR struct usbhost_state_s *priv,
                                     FAR char *devname);

/* Worker thread actions */

static void usbhost_destroy(FAR void *arg);

/* Helpers for usbhost_connect() */

static inline int usbhost_cfgdesc(FAR struct usbhost_state_s *priv,
                                  FAR const uint8_t *configdesc,
                                  int desclen);

/* (Little Endian) Data helpers */

static inline uint16_t usbhost_getle16(const uint8_t *val);

/* struct usbhost_registry_s methods */

static struct usbhost_class_s *
  usbhost_create(FAR struct usbhost_hubport_s *hport,
                 FAR const struct usbhost_id_s *id);

/* struct usbhost_class_s methods */

static int usbhost_connect(FAR struct usbhost_class_s *usbclass,
                           FAR const uint8_t *configdesc, int desclen);
static int usbhost_disconnected(FAR struct usbhost_class_s *usbclass);

/* struct block_operations methods */

static int     usbhost_ptp_open(FAR struct file *filep);
static int     usbhost_ptp_close(FAR struct file *filep);
static ssize_t usbhost_ptp_read(FAR struct file *filep, FAR char *buffer,
                               size_t buflen);
static ssize_t usbhost_ptp_write(FAR struct file *filep,
                                FAR const char *buffer, size_t buflen);
static int     usbhost_ptp_ioctl(FAR struct file *filep, int cmd,
                                unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* This structure provides the registry entry ID information that will  be
 * used to associate the USB host mass storage class to a connected USB
 * device.
 */

static const struct usbhost_id_s g_id =
{
  USB_CLASS_STILL_IMAGE,       /* base     */
  USBPTP_SUBCLASS_STILL_IMAGE, /* subclass */
  USBPTP_PROTO_PIMA15740,      /* proto    */
  0,                           /* vid      */
  0                            /* pid      */
};

/* This is the USB host PTP class's registry entry */

static struct usbhost_registry_s g_ptp =
{
  NULL,                   /* flink     */
  usbhost_create,         /* create    */
  1,                      /* nids      */
  &g_id                   /* id[]      */
};

/* Block driver operations.  This is the interface exposed to NuttX by the
 * class that permits it to behave like a block driver.
 */

static const struct file_operations g_ptp_fops =
{
  usbhost_ptp_open,    /* open */
  usbhost_ptp_close,   /* close */
  usbhost_ptp_read,    /* read */
  usbhost_ptp_write,   /* write */
  NULL,                /* seek */
  usbhost_ptp_ioctl,   /* ioctl */
};

/* This is a bitmap that is used to allocate device names.  */

static uint32_t g_devinuse;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: usbhost_takesem
 *
 * Description:
 *   This is just a wrapper to handle the annoying behavior of semaphore
 *   waits that return due to the receipt of a signal.
 *
 ****************************************************************************/

static int usbhost_takesem(FAR sem_t *sem)
{
  return nxsem_wait_uninterruptible(sem);
}

/****************************************************************************
 * Name: usbhost_allocclass
 *
 * Description:
 *   This is really part of the logic that implements the create() method
 *   of struct usbhost_registry_s.  This function allocates memory for one
 *   new class instance.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   On success, this function will return a non-NULL instance of struct
 *   usbhost_class_s.  NULL is returned on failure; this function will
 *   will fail only if there are insufficient resources to create another
 *   USB host class instance.
 *
 ****************************************************************************/

static inline FAR struct usbhost_state_s *usbhost_allocclass(void)
{
  FAR struct usbhost_state_s *priv;

  /* We are not executing from an interrupt handler so we can just call
   * kmm_malloc() to get memory for the class instance.
   */

  DEBUGASSERT(!up_interrupt_context());

  priv = (FAR struct usbhost_state_s *)
    kmm_malloc(sizeof(struct usbhost_state_s));

  uinfo("Allocated: %p\n", priv);
  return priv;
}

/****************************************************************************
 * Name: usbhost_freeclass
 *
 * Description:
 *   Free a class instance previously allocated by usbhost_allocclass().
 *
 * Input Parameters:
 *   usbclass - A reference to the class instance to be freed.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void usbhost_freeclass(FAR struct usbhost_state_s *usbclass)
{
  DEBUGASSERT(usbclass != NULL);

  /* Free the class instance (calling kmm_free() in case we are executing
   * from an interrupt handler.
   */

  uinfo("Freeing: %p\n", usbclass);
  kmm_free(usbclass);
}

/****************************************************************************
 * Name: Device name management
 *
 * Description:
 *   Some tiny functions to coordinate management of PTP names.
 *
 ****************************************************************************/

static int usbhost_allocdevno(FAR struct usbhost_state_s *priv)
{
  irqstate_t flags;
  int devno;

  flags = enter_critical_section();
  for (devno = 0; devno < 26; devno++)
    {
      uint32_t bitno = 1 << devno;
      if ((g_devinuse & bitno) == 0)
        {
          g_devinuse |= bitno;
          priv->ptpchar = devno;
          leave_critical_section(flags);
          return OK;
        }
    }

  leave_critical_section(flags);
  return -EMFILE;
}

static void usbhost_freedevno(FAR struct usbhost_state_s *priv)
{
  int devno = priv->ptpchar;

  if (devno >= 0 && devno < 26)
    {
      irqstate_t flags = enter_critical_section();
      g_devinuse &= ~(1 << devno);
      leave_critical_section(flags);
    }
}

static inline void usbhost_mkdevname(FAR struct usbhost_state_s *priv,
                                     FAR char *devname)
{
  snprintf(devname, DEV_NAMELEN, DEV_NAME, priv->ptpchar);
}

/****************************************************************************
 * Name: usbhost_destroy
 *
 * Description:
 *   The USB mass storage device has been disconnected and the reference
 *   count on the USB host class instance has gone to 1.. Time to destroy
 *   the USB host class instance.
 *
 * Input Parameters:
 *   arg - A reference to the class instance to be destroyed.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void usbhost_destroy(FAR void *arg)
{
  FAR struct usbhost_state_s *priv = (FAR struct usbhost_state_s *)arg;
  FAR struct usbhost_hubport_s *hport;
  char devname[DEV_NAMELEN];

  DEBUGASSERT(priv != NULL && priv->usbclass.hport != NULL);
  hport = priv->usbclass.hport;

  uinfo("crefs: %d\n", priv->crefs);

  /* Unregister the driver */

  usbhost_mkdevname(priv, devname);
  unregister_driver(devname);

  /* Release the device name used by this connection */

  usbhost_freedevno(priv);

  /* Free the bulk endpoints */

  if (priv->bulkout)
    {
      DRVR_EPFREE(hport->drvr, priv->bulkout);
    }

  if (priv->bulkin)
    {
      DRVR_EPFREE(hport->drvr, priv->bulkin);
    }

  /* Destroy the semaphores */

  nxsem_destroy(&priv->exclsem);

  /* Disconnect the USB host device */

  DRVR_DISCONNECT(hport->drvr, hport);

  /* Free the function address assigned to this device */

  usbhost_devaddr_destroy(hport, hport->funcaddr);
  hport->funcaddr = 0;

  /* And free the class instance.  */

  usbhost_freeclass(priv);
}

/****************************************************************************
 * Name: usbhost_cfgdesc
 *
 * Description:
 *   This function implements the connect() method of struct
 *   usbhost_class_s.  This method is a callback into the class
 *   implementation.  It is used to provide the device's configuration
 *   descriptor to the class so that the class may initialize properly
 *
 * Input Parameters:
 *   priv - The USB host class instance.
 *   configdesc - A pointer to a uint8_t buffer container the configuration
 *     descriptor.
 *   desclen - The length in bytes of the configuration descriptor.
 *
 * Returned Value:
 *   On success, zero (OK) is returned. On a failure, a negated errno value
 *   is returned indicating the nature of the failure
 *
 * Assumptions:
 *   This function will *not* be called from an interrupt handler.
 *
 ****************************************************************************/

static inline int usbhost_cfgdesc(FAR struct usbhost_state_s *priv,
                                 FAR const uint8_t *configdesc, int desclen)
{
  FAR struct usbhost_hubport_s *hport;
  FAR struct usb_cfgdesc_s *cfgdesc;
  FAR struct usb_desc_s *desc;
  FAR struct usbhost_epdesc_s bindesc;
  FAR struct usbhost_epdesc_s boutdesc;
  FAR struct usbhost_epdesc_s intindesc;
  int remaining;
  uint8_t found = 0;
  int ret;

  DEBUGASSERT(priv != NULL && priv->usbclass.hport &&
              configdesc != NULL && desclen >= sizeof(struct usb_cfgdesc_s));
  hport = priv->usbclass.hport;

  /* Keep the compiler from complaining about uninitialized variables */
  memset(&bindesc, 0, sizeof(struct usbhost_epdesc_s));
  memset(&boutdesc, 0, sizeof(struct usbhost_epdesc_s));
  memset(&intindesc, 0, sizeof(struct usbhost_epdesc_s));

  /* Verify that we were passed a configuration descriptor */
  cfgdesc = (FAR struct usb_cfgdesc_s *)configdesc;
  if (cfgdesc->type != USB_DESC_TYPE_CONFIG)
    {
      return -EINVAL;
    }

  /* Get the total length of the configuration descriptor (little endian) */
  remaining = (int)usbhost_getle16(cfgdesc->totallen);

  /* Skip to the next entry descriptor */
  configdesc += cfgdesc->len;
  remaining  -= cfgdesc->len;

  /* Loop where there are more descriptors to examine */
  while (remaining >= sizeof(struct usb_desc_s))
    {
      /* What is the next descriptor? */
      desc = (FAR struct usb_desc_s *)configdesc;
      switch (desc->type)
        {
        /* Interface descriptor */
        case USB_DESC_TYPE_INTERFACE:
          {
            FAR struct usb_ifdesc_s *ifdesc = (FAR struct usb_ifdesc_s *)configdesc;

            uinfo("Interface descriptor\n");
            DEBUGASSERT(remaining >= USB_SIZEOF_IFDESC);

            /* Save the interface number and mark interface found */
            priv->ifno = ifdesc->ifno;
            found |= 0x01;  /* Interface found */
          }
          break;

        /* Endpoint descriptor */
        case USB_DESC_TYPE_ENDPOINT:
          {
            FAR struct usb_epdesc_s *epdesc = (FAR struct usb_epdesc_s *)configdesc;

            uinfo("Endpoint descriptor\n");
            DEBUGASSERT(remaining >= USB_SIZEOF_EPDESC);

            /* Handle according to endpoint type */
            switch (epdesc->attr & USB_EP_ATTR_XFERTYPE_MASK)
              {
                /* Bulk endpoints */
                case USB_EP_ATTR_XFER_BULK:
                  if (USB_ISEPOUT(epdesc->addr))
                    {
                      /* Save bulk OUT endpoint */
                      boutdesc.hport        = hport;
                      boutdesc.addr         = epdesc->addr & USB_EP_ADDR_NUMBER_MASK;
                      boutdesc.in           = false;
                      boutdesc.xfrtype      = USB_EP_ATTR_XFER_BULK;
                      boutdesc.interval     = epdesc->interval;
                      boutdesc.mxpacketsize = usbhost_getle16(epdesc->mxpacketsize);
                      found |= 0x02;  /* Bulk OUT found */
                      
                      uinfo("Bulk OUT EP addr:%d mxpacketsize:%d\n",
                            boutdesc.addr, boutdesc.mxpacketsize);
                    }
                  else
                    {
                      /* Save bulk IN endpoint */
                      bindesc.hport        = hport;
                      bindesc.addr         = epdesc->addr & USB_EP_ADDR_NUMBER_MASK;
                      bindesc.in           = true;
                      bindesc.xfrtype      = USB_EP_ATTR_XFER_BULK;
                      bindesc.interval     = epdesc->interval;
                      bindesc.mxpacketsize = usbhost_getle16(epdesc->mxpacketsize);
                      found |= 0x04;  /* Bulk IN found */
                      
                      uinfo("Bulk IN EP addr:%d mxpacketsize:%d\n",
                            bindesc.addr, bindesc.mxpacketsize);
                    }
                  break;

                /* Interrupt endpoints */
                case USB_EP_ATTR_XFER_INT:
                  if (USB_ISEPIN(epdesc->addr))
                    {
                      /* Save interrupt IN endpoint */
                      intindesc.hport        = hport;
                      intindesc.addr         = epdesc->addr & USB_EP_ADDR_NUMBER_MASK;
                      intindesc.in           = true;
                      intindesc.xfrtype      = USB_EP_ATTR_XFER_INT;
                      intindesc.interval     = epdesc->interval;
                      intindesc.mxpacketsize = usbhost_getle16(epdesc->mxpacketsize);
                      found |= 0x08;  /* Interrupt IN found */
                      
                      uinfo("Interrupt IN EP addr:%d mxpacketsize:%d\n",
                            intindesc.addr, intindesc.mxpacketsize);
                    }
                  break;
              }
          }
          break;

        /* Skip other descriptors */
        default:
          break;
        }

      /* Increment to the next descriptor */
      configdesc += desc->len;
      remaining  -= desc->len;
    }

  /* Was everything we need found? */
  if ((found & 0x0f) != 0x0f)  /* Interface + Bulk IN + Bulk OUT + Interrupt IN */
    {
      uerr("ERROR: Found IF:%s BIN:%s BOUT:%s INTIN:%s\n",
           (found & 0x01) ? "YES" : "NO",
           (found & 0x04) ? "YES" : "NO",
           (found & 0x02) ? "YES" : "NO",
           (found & 0x08) ? "YES" : "NO");
      return -EINVAL;
    }

  /* We are good... Allocate the endpoints */
  ret = DRVR_EPALLOC(hport->drvr, &boutdesc, &priv->bulkout);
  if (ret < 0)
    {
      uerr("ERROR: Failed to allocate Bulk OUT endpoint\n");
      return ret;
    }

  ret = DRVR_EPALLOC(hport->drvr, &bindesc, &priv->bulkin);
  if (ret < 0)
    {
      uerr("ERROR: Failed to allocate Bulk IN endpoint\n");
      DRVR_EPFREE(hport->drvr, priv->bulkout);
      return ret;
    }

  ret = DRVR_EPALLOC(hport->drvr, &intindesc, &priv->intin);
  if (ret < 0)
    {
      uerr("ERROR: Failed to allocate Interrupt IN endpoint\n");
      DRVR_EPFREE(hport->drvr, priv->bulkin);
      DRVR_EPFREE(hport->drvr, priv->bulkout);
      return ret;
    }

  uinfo("Endpoints allocated\n");
  return OK;
}

/****************************************************************************
 * Name: usbhost_getle16
 *
 * Description:
 *   Get a (possibly unaligned) 16-bit little endian value.
 *
 * Input Parameters:
 *   val - A pointer to the first byte of the little endian value.
 *
 * Returned Value:
 *   A uint16_t representing the whole 16-bit integer value
 *
 ****************************************************************************/

static inline uint16_t usbhost_getle16(const uint8_t *val)
{
  return (uint16_t)val[1] << 8 | (uint16_t)val[0];
}

/****************************************************************************
 * Name: usbhost_create
 *
 * Description:
 *   This function implements the create() method of struct
 *   usbhost_registry_s.  The create() method is a callback into the class
 *   implementation.  It is used to (1) create a new instance of the USB host
 *   class state and to (2) bind a USB host driver "session" to the class
 *   instance.  Use of this create() method will support environments where
 *   there may be multiple USB ports and multiple USB devices simultaneously
 *   connected.
 *
 * Input Parameters:
 *   hport - The hub port that manages the new class instance.
 *   id - In the case where the device supports multiple base classes,
 *     subclasses, or protocols, this specifies which to configure for.
 *
 * Returned Value:
 *   On success, this function will return a non-NULL instance of struct
 *   usbhost_class_s that can be used by the USB host driver to communicate
 *   with the USB host class.  NULL is returned on failure; this function
 *   will fail only if the hport input parameter is NULL or if there are
 *   insufficient resources to create another USB host class instance.
 *
 ****************************************************************************/

static FAR struct usbhost_class_s *
  usbhost_create(FAR struct usbhost_hubport_s *hport,
                 FAR const struct usbhost_id_s *id)
{
  FAR struct usbhost_state_s *priv;

  /* Allocate a USB host PTP class instance */

  priv = usbhost_allocclass();
  if (priv)
    {
      /* Initialize the allocated PTP class instance */

      memset(priv, 0, sizeof(struct usbhost_state_s));

      /* Assign a device number to this class instance */

      if (usbhost_allocdevno(priv) == OK)
        {
          /* Initialize class method function pointers */

          priv->usbclass.hport        = hport;
          priv->usbclass.connect      = usbhost_connect;
          priv->usbclass.disconnected = usbhost_disconnected;

          /* The initial reference count is 1... One reference is held by
           * the driver.
           */

          priv->crefs = 1;

          /* Initialize semaphores
           * (this works okay in the interrupt context)
           */

          nxsem_init(&priv->exclsem, 0, 1);

          /* Return the instance of the PTP class */

          return &priv->usbclass;
        }
      else
        {
          uinfo("usbhost_allocdevno failed!\n");
        }
    }

  /* An error occurred. Free the allocation and return NULL on all failures */

  if (priv)
    {
      uinfo("freeing here!\n");
      usbhost_freeclass(priv);
    }

  return NULL;
}

/****************************************************************************
 * Name: usbhost_disconnected
 *
 * Description:
 *   This function implements the disconnected() method of struct
 *   usbhost_class_s.  This method is a callback into the class
 *   implementation.  It is used to inform the class that the USB device has
 *   been disconnected.
 *
 * Input Parameters:
 *   usbclass - The USB host class entry previously obtained from a call to
 *     create().
 *
 * Returned Value:
 *   On success, zero (OK) is returned. On a failure, a negated errno value
 *   is returned indicating the nature of the failure
 *
 * Assumptions:
 *   This function may be called from an interrupt handler.
 *
 ****************************************************************************/

static int usbhost_disconnected(struct usbhost_class_s *usbclass)
{
  FAR struct usbhost_state_s *priv = (FAR struct usbhost_state_s *)usbclass;
  irqstate_t flags;

  DEBUGASSERT(priv != NULL);

  /* Set an indication to any users of the PTP device that the
   * device is no longer available.
   */

  flags              = enter_critical_section();
  priv->disconnected = true;

  /* Now check the number of references on the class instance.  If it is one,
   * then we can free the class instance now.  Otherwise, we will have to
   * wait until the holders of the references free them by closing the
   * block driver.
   */

  uinfo("crefs: %d\n", priv->crefs);
  if (priv->crefs == 1)
    {
      /* Destroy the class instance.  If we are executing from an interrupt
       * handler, then defer the destruction to the worker thread.
       * Otherwise, destroy the instance now.
       */

      if (up_interrupt_context())
        {
          /* Destroy the instance on the worker thread. */

          uinfo("Queuing destruction: worker %p->%p\n",
                priv->work.worker, usbhost_destroy);
          DEBUGASSERT(priv->work.worker == NULL);
          work_queue(HPWORK, &priv->work, usbhost_destroy, priv, 0);
        }
      else
        {
          /* Do the work now */

          usbhost_destroy(priv);
        }
    }

  leave_critical_section(flags);
  return OK;
}

/****************************************************************************
 * Character Driver Methods
 ****************************************************************************/

static int usbhost_ptp_open(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct usbhost_state_s *priv = inode->i_private;
  int ret;

  /* Get exclusive access to the device */
  ret = usbhost_takesem(&priv->exclsem);
  if (ret < 0)
    {
      return ret;
    }

  /* Increment the reference count */
  priv->crefs++;
  usbhost_givesem(&priv->exclsem);

  return OK;
}

static int usbhost_ptp_close(FAR struct file *filep)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct usbhost_state_s *priv = inode->i_private;
  int ret;
  irqstate_t flags;

  /* Get exclusive access to the device */
  ret = usbhost_takesem(&priv->exclsem);
  if (ret < 0)
    {
      return ret;
    }

  /* Decrement the reference count */
  if (priv->crefs > 0)
    {
      priv->crefs--;
    }

  flags = enter_critical_section();

  /* Check if the USB PTP device is still connected.  If the
   * device is not connected and the reference count just
   * decremented to one, then unregister the device.
   */

  if (priv->crefs <= 1 && priv->disconnected)
    {
      /* Destroy the class instance */

      DEBUGASSERT(priv->crefs == 1);
      usbhost_destroy(priv);
    }

  leave_critical_section(flags);

  usbhost_givesem(&priv->exclsem);
  return OK;
}

static ssize_t usbhost_ptp_read(FAR struct file *filep, FAR char *buffer,
                               size_t buflen)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct usbhost_state_s *priv = inode->i_private;
  int ret;

  /* Get exclusive access */
  ret = usbhost_takesem(&priv->exclsem);
  if (ret < 0)
    {
      return ret;
    }

  /* Read from bulk IN endpoint */
  ret = DRVR_TRANSFER(priv->usbclass.hport->drvr, priv->bulkin,
                      (FAR uint8_t *)buffer, buflen);

  usbhost_givesem(&priv->exclsem);
  return ret;
}

static ssize_t usbhost_ptp_write(FAR struct file *filep,
                                FAR const char *buffer, size_t buflen)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct usbhost_state_s *priv = inode->i_private;
  int ret;

  /* Get exclusive access */
  ret = usbhost_takesem(&priv->exclsem);
  if (ret < 0)
    {
      return ret;
    }

  /* Write to bulk OUT endpoint */
  ret = DRVR_TRANSFER(priv->usbclass.hport->drvr, priv->bulkout,
                      (FAR uint8_t *)buffer, buflen);

  usbhost_givesem(&priv->exclsem);
  return ret;
}

static int usbhost_ptp_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct inode *inode = filep->f_inode;
  FAR struct usbhost_state_s *priv = inode->i_private;
  FAR struct ptp_container_s *container;
  int ret;

  /* Get exclusive access */
  ret = usbhost_takesem(&priv->exclsem);
  if (ret < 0)
    {
      return ret;
    }

  switch (cmd)
    {
      case PTP_IOC_SENDREQ:
        /* Send a PTP request container */
        container = (FAR struct ptp_container_s *)arg;
        ret = DRVR_TRANSFER(priv->usbclass.hport->drvr, priv->bulkout,
                           (FAR uint8_t *)container, container->length);
        break;

      case PTP_IOC_GETDATA:
        /* Get a PTP data or response container */
        container = (FAR struct ptp_container_s *)arg;
        ret = DRVR_TRANSFER(priv->usbclass.hport->drvr, priv->bulkin,
                           (FAR uint8_t *)container, sizeof(struct ptp_container_s));
        break;

      case PTP_IOC_GETEVENT:
        /* Get a PTP event from the interrupt endpoint */
        container = (FAR struct ptp_container_s *)arg;
        ret = DRVR_TRANSFER(priv->usbclass.hport->drvr, priv->intin,
                           (FAR uint8_t *)container, sizeof(struct ptp_container_s));
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  usbhost_givesem(&priv->exclsem);
  return ret;
}

/****************************************************************************
 * Device Setup
 ****************************************************************************/

static int usbhost_connect(FAR struct usbhost_class_s *usbclass,
                          FAR const uint8_t *configdesc, int desclen)
{
  FAR struct usbhost_state_s *priv = (FAR struct usbhost_state_s *)usbclass;
  int ret;

  /* Parse the configuration descriptor and set up endpoints */
  ret = usbhost_cfgdesc(priv, configdesc, desclen);
  if (ret < 0)
    {
      return ret;
    }

  /* Register the character device */
  char devname[DEV_NAMELEN];
  usbhost_mkdevname(priv, devname);
  ret = register_driver(devname, &g_ptp_fops, 0666, priv);
  if (ret < 0)
    {
      return ret;
    }

  return OK;
}

int usbhost_ptp_initialize(void)
{
  /* Advertise our availability to support PTP devices */
  return usbhost_registerclass(&g_ptp);
}


#endif /* CONFIG_USBHOST && !CONFIG_USBHOST_BULK_DISABLE */
