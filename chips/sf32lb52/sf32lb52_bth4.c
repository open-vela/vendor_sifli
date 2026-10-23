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
#ifndef BT_HCI_OP_DELETE_STORED_LINK_KEY
#  define BT_HCI_OP_DELETE_STORED_LINK_KEY   BT_OP(BT_OGF_BASEBAND, 0x0012)
#endif
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
  /* Opcodes of commands injected by this driver (EIR, SSP replies).
   * Their Command Complete events must be swallowed before zblue sees
   * them: zblue never sent those commands, so hci_cmd_done() hits the
   * opcode-mismatch path, drops the frame and never gives ncmd back,
   * and every later send_sync times out and asserts. */
  uint16_t pending_extra_ops[8];
  int pending_extra_count;
  size_t rxlen;
  bool drop_rx_until_tx;
};
static int sf32lb52_bt_open(struct bt_driver_s *drv);
static int sf32lb52_bt_send(struct bt_driver_s *drv,
                            enum bt_buf_type_e type,
                            void *data, size_t len);
static void sf32lb52_bt_close(struct bt_driver_s *drv);
/* ═══════════════════════════════════════════════════
 * EIR state — device name broadcast via HCI 0x0c52.
 *
 * WHY: LCPU rejects WRITE_LOCAL_NAME (0x0c13) and zblue never
 * sends WRITE_EXTENDED_INQUIRY_RESPONSE (0x0c52), so phones see
 * only a MAC address. We inject EIR ourselves from the RX path.
 *
 * s_eir_dirty  — set to 1 when name changes; consumed when EIR is sent.
 * s_eir_retry  — LCPU may reject EIR; retry up to 3 times per name.
 * pending_extra — opcodes of driver-injected commands whose CC/CS
 *                  must be swallowed before zblue sees them.
 * ═══════════════════════════════════════════════════ */
static char s_eir_name[249] = "XIAOMI VELA";
static volatile int s_eir_dirty = 1;
static int s_eir_retry = 0;
static volatile int s_in_send = 0;  /* reentrancy guard for send_eir */
/* Forward declarations for ncmd tracking (used by SSP handler) */
static void sf32lb52_bt_track_injected(struct sf32lb52_bt_priv_s *priv,
                                        uint16_t opcode);
/* SSP auto-reply in bth4: handle SSP handshake entirely in the H4 driver.
 * zblue's SSP handling calls bt_hci_cmd_send_sync from the event handler
 * context which triggers a HardFault. Reply to SSP events here and swallow
 * them before zblue sees them. Link Key Notification (0x18) and Simple
 * Pairing Complete (0x36) still pass through to zblue for bond storage. */
