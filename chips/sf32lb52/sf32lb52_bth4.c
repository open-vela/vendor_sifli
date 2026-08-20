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

/* Per-chunk HCI tracing. It was pinned on through the whole bring-up, but it
 * emits a syslog line for every chunk the LCPU hands up, which on the BNEP
 * data path means one console write per ACL packet. Debug builds only. */
#ifdef CONFIG_SF32LB52_BT_TRACE
#  define SF32LB52_BT_TRACE 1
#else
#  define SF32LB52_BT_TRACE 0
#endif

/* Full-length ACL tracing for Gate B (docs_ble/tools/gate_b.sh). Emitted in
 * 32-byte chunks so the hex buffer stays off the 1200-byte BT RX stack.
 * Debug-only: the product defconfig leaves this off. */
#ifdef CONFIG_SF32LB52_BT_TRACE_ACL_FULL
#  define SF32LB52_BT_TRACE_ACL_FULL 1
#else
#  define SF32LB52_BT_TRACE_ACL_FULL 0
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

/* The LCPU never answers Delete Stored Link Key (0x0c12); zblue calls it
 * from bt_conn_pair_br_mc on every createbond and its blocking send_sync
 * asserts on the timeout (Kernel oops, bluetoothd dies). The controller
 * keeps no persistent store anyway, so report "0 keys deleted". */
#ifndef BT_HCI_OP_DELETE_STORED_LINK_KEY
#  define BT_HCI_OP_DELETE_STORED_LINK_KEY   BT_OP(BT_OGF_BASEBAND, 0x0012)
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

/* R104: SSP auto-reply opcodes — handle SSP entirely in bth4 to avoid
 * zblue sysworkq HardFault when bt_hci_cmd_send_sync is called from
 * the SSP event handler context. Mimics xiaozhi BTS2 approach where
 * the stack handles SSP internally without app involvement. */
#ifndef BT_HCI_OP_IO_CAPABILITY_REPLY
#  define BT_HCI_OP_IO_CAPABILITY_REPLY     BT_OP(BT_OGF_LINK_CTRL, 0x002b)
#endif
#ifndef BT_HCI_OP_USER_CONFIRM_REPLY
#  define BT_HCI_OP_USER_CONFIRM_REPLY      BT_OP(BT_OGF_LINK_CTRL, 0x002c)
#endif
#ifndef BT_HCI_OP_LINK_KEY_REQ_REPLY
#  define BT_HCI_OP_LINK_KEY_REQ_REPLY      BT_OP(BT_OGF_LINK_CTRL, 0x000b)
#endif
#ifndef BT_HCI_OP_LINK_KEY_REQ_NEG_REPLY
#  define BT_HCI_OP_LINK_KEY_REQ_NEG_REPLY  BT_OP(BT_OGF_LINK_CTRL, 0x000c)
#endif
#ifndef BT_HCI_OP_AUTH_REQUESTED
#  define BT_HCI_OP_AUTH_REQUESTED          BT_OP(BT_OGF_LINK_CTRL, 0x0011)
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

#define SF32LB52_BT_CONN_MAX 4

struct sf32lb52_bt_conn_s
{
  uint16_t handle;
  uint8_t addr[6];
};

struct sf32lb52_bt_priv_s
{
  struct bt_driver_s drv;
  uint8_t rxbuf[SF32LB52_BT_H4_RX_BUFSIZE];
  size_t rxlen;
  bool drop_rx_until_tx;
  /* Opcodes of commands this driver injected itself (scan activity,
   * local name, inquiry probe). Their Command Complete events must be
   * swallowed before zblue sees them: zblue never sent those commands,
   * so hci_cmd_done() hits the opcode-mismatch path, drops the frame
   * and never gives ncmd back -> every later send_sync (e.g. the
   * Delete Stored Link Key inside createbond) times out and asserts. */
  uint16_t pending_extra_ops[8];
  int pending_extra_count;
  struct sf32lb52_bt_conn_s conns[SF32LB52_BT_CONN_MAX];
};

static int sf32lb52_bt_open(struct bt_driver_s *drv);
static int sf32lb52_bt_send(struct bt_driver_s *drv,
                            enum bt_buf_type_e type,
                            void *data, size_t len);
static int sf32lb52_bt_synth_status_complete(struct sf32lb52_bt_priv_s *priv,
                                             uint16_t opcode);
static void sf32lb52_bt_close(struct bt_driver_s *drv);
static int sf32lb52_bt_recv_cb(uint8_t *data, uint16_t len);
static int sf32lb52_bt_ensure_controller_enabled(uint16_t opcode);

