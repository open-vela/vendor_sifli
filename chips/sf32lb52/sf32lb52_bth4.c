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
#include <nuttx/irq.h>
#include <nuttx/serial/uart_bth4.h>
#include <nuttx/wireless/bluetooth/bt_driver.h>
#include <nuttx/wireless/bluetooth/bt_uart.h>

#include "sf32lb52_bt_adapter.h"

#define SF32LB52_BT_H4_RX_BUFSIZE 2048
#define SF32LB52_BT_TRACE         0

#ifndef SF32LB52_ACL_OBSERVER
#  define SF32LB52_ACL_OBSERVER 1
#endif

/* Set to 0 to compile out all Extended Advertising HCI diagnostics. */

#define OV_BLE_HCI_ADV_DIAG       1

#ifndef OV_BLE_HCI_LINK_EVENT_DIAG
#  define OV_BLE_HCI_LINK_EVENT_DIAG 0
#endif

#ifndef OV_BLE_HCI_LE_EVENT_MASK_PASSTHROUGH
#  define OV_BLE_HCI_LE_EVENT_MASK_PASSTHROUGH 0
#endif

#ifndef OV_BLE_HCI_LE_EVENT_MASK_AB_DIAG
#  define OV_BLE_HCI_LE_EVENT_MASK_AB_DIAG 0
#endif

#ifndef OV_BLE_HCI_ADV_LIFECYCLE_BTH4_DIAG
#  define OV_BLE_HCI_ADV_LIFECYCLE_BTH4_DIAG 0
#endif

#ifndef OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
#  define OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG 0
#endif

#define OV_BLE_HCI_EXT_ADV_DATA   0x2037
#define OV_BLE_HCI_EXT_SCAN_RSP   0x2038

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

#ifndef BT_HCI_OP_LE_SET_EXT_ADV_PARAM
#  define BT_HCI_OP_LE_SET_EXT_ADV_PARAM    BT_OP(BT_OGF_LE, 0x0036)
#endif

#ifndef BT_HCI_OP_LE_REMOVE_ADV_SET
#  define BT_HCI_OP_LE_REMOVE_ADV_SET       BT_OP(BT_OGF_LE, 0x003c)
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

struct sf32lb52_bt_acl_tx_record
{
  uint32_t seq;
    uint32_t t_ticks;
  uint16_t len;
  uint8_t truncated;
  uint8_t copied;
  int16_t result;
  uint8_t data[64];
};

struct sf32lb52_bt_priv_s
{
  struct bt_driver_s drv;
  uint8_t rxbuf[SF32LB52_BT_H4_RX_BUFSIZE];
  size_t rxlen;
  bool drop_rx_until_tx;
  /* Connection-independent ACL transport counters.  These are deliberately
   * not tied to an ATT PDU: the HCI driver has no safe cross-layer identity. */
  volatile uint32_t tx_acl_calls;
  volatile uint32_t tx_acl_errors;
  volatile uint32_t tx_acl_success;
  volatile int32_t tx_acl_last_error;
#if SF32LB52_ACL_OBSERVER
  uint32_t acl_tx_seq;
  uint32_t acl_tx_dropped;
  uint32_t acl_tx_early;
  uint32_t acl_tx_tail_count;
  uint32_t acl_tx_tail_next;
  struct sf32lb52_bt_acl_tx_record acl_tx_log[32];
#endif
#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
  uint32_t diag_complete_h4_count;
  uint32_t diag_forwarded_h4_count;
  uint32_t diag_sequence;
  uint16_t diag_last_opcode;
  uint8_t diag_last_h4_type;
  uint8_t diag_last_event;
#endif
};

#if SF32LB52_ACL_OBSERVER
static struct sf32lb52_bt_acl_tx_record g_acl_tx_dump[32];

