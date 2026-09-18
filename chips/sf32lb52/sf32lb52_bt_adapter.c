/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb52_bt_adapter.c
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
#include <string.h>
#include <unistd.h>
#include <syslog.h>

#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include "bf0_hal.h"
#include "circular_buf.h"
#include "ipc_hw.h"
#include "ipc_queue.h"
#include "sf32lb52_bt_adapter.h"

#define SF32LB52_BT_QID          0
#define SF32LB52_BT_TX_BUF_SIZE  HCPU2LCPU_MB_CH1_BUF_SIZE
#define SF32LB52_BT_TX_BUF_ADDR  HCPU2LCPU_MB_CH1_BUF_START_ADDR
#define SF32LB52_BT_TX_BUF_ALIAS HCPU_ADDR_2_LCPU_ADDR(HCPU2LCPU_MB_CH1_BUF_START_ADDR)
#define SF32LB52_BT_RX_BUF_ADDR  LCPU_ADDR_2_HCPU_ADDR(LCPU2HCPU_MB_CH1_BUF_START_ADDR)
#define SF32LB52_BT_RX_BUF_SIZE  LCPU2HCPU_MB_CH1_BUF_SIZE
#define SF32LB52_BT_RING_DATA_SIZE \
  ((SF32LB52_BT_RX_BUF_SIZE - sizeof(struct circular_buf)) & ~3UL)
#define SF32LB52_BT_NVDS_BUF_START 0x2040FE00
#define SF32LB52_BT_NVDS_BUF_SIZE  512
#define SF32LB52_BT_NVDS_PATTERN   0x4e564453
#define SF32LB52_BT_TRACE          0
#define SF32LB52_BT_H4_CMD         0x01

/* Set to 0 to compile out all Extended Advertising HCI diagnostics. */

#define OV_BLE_HCI_ADV_DIAG        1

#ifndef OV_BLE_HCI_TERM_RX_DIAG
#  define OV_BLE_HCI_TERM_RX_DIAG  0
#endif

#ifndef OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
#  define OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG 0
#endif

#define OV_BLE_HCI_EXT_ADV_DATA    0x2037
#define OV_BLE_HCI_EXT_SCAN_RSP    0x2038

static uint16_t sf32lb52_bt_diag_get_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool sf32lb52_bt_diag_opcode(uint16_t opcode)
{
  return opcode == OV_BLE_HCI_EXT_ADV_DATA ||
         opcode == OV_BLE_HCI_EXT_SCAN_RSP;
}

#if OV_BLE_HCI_ADV_DIAG
static void sf32lb52_bt_diag_rx_chunk(const uint8_t *data, size_t len,
                                     uint32_t rd_ptr, uint32_t wr_ptr)
{
  size_t offset = 0;

  while (offset + 3 <= len)
    {
      size_t packet_len;
      uint16_t opcode;
      uint8_t event;

      if (data[offset] != 0x04)
        {
          return;
        }

      event = data[offset + 1];
      packet_len = 3 + data[offset + 2];
      if (packet_len > len - offset)
        {
          return;
        }

      if (event == 0x0e && packet_len >= 7)
        {
          opcode = sf32lb52_bt_diag_get_le16(&data[offset + 4]);
        }
      else if (event == 0x0f && packet_len >= 7)
        {
          opcode = sf32lb52_bt_diag_get_le16(&data[offset + 5]);
        }
      else
        {
          offset += packet_len;
          continue;
        }

      if (sf32lb52_bt_diag_opcode(opcode))
        {
          syslog(LOG_INFO,
                 "ov_ble_hci_adv_diag adapter_rx mailbox=drained event=0x%02x len=%lu opcode=0x%04x rd=%08lx wr=%08lx\n",
                 event, (unsigned long)packet_len, opcode,
                 (unsigned long)rd_ptr, (unsigned long)wr_ptr);
        }

      offset += packet_len;
    }
}
#endif

#if OV_BLE_HCI_TERM_RX_DIAG
#  define OV_BLE_HCI_TERM_EVT_LEN  6

struct sf32lb52_bt_term_rx_diag_s
{
  uint8_t type;
  uint8_t header[4];
  uint8_t header_len;
  uint8_t header_need;
  uint8_t payload[OV_BLE_HCI_TERM_EVT_LEN];
  uint8_t payload_len;
  uint16_t payload_remaining;
  bool target;
  uint32_t sequence;
};

static void sf32lb52_bt_term_rx_diag_reset(
    struct sf32lb52_bt_term_rx_diag_s *diag)
{
  diag->type = 0;
  diag->header_len = 0;
  diag->header_need = 0;
  diag->payload_len = 0;
  diag->payload_remaining = 0;
  diag->target = false;
}

static uint8_t sf32lb52_bt_term_rx_diag_header_len(uint8_t type)
{
  switch (type)
    {
      case 0x01:
      case 0x03:
        return 3;

      case 0x02:
      case 0x05:
        return 4;

      case 0x04:
        return 2;

      default:
        return 0;
    }
}

static uint16_t sf32lb52_bt_term_rx_diag_payload_len(
    const struct sf32lb52_bt_term_rx_diag_s *diag)
{
  switch (diag->type)
    {
      case 0x01:
      case 0x03:
        return diag->header[2];

      case 0x02:
        return (uint16_t)diag->header[2] |
               ((uint16_t)diag->header[3] << 8);

      case 0x04:
        return diag->header[1];

      case 0x05:
        return ((uint16_t)diag->header[2] |
                ((uint16_t)diag->header[3] << 8)) & 0x3fff;

      default:
        return 0;
    }
}