/* zblue's SYS_INIT table (z_sys_init) is deliberately NOT run from here.
 *
 * Its entries start zblue's work-queue threads (sysworkq, "BT LW WQ"), and
 * k_thread_create() in the zblue port creates them as pthreads of whatever
 * task called it. NuttX file descriptor tables are per task group, so those
 * threads have to belong to the process that opens /dev/ttyHCI0 - bluetoothd,
 * via bt_sal_init() -> z_sys_init() -> ... -> h4_open(). Calling it here as
 * well used to create a second sysworkq and a second BT LW WQ on the same
 * static K_THREAD_STACK_DEFINE() buffers, which is what hard-faulted the
 * "BT LW WQ" thread a couple of seconds into boot once rcS started
 * bluetoothd automatically. z_sys_init() is now idempotent too
 * (external/zblue port/kernel/init.c), so the first caller wins - it must be
 * bluetoothd, not this driver.
 *
 * Registering the character device is all this driver owes the stack: the
 * H4 device entry's init_fn only logs, and the fd plus the RX thread are
 * created later in h4_open().
 */

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

/* ---- R65: BR/EDR SSP event bridge, CORRECTED (2026-08-17).
 *
 * The LCPU controller emits STANDARD HCI event numbers (verified on the
 * wire: 0x23 = Read_Remote_Extended_Features_Complete, 13 params, exactly
 * per spec; 0x1b = Max_Slots_Change). This zblue branch uses OFFSET codes
 * for the SSP events only (0x31..0x3b) while keeping standard codes for
 * everything else - including 0x23, which zblue handles natively as
 * remote-ext-features.
 *
 * The old R57 table mapped 0x23 -> 0x31 (io_capa_req). That HIJACKED the
 * remote-ext-features completion, so a board-initiated createbond stalled
 * forever right after Read_Remote_Extended_Features (the pairing never
 * started, zblue also sent a bogus IO_Capability_Reply that the LCPU
 * rejected with Command Disallowed 0x0c), and the phone disconnected
 * after ~30s with reason 0x13 ("unable to communicate").
 *
 * Correct standard SSP event numbers (BT Core spec):
 *   0x24 IO_Capability_Request        bdaddr(6)
 *   0x25 IO_Capability_Response       bdaddr(6)+cap+oob+auth (9)
 *   0x26 User_Confirmation_Request    bdaddr(6)+passkey(4)   (10)
 *   0x27 User_Passkey_Request         bdaddr(6)
 *   0x29 Simple_Pairing_Complete      bdaddr(6)
 *   0x3b User_Passkey_Notification    bdaddr(6)+passkey(4)   (same code in zblue: no bridge)
 * Both sides address these events by bdaddr, so the bridge is a PURE
 * event-code rewrite - parameters pass through unchanged. */

