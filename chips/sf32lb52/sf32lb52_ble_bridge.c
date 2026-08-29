/****************************************************************************
 * chips/sf32lb52/sf32lb52_ble_bridge.c
 *
 * AI Watch custom GATT bridge - the narrow BSP translation unit that talks
 * to zblue directly. Compiled only with CONFIG_AI_WATCH_BLE_BRIDGE=y
 * (which depends on CONFIG_BT && CONFIG_UART_BTH4).
 *
 * Responsibilities:
 *   - bt_enable() the zblue host after the board has registered /dev/ttyHCI0
 *   - register the AI Watch custom GATT service (4 characteristics)
 *   - start connectable advertising as CONFIG_BT_DEVICE_NAME
 *   - validate TimeSync writes and enqueue Command writes for the app
 *   - expose the plain-C wrapper declared in ai_watch_ble_bsp.h
 *
 * Thread safety: GATT/conn callbacks execute on zblue RX threads; the
 * wrapper take_* / notify functions execute on the application (LVGL)
 * thread. Shared data is protected with sched_lock() critical sections
 * (single HCPU core, all callers are task context). Callbacks never touch
 * LVGL - they only enqueue data.
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/sched.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "ai_watch_ble_bsp.h"

/****************************************************************************
 * h4.c (zblue H4 TTY transport) references btsnoop_log_capture()
 * unconditionally. The implementation lives in the optional SiFli BT
 * framework (CONFIG_BLUETOOTH_LOG), which is not part of this build, so
 * provide a no-op stub here. If that framework is ever linked in, this
 * stub will collide with its strong definition and fail the link loudly
 * instead of silently disabling its logging.
 ****************************************************************************/

void btsnoop_log_capture(uint8_t is_receive, uint8_t *hci_pkt,
                         uint32_t hci_pkt_size)
{
  (void)is_receive;
  (void)hci_pkt;
  (void)hci_pkt_size;
}

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BRIDGE_LOG(...)  syslog(LOG_INFO, "aiwatch-ble: " __VA_ARGS__)

/* Protocol version (also documented in ai_watch_ble.h) */

#define BRIDGE_PROTOCOL_VERSION  1

/* TimeSync validity window: 2020-01-01 .. 2100-01-01 (UTC seconds) */

#define BRIDGE_TS_MIN_UTC        1577836800u
#define BRIDGE_TS_MAX_UTC        4102444800u
#define BRIDGE_TS_MIN_TZ         (-720)
#define BRIDGE_TS_MAX_TZ         840

/* Attribute table row indexes (must match the table below) */

#define ATTR_IDX_SVC             0
#define ATTR_IDX_STATUS_CHR      1
#define ATTR_IDX_STATUS_VAL      2
#define ATTR_IDX_STATUS_CCC      3
#define ATTR_IDX_TIMESYNC_CHR    4
#define ATTR_IDX_TIMESYNC_VAL    5
#define ATTR_IDX_DATA_CHR        6
#define ATTR_IDX_DATA_VAL        7
#define ATTR_IDX_DATA_CCC        8
#define ATTR_IDX_CMD_CHR         9
#define ATTR_IDX_CMD_VAL         10

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Shared state between zblue threads and the application thread */

static struct
{
  volatile enum ai_watch_ble_bsp_state_e state;
  struct bt_conn *conn;                 /* active central, NULL if none */

  bool started;                         /* bsp_start() called */
  bool enabled;                         /* app-side Bluetooth switch */

  /* Last-sync stamp surfaced through the Status characteristic */

  uint32_t last_sync_utc;

  /* Pending validated TimeSync (BT thread -> app thread) */

  struct ai_watch_ble_bsp_timesync_s ts;
  bool ts_pending;

  /* Raw Command frame ring (BT thread -> app thread) */

  uint8_t cmdq[AI_WATCH_BLE_CMD_QUEUE_LEN][AI_WATCH_BLE_CMD_MAX];
  uint8_t cmdq_len[AI_WATCH_BLE_CMD_QUEUE_LEN];
  volatile uint8_t cmdq_head;           /* writer index */
  volatile uint8_t cmdq_tail;           /* reader index */
} g_bridge;

/****************************************************************************
 * Private Data - UUIDs
 *
 * Custom service  12345678-9abc-def0-1234-56789abcdef0
 *   Status     (f1) 12345678-9abc-def0-1234-56789abcdef1
 *   TimeSync   (f2) 12345678-9abc-def0-1234-56789abcdef2
 *   DataUpload (f3) 12345678-9abc-def0-1234-56789abcdef3
 *   Command    (f4) 12345678-9abc-def0-1234-56789abcdef4
 *
 * BT_UUID_128_ENCODE emits the little-endian byte order required on air.
 ****************************************************************************/