static void sf32lb52_bt_term_rx_diag_chunk(
    struct sf32lb52_bt_term_rx_diag_s *diag,
    const uint8_t *data, size_t len, uint32_t rd_ptr, uint32_t wr_ptr,
    size_t read_len, uint32_t *first_sequence, uint32_t *event_count)
{
  size_t offset = 0;

  while (offset < len)
    {
      if (diag->type == 0)
        {
          diag->type = data[offset++];
          diag->header_need =
              sf32lb52_bt_term_rx_diag_header_len(diag->type);
          if (diag->header_need == 0)
            {
              sf32lb52_bt_term_rx_diag_reset(diag);
            }

          continue;
        }

      if (diag->header_len < diag->header_need)
        {
          diag->header[diag->header_len++] = data[offset++];
          if (diag->header_len < diag->header_need)
            {
              continue;
            }

          diag->payload_remaining =
              sf32lb52_bt_term_rx_diag_payload_len(diag);
          diag->target = diag->type == 0x04 &&
                         diag->header[0] == 0x3e &&
                         diag->header[1] == OV_BLE_HCI_TERM_EVT_LEN;
          if (diag->payload_remaining == 0)
            {
              sf32lb52_bt_term_rx_diag_reset(diag);
            }

          continue;
        }

      if (diag->target && diag->payload_len < sizeof(diag->payload))
        {
          diag->payload[diag->payload_len++] = data[offset];
        }

      offset++;
      diag->payload_remaining--;
      if (diag->payload_remaining == 0)
        {
          if (diag->target &&
              diag->payload_len == OV_BLE_HCI_TERM_EVT_LEN &&
              diag->payload[0] == 0x12)
            {
              uint16_t conn_handle =
                  (uint16_t)diag->payload[3] |
                  ((uint16_t)diag->payload[4] << 8);
              uint32_t sequence = ++diag->sequence;

              if (*event_count == 0)
                {
                  *first_sequence = sequence;
                }

              (*event_count)++;
              syslog(LOG_INFO,
                     "ov_ble_hci_term_rx_diag term_rx copied seq=%lu rd=%08lx wr=%08lx read_len=%lu status=0x%02x adv_handle=%u conn_handle=0x%04x count=%u\n",
                     (unsigned long)sequence, (unsigned long)rd_ptr,
                     (unsigned long)wr_ptr, (unsigned long)read_len,
                     diag->payload[1], diag->payload[2], conn_handle,
                     diag->payload[5]);
            }

          sf32lb52_bt_term_rx_diag_reset(diag);
        }
    }
}
#endif

typedef enum
{
  SF32LB52_BT_STATUS_IDLE = 0,
  SF32LB52_BT_STATUS_INITED,
  SF32LB52_BT_STATUS_ENABLED,
} sf32lb52_bt_status_t;

struct sf32lb52_bt_env_s
{
  ipc_queue_handle_t ipc_port;
  uint8_t data_buf[SF32LB52_BT_RX_BUF_SIZE];
  sf32lb52_bt_rx_callback_t notify_host;
  struct work_s rx_work;
  bool queue_open;
  bool wake_held;
  volatile bool rx_work_pending;
  bool rx_worker_running;
  uint32_t rx_read_idx_mirror;
  uint32_t rx_count;
#if OV_BLE_HCI_TERM_RX_DIAG
  struct sf32lb52_bt_term_rx_diag_s term_rx_diag;
#endif
};

static struct sf32lb52_bt_env_s g_sf32lb52_bt_env;
static sf32lb52_bt_status_t g_sf32lb52_bt_status = SF32LB52_BT_STATUS_IDLE;

#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
struct sf32lb52_bt_rx_snapshot_diag_s
{
  uint32_t mailbox_count;
  uint32_t worker_count;
  uint32_t drain_count;
  uint32_t copied_bytes;
  uint32_t callback_count;
  uint32_t sequence;
};

static struct sf32lb52_bt_rx_snapshot_diag_s g_sf32lb52_bt_rx_snapshot_diag;
#endif
static ipc_hw_q_handle_t g_sf32lb52_bt_tx_hw =
{
  .ch_id = SF32LB52_BT_QID / IPC_HW_QUEUE_NUM,
  .q_idx = SF32LB52_BT_QID % IPC_HW_QUEUE_NUM,
};

struct sf32lb52_bt_nvds_mem_init_s
{
  uint32_t pattern;
  uint16_t used_mem;
  uint16_t writting;
};

static const uint8_t g_sf32lb52_bt_nvds_default_rc10k[] =
{
  0x0d, 0x02, 0x64, 0x19, 0x12, 0x01, 0x01, 0x2f,
  0x04, 0x20, 0x00, 0x00, 0x00, 0x01, 0x06, 0x12,
  0x34, 0x56, 0x78, 0xab, 0xcd, 0x15, 0x01, 0x01
};

static const uint8_t g_sf32lb52_bt_nvds_default_lxt32k[] =
{
  0x2f, 0x04, 0x20, 0x00, 0x00, 0x00, 0x01, 0x06,
  0x12, 0x34, 0x56, 0x78, 0xab, 0xcd, 0x15, 0x01,
  0x01
};

extern uint8_t lcpu_power_on(void);
extern uint8_t lcpu_power_off(void);
extern void ipc_queue_data_ind(uint32_t user_data);

static void sf32lb52_bt_clean_nvds_shared(void)
{
  up_clean_dcache((uintptr_t)SF32LB52_BT_NVDS_BUF_START,
                  (uintptr_t)SF32LB52_BT_NVDS_BUF_START +
                  SF32LB52_BT_NVDS_BUF_SIZE);
}

