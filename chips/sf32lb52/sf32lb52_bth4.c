/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb52_bth4.c
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
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <debug.h>

#include <nuttx/wireless/bluetooth/bt_hci.h>
#include <nuttx/serial/uart_bth4.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>
#include <nuttx/wireless/bluetooth/bt_uart.h>

#include "sf32lb52_bt_adapter.h"

#define SF32LB52_BT_H4_RX_BUFSIZE 2048
#define SF32LB52_BT_TRACE         0

#ifndef BT_HCI_OP_READ_SUPPORTED_COMMANDS
#  define BT_HCI_OP_READ_SUPPORTED_COMMANDS BT_OP(BT_OGF_INFO, 0x0002)
#endif

#ifndef BT_HCI_OP_READ_LOCAL_EXT_FEATURES
#  define BT_HCI_OP_READ_LOCAL_EXT_FEATURES BT_OP(BT_OGF_INFO, 0x0004)
#endif

#ifndef BT_HCI_OP_READ_BUFFER_SIZE
#  define BT_HCI_OP_READ_BUFFER_SIZE        BT_OP(BT_OGF_INFO, 0x0005)
#endif

#ifndef BT_HCI_OP_WRITE_DEFAULT_LINK_POLICY_SETTINGS
#  define BT_HCI_OP_WRITE_DEFAULT_LINK_POLICY_SETTINGS BT_OP(BT_OGF_LINK_POLICY, 0x000f)
#endif

#ifndef BT_HCI_OP_SET_EVENT_MASK
#  define BT_HCI_OP_SET_EVENT_MASK          BT_OP(BT_OGF_BASEBAND, 0x0001)
#endif

#ifndef BT_HCI_OP_WRITE_LOCAL_NAME
#  define BT_HCI_OP_WRITE_LOCAL_NAME        BT_OP(BT_OGF_BASEBAND, 0x0013)
#endif

#ifndef BT_HCI_OP_WRITE_PAGE_TIMEOUT
#  define BT_HCI_OP_WRITE_PAGE_TIMEOUT      BT_OP(BT_OGF_BASEBAND, 0x0018)
#endif

#ifndef BT_HCI_OP_WRITE_SCAN_ENABLE
#  define BT_HCI_OP_WRITE_SCAN_ENABLE       BT_OP(BT_OGF_BASEBAND, 0x001a)
#endif

#ifndef BT_HCI_OP_WRITE_PAGE_SCAN_ACTIVITY
#  define BT_HCI_OP_WRITE_PAGE_SCAN_ACTIVITY BT_OP(BT_OGF_BASEBAND, 0x001c)
#endif

#ifndef BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY
#  define BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY BT_OP(BT_OGF_BASEBAND, 0x001e)
#endif

#ifndef BT_HCI_OP_WRITE_CLASS_OF_DEVICE
#  define BT_HCI_OP_WRITE_CLASS_OF_DEVICE   BT_OP(BT_OGF_BASEBAND, 0x0024)
#endif

#ifndef BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE
#  define BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE BT_OP(BT_OGF_BASEBAND, 0x0043)
#endif

#ifndef BT_HCI_OP_WRITE_EXTENDED_INQUIRY_RESPONSE
#  define BT_HCI_OP_WRITE_EXTENDED_INQUIRY_RESPONSE BT_OP(BT_OGF_BASEBAND, 0x0052)
#endif

#ifndef BT_HCI_OP_WRITE_INQUIRY_MODE
#  define BT_HCI_OP_WRITE_INQUIRY_MODE      BT_OP(BT_OGF_BASEBAND, 0x0045)
#endif

#ifndef BT_HCI_OP_WRITE_PAGE_SCAN_TYPE
#  define BT_HCI_OP_WRITE_PAGE_SCAN_TYPE    BT_OP(BT_OGF_BASEBAND, 0x0047)
#endif

#ifndef BT_HCI_OP_WRITE_SSP_MODE
#  define BT_HCI_OP_WRITE_SSP_MODE          BT_OP(BT_OGF_BASEBAND, 0x0056)
#endif

#ifndef BT_HCI_OP_SET_EVENT_MASK_PAGE_2
#  define BT_HCI_OP_SET_EVENT_MASK_PAGE_2   BT_OP(BT_OGF_BASEBAND, 0x0063)
#endif

