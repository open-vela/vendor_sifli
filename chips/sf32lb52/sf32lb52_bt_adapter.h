/****************************************************************************
 * vendor/sifli/chips/sf32lb52/sf32lb52_bt_adapter.h
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

#ifndef __VENDOR_SIFLI_CHIPS_SF32LB52_SF32LB52_BT_ADAPTER_H
#define __VENDOR_SIFLI_CHIPS_SF32LB52_SF32LB52_BT_ADAPTER_H

#include <stdint.h>

typedef int (*sf32lb52_bt_rx_callback_t)(uint8_t *data, uint16_t len);

int sf32lb52_bt_controller_init(void);
int sf32lb52_bt_controller_deinit(void);
int sf32lb52_bt_controller_enable(void);
int sf32lb52_bt_controller_disable(void);
int sf32lb52_hci_register_callback(sf32lb52_bt_rx_callback_t callback);
int sf32lb52_host_send_packet(const uint8_t *data, uint16_t len);

#endif
