# ##############################################################################
# vendor/sifli/chip/sf32lb52/src/CMakeLists.txt
#
# Licensed to the Apache Software Foundation (ASF) under one or more contributor
# license agreements.  See the NOTICE file distributed with this work for
# additional information regarding copyright ownership.  The ASF licenses this
# file to you under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License.  You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations under
# the License.
#
# ##############################################################################

set(SRCS sifli_start.c sifli_irq.c sifli_uart.c sifli_lowput.c
         sifli_oneshot.c sifli_allocateheap.c sifli_gpio.c sifli_i2c.c
         sifli_spi.c)

if(CONFIG_ADC)
    list(APPEND SRCS sf32lb_adc.c)
endif()

if(CONFIG_RTC)
    list(APPEND SRCS sf32lb_rtc.c)
endif()

if(CONFIG_WATCHDOG)
    list(APPEND SRCS sf32lb_iwdg.c)
endif()

if(CONFIG_MTD)
    list(APPEND SRCS sf32lb_flash.c)
endif()

if(CONFIG_TIMER)
    list(APPEND SRCS sf32lb_timer.c)
endif()

if(CONFIG_PWM)
    list(APPEND SRCS sf32lb_pwm.c)
endif()

if(CONFIG_SF32LB52_USBDEV)
    list(APPEND SRCS sf32lb_usbdev.c)
endif()

set(IPC_QUEUE_SRCS
    ${NUTTX_CHIP_ABS_DIR}/ipc_queue/common/circular_buf.c
    ${NUTTX_CHIP_ABS_DIR}/ipc_queue/common/ipc_hw.c
    ${NUTTX_CHIP_ABS_DIR}/ipc_queue/common/ipc_queue.c
    ${NUTTX_CHIP_ABS_DIR}/ipc_queue/porting/sf32lb52x/hcpu/ipc_hw_port.c)

set(LCPU_BT_SRCS)

if(CONFIG_UART_BTH4)
    list(APPEND LCPU_BT_SRCS
         ${NUTTX_CHIP_ABS_DIR}/sf32lb52_bt_adapter.c
         ${NUTTX_CHIP_ABS_DIR}/sf32lb52_bth4.c
         ${NUTTX_CHIP_ABS_DIR}/sf32lb52_lcpu_boot.c
         ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x/lcpu_patch.c
         ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x/lcpu_patch_rev_b.c
         ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x/bt_rf_fulcal.c)
endif()

# Add all HAL source files
file(GLOB HAL_SRCS ${NUTTX_CHIP_ABS_DIR}/../drivers/hal/*.c)

# For SF32LB52X, exclude certain files based on SConscript rules
list(REMOVE_ITEM HAL_SRCS 
    ${NUTTX_CHIP_ABS_DIR}/../drivers/hal/bf0_hal_audcodec.c
    ${NUTTX_CHIP_ABS_DIR}/../drivers/hal/bf0_hal_qspi.c
    ${NUTTX_CHIP_ABS_DIR}/../drivers/hal/bf0_hal_qspi_ex.c)

# Add system initialization file
set(SYSTEM_SRCS ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x/Templates/system_bf0_ap.c
                ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x/bf0_pin_const.c
                ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x/lcpu_config_type.c)

target_sources(arch PRIVATE ${SRCS} ${HAL_SRCS} ${SYSTEM_SRCS} ${IPC_QUEUE_SRCS} ${LCPU_BT_SRCS})

# AI Watch custom GATT bridge (CONFIG_AI_WATCH_BLE_BRIDGE).
#
# This is the only translation unit outside apps/external/zblue that
# compiles against zblue headers. It stays narrow on purpose:
#   - include dirs are attached to this single source file, not the
#     whole arch target (via the per-source INCLUDE_DIRECTORIES property)
#   - the wrapper header ai_watch_ble_bsp.h lives in the contest app
#     directory, reached through the apps/packages symlink
set(AI_WATCH_BLE_BRIDGE_SRC ${NUTTX_CHIP_ABS_DIR}/sf32lb52_ble_bridge.c)
set(AI_WATCH_BLE_APP_DIR ${NUTTX_APPS_DIR}/packages/demos/contest2026_403_ai_watch)
if(CONFIG_AI_WATCH_BLE_BRIDGE AND EXISTS ${AI_WATCH_BLE_BRIDGE_SRC}
   AND EXISTS ${AI_WATCH_BLE_APP_DIR}/ai_watch_ble_bsp.h)
  # Mirror the include list of the zblue_headers INTERFACE target
  # (apps/external/zblue/CMakeLists.default.txt). Keep in sync.
  set(AI_WATCH_BLE_ZBLUE_DIR ${NUTTX_APPS_DIR}/external/zblue)
  set(AI_WATCH_BLE_ZBLUE_BIN ${CMAKE_BINARY_DIR}/apps/external/zblue)
  nuttx_add_library(aiwatch_ble_bridge STATIC)
  target_sources(aiwatch_ble_bridge PRIVATE ${AI_WATCH_BLE_BRIDGE_SRC})
  target_include_directories(aiwatch_ble_bridge PRIVATE
    ${AI_WATCH_BLE_ZBLUE_DIR}/zblue/port/include
    ${AI_WATCH_BLE_ZBLUE_DIR}/zblue/port/kernel/include
    ${AI_WATCH_BLE_ZBLUE_BIN}/include
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/sys
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/kernel
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/arch
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/linker
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/devicetree
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/debug
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/fs
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/shell
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/tracing
    ${AI_WATCH_BLE_ZBLUE_BIN}/include/zephyr/app_memory
    ${AI_WATCH_BLE_ZBLUE_DIR}/zblue/include
    ${AI_WATCH_BLE_ZBLUE_DIR}/zblue/subsys/bluetooth
    ${AI_WATCH_BLE_ZBLUE_DIR}/zblue/subsys/bluetooth/host
    ${AI_WATCH_BLE_ZBLUE_DIR}/zblue/subsys/settings/include
    ${AI_WATCH_BLE_ZBLUE_DIR}/zblue/subsys/settings/include/settings
    ${AI_WATCH_BLE_APP_DIR})
  add_dependencies(aiwatch_ble_bridge nuttx_context)
elseif(CONFIG_AI_WATCH_BLE_BRIDGE)
  message(WARNING
    "CONFIG_AI_WATCH_BLE_BRIDGE is set but the bridge source or the AI Watch "
    "app directory was not found:\n"
    "  bridge src: ${AI_WATCH_BLE_BRIDGE_SRC}\n"
    "  app dir:    ${AI_WATCH_BLE_APP_DIR}")
endif()



target_compile_definitions(arch PRIVATE SOC_BF0_HCPU 
                                        SF32LB52X
                                        HAL_TICK_PER_SECOND=1000
                                        USE_HAL_DRIVER
                                        USE_SERIALDRIVER)

target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/Include)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/Include)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/../external/CMSIS/Include)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/../boards/include/config/sf32lb52x)
target_include_directories(arch PRIVATE ${NUTTX_BOARD_ABS_DIR}/include)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue/common)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue/porting/os/vela)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue/porting/sf32lb52x/hcpu)

# Suppress warnings for HAL driver files
target_compile_options(arch PRIVATE 
    $<$<COMPILE_LANGUAGE:C>:-w>
    $<$<COMPILE_LANGUAGE:CXX>:-w>)