#ifndef BT_HCI_OP_WRITE_SC_HOST_SUPP
#  define BT_HCI_OP_WRITE_SC_HOST_SUPP      BT_OP(BT_OGF_BASEBAND, 0x007a)
#endif

#ifndef BT_HCI_OP_LE_SET_EVENT_MASK
#  define BT_HCI_OP_LE_SET_EVENT_MASK       BT_OP(BT_OGF_LE, 0x0001)
#endif

#ifndef BT_HCI_OP_LE_READ_SUPP_STATES
#  define BT_HCI_OP_LE_READ_SUPP_STATES     BT_OP(BT_OGF_LE, 0x001c)
#endif

#ifndef BT_HCI_OP_LE_READ_LOCAL_FEATURES
#  define BT_HCI_OP_LE_READ_LOCAL_FEATURES  BT_OP(BT_OGF_LE, 0x0003)
#endif

#ifndef BT_HCI_OP_LE_WRITE_DEFAULT_DATA_LEN
#  define BT_HCI_OP_LE_WRITE_DEFAULT_DATA_LEN BT_OP(BT_OGF_LE, 0x0024)
#endif

#ifndef BT_HCI_OP_LE_READ_RL_SIZE
#  define BT_HCI_OP_LE_READ_RL_SIZE         BT_OP(BT_OGF_LE, 0x002a)
#endif

#ifndef BT_HCI_OP_LE_SET_RPA_TIMEOUT
#  define BT_HCI_OP_LE_SET_RPA_TIMEOUT      BT_OP(BT_OGF_LE, 0x002e)
#endif

#ifndef BT_HCI_OP_LE_READ_MAX_DATA_LEN
#  define BT_HCI_OP_LE_READ_MAX_DATA_LEN    BT_OP(BT_OGF_LE, 0x002f)
#endif

#ifndef BT_HCI_OP_LE_READ_MAX_ADV_DATA_LEN
#  define BT_HCI_OP_LE_READ_MAX_ADV_DATA_LEN BT_OP(BT_OGF_LE, 0x003a)
#endif

#ifndef BT_HCI_OP_LE_SET_HOST_FEATURE
#  define BT_HCI_OP_LE_SET_HOST_FEATURE     BT_OP(BT_OGF_LE, 0x0074)
#endif

#define SF32LB52_HCI_STATUS_SUCCESS          0x00
#define SF32LB52_HCI_READ_COMMANDS_RPLEN     65
#define SF32LB52_HCI_RAND_RPLEN              9
#define SF32LB52_HCI_MAX_CMD_COMPLETE_RPLEN  SF32LB52_HCI_READ_COMMANDS_RPLEN

struct sf32lb52_bt_priv_s
{
  struct bt_driver_s drv;
  uint8_t rxbuf[SF32LB52_BT_H4_RX_BUFSIZE];
  size_t rxlen;
  bool drop_rx_until_tx;
};

static int sf32lb52_bt_open(struct bt_driver_s *drv);
int sf32lb52_bth4_controller_restart(void);
void sf32lb52_lcpu_boot_dump_evidence(void);
static int sf32lb52_bt_send(struct bt_driver_s *drv,
                            enum bt_buf_type_e type,
                            void *data, size_t len);
static void sf32lb52_bt_close(struct bt_driver_s *drv);
static int sf32lb52_bt_recv_cb(uint8_t *data, uint16_t len);
static int sf32lb52_bt_ensure_controller_enabled(uint16_t opcode);

#ifdef CONFIG_BT
extern void z_sys_init(void);

static bool g_sf32lb52_zblue_inited;

static void sf32lb52_bt_zblue_init_once(void)
{
  if (!g_sf32lb52_zblue_inited)
    {
      z_sys_init();
      g_sf32lb52_zblue_inited = true;
    }
}
#else
static void sf32lb52_bt_zblue_init_once(void)
{
}
#endif

static uint16_t sf32lb52_bt_get_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void sf32lb52_bt_put_le16(uint8_t *data, uint16_t value)
{
  data[0] = value & 0xff;
  data[1] = value >> 8;
}