static void sf32lb52_bt_acl_tx_log_dump(struct sf32lb52_bt_priv_s *priv)
{
  uint32_t i;
  uint32_t tail_start;

  irqstate_t flags = enter_critical_section();
  memcpy(g_acl_tx_dump, priv->acl_tx_log, sizeof(g_acl_tx_dump));
  uint32_t early = priv->acl_tx_early;
  uint32_t tail_count = priv->acl_tx_tail_count;
  uint32_t tail_next = priv->acl_tx_tail_next;
  uint32_t dropped = priv->acl_tx_dropped;
  leave_critical_section(flags);

  syslog(LOG_INFO, "A5_ACL_TX_DUMP count=%lu dropped=%lu\n",
         (unsigned long)(early + tail_count), (unsigned long)dropped);
  for (i = 0; i < early + tail_count; i++)
    {
      uint32_t slot;
      char hex[129];
      uint32_t j;

      if (i < early)
        {
          slot = i;
        }
      else
        {
          tail_start = tail_count == 28 ? tail_next : 0;
          slot = 4 + ((tail_start + i - early) % 28);
        }

      for (j = 0; j < g_acl_tx_dump[slot].copied; j++)
        {
          static const char digits[] = "0123456789abcdef";
          hex[j * 2] = digits[g_acl_tx_dump[slot].data[j] >> 4];
          hex[j * 2 + 1] = digits[g_acl_tx_dump[slot].data[j] & 0xf];
        }
      hex[g_acl_tx_dump[slot].copied * 2] = '\0';
      syslog(LOG_INFO, "A5_ACL_TX seq=%lu t_ticks=%lu len=%u copied=%u "
             "trunc=%u result=%d p=%s\n",
             (unsigned long)g_acl_tx_dump[slot].seq,
             (unsigned long)g_acl_tx_dump[slot].t_ticks,
             g_acl_tx_dump[slot].len, g_acl_tx_dump[slot].copied,
             g_acl_tx_dump[slot].truncated, g_acl_tx_dump[slot].result, hex);
    }
}
#endif /* SF32LB52_ACL_OBSERVER */

static int sf32lb52_bt_open(struct bt_driver_s *drv);
static int sf32lb52_bt_send(struct bt_driver_s *drv,
                            enum bt_buf_type_e type,
                            void *data, size_t len);
static void sf32lb52_bt_close(struct bt_driver_s *drv);
static int sf32lb52_bt_recv_cb(uint8_t *data, uint16_t len);
static int sf32lb52_bt_ensure_controller_enabled(uint16_t opcode);
static uint16_t sf32lb52_bt_get_le16(const uint8_t *data);

#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
static void sf32lb52_bt_snapshot_note_complete(
    struct sf32lb52_bt_priv_s *priv, const uint8_t *data, size_t len)
{
  irqstate_t flags;
  uint16_t opcode = 0;
  uint8_t event = 0;

  if (len >= 3 && data[0] == 0x04)
    {
      event = data[1];
      if (event == 0x0e && len >= 7)
        {
          opcode = sf32lb52_bt_get_le16(&data[4]);
        }
      else if (event == 0x0f && len >= 7)
        {
          opcode = sf32lb52_bt_get_le16(&data[5]);
        }
    }

  flags = enter_critical_section();
  priv->diag_complete_h4_count++;
  priv->diag_sequence++;
  priv->diag_last_h4_type = data[0];
  priv->diag_last_event = event;
  priv->diag_last_opcode = opcode;
  leave_critical_section(flags);
}
#endif

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

static bool sf32lb52_bt_adv_diag_opcode(uint16_t opcode)
{
  return opcode == OV_BLE_HCI_EXT_ADV_DATA ||
         opcode == OV_BLE_HCI_EXT_SCAN_RSP;
}

#if OV_BLE_HCI_ADV_DIAG
static void sf32lb52_bt_diag_complete_event(const uint8_t *data, size_t len)
{
  uint16_t opcode;
  uint8_t status;
  uint8_t ncmd;

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 4 ||
      data[0] != H4_EVT)
    {
      return;
    }

  if (data[1] == BT_HCI_EVT_CMD_COMPLETE && data[2] >= 4)
    {
      ncmd = data[3];
      opcode = sf32lb52_bt_get_le16(&data[4]);
      status = data[6];
    }
  else if (data[1] == BT_HCI_EVT_CMD_STATUS && data[2] >= 4)
    {
      status = data[3];
      ncmd = data[4];
      opcode = sf32lb52_bt_get_le16(&data[5]);
    }
  else
    {
      return;
    }

  if (sf32lb52_bt_adv_diag_opcode(opcode))
    {
      syslog(LOG_INFO,
             "ov_ble_hci_adv_diag bth4_rx event=0x%02x len=%lu opcode=0x%04x status=0x%02x ncmd=%u\n",
             data[1], (unsigned long)len, opcode, status, ncmd);
    }
}
#endif

