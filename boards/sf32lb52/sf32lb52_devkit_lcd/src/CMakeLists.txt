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
  # PATH is what genromfs actually packs. It has to be the binary dir, not
  # ${CMAKE_CURRENT_LIST_DIR}: RCSRCS runs each script through "gcc -E -P -x c"
  # into ${CMAKE_CURRENT_BINARY_DIR}/etc and RCRAWS copies the rest there, so
  # pointing PATH at the source tree threw the preprocessed output away and
  # packed the raw files instead (nuttx/cmake/nuttx_add_romfs.cmake:184-189).
  #
  # That made the preprocessing step a no-op and turned comments in rcS into a
  # trap with two different failure modes: a '#' comment is an invalid
  # preprocessing directive and breaks the build, while a /* */ comment
  # survives into the packed file and NSH tries to run it as a command - which
  # aborts the rest of the script, because nsh_script.c stops on first error.
  # Every file listed above is either an RCSRC or an RCRAW, so the packed /etc
  # has the same contents either way.
  PATH
  ${CMAKE_CURRENT_BINARY_DIR}/etc)

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