static ssize_t sf32lb52_bt_h4_packet_len(const uint8_t *data, size_t len)
{
  uint16_t payload_len;

  if (len < H4_HEADER_SIZE)
    {
      return 0;
    }

  switch (data[0])
    {
      case H4_EVT:
        if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s))
          {
            return 0;
          }

        payload_len = data[H4_HEADER_SIZE + 1];
        return H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + payload_len;

      case H4_ACL:
        if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_acl_hdr_s))
          {
            return 0;
          }

        payload_len = sf32lb52_bt_get_le16(&data[H4_HEADER_SIZE + 2]);
        return H4_HEADER_SIZE + sizeof(struct bt_hci_acl_hdr_s) + payload_len;

      case H4_ISO:
        if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_iso_hdr_s))
          {
            return 0;
          }

        payload_len = sf32lb52_bt_get_le16(&data[H4_HEADER_SIZE + 2]) & 0x3fff;
        return H4_HEADER_SIZE + sizeof(struct bt_hci_iso_hdr_s) + payload_len;

      default:
        return -EINVAL;
    }
}

static void sf32lb52_bt_normalize_event(uint8_t *data, size_t len)
{
  uint16_t opcode;

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 4 ||
      data[0] != H4_EVT)
    {
      return;
    }

  if (data[1] == BT_HCI_EVT_CMD_COMPLETE && data[2] >= 4)
    {
      opcode = sf32lb52_bt_get_le16(&data[4]);
      if (opcode == BT_HCI_OP_RESET && data[6] != SF32LB52_HCI_STATUS_SUCCESS)
        {
          data[6] = SF32LB52_HCI_STATUS_SUCCESS;
        }
    }
  else if (data[1] == BT_HCI_EVT_CMD_STATUS && data[2] >= 4)
    {
      opcode = sf32lb52_bt_get_le16(&data[5]);
      if (opcode == BT_HCI_OP_RESET && data[3] != SF32LB52_HCI_STATUS_SUCCESS)
        {
          data[3] = SF32LB52_HCI_STATUS_SUCCESS;
        }
    }
}

static int sf32lb52_bt_forward_packet(struct sf32lb52_bt_priv_s *priv,
                                      uint8_t *data, size_t len)
{
  enum bt_buf_type_e type;
  int ret;

  if (len <= H4_HEADER_SIZE)
    {
      return -EINVAL;
    }

  switch (data[0])
    {
      case H4_EVT:
        sf32lb52_bt_normalize_event(data, len);
        type = BT_EVT;
        break;

      case H4_ACL:
        type = BT_ACL_IN;
        break;

      case H4_ISO:
        type = BT_ISO_IN;
        break;

      default:
        return -EINVAL;
    }

  ret = bt_netdev_receive(&priv->drv,
                          type,
                          (void *)&data[H4_HEADER_SIZE],
                          len - H4_HEADER_SIZE);
  if (ret < 0)
    {
      wlerr("Failed to receive HCI packet: %d\n", ret);
    }

  return ret;
}

static int sf32lb52_bt_synth_cmd_complete(struct sf32lb52_bt_priv_s *priv,
                                          uint16_t opcode,
                                          const uint8_t *return_params,
                                          size_t return_len)
{
  uint8_t event[H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 3 +
                SF32LB52_HCI_MAX_CMD_COMPLETE_RPLEN];
  size_t payload_len;

  if (return_len > SF32LB52_HCI_MAX_CMD_COMPLETE_RPLEN)
    {
      return -E2BIG;
    }

  payload_len = 3 + return_len;
  event[0] = H4_EVT;
  event[1] = BT_HCI_EVT_CMD_COMPLETE;
  event[2] = payload_len;
  event[3] = 1;
  sf32lb52_bt_put_le16(&event[4], opcode);
  if (return_len > 0)
    {
      memcpy(&event[6], return_params, return_len);
    }

  return sf32lb52_bt_forward_packet(priv, event,
                                    H4_HEADER_SIZE +
                                    sizeof(struct bt_hci_evt_hdr_s) +
                                    payload_len);
}

static int sf32lb52_bt_synth_status_complete(struct sf32lb52_bt_priv_s *priv,
                                             uint16_t opcode)
{
  const uint8_t status = SF32LB52_HCI_STATUS_SUCCESS;

  return sf32lb52_bt_synth_cmd_complete(priv, opcode, &status,
                                        sizeof(status));
}