static int sf32lb52_bt_conn_reg(struct sf32lb52_bt_priv_s *priv,
                                uint16_t handle, const uint8_t *addr)
{
  int i;
  int free_slot = -1;

  for (i = 0; i < SF32LB52_BT_CONN_MAX; i++)
    {
      if (priv->conns[i].handle == handle && priv->conns[i].addr[0] != 0)
        {
          memcpy(priv->conns[i].addr, addr, 6);
          return 0;
        }

      if (free_slot < 0 && priv->conns[i].addr[0] == 0)
        {
          free_slot = i;
        }
    }

  if (free_slot < 0)
    {
      syslog(LOG_ERR, "sf32lb52 bth4: conn table full\n");
      return -ENOMEM;
    }

  priv->conns[free_slot].handle = handle;
  memcpy(priv->conns[free_slot].addr, addr, 6);
  syslog(LOG_INFO,
         "sf32lb52 bth4: conn reg handle=0x%04x addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
         handle, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
  return 0;
}

static void sf32lb52_bt_conn_unreg(struct sf32lb52_bt_priv_s *priv,
                                   uint16_t handle)
{
  int i;

  for (i = 0; i < SF32LB52_BT_CONN_MAX; i++)
    {
      if (priv->conns[i].handle == handle && priv->conns[i].addr[0] != 0)
        {
          priv->conns[i].addr[0] = 0;
          syslog(LOG_INFO, "sf32lb52 bth4: conn unreg handle=0x%04x\n",
                 handle);
          return;
        }
    }
}

static const uint8_t *sf32lb52_bt_conn_lookup(
    const struct sf32lb52_bt_priv_s *priv, uint16_t handle)
{
  int i;

  for (i = 0; i < SF32LB52_BT_CONN_MAX; i++)
    {
      if (priv->conns[i].handle == handle && priv->conns[i].addr[0] != 0)
        {
          return priv->conns[i].addr;
        }
    }

  return NULL;
}

/* R103: Convert Inquiry Result (0x02) to Inquiry Result With RSSI (0x22).
 * The LCPU sends basic Inquiry Result which zblue doesn't handle (it only
 * handles 0x22 and 0x2f). The layouts differ:
 *   0x02: num(1) + [bdaddr(6) + psr(1) + reserved(2) + cod(3) + co(2)] × N
 *   0x22: num(1) + [bdaddr(6) + psr(1) + reserved(1) + cod(3) + co(2) + rssi(1)] × N
 * We drop one reserved byte and add rssi = -127 dBm (0x81) per response. */
static ssize_t sf32lb52_bt_convert_inquiry_result(
    const uint8_t *frame, size_t len,
    uint8_t *out, size_t out_cap)
{
  if (len < 4 || frame[0] != H4_EVT || frame[1] != 0x02)
    {
      return 0;
    }

  uint8_t plen = frame[2];
  uint8_t num = frame[3];
  /* Each response is 15 bytes in 0x02, 14 bytes in 0x22 */
  size_t expected = 1 + 15 * num;
  if (plen != expected || len < 3u + plen)
    {
      return 0; /* malformed or incomplete */
    }

  size_t new_plen = 1 + 14 * num;
  size_t olen = 3 + new_plen;
  if (olen > out_cap)
    {
      return 0;
    }

  out[0] = H4_EVT;
  out[1] = 0x22; /* INQUIRY_RESULT_WITH_RSSI */
  out[2] = (uint8_t)new_plen;
  out[3] = num;

  for (uint8_t i = 0; i < num; i++)
    {
      const uint8_t *src = &frame[4 + i * 15];
      uint8_t *dst = &out[4 + i * 14];
      memcpy(dst, src, 8);         /* bdaddr(6) + psr(1) + reserved(1) */
      dst[8] = 0x81;               /* rssi = -127 dBm (unknown) */
      memcpy(dst + 9, src + 9, 5); /* cod(3) + clock_offset(2) */
    }

  syslog(LOG_INFO,
         "sf32lb52 bth4: Inquiry Result 0x02 -> 0x22 (%u devs)\n", num);
  return (ssize_t)olen;
}

/* R104: Send a raw HCI command to the LCPU. The cmd buffer must include
 * the H4 header (byte 0 = 0x01), HCI opcode (bytes 1-2), param length
 * (byte 3), and parameters. Total length = 1 + 3 + param_len. */
static void sf32lb52_bt_send_hci_cmd(struct sf32lb52_bt_priv_s *priv,
                                      const uint8_t *cmd, size_t len)
{
  (void)priv;
  sf32lb52_host_send_packet(cmd, (uint16_t)len);
}

/* R104: SSP auto-reply in bth4 — mimics xiaozhi BTS2 approach.
 * Intercept SSP events at the HCI transport layer, auto-reply to the
 * controller, and swallow the events so zblue's SSP handler (which
 * crashes due to bt_hci_cmd_send_sync on sysworkq) is never invoked.
 *
 * Events we handle:
 *   0x24 IO_CAPA_REQ       → reply IO_CAPABILITY_REPLY (DisplayYesNo, MITM)
 *   0x26 USER_CONFIRM_REQ  → reply USER_CONFIRM_REPLY (auto-accept)
 *   0x17 LINK_KEY_REQ      → reply LINK_KEY_REQ_NEG_REPLY (no stored key)
 *
 * Events we pass through to zblue:
 *   0x18 LINK_KEY_NOTIFY   → zblue stores the key
 *   0x36 SSP_COMPLETE      → zblue marks pairing done
 *   0x06 AUTH_COMPLETE     → zblue triggers encryption
 *   0x08 ENCRYPT_CHANGE    → zblue marks link encrypted
 */
static bool sf32lb52_bt_handle_ssp_auto(struct sf32lb52_bt_priv_s *priv,
                                         const uint8_t *frame, size_t len)
{
  uint8_t evt;
  uint16_t opcode;
  uint8_t cmd[12];

  if (len < 3 || frame[0] != H4_EVT)
    {
      return false;
    }

  evt = frame[1];

  switch (evt)
    {
    case 0x24: /* IO_CAPA_REQ: bdaddr(6) */
      {
        if (len < 3 + 6) return false;
        const uint8_t *addr = &frame[3];

        /* Send IO_CAPABILITY_REPLY:
         * bdaddr(6) + capability(1) + oob(1) + auth(1) = 9 bytes */
        opcode = BT_HCI_OP_IO_CAPABILITY_REPLY;
        cmd[0] = (uint8_t)(opcode & 0xff);
        cmd[1] = (uint8_t)(opcode >> 8);
        cmd[2] = 9; /* param length */
        memcpy(&cmd[3], addr, 6);
        cmd[9] = 0x01; /* capability = DisplayYesNo */
        cmd[10] = 0x00; /* oob = not present */
        cmd[11] = 0x01; /* auth = MITM required (General Bonding) */

        sf32lb52_bt_send_hci_cmd(priv, cmd, 12);
        syslog(LOG_INFO,
               "sf32lb52 bth4 R104: IO_CAPA_REQ auto-reply "
               "(DisplayYesNo+MITM) for %02x:%02x:%02x:%02x:%02x:%02x\n",
               addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
        return true; /* swallow the event */
      }

    case 0x26: /* USER_CONFIRM_REQ: bdaddr(6) + passkey(4) + auth(1) */
      {
        if (len < 3 + 11) return false;
        const uint8_t *addr = &frame[3];

        /* Send USER_CONFIRM_REPLY: bdaddr(6) = 6 bytes */
        opcode = BT_HCI_OP_USER_CONFIRM_REPLY;
        cmd[0] = (uint8_t)(opcode & 0xff);
        cmd[1] = (uint8_t)(opcode >> 8);
        cmd[2] = 6; /* param length */
        memcpy(&cmd[3], addr, 6);

        sf32lb52_bt_send_hci_cmd(priv, cmd, 9);
        syslog(LOG_INFO,
               "sf32lb52 bth4 R104: USER_CONFIRM_REQ auto-accept "
               "for %02x:%02x:%02x:%02x:%02x:%02x\n",
               addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
        return true; /* swallow the event */
      }

    case 0x17: /* LINK_KEY_REQ: bdaddr(6) */
      {
        if (len < 3 + 6) return false;
        const uint8_t *addr = &frame[3];

        /* No stored key — send NEG_REPLY to force re-pairing */
        opcode = BT_HCI_OP_LINK_KEY_REQ_NEG_REPLY;
        cmd[0] = (uint8_t)(opcode & 0xff);
        cmd[1] = (uint8_t)(opcode >> 8);
        cmd[2] = 6; /* param length */
        memcpy(&cmd[3], addr, 6);

        sf32lb52_bt_send_hci_cmd(priv, cmd, 9);
        syslog(LOG_INFO,
               "sf32lb52 bth4 R104: LINK_KEY_REQ neg-reply "
               "(no key, force re-pair) for %02x:%02x:%02x:%02x:%02x:%02x\n",
               addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
        return true; /* swallow the event */
      }

    default:
      return false; /* pass through to zblue */
    }
}

/* Rewrite a standard LCPU SSP event into the zblue event code. Both
 * formats are bdaddr-addressed with identical parameter layouts, so only
 * the event code changes. Returns the new frame length, or 0 to pass the
 * frame through unchanged. */
static ssize_t sf32lb52_bt_convert_ssp_event(
    const struct sf32lb52_bt_priv_s *priv,
    const uint8_t *frame, size_t len,
    uint8_t *out, size_t out_cap)
{
  uint8_t evt = frame[1];
  uint8_t zevt;
  size_t plen;
  size_t olen;

  if (len < 4 || frame[0] != H4_EVT)
    {
      return 0;
    }

  plen = frame[2];
  switch (evt)
    {
      case 0x24: zevt = 0x31; break;  /* IO_CAPA_REQ        */
      case 0x25: zevt = 0x32; break;  /* IO_CAPA_RESP       */
      case 0x26: zevt = 0x33; break;  /* USER_CONFIRM_REQ   */
      case 0x27: zevt = 0x34; break;  /* USER_PASSKEY_REQ   */
      case 0x29: zevt = 0x36; break;  /* SSP_COMPLETE       */
      default: return 0;              /* 0x23 ext-features, 0x2b
                                       * link-supervision etc. are
                                       * standard events zblue already
                                       * handles natively - pass through */
    }

  if (len < 3u + plen)
    {
      return 0; /* incomplete frame, wait for more bytes */
    }

  olen = 1 + 1 + 1 + plen;
  if (olen > out_cap)
    {
      return 0;
    }

  out[0] = H4_EVT;
  out[1] = zevt;
  out[2] = (uint8_t)plen;
  memcpy(&out[3], &frame[3], plen);

  syslog(LOG_INFO,
         "sf32lb52 bth4: SSP bridge 0x%02x -> 0x%02x (plen %u)\n",
         evt, zevt, (unsigned)plen);
  return (ssize_t)olen;
}

/* R106: uart_bth4_receive now calls bt_recv directly for HCI events,
 * so bt_netdev_receive works correctly for event delivery. */
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

      case BT_HCI_OP_DELETE_STORED_LINK_KEY:
        /* zblue ignores the return params; the 1-byte status-only
         * completion is accepted by its cmd_complete handler */
        *ret = sf32lb52_bt_synth_status_complete(priv, opcode);
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
      /* LE_SET_EVENT_MASK is forwarded to the LCPU controller: the
       * emulated success kept the controller's LE event mask at its
       * reset default (all masked), so LE Connection Complete (0x3E)
       * was never reported and phone GATT connects never completed. */
      case BT_HCI_OP_LE_WRITE_DEFAULT_DATA_LEN:
      case BT_HCI_OP_LE_SET_RPA_TIMEOUT:
      /* SET_EVENT_MASK (0x0c01) moved OUT of the emulate table (R69):
       * the LCPU is a real controller that GATES events on this mask.
       * zblue computes the SSP mask bits from its offset event numbers
       * (BIT(48..53)), so the standard SSP events (0x24..0x2b, bits
       * 35..42) stayed masked -> the LCPU never delivered
       * IO_Capability_Request -> pairing stalled forever (Auth 0x05 /
       * LMP timeout 0x22). The mask is patched at the top of
       * sf32lb52_bt_send and FORWARDED here with a synthesized CC
       * (the LCPU often never answers it, same class as scan activity). */
      case BT_HCI_OP_WRITE_LOCAL_NAME:
      /* WRITE_SCAN_ENABLE is FORWARDED to the LCPU controller: emulated
       * success left BR inquiry/page scan disabled forever, so phones
       * could never discover this board over BR/EDR (observed 2026-08-15;
       * Sifli SDK BT PAN example confirms the controller supports BR
       * scan enable). Not part of bt_br_init(), so a controller error
       * only fails set_scan_mode, never stack enable. */
      /* PAGE/INQUIRY SCAN ACTIVITY also forwarded: the LCPU default
       * activity is invalid (0), so page scan never actually scanned and
       * phones got "couldn't communicate" right after discovery. zblue
       * sets explicit activity at init (bt_br_init). */
      case BT_HCI_OP_WRITE_PAGE_TIMEOUT:
      case BT_HCI_OP_WRITE_CLASS_OF_DEVICE:
      case BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE:
      case BT_HCI_OP_WRITE_EXTENDED_INQUIRY_RESPONSE:
      case BT_HCI_OP_WRITE_INQUIRY_MODE:
      case BT_HCI_OP_WRITE_PAGE_SCAN_TYPE:
      /* WRITE_SSP_MODE is FORWARDED to the LCPU controller: emulated
       * success kept the controller in legacy-PIN-only pairing, which
       * HyperOS phones silently ignore (no PIN dialog) -> pairing stalls
       * and the link fails. LCPU reports LMP 5.3 (SSP capable). It is part
       * of bt_br_init(), so a controller error would fail stack enable -
       * acceptable, we want the real mode. */
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

static void sf32lb52_bt_track_extra_op(struct sf32lb52_bt_priv_s *priv,
                                       uint16_t opcode)
{
  if (priv->pending_extra_count >=
        (int)(sizeof(priv->pending_extra_ops) /
              sizeof(priv->pending_extra_ops[0])))
    {
      memmove(&priv->pending_extra_ops[0], &priv->pending_extra_ops[1],
              sizeof(priv->pending_extra_ops) -
                sizeof(priv->pending_extra_ops[0]));
      priv->pending_extra_count--;
    }

  priv->pending_extra_ops[priv->pending_extra_count++] = opcode;
}

/* Returns true (and consumes the slot) when the frame is a Command
 * Complete for one of the driver-injected opcodes. */
static bool sf32lb52_bt_consume_extra_cc(struct sf32lb52_bt_priv_s *priv,
                                         const uint8_t *frame,
                                         size_t packet_len)
{
  uint16_t opcode;
  int i;

  if (packet_len < H4_HEADER_SIZE + sizeof(struct bt_hci_evt_hdr_s) + 4 ||
      frame[0] != H4_EVT || frame[1] != BT_HCI_EVT_CMD_COMPLETE)
    {
      return false;
    }

  opcode = sf32lb52_bt_get_le16(&frame[4]);

  for (i = 0; i < priv->pending_extra_count; i++)
    {
      if (priv->pending_extra_ops[i] == opcode)
        {
          priv->pending_extra_ops[i] =
              priv->pending_extra_ops[--priv->pending_extra_count];
          return true;
        }
    }

  return false;
}

#if SF32LB52_BT_TRACE_ACL_FULL
static void sf32lb52_bt_trace_acl(const char *dir, const uint8_t *pkt,
                                  size_t total)
{
  static unsigned int seq_tx;
  static unsigned int seq_rx;
  unsigned int seq;
  char hex[32 * 3 + 1];

  if (total < 1 || pkt[0] != H4_ACL)
    {
      return;
    }

  seq = (dir[0] == 't') ? seq_tx++ : seq_rx++;

  for (size_t off = 0; off < total; off += 32)
    {
      size_t n = (total - off > 32) ? 32 : (total - off);
      int pos = 0;

      for (size_t i = 0; i < n; i++)
        {
          pos += snprintf(&hex[pos], sizeof(hex) - pos, "%02x ",
                          pkt[off + i]);
        }

      syslog(LOG_INFO,
             "sf32lb52 bth4 acl: dir=%s seq=%u off=%u total=%u %s\n",
             dir, seq, (unsigned int)off, (unsigned int)total, hex);
    }
}
#endif

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
  {
    char hex[256];
    int pos = 0;
    int n = (len > 40) ? 40 : len;
    for (int i = 0; i < n && pos < 200; i++) {
      pos += snprintf(&hex[pos], sizeof(hex) - pos, "%02x ",
                      priv->rxbuf[i]);
    }
    syslog(LOG_INFO,
           "sf32lb52 bth4 recv: len=%u pending=%lu %s%s\n",
           len, (unsigned long)priv->rxlen, hex,
           (len > 1 && priv->rxbuf[0] == 0x04 && priv->rxbuf[1] == 0x3e)
             ? "<LE META>" : "");
  }
#endif

  ret = OK;

  /* R61 inquiry probe REMOVED (2026-08-17): the injected 12.8s inquiry
   * overlapped the stack's own discovery (bttool inquiry / createbond),
   * the LCPU goes silent on overlapping inquiries and the host then
   * times out a sync command. Radio reachability was proven in R61/R62;
   * no further need to inject. */

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

#if SF32LB52_BT_TRACE_ACL_FULL
      sf32lb52_bt_trace_acl("rx", priv->rxbuf, (size_t)packet_len);
#endif

      /* Event tracing (R54): 0x13 is Number-of-Completed-Packets (a valid
       * event: num handle count), NOT an Encryption Change. The R52-era
       * normalization was corrupting it (num 1 -> 0). Real Encryption Change
       * is 0x08; trace it without modifying, plus log the local features
       * response (0x0403) which gates zblue's bt_br_init (BT_FEAT_BREDR). */
      if (packet_len >= 8 && priv->rxbuf[0] == 0x04 && priv->rxbuf[1] == 0x08)
        {
          syslog(LOG_INFO,
                 "sf32lb52 bth4: EVT 0x08 encrypt-change: %02x %02x %02x %02x %02x\n",
                 priv->rxbuf[3], priv->rxbuf[4], priv->rxbuf[5],
                 priv->rxbuf[6], priv->rxbuf[7]);
        }
      else if (packet_len >= 14 && priv->rxbuf[0] == 0x04 &&
               priv->rxbuf[1] == 0x0e && priv->rxbuf[2] >= 12 &&
               priv->rxbuf[4] == 0x03 && priv->rxbuf[5] == 0x04)
        {
          syslog(LOG_INFO,
                 "sf32lb52 bth4: ReadLocalFeatures rsp status=%02x feat: %02x %02x %02x %02x %02x %02x %02x %02x\n",
                 priv->rxbuf[6], priv->rxbuf[7], priv->rxbuf[8], priv->rxbuf[9],
                 priv->rxbuf[10], priv->rxbuf[11], priv->rxbuf[12], priv->rxbuf[13],
                 priv->rxbuf[14]);
        }

      /* R57: keep the handle->addr map for the SSP bridge. */
      if (priv->rxbuf[0] == H4_EVT && priv->rxbuf[1] == 0x03 &&
          packet_len >= 14 && priv->rxbuf[3] == 0x00)
        {
          sf32lb52_bt_conn_reg(priv,
                               sf32lb52_bt_get_le16(&priv->rxbuf[4]),
                               &priv->rxbuf[6]);
        }
      else if (priv->rxbuf[0] == H4_EVT && priv->rxbuf[1] == 0x05 &&
               packet_len >= 7)
        {
          sf32lb52_bt_conn_unreg(priv,
                                 sf32lb52_bt_get_le16(&priv->rxbuf[4]));
        }

      /* Swallow Command Complete frames for driver-injected commands
       * before they reach zblue (see sf32lb52_bt_priv_s comment). */
      if (sf32lb52_bt_consume_extra_cc(priv, priv->rxbuf,
                                       (size_t)packet_len))
        {
          syslog(LOG_INFO,
                 "sf32lb52 bth4: swallowed CC for injected op\n");
          priv->rxlen -= (size_t)packet_len;
          if (priv->rxlen > 0)
            {
              memmove(priv->rxbuf, &priv->rxbuf[packet_len], priv->rxlen);
            }
          continue;
        }

      /* R104: SSP auto-reply — handle SSP events at the bth4 layer
       * to avoid sysworkq deadlock in bt_hci_cmd_send_sync. */
      if (sf32lb52_bt_handle_ssp_auto(priv, priv->rxbuf, (size_t)packet_len))
        {
          priv->rxlen -= (size_t)packet_len;
          if (priv->rxlen > 0)
            {
              memmove(priv->rxbuf, &priv->rxbuf[packet_len], priv->rxlen);
            }
          continue;
        }

      {
        uint8_t sspbuf[32];
        ssize_t ssp_len = sf32lb52_bt_convert_ssp_event(priv, priv->rxbuf,
                                                        (size_t)packet_len,
                                                        sspbuf,
                                                        sizeof(sspbuf));
        if (ssp_len > 0)
          {
            ret = sf32lb52_bt_forward_packet(priv, sspbuf, (size_t)ssp_len);
          }
        else
          {
            ret = sf32lb52_bt_forward_packet(priv, priv->rxbuf,
                                             (size_t)packet_len);
          }
      }

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
        return -EINVAL;
    }

  if (type == BT_CMD && len >= sizeof(struct bt_hci_cmd_hdr_s))
    {
      opcode = sf32lb52_bt_get_le16(data);

      /* R69: patch the Set_Event_Mask SSP bits BEFORE any dispatch -
       * zblue computes them from its offset event numbers (BIT(48..53))
       * while the LCPU is a standard controller that gates SSP events
       * (0x24..0x2b) on standard bits 35..42. Masked SSP events were
       * the root cause of every pairing failure (Auth 0x05 board-side,
       * LMP timeout 0x22 phone-side). 0x0c01 is then FORWARDED (with a
       * synthesized CC, see below) so the patched mask reaches the LCPU.
       * R70: also force bit 7 = Encryption Change (event 0x08). zblue
       * only sets it under CONFIG_BT_SMP with the LE-encrypt feature,
       * so BR encryption completed but the host never saw the event
       * (security err 2 timeout, phone drops the ACL). */
      if (opcode == 0x0c01 && len >= 11)
        {
          uint8_t *m = &((uint8_t *)data)[3];
          uint8_t need = (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6);

          if ((m[4] & need) != need || (m[5] & 0x05) != 0x05
              || (m[0] & 0x80) == 0)
            {
              m[0] |= 0x80;    /* bit 7  = event 0x08 Encrypt Change */
              m[4] |= need;    /* bits 35..38 = SSP events 0x24..0x27 */
              m[5] |= 0x05;    /* bits 40,42 = SSP complete 0x29, notify 0x2b */
              syslog(LOG_WARNING,
                     "sf32lb52 bth4: Set_Event_Mask SSP+encrypt bits "
                     "forced on (zblue offset-mask bug)\n");
            }
        }

      /* Scan-activity commands MUST reach the LCPU (the controller
       * default activity is invalid), but the LCPU frequently never
       * answers them with a Command Complete, which deadlocks zblue's
       * ncmd_sem and makes every later send_sync assert. Forward the
       * command AND immediately synthesize a CC for zblue; the LCPU's
       * late CC (if any) is swallowed by consume_extra_cc.
       * Write_Local_Name / EIR are NOT forwarded at all: the 252-byte
       * frame floods the IPC TX ring (observed pending=175 chunks) and
       * the LCPU stops answering any command afterwards. The LCPU name
       * field is broken anyway (shows the address). */
      if (opcode == BT_HCI_OP_WRITE_LOCAL_NAME ||
          opcode == BT_HCI_OP_WRITE_EXTENDED_INQUIRY_RESPONSE)
        {
          int cc_ret = sf32lb52_bt_synth_status_complete(priv, opcode);
          return cc_ret < 0 ? cc_ret : (int)len;
        }

      /* R92: force CoD to PANU before forwarding to LCPU. */
      if (opcode == BT_HCI_OP_WRITE_CLASS_OF_DEVICE && len >= 6)
        {
          uint8_t *cod = &((uint8_t *)data)[3];
          uint32_t desired = 0x00020510;
          uint32_t current = cod[0] | (cod[1] << 8) | (cod[2] << 16);
          if (current != desired)
            {
              cod[0] = (uint8_t)(desired & 0xFF);
              cod[1] = (uint8_t)((desired >> 8) & 0xFF);
              cod[2] = (uint8_t)((desired >> 16) & 0xFF);
              syslog(LOG_INFO,
                     "sf32lb52 bth4: CoD 0x%06x -> 0x%06x (PANU)\n",
                     (unsigned)current, (unsigned)desired);
            }
        }

      if (opcode == BT_HCI_OP_WRITE_PAGE_SCAN_ACTIVITY ||
          opcode == BT_HCI_OP_WRITE_INQUIRY_SCAN_ACTIVITY ||
          opcode == BT_HCI_OP_WRITE_INQUIRY_SCAN_TYPE ||
          opcode == BT_HCI_OP_WRITE_PAGE_SCAN_TYPE ||
          opcode == BT_HCI_OP_WRITE_CLASS_OF_DEVICE ||
          opcode == BT_HCI_OP_SET_EVENT_MASK)
        {
          int cc_ret;

          cc_ret = sf32lb52_host_send_packet(hdr, len + drv->head_reserve);
          if (cc_ret < 0)
            {
              return cc_ret;
            }

          sf32lb52_bt_track_extra_op(priv, opcode);
          cc_ret = sf32lb52_bt_synth_status_complete(priv, opcode);
          syslog(LOG_INFO,
                 "sf32lb52 bth4: forwarded + synth CC for op 0x%04x\n",
                 opcode);
          return cc_ret < 0 ? cc_ret : (int)len;
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

  priv->drop_rx_until_tx = false;

  /* R73: IO_Capability_Reply (0x042b) auth requirement fix. zblue (as
   * acceptor) mirrors what it can provide and replies with General
   * Bonding WITHOUT the MITM bit (0x04). Phones that requested
   * Dedicated/General Bonding + MITM (auth 0x03/0x05) then find SSP
   * Numeric Comparison infeasible (our old IO capa was NoInputNoOutput)
   * and fall back to legacy PIN entry - which nobody answers, so the
   * phone reports "PIN incorrect or refused". With the DisplayYesNo
   * upgrade (SAL pairing_confirm) the association model is viable, so
   * also set the MITM bit to match the phone's request.
   * Layout: data[0..1]=opcode, data[2]=plen(9), data[3..8]=bdaddr,
   * data[9]=capability, data[10]=oob, data[11]=authentication. */
  if (type == BT_CMD && opcode == 0x042b && len >= 12)
    {
      uint8_t auth = ((uint8_t *)data)[11];

      if ((auth & 0x01) == 0)
        {
          ((uint8_t *)data)[11] = auth | 0x01;
          syslog(LOG_INFO,
                 "sf32lb52 bth4: IO_Capability_Reply auth 0x%02x -> 0x%02x "
                 "(+MITM, phone requires it)\n", auth, auth | 0x01);
        }
    }

  /* R60: Write_Scan_Enable (0x0c1a) - force inquiry scan on BEFORE the
   * command goes out. zblue's enable only requests page scan (connectable,
   * param 0x02); inquiry scan (discoverable) is never enabled, so phones
   * cannot find the board in their Bluetooth settings. */
  if (type == BT_CMD && opcode == 0x0c1a && len >= 4)
    {
      if ((((uint8_t *)data)[3] & 0x01) == 0)
        {
          ((uint8_t *)data)[3] |= 0x01;
          syslog(LOG_INFO,
                 "sf32lb52 bth4: forcing inquiry scan on (0x0c1a)\n");
        }
    }

  /* R63: Inquiry (0x0401) num_rsp fix. zblue hardcodes num_rsp = 0xff
   * ("unlimited") but the LCPU firmware never answers an Inquiry whose
   * Num_Responses byte is 0xff - not even a Command Status (observed
   * 2026-08-17: send_sync 0x0401 timeout, zero RX frames). The R61 probe
   * with num_rsp 0x00 got Inquiry Results. Per BT Core spec 0x00 IS the
   * unlimited value, so rewrite it. Layout here: data[0..1]=opcode,
   * data[2]=plen, data[3..5]=LAP, data[6]=duration, data[7]=num_rsp. */
  if (type == BT_CMD && opcode == 0x0401 && len >= 8)
    {
      if (((uint8_t *)data)[7] != 0x00)
        {
          ((uint8_t *)data)[7] = 0x00;
          syslog(LOG_INFO,
                 "sf32lb52 bth4: Inquiry num_rsp 0xff -> 0x00 (LCPU quirk)\n");
        }
    }

  /* R68: Write_SSP_Mode (0x0c56) forced OFF ... REMOVED: the LCPU
   * rejects mode 0 with Invalid Parameters (0x12), SSP cannot be
   * disabled on this firmware. Instead see the Set_Event_Mask fix
   * below, which is the real root cause of the pairing stall. */

  /* R69 note: the old Set_Event_Mask patch block was relocated to the
   * top of sf32lb52_bt_send (before the emulate table) - see above. */

  /* Trace ALL HCI output (commands included) so BREDR command handling
   * (e.g. Write_Scan_Enable) can be verified end-to-end. */
  if (SF32LB52_BT_TRACE)
    {
      char thex[256];
      int tpos = 0;
      int tn = (len + drv->head_reserve > 40) ? 40 : (int)(len + drv->head_reserve);
      for (int i = 0; i < tn && tpos < 200; i++) {
        tpos += snprintf(&thex[tpos], sizeof(thex) - tpos, "%02x ", hdr[i]);
      }
      syslog(LOG_INFO,
             "sf32lb52 bth4 tx: type=%u len=%lu h4=%s\n",
             (unsigned int)type,
             (unsigned long)(len + drv->head_reserve),
             thex);
    }

#if SF32LB52_BT_TRACE_ACL_FULL
  sf32lb52_bt_trace_acl("tx", hdr, len + drv->head_reserve);
#endif

  ret = sf32lb52_host_send_packet(hdr, len + drv->head_reserve);
  if (ret < 0)
    {
      return ret;
    }

  /* R98: do NOT inject any scan-activity commands (inquiry or page).
   * Every value of Write_Inquiry_Scan_Activity triggers Hardware Error
   * 0x00 on this LCPU firmware (even 0x0800/0x0012). R64 proved the
   * board IS discoverable with only Write_Scan_Enable (inquiry bit ON)
   * — the LCPU handles scan activity internally with reasonable
   * defaults. The R60 inquiry-bit forcing above is sufficient. */

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

  return OK;
}
