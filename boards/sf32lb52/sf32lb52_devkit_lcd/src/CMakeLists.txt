# ##############################################################################
# vendor/sifli/boards/sf32lb52_devkit_lcd/src/CMakeLists.txt
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

set(SRCS sifli_ap.c sifli_gpio.c sf32lb52_buttons.c bsp_init.c bsp_lcd_tp.c bsp_pinmux.c bsp_power.c )

nuttx_add_romfs(
  NAME
  etc
  MOUNTPOINT
  etc
  RCSRCS
  etc/init.d/rcS
  etc/init.d/rc.sysinit
  RCRAWS
  etc/group
  etc/1.txt
  PATH
  ${CMAKE_CURRENT_LIST_DIR}/etc)

target_link_libraries(board PRIVATE romfs_etc)

target_sources(board PRIVATE ${SRCS})

target_compile_definitions(board PRIVATE SOC_BF0_HCPU
                                         SF32LB52X
                                         HAL_TICK_PER_SECOND=1000
                                         USE_HAL_DRIVER)

target_include_directories(board PRIVATE ${NUTTX_BOARD_ABS_DIR}/../drivers/audio)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/Include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/Include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../external/CMSIS/Include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../boards/include/config/sf32lb52x)

target_compile_options(board PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-w>
    $<$<COMPILE_LANGUAGE:CXX>:-w>)

if(CONFIG_CRYPTO)
  nuttx_add_application(
    NAME
    md5_test
    STACKSIZE
    4096
    PRIORITY
    100
    SRCS
    md5_test.c)
endif()

if(CONFIG_SIFLI_AUDCODEC_TESTAPPS)
  # Play a .wav file: parse the header, then play over DMA
  nuttx_add_application(
    NAME
    wavplay
    STACKSIZE
    4096
    PRIORITY
    100
    SRCS
    wavplay_main.c
    INCLUDE_DIRECTORIES
    ${NUTTX_BOARD_ABS_DIR}/../drivers/audio)

  # Staged speaker bring-up tests for the on-chip AUDCODEC DAC
  nuttx_add_application(
    NAME
    audtest
    STACKSIZE
    4096
    PRIORITY
    100
    SRCS
    audtest_main.c
    INCLUDE_DIRECTORIES
    ${NUTTX_BOARD_ABS_DIR}/../drivers/audio)
endif()
