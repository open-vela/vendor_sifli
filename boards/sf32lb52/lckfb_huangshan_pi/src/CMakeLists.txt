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

  # system.router / system.app are provided ONLY by the prebuilt
  # libquickappfeatures.a (there is no source under
  # frameworks/runtimes/quickapp/modules/{router,app} in this workspace), so the
  # auto-generated feature registry (jidl_generated/features_registry_table.h)
  # never lists them and the feature manager never registers them.  Append their
  # initFeature entries to the generated registry here; referencing the symbols
  # also makes the linker pull the corresponding objects out of the prebuilt lib.
  set(_QAPP_REG_DIR ${CMAKE_BINARY_DIR}/jidl_generated)
  foreach(_qf system_router system_app)
    set(_qf_stamp ${_QAPP_REG_DIR}/feature_${_qf}.pdat)
    add_custom_command(
      OUTPUT ${_qf_stamp}
      COMMAND echo "bool jse_${_qf}_initFeature(FeatureRegistryHandle handle);" >>
              ${_QAPP_REG_DIR}/features_registry_list.h
      COMMAND echo "jse_${_qf}_initFeature," >>
              ${_QAPP_REG_DIR}/features_registry_table.h
      COMMAND touch ${_qf_stamp}
      VERBATIM
      COMMENT "register prebuilt quickapp feature: ${_qf}")
    add_custom_target(quickapp_prebuilt_feature_${_qf} DEPENDS ${_qf_stamp})
    add_dependencies(apps_context quickapp_prebuilt_feature_${_qf})
  endforeach()
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
