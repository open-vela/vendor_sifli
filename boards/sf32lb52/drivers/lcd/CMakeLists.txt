# ##############################################################################
# vendor/sifli/boards/sf32lb52/drivers/lcd/CMakeLists.txt
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

set(SRCS)

if(CONFIG_LCD)
  list(APPEND SRCS sf32lb_lcd.c)
endif()

if(CONFIG_LCD_USING_CO5300)
  list(APPEND SRCS co5300.c)
endif()

if(CONFIG_LCD_USING_ILI8688E)
  list(APPEND SRCS ili8688e.c)
endif()

target_sources(board PRIVATE ${SRCS})
target_include_directories(board PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Force linker to keep LCD driver descriptors from static library.
# LCD_DRIVER_EXPORT places descriptors into the "LcdDriverDescTab" section
# which is referenced via linker symbols, not C references. Without -u the
# linker will discard the unreferenced .o from the static archive.

if(CONFIG_LCD_USING_CO5300)
  target_link_options(board PRIVATE -u __lcddriver_co5300)
endif()

if(CONFIG_LCD_USING_ILI8688E)
  target_link_options(board PRIVATE -u __lcddriver_ili8688e)
endif()
