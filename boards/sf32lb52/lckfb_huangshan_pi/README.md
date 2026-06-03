# LCKFB Huangshan Pi (SF32LB52) Board Support for openvela

\[ English | [简体中文](README_zh-cn.md) \]

## Overview

This directory provides the openvela board support package (BSP) for the **LCKFB Huangshan Pi** development board. The board is based on the **SiFli SF32LB52** chip and is produced by LCKFB (JLC). It is one of the recommended boards for the openvela AI Hardware Contest.

> For hardware details, pin definitions, and schematics, refer to SiFli's official documentation: [LCKFB Huangshan Pi User Guide](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-%E9%BB%84%E5%B1%B1%E6%B4%BE.html).

> Contest participants should develop on the contest branch `dev-ai-contest-2026`.

### Hardware Specifications

| Item | Description |
| ---- | ----------- |
| Chip | SiFli SF32LB52 (Cortex-M33 @ 240 MHz) |
| Module | SF32LB525UC6 (16 MB NOR + 8 MB OPI-PSRAM in package) |
| Display | 1.85" 390×450 AMOLED (CO5300 controller + BV6802W PMIC) |
| Touch | FT6146 capacitive touch (I²C1: PA37 SCL / PA33 SDA, INT on PA41) |
| Serial | USB-UART: CH340 @ 1 Mbps |

> The values above come from board adaptation verification; refer to SiFli's official documentation as the authoritative source.

## Supported Capabilities (refer to `configs/nsh/defconfig` for the actual set)

| Category | Description |
| -------- | ----------- |
| Display | LCD (CO5300, 390×450) + LCDC + framebuffer |
| Touch | FT6146 capacitive touchscreen (I²C1: PA37 SCL / PA33 SDA, INT on PA41) |
| Bluetooth | BLE (zblue stack, HCI over `/dev/ttyHCI0`) |
| Buttons | Onboard button (PA43_KEY2) |
| Serial | UART1 / UART2 (with DMA), UART console |
| Other peripherals | SPI, PWM, RTC (with alarm and date) |
| Storage & filesystem | MTD, LittleFS, ROMFS, FAT |

## Directory Structure

```
lckfb_huangshan_pi/
├── Kconfig            # Board-level Kconfig options
├── include/           # Board headers (board.h, drv_io.h, etc.)
├── src/               # Board bring-up sources
│   ├── bsp_init.c       Board initialization
│   ├── bsp_pinmux.c     Pin mux configuration
│   ├── bsp_lcd_tp.c     LCD + touch
│   ├── bsp_power.c      Power management
│   ├── sifli_gpio.c     GPIO
│   ├── sf32lb52_buttons.c  Buttons
│   └── ...
├── scripts/           # Linker script and build rules (ld.script, Make.defs)
└── configs/
    └── nsh            # Basic NSH shell configuration
```

## Build

The `build.sh` script at the openvela project root is the unified build entry point:

```bash
# Optional: only needed when switching configs or after menuconfig changes
./build.sh vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh -j8 distclean

# Build
./build.sh vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh -j8
```

## Environment Setup, Flashing, and Running

After building, the firmware is at `cmake_out/.../nuttx.bin` (~1.5 MB). This board is flashed with SiFli's official tool **`sftool`**. Example command (adjust the serial port and parameters to your environment):

```bash
# Flash over USB-UART (CH340), write address 0x12010000
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
    --after soft_reset write_flash \
    cmake_out/<your-build-dir>/nuttx.bin@0x12010000
```

After flashing, connect a serial terminal (1 Mbps) to see the `NuttShell (NSH)` prompt.

> For the complete environment setup, `sftool` installation, serial connection, and getting-started tutorial, **refer to SiFli's official documentation**:

- [LCKFB Huangshan Pi User Guide (SiFli official)](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-%E9%BB%84%E5%B1%B1%E6%B4%BE.html)

Additional references:

- [openvela Development Board adaptation cases](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/en/dev_board/Development_Board.md)
- [openvela Chip Porting guide](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/en/chip_porting/porting_guide.md)

## License

Files in this directory follow the license declared in each file's header.