static bool sf32lb52_bt_rx_ring_valid(struct circular_buf *rx_ring)
{
  uint32_t rd_ptr = rx_ring->read_idx_mirror;
  uint32_t wr_ptr = rx_ring->write_idx_mirror;
  uint32_t rd_idx = CB_GET_PTR_IDX(rd_ptr);
  uint32_t wr_idx = CB_GET_PTR_IDX(wr_ptr);
  long buf_size = rx_ring->buffer_size;

  return buf_size == (long)SF32LB52_BT_RING_DATA_SIZE &&
         rd_idx <= SF32LB52_BT_RING_DATA_SIZE &&
         wr_idx <= SF32LB52_BT_RING_DATA_SIZE;
}

static bool sf32lb52_bt_rx_ring_ready(const char *tag)
{
  struct circular_buf *rx_ring = (struct circular_buf *)SF32LB52_BT_RX_BUF_ADDR;
  uint32_t rd_ptr = rx_ring->read_idx_mirror;
  uint32_t wr_ptr = rx_ring->write_idx_mirror;
  long buf_size = rx_ring->buffer_size;

  if (!sf32lb52_bt_rx_ring_valid(rx_ring))
    {
      syslog(LOG_WARNING,
             "%s rx ring invalid: buf=%ld expected=%lu rd=%08lx wr=%08lx\n",
             tag,
             buf_size,
             (unsigned long)SF32LB52_BT_RING_DATA_SIZE,
             (unsigned long)rd_ptr,
             (unsigned long)wr_ptr);
      return false;
    }

  return true;
}

static int sf32lb52_bt_wait_rx_ring_ready(void)
{
  struct circular_buf *rx_ring =
      (struct circular_buf *)SF32LB52_BT_RX_BUF_ADDR;
  int i;

  for (i = 0; i < 1000; i++)
    {
      up_invalidate_dcache((uintptr_t)SF32LB52_BT_RX_BUF_ADDR,
                           (uintptr_t)SF32LB52_BT_RX_BUF_ADDR +
                           SF32LB52_BT_RX_BUF_SIZE);

      if (sf32lb52_bt_rx_ring_valid(rx_ring))
        {
          return OK;
        }

      usleep(1000);
    }

  sf32lb52_bt_rx_ring_ready("sf32lb52 wait");
  return -ETIMEDOUT;
}

static void sf32lb52_bt_prepare_stack_nvds(void)
{
  struct sf32lb52_bt_nvds_mem_init_s *nvds;
  const uint8_t *defaults;
  size_t defaults_len;

  if (HAL_LXT_DISABLED())
    {
      defaults = g_sf32lb52_bt_nvds_default_rc10k;
      defaults_len = sizeof(g_sf32lb52_bt_nvds_default_rc10k);
    }
  else
    {
      defaults = g_sf32lb52_bt_nvds_default_lxt32k;
      defaults_len = sizeof(g_sf32lb52_bt_nvds_default_lxt32k);
    }

  HAL_HPAON_WakeCore(CORE_ID_LCPU);

  nvds = (struct sf32lb52_bt_nvds_mem_init_s *)SF32LB52_BT_NVDS_BUF_START;
  memset((void *)SF32LB52_BT_NVDS_BUF_START, 0, SF32LB52_BT_NVDS_BUF_SIZE);
  nvds->pattern = SF32LB52_BT_NVDS_PATTERN;
  nvds->used_mem = defaults_len;
  nvds->writting = 0;
  memcpy((void *)(nvds + 1), defaults, defaults_len);
  sf32lb52_bt_clean_nvds_shared();

  HAL_HPAON_CANCEL_LP_ACTIVE_REQUEST();
}

static size_t sf32lb52_bt_ring_data_len(uint32_t rd_ptr, uint32_t wr_ptr,
                                        uint32_t buffer_size)
{
  uint32_t rd_idx = CB_GET_PTR_IDX(rd_ptr);
  uint32_t wr_idx = CB_GET_PTR_IDX(wr_ptr);
  uint32_t rd_mirror = CB_GET_PTR_MIRROR(rd_ptr);
  uint32_t wr_mirror = CB_GET_PTR_MIRROR(wr_ptr);

  if (rd_idx == wr_idx)
    {
      return rd_mirror == wr_mirror ? 0 : buffer_size;
    }

  if (wr_idx > rd_idx)
    {
      return wr_idx - rd_idx;
    }

  return buffer_size - (rd_idx - wr_idx);
}

static size_t sf32lb52_bt_ring_space_len(uint32_t rd_ptr, uint32_t wr_ptr,
                                         uint32_t buffer_size)
{
  return buffer_size - sf32lb52_bt_ring_data_len(rd_ptr, wr_ptr,
                                                buffer_size);
}

static uint32_t sf32lb52_bt_ring_advance(uint32_t ptr, size_t len,
                                         uint32_t buffer_size)
{
  uint32_t idx = CB_GET_PTR_IDX(ptr);
  uint32_t mirror = CB_GET_PTR_MIRROR(ptr);

  idx += len;
  if (idx >= buffer_size)
    {
      idx -= buffer_size;
      mirror = ~mirror;
    }

  return CB_MAKE_PTR_IDX_MIRROR(idx, mirror);
}

static size_t sf32lb52_bt_ring_copy(uint8_t *dst,
                                    const struct circular_buf *rx_ring,
                                    uint32_t rd_ptr,
                                    size_t len)
{
  const uint8_t *pool = (const uint8_t *)(rx_ring + 1);
  uint32_t rd_idx = CB_GET_PTR_IDX(rd_ptr);
  size_t tail;

  if (len == 0)
    {
      return 0;
    }

  tail = rx_ring->buffer_size - rd_idx;
  if (tail >= len)
    {
      memcpy(dst, &pool[rd_idx], len);
      return len;
    }

  memcpy(dst, &pool[rd_idx], tail);
  memcpy(&dst[tail], pool, len - tail);
  return len;
}