static bool sf32lb52_bt_emulate_cmd(struct sf32lb52_bt_priv_s *priv,
                                    uint16_t opcode, int *ret)
{
  uint8_t params[SF32LB52_HCI_MAX_CMD_COMPLETE_RPLEN];

  switch (opcode)
    {
      case BT_HCI_OP_READ_SUPPORTED_COMMANDS:
        memset(params, 0, sizeof(params));
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        params[1 + 27] = 1 << 7;
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params,
                                              SF32LB52_HCI_READ_COMMANDS_RPLEN);
        return true;

      case BT_HCI_OP_LE_RAND:
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        arc4random_buf(&params[1], SF32LB52_HCI_RAND_RPLEN - 1);
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params,
                                              SF32LB52_HCI_RAND_RPLEN);
        return true;

      case BT_HCI_OP_READ_LOCAL_EXT_FEATURES:
        memset(params, 0, 11);
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 11);
        return true;

      case BT_HCI_OP_READ_BUFFER_SIZE:
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        sf32lb52_bt_put_le16(&params[1], 0x00fb);
        params[3] = 0;
        sf32lb52_bt_put_le16(&params[4], 4);
        sf32lb52_bt_put_le16(&params[6], 0);
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 8);
        return true;

      case BT_HCI_OP_LE_READ_BUFFER_SIZE:
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        sf32lb52_bt_put_le16(&params[1], 0x00fb);
        params[3] = 4;
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 4);
        return true;

      case BT_HCI_OP_LE_READ_LOCAL_FEATURES:
        memset(params, 0, 9);
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 9);
        return true;

      case BT_HCI_OP_LE_READ_MAX_DATA_LEN:
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        sf32lb52_bt_put_le16(&params[1], 0x00fb);
        sf32lb52_bt_put_le16(&params[3], 0x0148);
        sf32lb52_bt_put_le16(&params[5], 0x00fb);
        sf32lb52_bt_put_le16(&params[7], 0x0148);
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 9);
        return true;

      case BT_HCI_OP_LE_READ_MAX_ADV_DATA_LEN:
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        sf32lb52_bt_put_le16(&params[1], 31);
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 3);
        return true;

      case BT_HCI_OP_LE_READ_SUPP_STATES:
        memset(params, 0, 9);
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 9);
        return true;

      case BT_HCI_OP_LE_READ_RL_SIZE:
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        params[1] = 0;
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 2);
        return true;

      case BT_HCI_OP_LE_WRITE_LE_HOST_SUPP:
      case BT_HCI_OP_LE_SET_HOST_FEATURE:
      case BT_HCI_OP_LE_SET_EVENT_MASK:
      case BT_HCI_OP_LE_WRITE_DEFAULT_DATA_LEN:
      case BT_HCI_OP_LE_SET_RPA_TIMEOUT:
      case BT_HCI_OP_SET_EVENT_MASK:
      case BT_HCI_OP_SET_EVENT_MASK_PAGE_2:
      case BT_HCI_OP_WRITE_LOCAL_NAME:
      case BT_HCI_OP_WRITE_SCAN_ENABLE:
      case BT_HCI_OP_WRITE_PAGE_SCAN_ACTIVITY:
      case BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY:
      case BT_HCI_OP_WRITE_PAGE_TIMEOUT:
      case BT_HCI_OP_WRITE_CLASS_OF_DEVICE:
      case BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE:
      case BT_HCI_OP_WRITE_EXTENDED_INQUIRY_RESPONSE:
      case BT_HCI_OP_WRITE_INQUIRY_MODE:
      case BT_HCI_OP_WRITE_PAGE_SCAN_TYPE:
      case BT_HCI_OP_WRITE_SSP_MODE:
      case BT_HCI_OP_WRITE_SC_HOST_SUPP:
      case BT_HCI_OP_WRITE_DEFAULT_LINK_POLICY_SETTINGS:
      case BT_HCI_OP_HOST_BUFFER_SIZE:
      case BT_HCI_OP_SET_CTL_TO_HOST_FLOW:
        *ret = sf32lb52_bt_synth_status_complete(priv, opcode);
        return true;

      default:
        return false;
    }
}