static uint16_t sf32lb52_bt_ssp_handler(struct sf32lb52_bt_priv_s *priv,
                                         uint8_t *d, uint16_t len)
{
  size_t off = 0;
  while (off + 3 <= (size_t)len)
    {
      if (d[off] != H4_EVT) break;
      uint8_t code = d[off + 1];
      uint8_t plen = d[off + 2];
      if (off + 3 + plen > (size_t)len) break;
      if (code == 0x31 && plen >= 6)  /* IO Capability Request */
        {
          syslog(LOG_INFO, "bth4: SSP IO_CAP_REQ, reply DisplayYesNo\n");
          uint8_t cmd[13];
          cmd[0] = H4_CMD;
          cmd[1] = 0x2b; cmd[2] = 0x04;  /* IO_Capability_Reply */
          cmd[3] = 9;
          memcpy(&cmd[4], &d[off+3], 6);  /* BD_ADDR */
          cmd[10] = 0x01;  /* DisplayYesNo */
          cmd[11] = 0x00;  /* no OOB */
          cmd[12] = 0x04;  /* General Bonding, no MITM */
          sf32lb52_bt_track_injected(priv, 0x042b);
          sf32lb52_bt_send(&priv->drv, BT_CMD, &cmd[1], 12);
          goto skip;
        }
      if (code == 0x33 && plen >= 10)  /* User Confirmation Request */
        {
          syslog(LOG_INFO, "bth4: SSP USER_CONFIRM, auto-accept\n");
          uint8_t cmd[10];
          cmd[0] = H4_CMD;
          cmd[1] = 0x2c; cmd[2] = 0x04;  /* User_Confirm_Reply */
          cmd[3] = 6;
          memcpy(&cmd[4], &d[off+3], 6);
          sf32lb52_bt_track_injected(priv, 0x042c);
          sf32lb52_bt_send(&priv->drv, BT_CMD, &cmd[1], 9);
          goto skip;
        }
      if (code == 0x17 && plen >= 6)  /* Link Key Request */
        {
          syslog(LOG_INFO, "bth4: SSP LINK_KEY_REQ, negative reply (no key)\n");
          uint8_t cmd[10];
          cmd[0] = H4_CMD;
          cmd[1] = 0x0c; cmd[2] = 0x04;  /* Link_Key_Request_Negative_Reply */
          cmd[3] = 6;
          memcpy(&cmd[4], &d[off+3], 6);
          sf32lb52_bt_track_injected(priv, 0x040c);
          sf32lb52_bt_send(&priv->drv, BT_CMD, &cmd[1], 9);
          goto skip;
        }
      off += 3 + plen;
      continue;
    skip:
      memmove(&d[off], &d[off+3+plen], len-off-3-plen);
      len -= 3+plen;
    }
  return len;
}
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
/* ── ncmd tracking for driver-injected commands ── */
static void sf32lb52_bt_track_injected(struct sf32lb52_bt_priv_s *priv,
                                        uint16_t opcode)
{
  if (priv->pending_extra_count < (int)(sizeof(priv->pending_extra_ops) / sizeof(priv->pending_extra_ops[0])))
    {
      priv->pending_extra_ops[priv->pending_extra_count++] = opcode;
    }
}
/* Swallow Command Complete events for driver-injected commands.
 * Must run before bt_netdev_receive() so zblue never sees them. */