static size_t sf32lb52_bt_ring_write(struct circular_buf *tx_ring,
                                     const uint8_t *src, size_t len)
{
  uint8_t *pool = (uint8_t *)(tx_ring + 1);
  uint32_t wr_ptr = tx_ring->write_idx_mirror;
  uint32_t wr_idx = CB_GET_PTR_IDX(wr_ptr);
  size_t space;
  size_t tail;

  space = sf32lb52_bt_ring_space_len(tx_ring->read_idx_mirror,
                                     wr_ptr,
                                     tx_ring->buffer_size);
  if (space == 0)
    {
      return 0;
    }

  if (len > space)
    {
      len = space;
    }

  tail = tx_ring->buffer_size - wr_idx;
  if (tail >= len)
    {
      memcpy(&pool[wr_idx], src, len);
      up_clean_dcache((uintptr_t)&pool[wr_idx],
                      (uintptr_t)&pool[wr_idx] + len);
    }
  else
    {
      memcpy(&pool[wr_idx], src, tail);
      memcpy(pool, &src[tail], len - tail);
      up_clean_dcache((uintptr_t)&pool[wr_idx],
                      (uintptr_t)&pool[wr_idx] + tail);
      up_clean_dcache((uintptr_t)pool,
                      (uintptr_t)pool + len - tail);
    }

  tx_ring->write_idx_mirror = sf32lb52_bt_ring_advance(wr_ptr, len,
                                                       tx_ring->buffer_size);
  up_clean_dcache((uintptr_t)tx_ring,
                  (uintptr_t)tx_ring + sizeof(*tx_ring));
  __DSB();

  return len;
}

static size_t sf32lb52_bt_tx_pending(struct circular_buf *tx_ring,
                                     uint32_t *rd_ptr,
                                     uint32_t *wr_ptr)
{
  uint32_t rd;
  uint32_t wr;

  up_invalidate_dcache((uintptr_t)SF32LB52_BT_TX_BUF_ADDR,
                       (uintptr_t)SF32LB52_BT_TX_BUF_ADDR +
                       sizeof(*tx_ring));

  rd = tx_ring->read_idx_mirror;
  wr = tx_ring->write_idx_mirror;

  if (rd_ptr != NULL)
    {
      *rd_ptr = rd;
    }

  if (wr_ptr != NULL)
    {
      *wr_ptr = wr;
    }

  return sf32lb52_bt_ring_data_len(rd, wr, tx_ring->buffer_size);
}

static void sf32lb52_bt_trigger_tx(void)
{
  __DSB();
  ipc_hw_trigger_interrupt(&g_sf32lb52_bt_tx_hw);
}

static int sf32lb52_bt_wait_tx_idle(struct circular_buf *tx_ring)
{
  uint32_t start_time = HAL_GetTick();
  uint32_t tick_count = 0;
  uint32_t rd_ptr = 0;
  uint32_t wr_ptr = 0;

  while (sf32lb52_bt_tx_pending(tx_ring, &rd_ptr, &wr_ptr) > 0)
    {
      sf32lb52_bt_trigger_tx();

      if (HAL_GetTick() != start_time)
        {
          tick_count++;
          start_time = HAL_GetTick();
        }

      if (tick_count >= 100)
        {
          syslog(LOG_ERR,
                 "sf32lb52 bt tx busy: rd=%08lx wr=%08lx\n",
                 (unsigned long)rd_ptr,
                 (unsigned long)wr_ptr);
          return -ETIMEDOUT;
        }

      usleep(1000);
    }

  return OK;
}

#if SF32LB52_BT_TRACE
static void sf32lb52_bt_log_tx_state(struct circular_buf *tx_ring,
                                     const char *tag,
                                     uint32_t target_wr)
{
  uint32_t rd_ptr = 0;
  uint32_t wr_ptr = 0;
  size_t pending;

  pending = sf32lb52_bt_tx_pending(tx_ring, &rd_ptr, &wr_ptr);
  syslog(LOG_INFO,
         "sf32lb52 bt tx %s: target=%08lx rd=%08lx wr=%08lx pending=%lu\n",
         tag,
         (unsigned long)target_wr,
         (unsigned long)rd_ptr,
         (unsigned long)wr_ptr,
         (unsigned long)pending);
}
#endif

static size_t sf32lb52_bt_tx_chunk_len(const uint8_t *data,
                                       size_t len,
                                       size_t offset)
{
  size_t remaining = len - offset;

  if (offset == 0 && remaining > 1)
    {
      return 1;
    }

  return remaining;
}