static struct sf32lb52_bt_priv_s g_sf32lb52_bt_priv =
{
  .drv =
    {
      .head_reserve = H4_HEADER_SIZE,
      .open         = sf32lb52_bt_open,
      .send         = sf32lb52_bt_send,
      .close        = sf32lb52_bt_close,
    },
};

static int sf32lb52_bt_recv_cb(uint8_t *data, uint16_t len)
{
  struct sf32lb52_bt_priv_s *priv = &g_sf32lb52_bt_priv;
  ssize_t packet_len;
  int ret;

  if (data == NULL || len == 0)
    {
      return -EINVAL;
    }

  if (priv->drop_rx_until_tx)
    {
      return OK;
    }

  if (priv->rxlen + len > sizeof(priv->rxbuf))
    {
      syslog(LOG_ERR,
             "sf32lb52 bth4 rx overflow: pending=%lu incoming=%u\n",
             (unsigned long)priv->rxlen,
             len);
      priv->rxlen = 0;
    }

  memcpy(&priv->rxbuf[priv->rxlen], data, len);
  priv->rxlen += len;

#if SF32LB52_BT_TRACE
  syslog(LOG_INFO,
         "sf32lb52 bth4 recv: len=%u pending=%lu first=%02x\n",
         len, (unsigned long)priv->rxlen, priv->rxbuf[0]);
#endif

  ret = OK;

  while (priv->rxlen > 0)
    {
      packet_len = sf32lb52_bt_h4_packet_len(priv->rxbuf, priv->rxlen);
      if (packet_len == 0)
        {
          break;
        }

      if (packet_len < 0)
        {
          syslog(LOG_WARNING,
                 "sf32lb52 bth4 drop invalid h4 type=%02x pending=%lu\n",
                 priv->rxbuf[0],
                 (unsigned long)priv->rxlen);
          memmove(priv->rxbuf, &priv->rxbuf[1], priv->rxlen - 1);
          priv->rxlen--;
          ret = -EINVAL;
          continue;
        }

      if ((size_t)packet_len > priv->rxlen)
        {
          break;
        }

      ret = sf32lb52_bt_forward_packet(priv, priv->rxbuf,
                   (size_t)packet_len);

      priv->rxlen -= (size_t)packet_len;
      if (priv->rxlen > 0)
        {
          memmove(priv->rxbuf,
                  &priv->rxbuf[packet_len],
                  priv->rxlen);
        }
    }

  return ret;
}

static int sf32lb52_bt_send(struct bt_driver_s *drv,
                            enum bt_buf_type_e type,
                            void *data, size_t len)
{
  struct sf32lb52_bt_priv_s *priv = &g_sf32lb52_bt_priv;
  uint8_t *hdr = (uint8_t *)data - drv->head_reserve;
  uint16_t opcode;
  int ret;

  switch (type)
    {
      case BT_CMD:
        *hdr = H4_CMD;
        break;
      case BT_ACL_OUT:
        *hdr = H4_ACL;
        break;
      case BT_ISO_OUT:
        *hdr = H4_ISO;
        break;
      default:
        syslog(LOG_ERR, "sf32lb52 bth4 send: bad type %u\n",
               (unsigned int)type);
        return -EINVAL;
    }

  if (type == BT_CMD && len >= sizeof(struct bt_hci_cmd_hdr_s))
    {
      opcode = sf32lb52_bt_get_le16(data);
      if (sf32lb52_bt_emulate_cmd(priv, opcode, &ret))
        {
          if (ret < 0)
            {
              syslog(LOG_ERR, "sf32lb52 bth4 send: emulate 0x%04x "
                     "failed: %d\n", opcode, ret);
              return ret;
            }

          return len;
        }

      ret = sf32lb52_bt_ensure_controller_enabled(opcode);
      if (ret < 0)
        {
          return ret;
        }
    }
  else
    {
      ret = sf32lb52_bt_ensure_controller_enabled(0);
      if (ret < 0)
        {
          return ret;
        }
    }

  priv->drop_rx_until_tx = false;

  if (SF32LB52_BT_TRACE && type == BT_ACL_OUT)
    {
      syslog(LOG_INFO,
             "sf32lb52 bth4 tx: type=%u len=%lu h4=%02x acl=%02x %02x %02x %02x\n",
             (unsigned int)type,
             (unsigned long)(len + drv->head_reserve),
             hdr[0],
             len >= 1 ? hdr[1] : 0,
             len >= 2 ? hdr[2] : 0,
             len >= 3 ? hdr[3] : 0,
             len >= 4 ? hdr[4] : 0);
    }

  ret = sf32lb52_host_send_packet(hdr, len + drv->head_reserve);
  if (ret < 0)
    {
      syslog(LOG_ERR, "sf32lb52 bth4 send: host_send_packet failed: %d\n",
             ret);
      return ret;
    }

  return len;
}