/* s_eir_* defined at file top */
static void sf32lb52_bt_swallow_completes(struct sf32lb52_bt_priv_s *priv,
                                           uint8_t *d, uint16_t *len_ptr)
{
  uint16_t len = *len_ptr;
  size_t off = 0;
  while (off + 3 <= (size_t)len)
    {
      if (d[off] == H4_EVT && (d[off + 1] == 0x0E || d[off + 1] == 0x0F)
          && off + 7 <= (size_t)len)
        {
          /* CC(0x0E): [num_pkt][op_lo][op_hi][status] -> opcode at +4..+5
           * CS(0x0F): [status][num_pkt][op_lo][op_hi] -> opcode at +5..+6 */
          uint16_t cc_op = (d[off + 1] == 0x0E)
              ? ((uint16_t)d[off + 4] | ((uint16_t)d[off + 5] << 8))
              : ((uint16_t)d[off + 5] | ((uint16_t)d[off + 6] << 8));
          bool found = false;
          for (int i = 0; i < priv->pending_extra_count; i++)
            {
              if (priv->pending_extra_ops[i] == cc_op)
                {
                  priv->pending_extra_ops[i] =
                      priv->pending_extra_ops[--priv->pending_extra_count];
                  found = true;
                  break;
                }
            }
          if (found)
            {
              uint16_t evt_len = 3 + d[off + 2];
              syslog(LOG_INFO, "bth4: swallow %s 0x%04x\n",
                     d[off + 1] == 0x0E ? "CC" : "CS", cc_op);
              if (cc_op == 0x0c52 && d[off + 1] == 0x0E) s_eir_retry = 0;
              memmove(&d[off], &d[off + evt_len], len - off - evt_len);
              len -= evt_len;
              continue;
            }
          /* Safety net: if we have pending injected commands and this is
           * a CS with error status, swallow it too (opcode match may fail
           * due to LCPU's non-standard CS byte layout). */
          if (!found && d[off + 1] == 0x0F && priv->pending_extra_count > 0
              && d[off + 3] != 0x00)
            {
              uint16_t evt_len = 3 + d[off + 2];
              syslog(LOG_INFO, "bth4: swallow orphan CS status=0x%02x\n", d[off + 3]);
              priv->pending_extra_count--;
              s_eir_retry++;
          if (s_eir_retry < 3) s_eir_dirty = 1;  /* EIR rejected — retry up to 3x */
          else syslog(LOG_INFO, "bth4: EIR retry exhausted\n");
              memmove(&d[off], &d[off + evt_len], len - off - evt_len);
              len -= evt_len;
              continue;
            }
          off += 3 + d[off + 2];
        }
      else if (d[off] == H4_EVT && d[off + 1] == 0x10)
        {
          /* Filter Hardware Error events: LCPU sends these after rejected
           * commands (e.g. EIR). zblue treats them as fatal and resets
           * the device, killing all subsequent commands. */
          uint16_t evt_len = 3 + d[off + 2];
          syslog(LOG_INFO, "bth4: filter HW error evt\n");
          memmove(&d[off], &d[off + evt_len], len - off - evt_len);
          len -= evt_len;
          continue;
        }
      else if (d[off] == H4_EVT)
        {
          if (off + 3 > (size_t)len) break;
          off += 3 + d[off + 2];
        }
      else if (d[off] == H4_ACL)
        {
          if (off + 5 > (size_t)len) break;
          off += 5 + ((uint16_t)d[off + 3] | ((uint16_t)d[off + 4] << 8));
        }
      else break;
    }
  *len_ptr = len;
}
/* ── EIR injection ── */
/* send EIR on first opportunity */
/* Exported for sal_adapter_interface.c to update the broadcast name */
void bth4_set_eir_name(const char *name)
{
  int i = 0;
  while (i < 248 && name[i]) { s_eir_name[i] = name[i]; i++; }
  s_eir_name[i] = '\0';
  s_eir_dirty = 1;
}
static void sf32lb52_bt_send_eir(struct sf32lb52_bt_priv_s *priv,
                                  const char *name)
{
  /* Skip if already sent for this name (avoid duplicate injection).
   * Use manual loop to avoid strcmp/strncpy (string.h may not be included). */
  static char last_sent[249] = "";
  {
    const char *a = last_sent;
    const char *b = name;
    int same = 1;
    while (*a || *b) { if (*a != *b) { same = 0; break; } a++; b++; }
    if (same) return;
    int ii = 0;
    while (ii < 248 && name[ii]) { last_sent[ii] = name[ii]; ii++; }
    last_sent[ii] = '\0';
  }
  static uint8_t frame[H4_HEADER_SIZE + 3 + 241];
  uint8_t *ec = &frame[H4_HEADER_SIZE];
  int nlen = 0;
  while (nlen < 238 && name[nlen]) nlen++;
  memset(frame, 0, sizeof(frame));
  ec[0] = 0x52; ec[1] = 0x0c;   /* opcode 0x0c52 LE */
  ec[2] = 241;                    /* parameter_total_length */
  ec[3] = 0x00;                   /* FEC not required */
  if (nlen > 0)
    {
      ec[4] = (uint8_t)(nlen + 1);   /* EIR field length */
      ec[5] = 0x09;                   /* Complete Local Name */
      memcpy(&ec[6], name, nlen);
    }
  /* NOTE: do NOT add Service UUID EIR fields — SiFli LCPU chokes on
   * EIR field type 0x03 and stops advertising entirely. PANU service
   * advertisement belongs in SDP records, not EIR. */
  sf32lb52_bt_track_injected(priv, 0x0c52);
  syslog(LOG_INFO, "bth4: send EIR name=%s len=%d\n", name, nlen);
  sf32lb52_bt_send(&priv->drv, BT_CMD, ec, 3 + 241);
  /* s_eir_retry reset happens on CC success in swallow_completes */
}
static int sf32lb52_bt_forward_packet(struct sf32lb52_bt_priv_s *priv,
                                      uint8_t *data, size_t len)
{
  syslog(LOG_INFO, "bth4: fp len=%d b0=%02x\n", (int)len, len > 0 ? ((uint8_t*)data)[0] : 0);
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
  /* Normalize LCPU Encryption Change (HCI EVT 0x08): the LCPU prepends
   * one extra parameter byte which shifts Status right by one. zblue then
   * read 0x01 as status and aborted the security procedure (pairing failed
   * with err: 1). Strip the extra byte so the event matches the BT spec:
   * [status][handle:2][enc_enabled]. */
  {
    uint8_t *d = (uint8_t *)data;
    size_t off = 0;
    while (off + 3 <= len)
      {
        if (d[off] == H4_EVT)
          {
            uint8_t code = d[off + 1];
            uint8_t plen = d[off + 2];
            if (off + 3 + plen > len) break;
            if (code == 0x08 && plen == 5)
              {
                syslog(LOG_INFO, "bth4: enc-change raw %02x %02x %02x %02x %02x\n",
                       d[off+3], d[off+4], d[off+5], d[off+6], d[off+7]);
                memmove(&d[off + 3], &d[off + 4], 4);
                d[off + 2] = 4;
                if (len > off + 8)
                  memmove(&d[off + 7], &d[off + 8], len - off - 8);
                len -= 1;
                continue;
              }
            off += 3 + plen;
          }
        else if (d[off] == H4_ACL)
          {
            if (off + 5 > len) break;
            off += 5 + ((uint16_t)d[off + 3] | ((uint16_t)d[off + 4] << 8));
          }
        else
          break;
      }
  }
    /* SSP diagnostic: log all HCI events from LCPU */
  {
    uint8_t *dd = (uint8_t *)data;
    size_t oo = 0;
    while (oo + 3 <= (size_t)len)
      {
        if (dd[oo] == H4_EVT)
          {
            uint8_t evt_code = dd[oo+1];
            uint8_t evt_plen = dd[oo+2];
            /* Only log SSP/auth/connection events (avoid BNEP data spam) */
            if (evt_code != 0x13)  /* skip only Number Of Completed Packets */
              {
                syslog(LOG_INFO, "bth4: rx evt 0x%02x plen=%d b=%02x %02x %02x %02x\n",
                       evt_code, evt_plen,
                       oo+3<len?dd[oo+3]:0, oo+4<len?dd[oo+4]:0,
                       oo+5<len?dd[oo+5]:0, oo+6<len?dd[oo+6]:0);
              }
            oo += 3 + evt_plen;
          }
        else if (dd[oo] == H4_ACL)
          {
            if (oo + 5 > (size_t)len) break;
            oo += 5 + ((uint16_t)dd[oo+3] | ((uint16_t)dd[oo+4] << 8));
          }
        else break;
      }
  }
  /* SSP auto-reply: handle and remove SSP events before zblue sees them */
  len = sf32lb52_bt_ssp_handler(priv, (uint8_t *)data, len);
  if (len == 0) return 0;
  /* Swallow Command Complete events for driver-injected commands
   * (EIR, SSP replies) before zblue sees them. */
  /* Diagnostic: log Create Connection (0x0405) status responses */
  {
    uint8_t *dc = (uint8_t *)data;
    size_t oc = 0;
    while (oc + 7 <= (size_t)len)
      {
        if (dc[oc] == H4_EVT && dc[oc + 1] == 0x0F &&
            dc[oc + 5] == 0x05 && dc[oc + 6] == 0x04)
          {
            syslog(LOG_INFO, "bth4: conn-create status=0x%02x num=%d\n",
                   dc[oc + 3], dc[oc + 4]);
          }
        if (dc[oc] == H4_EVT) { if (oc + 3 > (size_t)len) break; oc += 3 + dc[oc + 2]; }
        else if (dc[oc] == H4_ACL) { if (oc + 5 > (size_t)len) break; oc += 5 + (dc[oc + 3] | (dc[oc + 4] << 8)); }
        else break;
      }
  }
  sf32lb52_bt_swallow_completes(priv, (uint8_t *)data, &len);
  if (len == 0) return 0;
  /* Send EIR when dirty flag is set (name updated or first boot).
   * Called from RX path, NOT from send (avoids recursive send). */
  if (s_eir_dirty && !s_in_send)
    {
      s_eir_dirty = 0;
      sf32lb52_bt_send_eir(priv, s_eir_name);
    }
  ret = bt_netdev_receive(&priv->drv,
                          type,
                          (void *)&data[H4_HEADER_SIZE],
                          len - H4_HEADER_SIZE);
  syslog(LOG_INFO, "bth4: netdev ret=%d\n", ret);
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
  /* Hard intercept: these opcodes MUST reach LCPU for the phone to see
   * our device name and EIR data. Never emulate them. */
  switch (opcode)
    {
      /* WRITE_LOCAL_NAME is emulated below, not forwarded */
      case BT_HCI_OP_SET_EVENT_MASK:              /* 0x0c01 */
      case BT_HCI_OP_SET_EVENT_MASK_PAGE_2:       /* 0x0c63 */
      case BT_HCI_OP_WRITE_SSP_MODE:              /* 0x0c56 */
      case BT_HCI_OP_WRITE_EXTENDED_INQUIRY_RESPONSE: /* 0x0c52 */
      case BT_HCI_OP_WRITE_INQUIRY_MODE:         /* 0x0c45 */
      case BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY:/* 0x0c1e */
      case BT_HCI_OP_WRITE_PAGE_SCAN_ACTIVITY:   /* 0x0c1c */
      case BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE:    /* 0x0c43 */
      case BT_HCI_OP_WRITE_PAGE_SCAN_TYPE:       /* 0x0c47 */
        return false;  /* forward to LCPU */
      default:
        break;
    }
  switch (opcode)
    {
      case BT_HCI_OP_DELETE_STORED_LINK_KEY:
        /* LCPU never answers 0x0c12. zblue sends it inside createbond and
         * the blocking send_sync times out and asserts. Report "0 deleted". */
        memset(params, 0, sizeof(params));
        params[0] = SF32LB52_HCI_STATUS_SUCCESS;
        *ret = sf32lb52_bt_synth_cmd_complete(priv, opcode, params, 3);
        return true;
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
      /* SET_EVENT_MASK forwarded to LCPU so SSP events (0x31/0x33/0x36) are enabled */
      case BT_HCI_OP_WRITE_LOCAL_NAME:
      /* case BT_HCI_OP_WRITE_SCAN_ENABLE: - forward to LCPU */
      case BT_HCI_OP_WRITE_PAGE_SCAN_ACTIVITY:
      case BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY:
      case BT_HCI_OP_WRITE_PAGE_TIMEOUT:
      /* case BT_HCI_OP_WRITE_CLASS_OF_DEVICE: - forward to LCPU */
      case BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE:
      case BT_HCI_OP_WRITE_EXTENDED_INQUIRY_RESPONSE:
      case BT_HCI_OP_WRITE_INQUIRY_MODE:
      case BT_HCI_OP_WRITE_PAGE_SCAN_TYPE:
      /* case BT_HCI_OP_WRITE_SSP_MODE: - forward to LCPU */
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
  printf("bth4: recv_cb len=%d b0=%02x\n", (int)len, len > 0 ? data[0] : 0);
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
  s_in_send = 1;
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
      /* Save name for EIR BEFORE the emulate check (which may early-return).
       * Do NOT set s_eir_dirty here — only bth4_set_eir_name() (called from
       * set_name after init) triggers EIR injection. Sending EIR during
       * bt_init causes a race with other init commands. */
      if (opcode == BT_HCI_OP_WRITE_LOCAL_NAME && len >= 4)
        {
          memcpy(s_eir_name, (const char *)data + 3, 248);
          s_eir_name[248] = '\0';
          syslog(LOG_INFO, "bth4: save name for EIR: %s (no send)\n", s_eir_name);
        }
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
  if (type == BT_CMD && len >= sizeof(struct bt_hci_cmd_hdr_s))
    {
      syslog(LOG_INFO, "bth4: fwd cmd 0x%04x\n", sf32lb52_bt_get_le16(data));
    }
  else if (type == BT_ACL_OUT)
    {
      syslog(LOG_INFO, "bth4: fwd acl len=%d h=0x%04x\n", (int)len, len >= 3 ? ((uint8_t*)data)[1] | (((uint8_t*)data)[2] << 8) : 0);
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