static void sf32lb52_bt_rx_worker(FAR void *arg)
{
  struct sf32lb52_bt_env_s *env = arg;

#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
  irqstate_t diag_flags = enter_critical_section();
  g_sf32lb52_bt_rx_snapshot_diag.worker_count++;
  g_sf32lb52_bt_rx_snapshot_diag.sequence++;
  leave_critical_section(diag_flags);
#endif

  for (;;)
    {
      irqstate_t flags;
      int empty_retries = 0;

      for (;;)
        {
          size_t size;
          size_t read_len;
          int ret;
          struct circular_buf *rx_ring;
          uint32_t wr_ptr;
#if OV_BLE_HCI_TERM_RX_DIAG
          uint32_t term_first_sequence = 0;
          uint32_t term_event_count = 0;
#endif

          flags = enter_critical_section();

          if (!env->queue_open || env->ipc_port == IPC_QUEUE_INVALID_HANDLE)
            {
              env->rx_work_pending = false;
              env->rx_worker_running = false;
              leave_critical_section(flags);
              return;
            }

          leave_critical_section(flags);

          up_invalidate_dcache((uintptr_t)SF32LB52_BT_RX_BUF_ADDR,
                               (uintptr_t)SF32LB52_BT_RX_BUF_ADDR +
                               SF32LB52_BT_RX_BUF_SIZE);

          flags = enter_critical_section();

          if (!env->queue_open || env->ipc_port == IPC_QUEUE_INVALID_HANDLE)
            {
              env->rx_work_pending = false;
              env->rx_worker_running = false;
              leave_critical_section(flags);
              return;
            }

          rx_ring = (struct circular_buf *)SF32LB52_BT_RX_BUF_ADDR;
          wr_ptr = rx_ring->write_idx_mirror;
          size = sf32lb52_bt_ring_data_len(env->rx_read_idx_mirror,
                                          wr_ptr,
                                          rx_ring->buffer_size);
          if (size == 0)
            {
              uint32_t rd_ptr = rx_ring->read_idx_mirror;
              uint32_t local_rd = env->rx_read_idx_mirror;

              leave_critical_section(flags);

              if (empty_retries++ < 20)
                {
                  usleep(1000);
                  continue;
                }

          #if SF32LB52_BT_TRACE
              syslog(LOG_INFO,
                "sf32lb52 bt rx empty: local=%08lx rd=%08lx wr=%08lx\n",
                (unsigned long)local_rd,
                (unsigned long)rd_ptr,
                (unsigned long)wr_ptr);
          #endif
              break;
            }

          empty_retries = 0;

          if (size > sizeof(env->data_buf))
            {
              size = sizeof(env->data_buf);
            }

          read_len = sf32lb52_bt_ring_copy(env->data_buf, rx_ring,
                                           env->rx_read_idx_mirror, size);
          if (read_len == 0)
            {
              leave_critical_section(flags);
              break;
            }

#if OV_BLE_HCI_TERM_RX_DIAG
          sf32lb52_bt_term_rx_diag_chunk(&env->term_rx_diag,
                                         env->data_buf, read_len,
                                         env->rx_read_idx_mirror, wr_ptr,
                                         read_len, &term_first_sequence,
                                         &term_event_count);
#endif

          env->rx_read_idx_mirror = sf32lb52_bt_ring_advance(
              env->rx_read_idx_mirror, read_len, rx_ring->buffer_size);
          rx_ring->read_idx_mirror = env->rx_read_idx_mirror;
          __DSB();

          up_clean_dcache((uintptr_t)SF32LB52_BT_RX_BUF_ADDR,
                          (uintptr_t)SF32LB52_BT_RX_BUF_ADDR +
                          sizeof(*rx_ring));

          env->rx_count++;

#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
          g_sf32lb52_bt_rx_snapshot_diag.drain_count++;
          g_sf32lb52_bt_rx_snapshot_diag.copied_bytes += read_len;
          g_sf32lb52_bt_rx_snapshot_diag.sequence++;
#endif

          leave_critical_section(flags);

#if OV_BLE_HCI_ADV_DIAG
          sf32lb52_bt_diag_rx_chunk(env->data_buf, read_len,
                                    env->rx_read_idx_mirror, wr_ptr);
#endif

        #if SF32LB52_BT_TRACE
             syslog(LOG_INFO,
               "sf32lb52 bt rx drain: len=%lu local=%08lx wr=%08lx\n",
               (unsigned long)read_len,
               (unsigned long)env->rx_read_idx_mirror,
               (unsigned long)wr_ptr);
        #endif

          if (env->notify_host == NULL)
            {
              continue;
            }

#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
          flags = enter_critical_section();
          g_sf32lb52_bt_rx_snapshot_diag.callback_count++;
          g_sf32lb52_bt_rx_snapshot_diag.sequence++;
          leave_critical_section(flags);
#endif
          ret = env->notify_host(env->data_buf, read_len);
#if OV_BLE_HCI_TERM_RX_DIAG
          for (uint32_t i = 0; i < term_event_count; i++)
            {
              syslog(LOG_INFO,
                     "ov_ble_hci_term_rx_diag term_rx callback seq=%lu ret=%d\n",
                     (unsigned long)(term_first_sequence + i), ret);
            }
#endif
          if (ret < 0)
            {
              syslog(LOG_ERR, "sf32lb52 bt rx callback: %d\n", ret);
            }
        }

      flags = enter_critical_section();
      if (!env->rx_work_pending)
        {
          env->rx_worker_running = false;
          leave_critical_section(flags);
          break;
        }

      env->rx_work_pending = false;
      leave_critical_section(flags);
    }
}

static int32_t sf32lb52_bt_rx_ind(ipc_queue_handle_t handle, size_t size)
{
  struct sf32lb52_bt_env_s *env = &g_sf32lb52_bt_env;
  irqstate_t flags;
  bool queue_work;

  if (handle != env->ipc_port)
    {
      return -EINVAL;
    }

  if (!env->queue_open)
    {
      struct circular_buf *rx_ring = (struct circular_buf *)SF32LB52_BT_RX_BUF_ADDR;
      #if SF32LB52_BT_TRACE
            syslog(LOG_INFO, "sf32lb52 rx_ind (pre-open) wr=%08lx rd=%08lx\n",
              (unsigned long)rx_ring->write_idx_mirror,
              (unsigned long)rx_ring->read_idx_mirror);
      #endif
      return OK;
    }

  flags = enter_critical_section();
#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
  g_sf32lb52_bt_rx_snapshot_diag.mailbox_count++;
  g_sf32lb52_bt_rx_snapshot_diag.sequence++;
#endif
  env->rx_work_pending = true;
  queue_work = !env->rx_worker_running && work_available(&env->rx_work);
  if (queue_work)
    {
      env->rx_worker_running = true;
    }
  leave_critical_section(flags);

#if SF32LB52_BT_TRACE
  {
    struct circular_buf *rx_ring =
        (struct circular_buf *)SF32LB52_BT_RX_BUF_ADDR;

    up_invalidate_dcache((uintptr_t)SF32LB52_BT_RX_BUF_ADDR,
                         (uintptr_t)SF32LB52_BT_RX_BUF_ADDR +
                         sizeof(*rx_ring));

    syslog(LOG_INFO,
           "sf32lb52 rx_ind: size=%lu local=%08lx rd=%08lx wr=%08lx\n",
           (unsigned long)size,
           (unsigned long)env->rx_read_idx_mirror,
           (unsigned long)rx_ring->read_idx_mirror,
           (unsigned long)rx_ring->write_idx_mirror);
  }
#endif

  if (queue_work)
    {
      int ret;

      ret = work_queue(HPWORK, &env->rx_work, sf32lb52_bt_rx_worker,
                       env, 0);
      if (ret < 0)
        {
          flags = enter_critical_section();
          env->rx_worker_running = false;
          leave_critical_section(flags);

          syslog(LOG_ERR, "sf32lb52 bt queue rx work failed: %d\n", ret);
          return ret;
        }
    }

  return OK;
}