/* Full controller restart used by the Hardware-Error self-heal: power
 * the LCPU off, re-init the mailbox, re-register the RX callback
 * (controller_init clears the adapter env) and boot a fresh LCPU
 * (patch install + RF calibration + ring sync).  After this the
 * controller answers HCI again and the zblue host can tear itself
 * down cleanly.  Must run from a context where blocking is OK.
 */
int sf32lb52_bth4_controller_restart(void)
{
  int ret;

  /* Capture controller-side evidence BEFORE anything clears it: chip
   * revision (selects the LCPU patch path) and the LCPU assert record
   * (HAL_LCPU_ASSERT_INFO is cleared at every enable).  Implemented in
   * lcpu_boot.c - the only TU with bf0_hal access for these. */
  sf32lb52_lcpu_boot_dump_evidence();

  ret = sf32lb52_bt_controller_deinit();
  if (ret < 0 && ret != -EPERM)
    {
      syslog(LOG_ERR, "sf32lb52 bth4 restart: deinit failed: %d\n", ret);
      return ret;
    }

  ret = sf32lb52_bt_controller_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "sf32lb52 bth4 restart: init failed: %d\n", ret);
      return ret;
    }

  ret = sf32lb52_hci_register_callback(sf32lb52_bt_recv_cb);
  if (ret < 0)
    {
      syslog(LOG_ERR, "sf32lb52 bth4 restart: register cb failed: %d\n",
             ret);
      return ret;
    }

  return sf32lb52_bt_controller_enable();
}

static int sf32lb52_bt_ensure_controller_enabled(uint16_t opcode)
{
  int ret;

  ret = sf32lb52_bt_controller_enable();
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "sf32lb52 bth4 controller enable failed before opcode=0x%04x: %d\n",
             opcode,
             ret);
    }

  return ret;
}

static int sf32lb52_bt_open(struct bt_driver_s *drv)
{
  int ret;

  (void)drv;

  g_sf32lb52_bt_priv.rxlen = 0;
  g_sf32lb52_bt_priv.drop_rx_until_tx = true;

  ret = sf32lb52_bt_controller_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "sf32lb52 bth4: controller_init failed: %d\n", ret);
      return ret;
    }

  ret = sf32lb52_hci_register_callback(sf32lb52_bt_recv_cb);
  if (ret < 0)
    {
      syslog(LOG_ERR, "sf32lb52 bth4: register rx callback failed: %d\n",
             ret);
      return ret;
    }

  return OK;
}

static void sf32lb52_bt_close(struct bt_driver_s *drv)
{
  int ret;

  (void)drv;

  g_sf32lb52_bt_priv.rxlen = 0;

  /* Full deinit (not just disable) so that the next open re-initialises
   * the IPC queue from a clean state.  Without deinit, controller_init
   * is skipped on re-open (status != IDLE), and LCPU's ring-buffer
   * read pointer (reset on power-on) diverges from HCPU's stale write
   * pointer, causing all HCI commands to be silently dropped. */
  ret = sf32lb52_bt_controller_deinit();
  if (ret < 0)
    {
      wlerr("Failed to deinit HCI controller: %d\n", ret);
    }
}

int sf32lb52_bt_initialize(void)
{
  int ret;

  ret = uart_bth4_register("/dev/ttyHCI0", &g_sf32lb52_bt_priv.drv);
  if (ret < 0 && ret != -EEXIST)
    {
      wlerr("Failed to register /dev/ttyHCI0: %d\n", ret);
      return ret;
    }

  sf32lb52_bt_zblue_init_once();

  return OK;
}