#define BRIDGE_SVC_UUID_VAL \
  BT_UUID_128_ENCODE(0x12345678, 0x9abc, 0xdef0, 0x1234, 0x56789abcdef0)
#define BRIDGE_STATUS_UUID_VAL \
  BT_UUID_128_ENCODE(0x12345678, 0x9abc, 0xdef0, 0x1234, 0x56789abcdef1)
#define BRIDGE_TIMESYNC_UUID_VAL \
  BT_UUID_128_ENCODE(0x12345678, 0x9abc, 0xdef0, 0x1234, 0x56789abcdef2)
#define BRIDGE_DATA_UUID_VAL \
  BT_UUID_128_ENCODE(0x12345678, 0x9abc, 0xdef0, 0x1234, 0x56789abcdef3)
#define BRIDGE_CMD_UUID_VAL \
  BT_UUID_128_ENCODE(0x12345678, 0x9abc, 0xdef0, 0x1234, 0x56789abcdef4)

static const struct bt_uuid_128 g_uuid_svc =
  BT_UUID_INIT_128(BRIDGE_SVC_UUID_VAL);
static const struct bt_uuid_128 g_uuid_status =
  BT_UUID_INIT_128(BRIDGE_STATUS_UUID_VAL);
static const struct bt_uuid_128 g_uuid_timesync =
  BT_UUID_INIT_128(BRIDGE_TIMESYNC_UUID_VAL);
static const struct bt_uuid_128 g_uuid_data =
  BT_UUID_INIT_128(BRIDGE_DATA_UUID_VAL);
static const struct bt_uuid_128 g_uuid_cmd =
  BT_UUID_INIT_128(BRIDGE_CMD_UUID_VAL);

/****************************************************************************
 * Private Functions - Characteristic value callbacks
 ****************************************************************************/

/* Build the 6-byte Status value: version(1) conn_state(1) last_sync(4, LE) */

static ssize_t bridge_read_status(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset)
{
  uint8_t value[6];
  uint32_t sync = g_bridge.last_sync_utc;
  uint8_t conn_state = (g_bridge.state == AI_WATCH_BLE_BSP_CONNECTED) ?
                        1 : 0;

  value[0] = BRIDGE_PROTOCOL_VERSION;
  value[1] = conn_state;
  value[2] = (uint8_t)(sync >> 0);
  value[3] = (uint8_t)(sync >> 8);
  value[4] = (uint8_t)(sync >> 16);
  value[5] = (uint8_t)(sync >> 24);

  return bt_gatt_attr_read(conn, attr, buf, len, offset,
                           value, sizeof(value));
}

/* TimeSync write: version(1) utc(4, LE) tz(2, LE signed). Strictly
 * validated here on the BT thread; the payload is handed to the app
 * which performs clock_settime on its own thread.
 */

