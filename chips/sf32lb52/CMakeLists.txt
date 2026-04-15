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

target_sources(arch PRIVATE ${SRCS} ${HAL_SRCS} ${SYSTEM_SRCS})


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

# Suppress warnings for HAL driver files
target_compile_options(arch PRIVATE 
    $<$<COMPILE_LANGUAGE:C>:-w>
    $<$<COMPILE_LANGUAGE:CXX>:-w>)