int sf32lb52_bt_rx_ring_snapshot(
    struct bt_hci_rx_ring_snapshot_s *snapshot)
{
#if OV_BLE_HCI_RX_RING_SNAPSHOT_DIAG
  struct circular_buf *rx_ring;
  irqstate_t flags;

  if (snapshot == NULL)
    {
      return -EINVAL;
    }

  if (up_interrupt_context())
    {
      return -EPERM;
    }

  up_invalidate_dcache((uintptr_t)SF32LB52_BT_RX_BUF_ADDR,
                       (uintptr_t)SF32LB52_BT_RX_BUF_ADDR +
                       sizeof(struct circular_buf));

  flags = enter_critical_section();
  if (g_sf32lb52_bt_status != SF32LB52_BT_STATUS_ENABLED ||
      !g_sf32lb52_bt_env.queue_open)
    {
      leave_critical_section(flags);
      return -ENODEV;
    }

  rx_ring = (struct circular_buf *)SF32LB52_BT_RX_BUF_ADDR;
  if (!sf32lb52_bt_rx_ring_valid(rx_ring))
    {
      leave_critical_section(flags);
      return -EIO;
    }

  snapshot->ring_read_mirror = rx_ring->read_idx_mirror;
  snapshot->ring_write_mirror = rx_ring->write_idx_mirror;
  snapshot->local_read_mirror = g_sf32lb52_bt_env.rx_read_idx_mirror;
  snapshot->mailbox_count = g_sf32lb52_bt_rx_snapshot_diag.mailbox_count;
  snapshot->worker_count = g_sf32lb52_bt_rx_snapshot_diag.worker_count;
  snapshot->drain_count = g_sf32lb52_bt_rx_snapshot_diag.drain_count;
  snapshot->copied_bytes = g_sf32lb52_bt_rx_snapshot_diag.copied_bytes;
  snapshot->callback_count = g_sf32lb52_bt_rx_snapshot_diag.callback_count;
  snapshot->complete_h4_count = 0;
  snapshot->forwarded_h4_count = 0;
  snapshot->sequence = g_sf32lb52_bt_rx_snapshot_diag.sequence;
  snapshot->last_opcode = 0;
  snapshot->last_h4_type = 0;
  snapshot->last_event = 0;
  leave_critical_section(flags);
  return OK;
#else
  return -ENOSYS;
#endif
}

static int sf32lb52_bt_mailbox_init(void)
{
  struct sf32lb52_bt_env_s *env = &g_sf32lb52_bt_env;
  ipc_queue_cfg_t q_cfg;

  memset(&q_cfg, 0, sizeof(q_cfg));
  q_cfg.qid = SF32LB52_BT_QID;
  q_cfg.tx_buf_size = SF32LB52_BT_TX_BUF_SIZE;
  q_cfg.tx_buf_addr = SF32LB52_BT_TX_BUF_ADDR;
  q_cfg.tx_buf_addr_alias = SF32LB52_BT_TX_BUF_ALIAS;
  q_cfg.rx_buf_addr = SF32LB52_BT_RX_BUF_ADDR;
  q_cfg.rx_ind = sf32lb52_bt_rx_ind;

  env->ipc_port = ipc_queue_init(&q_cfg);
  if (env->ipc_port == IPC_QUEUE_INVALID_HANDLE)
    {
      return -ENODEV;
    }

  return OK;
}

int sf32lb52_bt_controller_init(void)
{
  int ret;

  if (g_sf32lb52_bt_status != SF32LB52_BT_STATUS_IDLE)
    {
      return OK;
    }

  memset(&g_sf32lb52_bt_env, 0, sizeof(g_sf32lb52_bt_env));
  ret = sf32lb52_bt_mailbox_init();
  if (ret < 0)
    {
      return ret;
    }

  g_sf32lb52_bt_status = SF32LB52_BT_STATUS_INITED;
  return OK;
}

int sf32lb52_bt_controller_deinit(void)
{
  int ret;

  if (g_sf32lb52_bt_status == SF32LB52_BT_STATUS_ENABLED)
    {
      ret = sf32lb52_bt_controller_disable();
      if (ret < 0)
        {
          return ret;
        }
    }

  if (g_sf32lb52_bt_status != SF32LB52_BT_STATUS_INITED)
    {
      return -EPERM;
    }

  g_sf32lb52_bt_env.queue_open = false;
  g_sf32lb52_bt_env.rx_work_pending = false;
  g_sf32lb52_bt_env.rx_worker_running = false;
  if (!work_available(&g_sf32lb52_bt_env.rx_work))
    {
      ret = work_cancel_sync(HPWORK, &g_sf32lb52_bt_env.rx_work);
      if (ret < 0 && ret != -ENOENT)
        {
          return ret;
        }
    }

  ret = ipc_queue_deinit(g_sf32lb52_bt_env.ipc_port);
  if (ret < 0)
    {
      return ret;
    }

  memset(&g_sf32lb52_bt_env, 0, sizeof(g_sf32lb52_bt_env));
  g_sf32lb52_bt_env.ipc_port = IPC_QUEUE_INVALID_HANDLE;
  g_sf32lb52_bt_status = SF32LB52_BT_STATUS_IDLE;
  return OK;
}