static ssize_t bridge_write_timesync(struct bt_conn *conn,
                                     const struct bt_gatt_attr *attr,
                                     const void *buf, uint16_t len,
                                     uint16_t offset, uint8_t flags)
{
  const uint8_t *data = buf;
  struct ai_watch_ble_bsp_timesync_s sync;
  uint32_t utc;
  uint16_t tzs;

  if (offset != 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

  if (len != AI_WATCH_BLE_TIMESYNC_SIZE)
    {
      BRIDGE_LOG("timesync rejected: len %u\n", len);
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

  if (data[0] != BRIDGE_PROTOCOL_VERSION)
    {
      BRIDGE_LOG("timesync rejected: version %u\n", data[0]);
      return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

  memcpy(&utc, &data[1], sizeof(utc));
  memcpy(&tzs, &data[5], sizeof(tzs));

  if (utc < BRIDGE_TS_MIN_UTC || utc > BRIDGE_TS_MAX_UTC)
    {
      BRIDGE_LOG("timesync rejected: utc %lu out of range\n",
                 (unsigned long)utc);
      return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

  if ((int16_t)tzs < BRIDGE_TS_MIN_TZ || (int16_t)tzs > BRIDGE_TS_MAX_TZ)
    {
      BRIDGE_LOG("timesync rejected: tz %d out of range\n", (int16_t)tzs);
      return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

  sync.version = data[0];
  sync.utc_seconds = utc;
  sync.tz_offset_min = (int16_t)tzs;

  sched_lock();
  g_bridge.ts = sync;
  g_bridge.ts_pending = true;
  sched_unlock();

  BRIDGE_LOG("timesync accepted: utc=%lu tz=%d\n",
             (unsigned long)utc, (int)sync.tz_offset_min);
  return len;
}

/* Command write: raw frame passed through to the app untouched except
 * for a size sanity check. The app owns parsing/validation.
 */

static ssize_t bridge_write_command(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    const void *buf, uint16_t len,
                                    uint16_t offset, uint8_t flags)
{
  uint8_t head;
  uint8_t next;

  if (offset != 0)
    {
      return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

  if (len == 0 || len > AI_WATCH_BLE_CMD_MAX)
    {
      BRIDGE_LOG("command rejected: len %u\n", len);
      return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

  sched_lock();
  head = g_bridge.cmdq_head;
  next = (uint8_t)((head + 1) % AI_WATCH_BLE_CMD_QUEUE_LEN);
  if (next == g_bridge.cmdq_tail)
    {
      /* Queue full - tell the phone to retry */

      sched_unlock();
      BRIDGE_LOG("command queue full, dropping %u bytes\n", len);
      return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }

  memcpy(g_bridge.cmdq[head], buf, len);
  g_bridge.cmdq_len[head] = (uint8_t)len;
  g_bridge.cmdq_head = next;
  sched_unlock();

  BRIDGE_LOG("command queued: %u bytes\n", len);
  return len;
}

static void bridge_status_ccc_changed(const struct bt_gatt_attr *attr,
                                      uint16_t value)
{
  BRIDGE_LOG("status ccc: 0x%04x\n", value);
}

static void bridge_data_ccc_changed(const struct bt_gatt_attr *attr,
                                    uint16_t value)
{
  BRIDGE_LOG("data ccc: 0x%04x\n", value);
}

/****************************************************************************
 * Private Data - GATT attribute table
 *
 * The service is registered at runtime with bt_gatt_service_register()
 * from bridge_bt_ready(). Do NOT use BT_GATT_SERVICE_DEFINE here: the
 * NuttX zblue port iterates a hand-curated symbol list instead of linker
 * sections, so statically defined services would be silently ignored.
 ****************************************************************************/

static struct bt_gatt_attr g_bridge_attrs[] =
{
  /* Primary Service */

  BT_GATT_PRIMARY_SERVICE(&g_uuid_svc),

  /* Status (f1): Read + Notify */

  BT_GATT_CHARACTERISTIC(&g_uuid_status.uuid,
                         BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                         BT_GATT_PERM_READ,
                         bridge_read_status, NULL, NULL),
  BT_GATT_CCC(bridge_status_ccc_changed, BT_GATT_PERM_READ |
                                         BT_GATT_PERM_WRITE),

  /* TimeSync (f2): Write */

  BT_GATT_CHARACTERISTIC(&g_uuid_timesync.uuid,
                         BT_GATT_CHRC_WRITE,
                         BT_GATT_PERM_WRITE,
                         NULL, bridge_write_timesync, NULL),

  /* DataUpload (f3): Notify */

  BT_GATT_CHARACTERISTIC(&g_uuid_data.uuid,
                         BT_GATT_CHRC_NOTIFY, 0,
                         NULL, NULL, NULL),
  BT_GATT_CCC(bridge_data_ccc_changed, BT_GATT_PERM_READ |
                                       BT_GATT_PERM_WRITE),

  /* Command (f4): Write / Write-Without-Response */

  BT_GATT_CHARACTERISTIC(&g_uuid_cmd.uuid,
                         BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                         BT_GATT_PERM_WRITE,
                         NULL, bridge_write_command, NULL),
};

static struct bt_gatt_service g_bridge_svc =
  BT_GATT_SERVICE(g_bridge_attrs);

/****************************************************************************
 * Private Functions - Advertising and connection callbacks
 ****************************************************************************/

static int bridge_adv_start(void)
{
  /* Built with explicit byte arrays rather than BT_DATA_BYTES():
   * the compound literal inside that macro is not accepted in a
   * file-scope initializer by this toolchain configuration.
   */

  static const uint8_t ad_flags[] =
  {
    BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR
  };
  static const uint8_t ad_svc_uuid128[] =
  {
    BRIDGE_SVC_UUID_VAL
  };
  static const struct bt_data ad[] =
  {
    { .type = BT_DATA_FLAGS, .data = ad_flags, .data_len = sizeof(ad_flags) },
    { .type = BT_DATA_UUID128_ALL, .data = ad_svc_uuid128,
      .data_len = sizeof(ad_svc_uuid128) },
  };
  static const struct bt_data sd[] =
  {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1),
  };
  int err;

  err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                        ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err != 0)
    {
      BRIDGE_LOG("advertising start failed: %d\n", err);
      return err;
    }

  BRIDGE_LOG("advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);
  return 0;
}

static void bridge_connected(struct bt_conn *conn, uint8_t err)
{
  if (err != 0)
    {
      BRIDGE_LOG("connection failed, err 0x%02x\n", err);
      if (bridge_adv_start() != 0)
        {
          g_bridge.state = AI_WATCH_BLE_BSP_INIT_FAILED;
        }

      return;
    }

  g_bridge.conn = bt_conn_ref(conn);
  g_bridge.state = AI_WATCH_BLE_BSP_CONNECTED;
  BRIDGE_LOG("connected\n");
}

static void bridge_disconnected(struct bt_conn *conn, uint8_t reason)
{
  BRIDGE_LOG("disconnected, reason 0x%02x\n", reason);

  sched_lock();
  if (g_bridge.conn != NULL)
    {
      bt_conn_unref(g_bridge.conn);
      g_bridge.conn = NULL;
    }

  /* When the radio is disabled through the app switch the state stays
   * OFF; otherwise it becomes DISCONNECTED and the application main
   * loop drives re-advertising through
   * ai_watch_ble_bsp_resume_advertising() (the RX context is not a
   * reliable place for the HCI command sequence, and BT_LE_ADV_CONN_*
   * includes the ONE_TIME flag, so the host does not auto-resume).
   */

  g_bridge.state = g_bridge.enabled ? AI_WATCH_BLE_BSP_DISCONNECTED
                                 : AI_WATCH_BLE_BSP_OFF;
  sched_unlock();
}

static struct bt_conn_cb g_bridge_conn_cbs =
{
  .connected = bridge_connected,
  .disconnected = bridge_disconnected,
};

static void bridge_conn_cb_register(void)
{
  static bool registered;

  if (!registered)
    {
      bt_conn_cb_register(&g_bridge_conn_cbs);
      registered = true;
    }
}

static void bridge_bt_ready(uint8_t dev_id, int err)
{
  int ret;

  (void)dev_id;

  if (err != 0)
    {
      BRIDGE_LOG("bt_enable failed: %d\n", err);
      g_bridge.state = AI_WATCH_BLE_BSP_INIT_FAILED;
      return;
    }

  bridge_conn_cb_register();

  ret = bt_gatt_service_register(&g_bridge_svc);
  if (ret != 0)
    {
      BRIDGE_LOG("service register failed: %d\n", ret);
      g_bridge.state = AI_WATCH_BLE_BSP_INIT_FAILED;
      return;
    }

  if (bridge_adv_start() != 0)
    {
      g_bridge.state = AI_WATCH_BLE_BSP_INIT_FAILED;
      return;
    }

  g_bridge.state = AI_WATCH_BLE_BSP_ADVERTISING;
  BRIDGE_LOG("ready, GATT service exposed\n");
}

/****************************************************************************
 * Public Functions - wrapper implementation (see ai_watch_ble_bsp.h)
 ****************************************************************************/

int ai_watch_ble_bsp_start(void)
{
  int err;

  if (g_bridge.started)
    {
      return 0;
    }

  g_bridge.started = true;
  g_bridge.enabled = true;

  /* Do NOT register conn callbacks here: in this zblue port
   * bt_conn_cb_register_mc() dereferences hdev->conn_ctx, which only
   * exists after bt_enable() has initialized the host. Registration
   * happens in bridge_bt_ready() instead.
   */

  /* bt_enable opens /dev/ttyHCI0 (CONFIG_BT_UART_ON_DEV_NAME), which
   * powers up the LCPU BLE controller on first open.
   */

  err = bt_enable(bridge_bt_ready);
  if (err != 0)
    {
      BRIDGE_LOG("bt_enable rejected: %d\n", err);
      g_bridge.state = AI_WATCH_BLE_BSP_INIT_FAILED;
      return err;
    }

  /* Completion is asynchronous: state flips in bridge_bt_ready() */

  return 0;
}

enum ai_watch_ble_bsp_state_e ai_watch_ble_bsp_get_state(void)
{
  return g_bridge.state;
}

bool ai_watch_ble_bsp_take_timesync(
    FAR struct ai_watch_ble_bsp_timesync_s *out)
{
  bool pending;

  sched_lock();
  pending = g_bridge.ts_pending;
  if (pending)
    {
      *out = g_bridge.ts;
      g_bridge.ts_pending = false;
    }
  sched_unlock();

  return pending;
}

size_t ai_watch_ble_bsp_take_command(FAR uint8_t *buf, size_t buflen)
{
  size_t copied = 0;

  sched_lock();
  if (g_bridge.cmdq_tail != g_bridge.cmdq_head)
    {
      uint8_t tail = g_bridge.cmdq_tail;

      copied = (g_bridge.cmdq_len[tail] <= buflen) ?
                g_bridge.cmdq_len[tail] : buflen;
      memcpy(buf, g_bridge.cmdq[tail], copied);
      memset(g_bridge.cmdq[tail], 0, sizeof(g_bridge.cmdq[tail]));
      g_bridge.cmdq_len[tail] = 0;
      g_bridge.cmdq_tail = (uint8_t)((tail + 1) %
                                     AI_WATCH_BLE_CMD_QUEUE_LEN);
    }
  sched_unlock();

  return copied;
}

int ai_watch_ble_bsp_notify(uint8_t chr, FAR const void *data,
                            uint16_t len)
{
  const struct bt_gatt_attr *attr;
  struct bt_conn *conn;
  int ret;

  if (chr != AI_WATCH_BLE_CHR_STATUS && chr != AI_WATCH_BLE_CHR_DATAUPLOAD)
    {
      return -EINVAL;
    }

  sched_lock();
  conn = (g_bridge.conn != NULL) ? bt_conn_ref(g_bridge.conn) : NULL;
  sched_unlock();

  if (conn == NULL)
    {
      return -ENOTCONN;
    }

  attr = (chr == AI_WATCH_BLE_CHR_STATUS) ?
          &g_bridge_svc.attrs[ATTR_IDX_STATUS_VAL] :
          &g_bridge_svc.attrs[ATTR_IDX_DATA_VAL];

  ret = bt_gatt_notify(conn, attr, data, len);
  bt_conn_unref(conn);

  if (ret == -EPERM || ret == -EACCES)
    {
      /* Peer has not subscribed to this CCC */

      return -EACCES;
    }

  return ret;
}

/****************************************************************************
 * Name: ai_watch_ble_bsp_resume_advertising
 *
 * Description:
 *   Re-enter connectable advertising after a link loss. Called from the
 *   application main loop (blocking HCI sequences are safe there);
 *   retries are expected until it succeeds. No-op unless the bridge is
 *   enabled, started and currently in the DISCONNECTED state.
 *
 ****************************************************************************/

void ai_watch_ble_bsp_resume_advertising(void)
{
  if (!g_bridge.enabled || !g_bridge.started ||
      g_bridge.state != AI_WATCH_BLE_BSP_DISCONNECTED)
    {
      return;
    }

  /* A ONE_TIME advertising set is stopped - but not deleted - when a
   * connection is accepted. bt_le_adv_start() then fails with -EALREADY
   * for as long as that stale set is registered (adv_create_legacy).
   * bt_le_adv_stop() takes the delete branch for a set that is no
   * longer advertising, so clear it before starting fresh.
   */

  bt_le_adv_stop();

  if (bridge_adv_start() == 0)
    {
      g_bridge.state = AI_WATCH_BLE_BSP_ADVERTISING;
    }
}

/****************************************************************************
 * Name: ai_watch_ble_bsp_set_enabled
 *
 * Description:
 *   Application-visible Bluetooth switch. Disabling stops advertising
 *   and terminates any active connection (radio goes quiet, state OFF);
 *   enabling brings advertising back (full host bring-up first if
 *   bsp_start() has never run). Must be called from the application
 *   thread - the stop path issues blocking HCI commands.
 *
 ****************************************************************************/

void ai_watch_ble_bsp_set_enabled(bool enabled)
{
  if (enabled)
    {
      g_bridge.enabled = true;

      if (!g_bridge.started)
        {
          ai_watch_ble_bsp_start();
          return;
        }

      if (g_bridge.state == AI_WATCH_BLE_BSP_OFF)
        {
          if (bridge_adv_start() == 0)
            {
              g_bridge.state = AI_WATCH_BLE_BSP_ADVERTISING;
            }
          else
            {
              g_bridge.state = AI_WATCH_BLE_BSP_INIT_FAILED;
            }
        }

      return;
    }

  g_bridge.enabled = false;

  /* Drop the connection first so the phone sees a clean disconnect,
   * then stop advertising. The disconnected callback keeps the state
   * at OFF because enabled is already false.
   */

  sched_lock();
  if (g_bridge.conn != NULL)
    {
      bt_conn_disconnect(g_bridge.conn, BT_HCI_ERR_LOCALHOST_TERM_CONN);
    }
  sched_unlock();

  bt_le_adv_stop();

  sched_lock();
  g_bridge.state = AI_WATCH_BLE_BSP_OFF;
  sched_unlock();
}