#if OV_BLE_HCI_LE_EVENT_MASK_AB_DIAG
static void sf32lb52_bt_le_mask_diag_complete_event(const uint8_t *data,
                                                    size_t len)
{
  uint16_t opcode;
  uint8_t status;
  uint8_t ncmd;

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 4 ||
      data[0] != H4_EVT)
    {
      return;
    }

  if (data[1] == BT_HCI_EVT_CMD_COMPLETE && data[2] >= 4)
    {
      ncmd = data[3];
      opcode = sf32lb52_bt_get_le16(&data[4]);
      status = data[6];
    }
  else if (data[1] == BT_HCI_EVT_CMD_STATUS && data[2] >= 4)
    {
      status = data[3];
      ncmd = data[4];
      opcode = sf32lb52_bt_get_le16(&data[5]);
    }
  else
    {
      return;
    }

  if (opcode == BT_HCI_OP_LE_SET_EVENT_MASK)
    {
      syslog(LOG_INFO,
             "ov_ble_hci_le_mask_ab_diag event=0x%02x opcode=0x%04x status=0x%02x ncmd=%u\n",
             data[1], opcode, status, ncmd);
    }
}
#endif

#if OV_BLE_HCI_ADV_LIFECYCLE_BTH4_DIAG
static bool sf32lb52_bt_adv_lifecycle_diag_opcode(uint16_t opcode)
{
  return opcode == BT_HCI_OP_LE_SET_EXT_ADV_PARAM ||
         opcode == BT_HCI_OP_LE_REMOVE_ADV_SET;
}

static void sf32lb52_bt_adv_lifecycle_diag_complete_event(
    const uint8_t *data, size_t len)
{
  uint16_t opcode;
  uint8_t status;
  uint8_t ncmd;

  if (len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 4 ||
      data[0] != H4_EVT)
    {
      return;
    }

  if (data[1] == BT_HCI_EVT_CMD_COMPLETE && data[2] >= 4)
    {
      ncmd = data[3];
      opcode = sf32lb52_bt_get_le16(&data[4]);
      status = data[6];
    }
  else if (data[1] == BT_HCI_EVT_CMD_STATUS && data[2] >= 4)
    {
      status = data[3];
      ncmd = data[4];
      opcode = sf32lb52_bt_get_le16(&data[5]);
    }
  else
    {
      return;
    }

  if (sf32lb52_bt_adv_lifecycle_diag_opcode(opcode))
    {
      syslog(LOG_INFO,
             "ov_ble_hci_adv_lifecycle_diag bth4_rx event=0x%02x opcode=0x%04x status=0x%02x ncmd=%u frame_len=%lu\n",
             data[1], opcode, status, ncmd, (unsigned long)len);
    }
}
#endif

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

#if OV_BLE_HCI_LINK_EVENT_DIAG || OV_BLE_HCI_LE_EVENT_MASK_AB_DIAG
#if OV_BLE_HCI_LINK_EVENT_DIAG
  if (len == 7 && data[0] == H4_EVT && data[1] == 0x05 &&
      data[2] == 0x04)
    {
      syslog(LOG_INFO,
             "ov_ble_hci_link_event_diag event=disconnect_complete status=0x%02x conn_handle=0x%04x reason=0x%02x frame_len=%lu\n",
             data[3], sf32lb52_bt_get_le16(&data[4]), data[6],
             (unsigned long)len);
    }
  else
#endif
  if (len == 9 && data[0] == H4_EVT && data[1] == 0x3e &&
      data[2] == 0x06 && data[3] == 0x12)
    {
#if OV_BLE_HCI_LINK_EVENT_DIAG
      syslog(LOG_INFO,
             "ov_ble_hci_link_event_diag event=adv_set_terminated status=0x%02x adv_handle=%u conn_handle=0x%04x count=%u frame_len=%lu\n",
             data[4], data[5], sf32lb52_bt_get_le16(&data[6]), data[8],
             (unsigned long)len);
#endif
#if OV_BLE_HCI_LE_EVENT_MASK_AB_DIAG
      syslog(LOG_INFO,
             "ov_ble_hci_le_mask_ab_diag event=adv_set_terminated status=0x%02x adv_handle=%u conn_handle=0x%04x count=%u frame_len=%lu\n",
             data[4], data[5], sf32lb52_bt_get_le16(&data[6]), data[8],
             (unsigned long)len);
#endif
    }