int sf32lb52_hci_register_callback(sf32lb52_bt_rx_callback_t callback)
{
  if (callback == NULL)
    {
      return -EINVAL;
    }

  if (g_sf32lb52_bt_status == SF32LB52_BT_STATUS_IDLE)
    {
      return -EPERM;
    }

  g_sf32lb52_bt_env.notify_host = callback;
  return OK;
}

int sf32lb52_bt_controller_enable(void)
{
  int ret;

  if (g_sf32lb52_bt_status == SF32LB52_BT_STATUS_ENABLED)
    {
      return OK;
    }

  if (g_sf32lb52_bt_status != SF32LB52_BT_STATUS_INITED)
    {
      return -EPERM;
    }

  sf32lb52_bt_prepare_stack_nvds();
  HAL_LCPU_ASSERT_INFO_clear();

  ret = lcpu_power_on();
  if (ret != 0)
    {
      return -EIO;
    }

  /* The L2H_MAILBOX (0x40002000) is on the LPSYS APB bus.  Its CxIER
   * register is only writable when the LPSYS bus bridge is active.
   * Wake the LCPU first so the bus bridge is powered, THEN open the
   * IPC queue (which writes CxIER to unmask the mailbox RX interrupt).
   * The SDK reference ipc_hw_enable_interrupt2() has the same
   * HAL_HPAON_WakeCore() before the UNMASK call.
   */

  HAL_HPAON_WakeCore(CORE_ID_LCPU);
  g_sf32lb52_bt_env.wake_held = true;

  usleep(500000);

  ret = ipc_queue_open(g_sf32lb52_bt_env.ipc_port);
  if (ret < 0)
    {
      HAL_HPAON_CANCEL_LP_ACTIVE_REQUEST();
      g_sf32lb52_bt_env.wake_held = false;
      return ret;
    }

  /* Flush H2L TX ring to SRAM immediately after ipc_queue_open so
   * LCPU sees the reset indices (read_idx=write_idx=0) before it
   * tries to process any stale data from a previous session.
   */
  up_clean_dcache((uintptr_t)SF32LB52_BT_TX_BUF_ADDR,
                  (uintptr_t)SF32LB52_BT_TX_BUF_ADDR +
                  SF32LB52_BT_TX_BUF_SIZE);
  __DSB();

  /* After every lcpu_power_on(), LCPU resets its TX ring write pointer
   * (write_idx_mirror) to 0 and writes its own boot responses into the
   * ring.  HCPU's read pointer (read_idx_mirror) may still hold the
   * non-zero value from the previous session -- dirty in DCache or
   * stored in SRAM.  If we do not synchronise them:
   *
   *   circular_buf_data_len() = wrap(write_idx - read_idx)
   *
   * returns a large garbage value, and LCPU's fresh boot events
   * plus ring garbage all replay as "new" HCI responses.  These
   * ghost events are consumed by the host as replies to real commands,
   * so HCI_Reset (0x0c01) is never actually executed by the controller,
   * and bt_le_adv_start later gets "Command Disallowed" (0x07).
   *
   * Fix: flush DCache to write back any dirty read_idx and to load
   * LCPU's current write_idx from SRAM, then advance read_idx to
   * write_idx (discard ALL pending LCPU boot data), then clean the
   * DCache so LCPU sees the updated read pointer in SRAM.
   */

  {
    struct circular_buf *rx_ring =
        (struct circular_buf *)SF32LB52_BT_RX_BUF_ADDR;

    ret = sf32lb52_bt_wait_rx_ring_ready();
    if (ret < 0)
      {
        syslog(LOG_ERR, "sf32lb52 rx ring not ready: %d\n", ret);
        ipc_queue_close(g_sf32lb52_bt_env.ipc_port);
        HAL_HPAON_CANCEL_LP_ACTIVE_REQUEST();
        g_sf32lb52_bt_env.wake_held = false;
        return ret;
      }

    rx_ring->read_idx_mirror = rx_ring->write_idx_mirror;
    g_sf32lb52_bt_env.rx_read_idx_mirror = rx_ring->write_idx_mirror;

    up_clean_dcache((uintptr_t)SF32LB52_BT_RX_BUF_ADDR,
                    (uintptr_t)SF32LB52_BT_RX_BUF_ADDR +
                    sizeof(*rx_ring));
    __DSB();
  }

  {
    volatile MAILBOX_CH_TypeDef *l2h = L2H_MAILBOX;
    uint32_t nvic_bit = (1UL << (LCPU2HCPU_IRQn & 0x1F));
    uint32_t nvic_en  = NVIC->ISER[LCPU2HCPU_IRQn >> 5];

    (void)l2h;
    (void)nvic_bit;
    (void)nvic_en;
  }

  g_sf32lb52_bt_env.queue_open = true;
  g_sf32lb52_bt_status = SF32LB52_BT_STATUS_ENABLED;

  return OK;
}

