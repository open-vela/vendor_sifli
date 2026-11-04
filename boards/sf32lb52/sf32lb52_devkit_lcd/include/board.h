/****************************************************************************
 * vendor/sifli/boards/sf32lb52_devkit_lcd/include/board.h
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

#ifndef __VENDOR_SIFLI_BOARDS_SF32LB52_DEVKIT_LCD_INCLUDE_BOARD_H
#define __VENDOR_SIFLI_BOARDS_SF32LB52_DEVKIT_LCD_INCLUDE_BOARD_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Buttons
 *
 * Two user keys, both active high (their pads default to pull-down):
 *   Key2 on PA11 -> bit 0
 *   Key1 on PA34 -> bit 1
 *
 * Key1 doubles as the pin the ROM UART download path samples, so its pad
 * configuration must stay at the reset default (see bsp_pinmux.c); reading
 * it as GPIO input at runtime does not disturb that.  Pin assignment and
 * active level match the xiaozhi-sf32 reference for this SoC family
 * (CONFIG_BSP_KEY1_PIN=34, CONFIG_BSP_KEY1_ACTIVE_HIGH=y).
 */

#define BUTTON_KEY2      0
#define BUTTON_KEY1      1
#define NUM_BUTTONS      2

#define BUTTON_KEY2_BIT  (1 << BUTTON_KEY2)
#define BUTTON_KEY1_BIT  (1 << BUTTON_KEY1)

#endif /* __VENDOR_SIFLI_BOARDS_SF32LB52_DEVKIT_LCD_INCLUDE_BOARD_H */