#endif

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
#if OV_BLE_HCI_LE_EVENT_MASK_PASSTHROUGH
        return false;
#endif
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

void sf32lb52_bt_diag_dump_acl_tx(void)
{
  struct sf32lb52_bt_priv_s *priv = &g_sf32lb52_bt_priv;
  irqstate_t flags = enter_critical_section();
  uint32_t calls = priv->tx_acl_calls;
  uint32_t errors = priv->tx_acl_errors;
  uint32_t success = priv->tx_acl_success;
  int32_t last = priv->tx_acl_last_error;
  leave_critical_section(flags);
  syslog(LOG_INFO,
         "sf32lb52_acl_tx_summary scope=transport calls=%lu success=%lu errors=%lu last_error=%ld\n",
         (unsigned long)calls, (unsigned long)success,
         (unsigned long)errors, (long)last);
}

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

#if SF32LB52_ACL_OBSERVER
      if (packet_len == 7 && priv->rxbuf[0] == H4_EVT &&
          priv->rxbuf[1] == 0x05 && priv->rxbuf[2] == 0x04)
        {
          sf32lb52_bt_acl_tx_log_dump(priv);
        }
#endif

#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
      sf32lb52_bt_snapshot_note_complete(priv, priv->rxbuf,
                                         (size_t)packet_len);
#endif

#if OV_BLE_HCI_ADV_DIAG
      sf32lb52_bt_diag_complete_event(priv->rxbuf, (size_t)packet_len);
#endif
#if OV_BLE_HCI_LE_EVENT_MASK_AB_DIAG
      sf32lb52_bt_le_mask_diag_complete_event(priv->rxbuf,
                                              (size_t)packet_len);
#endif
#if OV_BLE_HCI_ADV_LIFECYCLE_BTH4_DIAG
      sf32lb52_bt_adv_lifecycle_diag_complete_event(
          priv->rxbuf, (size_t)packet_len);
#endif

      ret = sf32lb52_bt_forward_packet(priv, priv->rxbuf,
                   (size_t)packet_len);

#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
      {
        irqstate_t flags = enter_critical_section();
        priv->diag_forwarded_h4_count++;
        priv->diag_sequence++;
        leave_critical_section(flags);
      }
#endif

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

int bt_hci_rx_ring_snapshot(struct bt_hci_rx_ring_snapshot_s *snapshot)
{
#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
  struct sf32lb52_bt_priv_s *priv = &g_sf32lb52_bt_priv;
  irqstate_t flags;
  int ret;

  ret = sf32lb52_bt_rx_ring_snapshot(snapshot);
  if (ret < 0)
    {
      return ret;
    }

  flags = enter_critical_section();
  snapshot->complete_h4_count = priv->diag_complete_h4_count;
  snapshot->forwarded_h4_count = priv->diag_forwarded_h4_count;
  snapshot->sequence += priv->diag_sequence;
  snapshot->last_opcode = priv->diag_last_opcode;
  snapshot->last_h4_type = priv->diag_last_h4_type;
  snapshot->last_event = priv->diag_last_event;
  leave_critical_section(flags);
  return OK;
#else
  return -ENOSYS;
#endif
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
        return -EINVAL;
    }

  if (type == BT_CMD && len >= sizeof(struct bt_hci_cmd_hdr_s))
    {
      opcode = sf32lb52_bt_get_le16(data);
#if OV_BLE_HCI_LE_EVENT_MASK_AB_DIAG
      const uint8_t *cmd = data;

      if (opcode == BT_HCI_OP_LE_SET_EVENT_MASK &&
          len == sizeof(struct bt_hci_cmd_hdr_s) + 8 && cmd[2] == 8)
        {
          syslog(LOG_INFO,
                 "ov_ble_hci_le_mask_ab_diag tx opcode=0x%04x mask=%02x%02x%02x%02x%02x%02x%02x%02x bit17=%u mode=%s\n",
                 opcode, cmd[10], cmd[9], cmd[8], cmd[7], cmd[6],
                 cmd[5], cmd[4], cmd[3], (cmd[5] >> 1) & 1,
#if OV_BLE_HCI_LE_EVENT_MASK_PASSTHROUGH
                 "passthrough");
#else
                 "emulated");
#endif
        }
