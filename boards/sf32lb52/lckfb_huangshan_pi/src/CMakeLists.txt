# ##############################################################################
# vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/src/CMakeLists.txt
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

# Collect all HAP app and font files for romfs
file(GLOB_RECURSE _HAP_ABS ${CMAKE_CURRENT_LIST_DIR}/etc/data/app/* ${CMAKE_CURRENT_LIST_DIR}/etc/data/font/*)
set(HAP_FILES)
foreach(_f ${_HAP_ABS})
  file(RELATIVE_PATH _rel ${CMAKE_CURRENT_LIST_DIR} ${_f})
  list(APPEND HAP_FILES ${_rel})
endforeach()

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
  ${HAP_FILES}
  PATH
  ${CMAKE_CURRENT_LIST_DIR}/etc)

target_link_libraries(board PRIVATE romfs_etc)

target_sources(board PRIVATE ${SRCS})

target_compile_definitions(board PRIVATE SOC_BF0_HCPU
                                         SF32LB52X
                                         HAL_TICK_PER_SECOND=1000
                                         USE_HAL_DRIVER)

target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/Include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/Include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../drivers/cmsis/sf32lb52x)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../external/CMSIS/Include)
target_include_directories(board PRIVATE ${NUTTX_CHIP_ABS_DIR}/../boards/include/config/sf32lb52x)

target_compile_options(board PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-w>
    $<$<COMPILE_LANGUAGE:CXX>:-w>)

# QuickApp prebuilt libraries
if(CONFIG_QUICKAPP)
  file(GLOB QUICKAPP_LIBS "${NUTTX_BOARD_ABS_DIR}/../libs/nsh_cmake/*.a")
  if(QUICKAPP_LIBS)
    nuttx_add_extra_library(${QUICKAPP_LIBS})
  endif()
endif()

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
