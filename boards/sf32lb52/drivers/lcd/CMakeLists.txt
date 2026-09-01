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
  list(APPEND SRCS drv_lcd.c)
  list(APPEND SRCS drv_lcd_private.c)

  if(CONFIG_BSP_USING_LCD_FRAMEBUFFER)
    list(APPEND SRCS drv_lcd_fb.c)
    list(APPEND SRCS drv_ext_dma.c)
    if(CONFIG_BSP_USING_EPIC AND CONFIG_LV_USE_SIFLI_EPIC)
      list(APPEND SRCS drv_epic.c)
    endif()
    # drv_dma.c is intentionally excluded. Framebuffer copies use EXTDMA
    # through drv_ext_dma.c and EPIC through LVGL's shared EPIC backend.
  endif()
endif()

if(CONFIG_LCD_USING_CO5300)
  list(APPEND SRCS co5300.c)
endif()

if(CONFIG_LCD_USING_EPD_SPI)
  list(APPEND SRCS epd_spi.c)
endif()

if(CONFIG_LCD_USING_ILI8688E)
  list(APPEND SRCS ili8688e.c)
endif()

target_sources(drivers PRIVATE ${SRCS})
target_include_directories(drivers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Force linker to keep LCD driver descriptors from static library.
# LCD_DRIVER_EXPORT places descriptors into the "LcdDriverDescTab" section
# which is referenced via linker symbols, not C references. Without -u the
# linker will discard the unreferenced .o from the static archive.

if(CONFIG_LCD_USING_CO5300)
  target_link_options(drivers PRIVATE -u __lcddriver_co5300)
endif()

if(CONFIG_LCD_USING_EPD_SPI)
  target_link_options(drivers PRIVATE -u __lcddriver_epd_spi)
endif()

if(CONFIG_LCD_USING_ILI8688E)
  target_link_options(drivers PRIVATE -u __lcddriver_ili8688e)
endif()
