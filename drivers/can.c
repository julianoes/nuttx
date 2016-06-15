/****************************************************************************
 * drivers/can.c
 *
 *   Copyright (C) 2008-2009, 2011-2012, 2014-2015 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <semaphore.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/fs/fs.h>
#include <nuttx/arch.h>
#include <nuttx/can.h>

#ifdef CONFIG_CAN_TXREADY
#  include <nuttx/wqueue.h>
#endif

#include <nuttx/irq.h>

#ifdef CONFIG_CAN

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
/* Configuration ************************************************************/

#ifdef CONFIG_CAN_TXREADY
#  if !defined(CONFIG_SCHED_WORKQUEUE)
#    error Work queue support required in this configuration
#    undef CONFIG_CAN_TXREADY
#    undef CONFIG_CAN_TXREADY_LOPRI
#    undef CONFIG_CAN_TXREADY_HIPRI
#  elif defined(CONFIG_CAN_TXREADY_LOPRI)
#    undef CONFIG_CAN_TXREADY_HIPRI
#    ifdef CONFIG_SCHED_LPWORK
#      define CANWORK LPWORK
#    else
#      error Low priority work queue support required in this configuration
#      undef CONFIG_CAN_TXREADY
#      undef CONFIG_CAN_TXREADY_LOPRI
#    endif
#  elif defined(CONFIG_CAN_TXREADY_HIPRI)
#    ifdef CONFIG_SCHED_HPWORK
#      define CANWORK HPWORK
#    else
#      error High priority work queue support required in this configuration
#      undef CONFIG_CAN_TXREADY
#      undef CONFIG_CAN_TXREADY_HIPRI
#    endif
#  else
#    error No work queue selection
#    undef CONFIG_CAN_TXREADY
#  endif
#endif

/* Debug ********************************************************************/
/* Non-standard debug that may be enabled just for testing CAN */

#ifdef CONFIG_DEBUG_CAN
#  define canerr    err
#  define caninfo   info
#  define canllerr  llerr
#  define canllinfo llinfo
#else
#  define canerr(x...)
#  define caninfo(x...)
#  define canllerr(x...)
#  define canllinfo(x...)
#endif

/* Timing Definitions *******************************************************/

#define HALF_SECOND_MSEC 500
#define HALF_SECOND_USEC 500000L

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* CAN helpers */

static uint8_t        can_dlc2bytes(uint8_t dlc);
#if 0 /* Not used */
static uint8_t        can_bytes2dlc(uint8_t nbytes);
#endif
#ifdef CONFIG_CAN_TXREADY
static void           can_txready_work(FAR void *arg);
#endif

/* Character driver methods */

static int            can_open(FAR struct file *filep);
static int            can_close(FAR struct file *filep);
static ssize_t        can_read(FAR struct file *filep, FAR char *buffer,
                         size_t buflen);
static int            can_xmit(FAR struct can_dev_s *dev);
static ssize_t        can_write(FAR struct file *filep,
                         FAR const char *buffer, size_t buflen);
static inline ssize_t can_rtrread(FAR struct can_dev_s *dev,
                         FAR struct canioc_rtr_s *rtr);