int sf32lb52_bt_controller_disable(void)
{
  int ret = OK;
  int tmpret;
  bool queue_open;

  if (g_sf32lb52_bt_status != SF32LB52_BT_STATUS_ENABLED)
    {
      return -EPERM;
    }

  queue_open = g_sf32lb52_bt_env.queue_open;
  g_sf32lb52_bt_env.queue_open = false;
  g_sf32lb52_bt_env.rx_work_pending = false;
  g_sf32lb52_bt_env.rx_worker_running = false;
  if (!work_available(&g_sf32lb52_bt_env.rx_work))
    {
      tmpret = work_cancel_sync(HPWORK, &g_sf32lb52_bt_env.rx_work);
      if (tmpret < 0 && tmpret != -ENOENT && ret == OK)
        {
          ret = tmpret;
        }
    }

  if (queue_open)
    {
      tmpret = ipc_queue_close(g_sf32lb52_bt_env.ipc_port);
      if (tmpret < 0 && ret == OK)
        {
          ret = tmpret;
        }
    }

  if (g_sf32lb52_bt_env.wake_held)
    {
      HAL_HPAON_CANCEL_LP_ACTIVE_REQUEST();
      g_sf32lb52_bt_env.wake_held = false;
    }

  tmpret = lcpu_power_off();
  if (tmpret != 0 && ret == OK)
    {
      ret = -EIO;
    }

  g_sf32lb52_bt_env.notify_host = NULL;
  g_sf32lb52_bt_status = SF32LB52_BT_STATUS_INITED;
  return ret;
}

int sf32lb52_host_send_packet(const uint8_t *data, uint16_t len)
{
  struct circular_buf *tx_ring =
      (struct circular_buf *)SF32LB52_BT_TX_BUF_ADDR;
  uint32_t start_time;
  uint32_t tick_count;
  size_t written;
  size_t remaining;
  size_t offset;
  size_t chunk;
  size_t chunks;
  uint32_t wr_ptr;
  int ret;
#if OV_BLE_HCI_ADV_DIAG
  uint16_t diag_opcode = 0;
  bool diag_command = false;
#endif

  if (data == NULL || len == 0)
    {
      return -EINVAL;
    }

  if (g_sf32lb52_bt_status != SF32LB52_BT_STATUS_ENABLED ||
      !g_sf32lb52_bt_env.queue_open)
    {
      return -ENODEV;
    }

#if OV_BLE_HCI_ADV_DIAG
  if (len >= 4 && data[0] == SF32LB52_BT_H4_CMD)
    {
      diag_opcode = sf32lb52_bt_diag_get_le16(&data[1]);
      diag_command = sf32lb52_bt_diag_opcode(diag_opcode);
    }
#endif

  offset = 0;
  remaining = len;
  start_time = HAL_GetTick();
  tick_count = 0;
  chunks = 0;
  wr_ptr = 0;

  ret = sf32lb52_bt_wait_tx_idle(tx_ring);
  if (ret < 0)
    {
      return ret;
    }

  while (remaining > 0)
    {
      irqstate_t flags;

      chunk = sf32lb52_bt_tx_chunk_len(data, len, offset);

      up_invalidate_dcache((uintptr_t)SF32LB52_BT_TX_BUF_ADDR,
                           (uintptr_t)SF32LB52_BT_TX_BUF_ADDR +
                           sizeof(*tx_ring));

      flags = enter_critical_section();
      written = sf32lb52_bt_ring_write(tx_ring, data + offset, chunk);
      leave_critical_section(flags);

      if (written == 0)
        {
          if (HAL_GetTick() != start_time)
            {
              tick_count++;
              start_time = HAL_GetTick();
            }

          if (tick_count >= 10)
            {
              syslog(LOG_ERR,
                     "sf32lb52 bt tx timeout: remaining=%lu\n",
                     (unsigned long)remaining);
              return -ETIMEDOUT;
            }

          continue;
        }

      offset += written;
      remaining -= written;

      __DSB();
      wr_ptr = tx_ring->write_idx_mirror;
      sf32lb52_bt_trigger_tx();
      chunks++;
    }

#if SF32LB52_BT_TRACE
  syslog(LOG_INFO,
         "sf32lb52 bt tx publish: total=%u chunks=%lu wr=%08lx first=%02x\n",
         len,
         (unsigned long)chunks,
         (unsigned long)wr_ptr,
         data[0]);

  sf32lb52_bt_log_tx_state(tx_ring, "queued", wr_ptr);
#endif

  if (data[0] == SF32LB52_BT_H4_CMD)
    {
#if OV_BLE_HCI_ADV_DIAG
      if (diag_command)
        {
          uint32_t rd_diag;
          uint32_t wr_diag;

          rd_diag = tx_ring->read_idx_mirror;
          wr_diag = tx_ring->write_idx_mirror;
          syslog(LOG_INFO,
                 "ov_ble_hci_adv_diag adapter_tx opcode=0x%04x h4_len=%u phase=published rd=%08lx wr=%08lx result=0\n",
                 diag_opcode, len, (unsigned long)rd_diag,
                 (unsigned long)wr_diag);
        }
#endif
      ret = sf32lb52_bt_wait_tx_idle(tx_ring);
#if OV_BLE_HCI_ADV_DIAG
      if (diag_command)
        {
          uint32_t rd_diag;
          uint32_t wr_diag;

          rd_diag = tx_ring->read_idx_mirror;
          wr_diag = tx_ring->write_idx_mirror;
          syslog(LOG_INFO,
                 "ov_ble_hci_adv_diag adapter_tx opcode=0x%04x h4_len=%u phase=consumed rd=%08lx wr=%08lx result=%d\n",
                 diag_opcode, len, (unsigned long)rd_diag,
                 (unsigned long)wr_diag, ret);
        }
#endif
      if (ret < 0)
        {
          return ret;
        }
    }

  return OK;
}
