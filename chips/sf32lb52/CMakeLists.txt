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
         sifli_spi.c sf32lb52_epic.c)

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

add_library(sifli_hal INTERFACE)
target_compile_definitions(sifli_hal INTERFACE
                           SOC_BF0_HCPU
                           SF32LB52X
                           HAL_TICK_PER_SECOND=1000
                           USE_HAL_DRIVER)
target_include_directories(sifli_hal INTERFACE
                           ${NUTTX_CHIP_ABS_DIR}/include
                           ${NUTTX_CHIP_ABS_DIR}/../drivers/Include
                           ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/Include
                           ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x
                           ${NUTTX_CHIP_ABS_DIR}/../external/CMSIS/Include
                           ${NUTTX_CHIP_ABS_DIR}/../boards/include/config/sf32lb52x)

target_sources(arch PRIVATE ${SRCS} ${HAL_SRCS} ${SYSTEM_SRCS} ${IPC_QUEUE_SRCS} ${LCPU_BT_SRCS})
target_link_libraries(arch PRIVATE sifli_hal)
target_compile_definitions(arch PRIVATE USE_SERIALDRIVER)
target_include_directories(arch PRIVATE ${NUTTX_BOARD_ABS_DIR}/include)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue/common)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue/porting/os/vela)
target_include_directories(arch PRIVATE ${NUTTX_CHIP_ABS_DIR}/ipc_queue/porting/sf32lb52x/hcpu)

# Suppress warnings for HAL driver files
target_compile_options(arch PRIVATE 
    $<$<COMPILE_LANGUAGE:C>:-w>
    $<$<COMPILE_LANGUAGE:CXX>:-w>)