static int            can_ioctl(FAR struct file *filep, int cmd,
                         unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct file_operations g_canops =
{
  can_open,  /* open */
  can_close, /* close */
  can_read,  /* read */
  can_write, /* write */
  0,         /* seek */
  can_ioctl  /* ioctl */
#ifndef CONFIG_DISABLE_POLL
  , 0        /* poll */
#endif
#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
  , 0        /* unlink */
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: can_dlc2bytes
 *
 * Description:
 *   In the CAN FD format, the coding of the DLC differs from the standard
 *   CAN format. The DLC codes 0 to 8 have the same coding as in standard
 *   CAN.  But the codes 9 to 15 all imply a data field of 8 bytes with
 *   standard CAN.  In CAN FD mode, the values 9 to 15 are encoded to values
 *   in the range 12 to 64.
 *
 * Input Parameter:
 *   dlc    - the DLC value to convert to a byte count
 *
 * Returned Value:
 *   The number of bytes corresponding to the DLC value.
 *
 ****************************************************************************/

static uint8_t can_dlc2bytes(uint8_t dlc)
{
  if (dlc > 8)
    {
#ifdef CONFIG_CAN_FD
      switch (dlc)
        {
          case 9:
            return 12;
          case 10:
            return 16;
          case 11:
            return 20;
          case 12:
            return 24;
          case 13:
            return 32;
          case 14:
            return 48;
          default:
          case 15:
            return 64;
        }
#else
      return 8;
#endif
    }

  return dlc;
}

/****************************************************************************
 * Name: can_bytes2dlc
 *
 * Description:
 *   In the CAN FD format, the coding of the DLC differs from the standard
 *   CAN format. The DLC codes 0 to 8 have the same coding as in standard
 *   CAN.  But the codes 9 to 15 all imply a data field of 8 bytes with
 *   standard CAN.  In CAN FD mode, the values 9 to 15 are encoded to values
 *   in the range 12 to 64.
 *
 * Input Parameter:
 *   nbytes - the byte count to convert to a DLC value
 *
 * Returned Value:
 *   The encoded DLC value corresponding to at least that number of bytes.
 *
 ****************************************************************************/

#if 0 /* Not used */
static uint8_t can_bytes2dlc(FAR struct sam_can_s *priv, uint8_t nbytes)
{
  if (nbytes <= 8)
    {
      return nbytes;
    }
#ifdef CONFIG_CAN_FD
  else if (nbytes <= 12)
    {
      return 9;
    }
  else if (nbytes <= 16)
    {
      return 10;
    }
  else if (nbytes <= 20)
    {
      return 11;
    }
  else if (nbytes <= 24)
    {
      return 12;
    }
  else if (nbytes <= 32)
    {
      return 13;
    }
  else if (nbytes <= 48)
    {
      return 14;
    }
  else /* if (nbytes <= 64) */
    {
      return 15;
    }
#else
  else
    {
      return 8;
    }
#endif
}
#endif

/****************************************************************************
 * Name: can_txready_work
 *
 * Description:
 *   This function performs deferred processing from can_txready.  See the
 *   discription of can_txready below for additionla information.
 *
 ****************************************************************************/

#ifdef CONFIG_CAN_TXREADY
static void can_txready_work(FAR void *arg)
{
  FAR struct can_dev_s *dev = (FAR struct can_dev_s *)arg;
  irqstate_t flags;
  int ret;

  canllinfo("xmit head: %d queue: %d tail: %d\n",
            dev->cd_xmit.tx_head, dev->cd_xmit.tx_queue,
            dev->cd_xmit.tx_tail);

  /* Verify that the xmit FIFO is not empty.  The following operations must
   * be performed with interrupt disabled.
   */

  flags = enter_critical_section();
  if (dev->cd_xmit.tx_head != dev->cd_xmit.tx_tail)
    {
      /* Send the next message in the FIFO. */

      ret = can_xmit(dev);

      /* If the message was successfully queued in the H/W FIFO, then
       * can_txdone() should have been called.  If the S/W FIFO were
       * full before then there should now be free space in the S/W FIFO.
       */

      if (ret >= 0)
        {
          /* Are there any threads waiting for space in the TX FIFO? */

          if (dev->cd_ntxwaiters > 0)
            {
              /* Yes.. Inform them that new xmit space is available */

              (void)sem_post(&dev->cd_xmit.tx_sem);
            }
        }
    }

  leave_critical_section(flags);
}
#endif

/****************************************************************************
 * Name: can_open
 *
 * Description:
 *   This function is called whenever the CAN device is opened.
 *
 ****************************************************************************/

static int can_open(FAR struct file *filep)
{
  FAR struct inode     *inode = filep->f_inode;
  FAR struct can_dev_s *dev   = inode->i_private;
  uint8_t               tmp;
  int                   ret   = OK;

  caninfo("ocount: %d\n", dev->cd_ocount);

  /* If the port is the middle of closing, wait until the close is finished */

  if (sem_wait(&dev->cd_closesem) != OK)
    {
      ret = -get_errno();
    }
  else
    {
      /* Increment the count of references to the device.  If this is the first
       * time that the driver has been opened for this device, then initialize
       * the device.
       */

      tmp = dev->cd_ocount + 1;
      if (tmp == 0)
        {
          /* More than 255 opens; uint8_t overflows to zero */

          ret = -EMFILE;
        }
      else
        {
          /* Check if this is the first time that the driver has been opened. */

          if (tmp == 1)
            {
              /* Yes.. perform one time hardware initialization. */

              irqstate_t flags = enter_critical_section();
              ret = dev_setup(dev);
              if (ret == OK)
                {
                  /* Mark the FIFOs empty */

                  dev->cd_xmit.tx_head  = 0;
                  dev->cd_xmit.tx_queue = 0;
                  dev->cd_xmit.tx_tail  = 0;
                  dev->cd_recv.rx_head  = 0;
                  dev->cd_recv.rx_tail  = 0;

                  /* Finally, Enable the CAN RX interrupt */

                  dev_rxint(dev, true);

                  /* Save the new open count only on success */

                  dev->cd_ocount = 1;
                }

              leave_critical_section(flags);
            }
          else
            {
              /* Save the incremented open count */

              dev->cd_ocount = tmp;
            }
        }

      sem_post(&dev->cd_closesem);
    }

  return ret;
}

/****************************************************************************
 * Name: can_close
 *
 * Description:
 *   This routine is called when the CAN device is closed.
 *   It waits for the last remaining data to be sent.
 *
 ****************************************************************************/

static int can_close(FAR struct file *filep)
{
  FAR struct inode     *inode = filep->f_inode;
  FAR struct can_dev_s *dev   = inode->i_private;
  irqstate_t            flags;
  int                   ret = OK;

  caninfo("ocount: %d\n", dev->cd_ocount);

  if (sem_wait(&dev->cd_closesem) != OK)
    {
      ret = -get_errno();
    }
  else
    {
      /* Decrement the references to the driver.  If the reference count will
       * decrement to 0, then uninitialize the driver.
       */

      if (dev->cd_ocount > 1)
        {
          dev->cd_ocount--;
          sem_post(&dev->cd_closesem);
        }
      else
        {
          /* There are no more references to the port */

          dev->cd_ocount = 0;

          /* Stop accepting input */

          dev_rxint(dev, false);

          /* Now we wait for the transmit FIFO to clear */

          while (dev->cd_xmit.tx_head != dev->cd_xmit.tx_tail)
            {
#ifndef CONFIG_DISABLE_SIGNALS
               usleep(HALF_SECOND_USEC);
#else
               up_mdelay(HALF_SECOND_MSEC);
#endif
            }

          /* And wait for the TX hardware FIFO to drain */

          while (!dev_txempty(dev))
            {
#ifndef CONFIG_DISABLE_SIGNALS
              usleep(HALF_SECOND_USEC);
#else
              up_mdelay(HALF_SECOND_MSEC);
#endif
            }

          /* Free the IRQ and disable the CAN device */

          flags = enter_critical_section();       /* Disable interrupts */
          dev_shutdown(dev);       /* Disable the CAN */
          leave_critical_section(flags);

          sem_post(&dev->cd_closesem);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: can_read
 *
 * Description:
 *   Read standard CAN messages
 *
 ****************************************************************************/

static ssize_t can_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen)
{
  FAR struct inode     *inode = filep->f_inode;
  FAR struct can_dev_s *dev   = inode->i_private;
  size_t                nread;
  irqstate_t            flags;
  int                   ret   = 0;

  caninfo("buflen: %d\n", buflen);

  /* The caller must provide enough memory to catch the smallest possible
   * message.  This is not a system error condition, but we won't permit
   * it,  Hence we return 0.
   */

  if (buflen >= CAN_MSGLEN(0))
    {
      /* Interrupts must be disabled while accessing the cd_recv FIFO */

      flags = enter_critical_section();

#ifdef CONFIG_CAN_ERRORS
      /* Check for internal errors */

      if (dev->cd_error != 0)
        {
          FAR struct can_msg_s *msg;

          /* Detected an internal driver error.  Generate a
           * CAN_ERROR_MESSAGE
           */

          if (buflen < CAN_MSGLEN(CAN_ERROR_DLC))
            {
              goto return_with_irqdisabled;
            }

          msg                   = (FAR struct can_msg_s *)buffer;
          msg->cm_hdr.ch_id     = CAN_ERROR_INTERNAL;
          msg->cm_hdr.ch_dlc    = CAN_ERROR_DLC;
          msg->cm_hdr.ch_rtr    = 0;
          msg->cm_hdr.ch_error  = 1;
#ifdef CONFIG_CAN_EXTID
          msg->cm_hdr.ch_extid  = 0;
#endif
          msg->cm_hdr.ch_unused = 0;
          memset(&(msg->cm_data), 0, CAN_ERROR_DLC);
          msg->cm_data[5]       = dev->cd_error;

          /* Reset the error flag */

          dev->cd_error         = 0;

          ret = CAN_MSGLEN(CAN_ERROR_DLC);
          goto return_with_irqdisabled;
        }
#endif /* CONFIG_CAN_ERRORS */

      while (dev->cd_recv.rx_head == dev->cd_recv.rx_tail)
        {
          /* The receive FIFO is empty -- was non-blocking mode selected? */

          if (filep->f_oflags & O_NONBLOCK)
            {
              ret = -EAGAIN;
              goto return_with_irqdisabled;
            }

          /* Wait for a message to be received */

          dev->cd_nrxwaiters++;
          do
            {
              ret = sem_wait(&dev->cd_recv.rx_sem);
            }
          while (ret >= 0 && dev->cd_recv.rx_head == dev->cd_recv.rx_tail);

          dev->cd_nrxwaiters--;

          if (ret < 0)
            {
              ret = -get_errno();
              goto return_with_irqdisabled;
            }
        }

      /* The cd_recv FIFO is not empty.  Copy all buffered data that will fit
       * in the user buffer.
       */

      nread = 0;
      do
        {
          /* Will the next message in the FIFO fit into the user buffer? */

          FAR struct can_msg_s *msg = &dev->cd_recv.rx_buffer[dev->cd_recv.rx_head];
          int nbytes = can_dlc2bytes(msg->cm_hdr.ch_dlc);
          int msglen = CAN_MSGLEN(nbytes);

          if (nread + msglen > buflen)
            {
              break;
            }

          /* Copy the message to the user buffer */

          memcpy(&buffer[nread], msg, msglen);
          nread += msglen;

          /* Increment the head of the circular message buffer */

          if (++dev->cd_recv.rx_head >= CONFIG_CAN_FIFOSIZE)
            {
              dev->cd_recv.rx_head = 0;
            }
        }
      while (dev->cd_recv.rx_head != dev->cd_recv.rx_tail);

      /* All on the messages have bee transferred.  Return the number of bytes
       * that were read.
       */

      ret = nread;

return_with_irqdisabled:
      leave_critical_section(flags);
    }

  return ret;
}

/****************************************************************************
 * Name: can_xmit
 *
 * Description:
 *   Send the message at the head of the cd_xmit FIFO
 *
 * Assumptions:
 *   Called with interrupts disabled
 *
 ****************************************************************************/

static int can_xmit(FAR struct can_dev_s *dev)
{
  int tmpndx;
  int ret = -EBUSY;

  canllinfo("xmit head: %d queue: %d tail: %d\n",
            dev->cd_xmit.tx_head, dev->cd_xmit.tx_queue, dev->cd_xmit.tx_tail);

  /* If there is nothing to send, then just disable interrupts and return */

  if (dev->cd_xmit.tx_head == dev->cd_xmit.tx_tail)
    {
      DEBUGASSERT(dev->cd_xmit.tx_queue == dev->cd_xmit.tx_head);

#ifndef CONFIG_CAN_TXREADY
      /* We can disable CAN TX interrupts -- unless there is a H/W FIFO.  In
       * that case, TX interrupts must stay enabled until the H/W FIFO is
       * fully emptied.
       */

      dev_txint(dev, false);
#endif
      return -EIO;
    }

  /* Check if we have already queued all of the data in the TX fifo.
   *
   * tx_tail:  Incremented in can_write each time a message is queued in the FIFO
   * tx_head:  Incremented in can_txdone each time a message completes
   * tx_queue: Incremented each time that a message is sent to the hardware.
   *
   * Logically (ignoring buffer wrap-around): tx_head <= tx_queue <= tx_tail
   * tx_head == tx_queue == tx_tail means that the FIFO is empty
   * tx_head < tx_queue == tx_tail means that all data has been queued, but
   * we are still waiting for transmissions to complete.
   */

  while (dev->cd_xmit.tx_queue != dev->cd_xmit.tx_tail && dev_txready(dev))
    {
      /* No.. The FIFO should not be empty in this case */

      DEBUGASSERT(dev->cd_xmit.tx_head != dev->cd_xmit.tx_tail);

      /* Increment the FIFO queue index before sending (because dev_send()
       * might call can_txdone()).
       */

      tmpndx = dev->cd_xmit.tx_queue;
      if (++dev->cd_xmit.tx_queue >= CONFIG_CAN_FIFOSIZE)
        {
          dev->cd_xmit.tx_queue = 0;
        }

      /* Send the next message at the FIFO queue index */

      ret = dev_send(dev, &dev->cd_xmit.tx_buffer[tmpndx]);
      if (ret != OK)
        {
          canerr("dev_send failed: %d\n", ret);
          break;
        }
    }

  /* Make sure that TX interrupts are enabled */

  dev_txint(dev, true);
  return ret;
}

/****************************************************************************
 * Name: can_write
 ****************************************************************************/

static ssize_t can_write(FAR struct file *filep, FAR const char *buffer,
                         size_t buflen)
{
  FAR struct inode        *inode = filep->f_inode;
  FAR struct can_dev_s    *dev   = inode->i_private;
  FAR struct can_txfifo_s *fifo  = &dev->cd_xmit;
  FAR struct can_msg_s    *msg;
  bool                     inactive;
  ssize_t                  nsent = 0;
  irqstate_t               flags;
  int                      nexttail;
  int                      nbytes;
  int                      msglen;
  int                      ret   = 0;

  caninfo("buflen: %d\n", buflen);

  /* Interrupts must disabled throughout the following */

  flags = enter_critical_section();

  /* Check if the TX is inactive when we started. In certain race conditions,
   * there may be a pending interrupt to kick things back off, but we will
   * be sure here that there is not.  That the hardware is IDLE and will
   * need to be kick-started.
   */

  inactive = dev_txempty(dev);

  /* Add the messages to the FIFO.  Ignore any trailing messages that are
   * shorter than the minimum.
   */

  while ((buflen - nsent) >= CAN_MSGLEN(0))
    {
      /* Check if adding this new message would over-run the drivers ability
       * to enqueue xmit data.
       */

      nexttail = fifo->tx_tail + 1;
      if (nexttail >= CONFIG_CAN_FIFOSIZE)
        {
          nexttail = 0;
        }

      /* If the XMIT FIFO becomes full, then wait for space to become available */

      while (nexttail == fifo->tx_head)
        {
          /* The transmit FIFO is full  -- was non-blocking mode selected? */

          if ((filep->f_oflags & O_NONBLOCK) != 0)
            {
              if (nsent == 0)
                {
                  ret = -EAGAIN;
                }
              else
                {
                  ret = nsent;
                }

              goto return_with_irqdisabled;
            }

          /* If the TX hardware was inactive when we started, then we will have
           * start the XMIT sequence generate the TX done interrupts needed
           * to clear the FIFO.
           */

          if (inactive)
            {
              (void)can_xmit(dev);
            }

          /* Wait for a message to be sent */

          do
            {
              DEBUGASSERT(dev->cd_ntxwaiters < 255);
              dev->cd_ntxwaiters++;
              ret = sem_wait(&fifo->tx_sem);
              dev->cd_ntxwaiters--;

              if (ret < 0 && get_errno() != EINTR)
                {
                  ret = -get_errno();
                  goto return_with_irqdisabled;
                }
            }
          while (ret < 0);

          /* Re-check the FIFO state */

          inactive = dev_txempty(dev);
        }

      /* We get here if there is space at the end of the FIFO.  Add the new
       * CAN message at the tail of the FIFO.
       */

      msg    = (FAR struct can_msg_s *)&buffer[nsent];
      nbytes = can_dlc2bytes(msg->cm_hdr.ch_dlc);
      msglen = CAN_MSGLEN(nbytes);
      memcpy(&fifo->tx_buffer[fifo->tx_tail], msg, msglen);

      /* Increment the tail of the circular buffer */

      fifo->tx_tail = nexttail;

      /* Increment the number of bytes that were sent */

      nsent += msglen;
    }

  /* We get here after all messages have been added to the FIFO.  Check if
   * we need to kick of the XMIT sequence.
   */

  if (inactive)
    {
      (void)can_xmit(dev);
    }

  /* Return the number of bytes that were sent */

  ret = nsent;

return_with_irqdisabled:
  leave_critical_section(flags);
  return ret;
}

/****************************************************************************
 * Name: can_rtrread
 *
 * Description:
 *   Read RTR messages.  The RTR message is a special message -- it is an
 *   outgoing message that says "Please re-transmit the message with the
 *   same identifier as this message.  So the RTR read is really a
 *   send-wait-receive operation.
 *
 ****************************************************************************/

static inline ssize_t can_rtrread(FAR struct can_dev_s *dev,
                                  FAR struct canioc_rtr_s *rtr)
{
  FAR struct can_rtrwait_s *wait = NULL;
  irqstate_t                flags;
  int                       i;
  int                       ret = -ENOMEM;

  /* Disable interrupts through this operation */

  flags = enter_critical_section();

  /* Find an available slot in the pending RTR list */

  for (i = 0; i < CONFIG_CAN_NPENDINGRTR; i++)
    {
      FAR struct can_rtrwait_s *tmp = &dev->cd_rtr[i];
      if (!rtr->ci_msg)
        {
          tmp->cr_id  = rtr->ci_id;
          tmp->cr_msg = rtr->ci_msg;
          dev->cd_npendrtr++;
          wait        = tmp;
          break;
        }
    }

  if (wait)
    {
      /* Send the remote transmission request */

      ret = dev_remoterequest(dev, wait->cr_id);
      if (ret == OK)
        {
          /* Then wait for the response */

          ret = sem_wait(&wait->cr_sem);
        }
    }

  leave_critical_section(flags);
  return ret;
}

/****************************************************************************
 * Name: can_ioctl
 ****************************************************************************/

static int can_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct inode     *inode = filep->f_inode;
  FAR struct can_dev_s *dev   = inode->i_private;
  int                   ret   = OK;

  caninfo("cmd: %d arg: %ld\n", cmd, arg);

  /* Handle built-in ioctl commands */

  switch (cmd)
    {
      /* CANIOC_RTR: Send the remote transmission request and wait for the
       * response.  Argument is a reference to struct canioc_rtr_s
       * (casting to uintptr_t first eliminates complaints on some
       * architectures where the sizeof long is different from the size of
       * a pointer).
       */

      case CANIOC_RTR:
        ret = can_rtrread(dev, (FAR struct canioc_rtr_s *)((uintptr_t)arg));
        break;

      /* Not a "built-in" ioctl command.. perhaps it is unique to this
       * lower-half, device driver.
       */

      default:
        ret = dev_ioctl(dev, cmd, arg);
        break;
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: can_register
 *
 * Description:
 *   Register serial console and serial ports.
 *
 ****************************************************************************/

int can_register(FAR const char *path, FAR struct can_dev_s *dev)
{
  int i;

  /* Initialize the CAN device structure */

  dev->cd_ocount     = 0;
  dev->cd_ntxwaiters = 0;
  dev->cd_nrxwaiters = 0;
  dev->cd_npendrtr   = 0;
#ifdef CONFIG_CAN_ERRORS
  dev->cd_error      = 0;
#endif

  sem_init(&dev->cd_xmit.tx_sem, 0, 0);
  sem_init(&dev->cd_recv.rx_sem, 0, 0);
  sem_init(&dev->cd_closesem, 0, 1);

  for (i = 0; i < CONFIG_CAN_NPENDINGRTR; i++)
    {
      sem_init(&dev->cd_rtr[i].cr_sem, 0, 0);
      dev->cd_rtr[i].cr_msg = NULL;
    }

  /* Initialize/reset the CAN hardware */

  dev_reset(dev);

  /* Register the CAN device */

  caninfo("Registering %s\n", path);
  return register_driver(path, &g_canops, 0666, dev);
}

/****************************************************************************
 * Name: can_receive
 *
 * Description:
 *   Called from the CAN interrupt handler when new read data is available
 *
 * Input Parameters:
 *   dev  - CAN driver state structure
 *   hdr  - CAN message header
 *   data - CAN message data (if DLC > 0)
 *
 * Returned Value:
 *   OK on success; a negated errno on failure.
 *
 * Assumptions:
 *   CAN interrupts are disabled.
 *
 ****************************************************************************/

int can_receive(FAR struct can_dev_s *dev, FAR struct can_hdr_s *hdr,
                FAR uint8_t *data)
{
  FAR struct can_rxfifo_s *fifo = &dev->cd_recv;
  FAR uint8_t             *dest;
  int                      nexttail;
  int                      errcode = -ENOMEM;
  int                      i;

  canllinfo("ID: %d DLC: %d\n", hdr->ch_id, hdr->ch_dlc);

  /* Check if adding this new message would over-run the drivers ability to
   * enqueue read data.
   */

  nexttail = fifo->rx_tail + 1;
  if (nexttail >= CONFIG_CAN_FIFOSIZE)
    {
      nexttail = 0;
    }

  /* First, check if this response matches any RTR response that we may be
   * waiting for.
   */

  if (dev->cd_npendrtr > 0)
    {
      /* There are pending RTR requests -- search the lists of requests
       * and see any any matches this new message.
       */

      for (i = 0; i < CONFIG_CAN_NPENDINGRTR; i++)
        {
          FAR struct can_rtrwait_s *rtr = &dev->cd_rtr[i];
          FAR struct can_msg_s     *msg = rtr->cr_msg;

          /* Check if the entry is valid and if the ID matches.  A valid
           * entry has a non-NULL receiving address
           */

          if (msg && hdr->ch_id == rtr->cr_id)
            {
              int nbytes;

              /* We have the response... copy the data to the user's buffer */

              memcpy(&msg->cm_hdr, hdr, sizeof(struct can_hdr_s));

              nbytes = can_dlc2bytes(hdr->ch_dlc);
              for (i = 0, dest = msg->cm_data; i < nbytes; i++)
                {
                  *dest++ = *data++;
                }

              /* Mark the entry unused */

              rtr->cr_msg = NULL;
              dev->cd_npendrtr--;

              /* And restart the waiting thread */

              sem_post(&rtr->cr_sem);
            }
        }
    }

  /* Refuse the new data if the FIFO is full */

  if (nexttail != fifo->rx_head)
    {
      int nbytes;

      /* Add the new, decoded CAN message at the tail of the FIFO.
       *
       * REVISIT:  In the CAN FD format, the coding of the DLC differs from
       * the standard CAN format. The DLC codes 0 to 8 have the same coding
       * as in standard CAN, the codes 9 to 15, which in standard CAN all
       * code a data field of 8 bytes, are encoded:
       *
       *   9->12, 10->16, 11->20, 12->24, 13->32, 14->48, 15->64
       */

      memcpy(&fifo->rx_buffer[fifo->rx_tail].cm_hdr, hdr, sizeof(struct can_hdr_s));

      nbytes = can_dlc2bytes(hdr->ch_dlc);
      for (i = 0, dest = fifo->rx_buffer[fifo->rx_tail].cm_data; i < nbytes; i++)
        {
          *dest++ = *data++;
        }

      /* Increment the tail of the circular buffer */

      fifo->rx_tail = nexttail;

      /* The increment the counting semaphore. The maximum value should be
       * CONFIG_CAN_FIFOSIZE -- one possible count for each allocated
       * message buffer.
       */

      if (dev->cd_nrxwaiters > 0)
        {
          sem_post(&fifo->rx_sem);
        }

      errcode = OK;
    }
#ifdef CONFIG_CAN_ERRORS
  else
    {
      /* Report rx overflow error */

      dev->cd_error |= CAN_ERROR5_RXOVERFLOW;
    }
#endif

  return errcode;
}

/****************************************************************************
 * Name: can_txdone
 *
 * Description:
 *   Called when the hardware has processed the outgoing TX message.  This
 *   normally means that the CAN messages was sent out on the wire.  But
 *   if the CAN hardware supports a H/W TX FIFO, then this call may mean
 *   only that the CAN message has been added to the H/W FIFO.  In either
 *   case, the upper-half CAN driver can remove the outgoing message from
 *   the S/W FIFO and discard it.
 *
 *   This function may be called in different contexts, depending upon the
 *   nature of the underlying CAN hardware.
 *
 *   1. No H/W TX FIFO (CONFIG_CAN_TXREADY not defined)
 *
 *      This function is only called from the CAN interrupt handler at the
 *      completion of a send operation.
 *
 *        can_write() -> can_xmit() -> dev_send()
 *        CAN interrupt -> can_txdone()
 *
 *      If the CAN hardware is busy, then the call to dev_send() will
 *      fail, the S/W TX FIFO will accumulate outgoing messages, and the
 *      thread calling can_write() may eventually block waiting for space in
 *      the S/W TX FIFO.
 *
 *      When the CAN hardware completes the transfer and processes the
 *      CAN interrupt, the call to can_txdone() will make space in the S/W
 *      TX FIFO and will awaken the waiting can_write() thread.
 *
 *   2a. H/W TX FIFO (CONFIG_CAN_TXREADY=y) and S/W TX FIFO not full
 *
 *      This function will be called back from dev_send() immediately when a
 *      new CAN message is added to H/W TX FIFO:
 *
 *        can_write() -> can_xmit() -> dev_send() -> can_txdone()
 *
 *      When the H/W TX FIFO becomes full, dev_send() will fail and
 *      can_txdone() will not be called.  In this case the S/W TX FIFO will
 *      accumulate outgoing messages, and the thread calling can_write() may
 *      eventually block waiting for space in the S/W TX FIFO.
 *
 *   2b. H/W TX FIFO (CONFIG_CAN_TXREADY=y) and S/W TX FIFO full
 *
 *      In this case, the thread calling can_write() is blocked waiting for
 *      space in the S/W TX FIFO.  can_txdone() will be called, indirectly,
 *      from can_txready_work() running on the thread of the work queue.
 *
 *        CAN interrupt -> can_txready() -> Schedule can_txready_work()
 *        can_txready_work() -> can_xmit() -> dev_send() -> can_txdone()
 *
 *      The call dev_send() should not fail in this case and the subsequent
 *      call to can_txdone() will make space in the S/W TX FIFO and will
 *      awaken the waiting thread.
 *
 * Input Parameters:
 *   dev  - The specific CAN device
 *   hdr  - The 16-bit CAN header
 *   data - An array contain the CAN data.
 *
 * Returned Value:
 *   OK on success; a negated errno on failure.
 *
 * Assumptions:
 *   Interrupts are disabled.  This is required by can_xmit() which is called
 *   by this function.  Interrupts are explicitly disabled when called
 *   through can_write().  Interrupts are expected be disabled when called
 *   from the CAN interrupt handler.
 *
 ****************************************************************************/

int can_txdone(FAR struct can_dev_s *dev)
{
  int ret = -ENOENT;

  canllinfo("xmit head: %d queue: %d tail: %d\n",
            dev->cd_xmit.tx_head, dev->cd_xmit.tx_queue, dev->cd_xmit.tx_tail);

  /* Verify that the xmit FIFO is not empty */

  if (dev->cd_xmit.tx_head != dev->cd_xmit.tx_tail)
    {
      /* The tx_queue index is incremented each time can_xmit() queues
       * the transmission.  When can_txdone() is called, the tx_queue
       * index should always have been advanced beyond the current tx_head
       * index.
       */

      DEBUGASSERT(dev->cd_xmit.tx_head != dev->cd_xmit.tx_queue);

      /* Remove the message at the head of the xmit FIFO */

      if (++dev->cd_xmit.tx_head >= CONFIG_CAN_FIFOSIZE)
        {
          dev->cd_xmit.tx_head = 0;
        }

      /* Send the next message in the FIFO */

      (void)can_xmit(dev);

      /* Are there any threads waiting for space in the TX FIFO? */

      if (dev->cd_ntxwaiters > 0)
        {
          /* Yes.. Inform them that new xmit space is available */

          ret = sem_post(&dev->cd_xmit.tx_sem);
        }
      else
        {
          ret = OK;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: can_txready
 *
 * Description:
 *   Called from the CAN interrupt handler at the completion of a send
 *   operation.  This interface is needed only for CAN hardware that
 *   supports queing of outgoing messages in a H/W FIFO.
 *
 *   The CAN upper half driver also supports a queue of output messages in a
 *   S/W FIFO.  Messages are added to that queue when when can_write() is
 *   called and removed from the queue in can_txdone() when each TX message
 *   is complete.
 *
 *   After each message is added to the S/W FIFO, the CAN upper half driver
 *   will attempt to send the message by calling into the lower half driver.
 *   That send will not be performed if the lower half driver is busy, i.e.,
 *   if dev_txready() returns false.  In that case, the number of messages in
 *   the S/W FIFO can grow.  If the S/W FIFO becomes full, then can_write()
 *   will wait for space in the S/W FIFO.
 *
 *   If the CAN hardware does not support a H/W FIFO then busy means that
 *   the hardware is actively sending the message and is guaranteed to
 *   become non-busy (i.e, dev_txready()) when the send transfer completes
 *   and can_txdone() is called.  So the call to can_txdone() means that the
 *   transfer has completed and also that the hardware is ready to accept
 *   another transfer.
 *
 *   If the CAN hardware supports a H/W FIFO, can_txdone() is not called
 *   when the tranfer is complete, but rather when the transfer is queued in
 *   the H/W FIFO.  When the H/W FIFO becomes full, then dev_txready() will
 *   report false and the number of queued messages in the S/W FIFO will grow.
 *
 *   There is no mechanism in this case to inform the upper half driver when
 *   the hardware is again available, when there is again space in the H/W
 *   FIFO.  can_txdone() will not be called again.  If the S/W FIFO becomes
 *   full, then the upper half driver will wait for space to become
 *   available, but there is no event to awaken it and the driver will hang.
 *
 *   Enabling this feature adds support for the can_txready() interface.
 *   This function is called from the lower half driver's CAN interrupt
 *   handler each time a TX transfer completes.  This is a sure indication
 *   that the H/W FIFO is no longer full.  can_txready() will then awaken
 *   the can_write() logic and the hang condition is avoided.
 *
 * Input Parameters:
 *   dev  - The specific CAN device
 *
 * Returned Value:
 *   OK on success; a negated errno on failure.
 *
 * Assumptions:
 *   Interrupts are disabled.  This function may execute in the context of
 *   and interrupt handler.
 *
 ****************************************************************************/

#ifdef CONFIG_CAN_TXREADY
int can_txready(FAR struct can_dev_s *dev)
{
  int ret = -ENOENT;

  canllinfo("xmit head: %d queue: %d tail: %d waiters: %d\n",
            dev->cd_xmit.tx_head, dev->cd_xmit.tx_queue, dev->cd_xmit.tx_tail,
            dev->cd_ntxwaiters);

  /* Verify that the xmit FIFO is not empty.  This is safe because interrupts
   * are always disabled when calling into can_xmit(); this cannot collide
   * with ongoing activity from can_write().
   */

  if (dev->cd_xmit.tx_head != dev->cd_xmit.tx_tail)
    {
      /* Is work already scheduled? */

      if (work_available(&dev->cd_work))
        {
          /* Yes... schedule to perform can_txready() work on the worker
           * thread.  Although data structures are protected by disabling
           * interrupts, the can_xmit() operations may involve semaphore
           * operations and, hence, should not be done at the interrupt
           * level.
           */

          ret = work_queue(CANWORK, &dev->cd_work, can_txready_work, dev, 0);
        }
      else
        {
          ret = -EBUSY;
        }
    }
  else
    {
      /* There should not be any threads waiting for space in the S/W TX
       * FIFO is it is empty.
       *
       * REVISIT: Assertion can fire in certain race conditions, i.e, when
       * all waiters have been awakened but have not yet had a chance to
       * decrement cd_ntxwaiters.
       */

      //DEBUGASSERT(dev->cd_ntxwaiters == 0);

#if 0 /* REVISIT */
      /* When the H/W FIFO has been emptied, we can disable further TX
       * interrupts.
       *
       * REVISIT:  The fact that the S/W FIFO is empty does not mean that
       * the H/W FIFO is also empty.  If we really want this to work this
       * way, then we would probably need and additional parameter to tell
       * us if the H/W FIFO is empty.
       */

      dev_txint(dev, false);
#endif
    }

  return ret;
}
#endif /* CONFIG_CAN_TXREADY */
#endif /* CONFIG_CAN */