#endif
      if (sf32lb52_bt_emulate_cmd(priv, opcode, &ret))
        {
          return ret < 0 ? ret : len;
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

  if (type == BT_ACL_OUT)
    {
      irqstate_t flags = enter_critical_section();
      priv->tx_acl_calls++;
      leave_critical_section(flags);
    }

#if SF32LB52_ACL_OBSERVER
  if (type == BT_ACL_OUT)
    {
      uint32_t seq = priv->acl_tx_seq++;
      uint32_t slot;
      uint32_t early = priv->acl_tx_early;

      if (early < 4)
        {
          slot = early;
          priv->acl_tx_early = early + 1;
        }
      else
        {
          slot = 4 + priv->acl_tx_tail_next;
          priv->acl_tx_tail_next = (priv->acl_tx_tail_next + 1) % 28;
          if (priv->acl_tx_tail_count < 28)
            priv->acl_tx_tail_count++;
          else
            priv->acl_tx_dropped++;
        }

      priv->acl_tx_log[slot].seq = seq;
      priv->acl_tx_log[slot].t_ticks = clock_systime_ticks();
      priv->acl_tx_log[slot].len = len + drv->head_reserve;
      priv->acl_tx_log[slot].copied =
        (len + drv->head_reserve < sizeof(priv->acl_tx_log[slot].data)) ?
        (len + drv->head_reserve) : sizeof(priv->acl_tx_log[slot].data);
      priv->acl_tx_log[slot].truncated =
        (len + drv->head_reserve) > sizeof(priv->acl_tx_log[slot].data);
      priv->acl_tx_log[slot].result = 0;
      memcpy(priv->acl_tx_log[slot].data, hdr,
             priv->acl_tx_log[slot].copied);
    }
#endif

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
  if (type == BT_ACL_OUT)
    {
      irqstate_t flags = enter_critical_section();
      if (ret < 0)
        {
          priv->tx_acl_errors++;
          priv->tx_acl_last_error = ret;
        }
      else
        {
          priv->tx_acl_success++;
        }
      leave_critical_section(flags);
    }
#if SF32LB52_ACL_OBSERVER
  if (type == BT_ACL_OUT)
    {
      uint32_t slot = priv->acl_tx_early < 4 ? priv->acl_tx_early - 1 :
        4 + ((priv->acl_tx_tail_next + 27) % 28);
      priv->acl_tx_log[slot].result = ret;
    }
#endif
#if OV_BLE_HCI_ADV_LIFECYCLE_BTH4_DIAG
  if (type == BT_CMD && sf32lb52_bt_adv_lifecycle_diag_opcode(opcode))
    {
      const uint8_t *cmd = data;

      if (len >= sizeof(struct bt_hci_cmd_hdr_s) &&
          len == sizeof(struct bt_hci_cmd_hdr_s) + cmd[2])
        {
          syslog(LOG_INFO,
                 "ov_ble_hci_adv_lifecycle_diag bth4_tx opcode=0x%04x h4_len=%lu result=%d\n",
                 opcode, (unsigned long)(len + drv->head_reserve), ret);
        }
    }
#endif
#if OV_BLE_HCI_ADV_DIAG
  if (type == BT_CMD && sf32lb52_bt_adv_diag_opcode(opcode))
    {
      syslog(LOG_INFO,
             "ov_ble_hci_adv_diag bth4_tx opcode=0x%04x h4_len=%lu result=%d\n",
             opcode, (unsigned long)(len + drv->head_reserve), ret);
    }
#endif
  if (ret < 0)
    {
      return ret;
    }

  return len;
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
      return ret;
    }

  ret = sf32lb52_hci_register_callback(sf32lb52_bt_recv_cb);
  if (ret < 0)
    {
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
