# LCKFB Huangshan Pi Board Support for openvela

[ English | [简体中文](README_zh-cn.md) ]

## Introduction

This directory provides openvela support for the
[**LCKFB Huangshan Pi** (立创·黄山派)](https://wiki.lckfb.com/zh-hans/hspi-sf32lb52/)
on the `dev-ai-contest-2026` branch.

The Huangshan Pi is an SF32LB52-based learner / wearable kit sold by
立创开发板 (LCKFB). It is a derivative of the SiFli **SF32LB52-LCHSPI-ULP**
reference design with identical PCB layout, identical CO5300 AMOLED
panel, identical FT6146 capacitive touch controller and identical
AW32001 charger IC. See [`../sf32lb52_lchspi_ulp`](../sf32lb52_lchspi_ulp)
for the SiFli reference variant.

For board hardware details, schematic and the official getting-started
guide, see the upstream LCKFB documentation:

- [Huangshan Pi Wiki](https://wiki.lckfb.com/zh-hans/hspi-sf32lb52/)
- [Hardware (pinout, schematic)](https://wiki.lckfb.com/zh-hans/hspi-sf32lb52/hardware/board.html)

> ⚠️ **Branch dependency**
>
> This board overlay only builds on the `dev-ai-contest-2026` branch of
> `open-vela/nuttx` and `open-vela/vendor_sifli`. Building it from
> `trunk` or `dev` will fail because the chip-side dependencies are not
> yet upstream.

## Directory Structure

```
vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/
├── Kconfig                   # board-level Kconfig options
├── include/board.h           # clock / GPIO / MTD board definitions
├── include/drv_io.h          # SDK-style IO abstraction headers
├── src/                      # board bring-up sources
│   ├── sifli_ap.c            # board_late_initialize → bringup
│   ├── bsp_pinmux.c          # pinmux for UART / I2C / QSPI / LCD
│   ├── bsp_lcd_tp.c          # LCD power-up + touch reset glue
│   ├── bsp_power.c           # AW32001 charger I2C2 + NOR DPD
│   ├── bsp_init.c            # MPU + early board hooks
│   ├── sf32lb52_buttons.c    # KEY1 / KEY2 button driver
│   └── ...
├── configs/nsh/              # NSH defconfig (LVGL + LCD + touch)
└── scripts/                  # link script (XIP @ 0x12010000)
```

## Supported Peripherals

| Peripheral | Driver | Device node |
|-----------|--------|-------------|
| 1.85" 390×450 AMOLED (CO5300, QSPI) | `co5300` + `sf32lb_lcd` | `/dev/lcd0`, `/dev/fb0` |
| FT6146 capacitive touch (I2C1)      | `ft6146`                | `/dev/input0` |
| AW32001 charger (I2C2 @0x49)        | `bsp_power.c` (kernel)  | `/dev/i2c1` (raw) |
| KEY1 / KEY2 push buttons            | `sf32lb52_buttons`      | `/dev/buttons` |
| ADC0 (8 channels, 12-bit)           | `sf32lb_adc`            | `/dev/adc0` |
| RTC                                 | `sf32lb_rtc`            | `/dev/rtc0` |
| Watchdog                            | `sf32lb_iwdg`           | `/dev/watchdog0` |
| Hardware timer                      | `sf32lb_tim`            | `/dev/timer0` |
| Internal NOR (16 MB, MPI2 bus)      | `sf32lb_flash` MTD      | `/dev/config0` |
| USB CDC ACM device                  | `cdcacm`                | `/dev/ttyACM0` |
| UART1 console (CH340N, 1 Mbps)      | `sf32lb_uart`           | `/dev/console`, `/dev/ttyS0` |

## GPIO Pin Map (nsh defconfig)

| Function | GPIO |
|----------|------|
| UART1 RX / TX (console)         | PA18 / PA19 |
| Touch I2C1 SDA / SCL            | PA33 / PA37 |
| Touch INT / RST                 | PA41 / PA09 |
| Charger I2C2 SDA / SCL          | PA11 / PA10 |
| QSPI LCD CS / CLK / TE          | PA03 / PA04 / PA02 |
| QSPI LCD D0 / D1 / D2 / D3      | PA05 / PA06 / PA07 / PA08 |
| LCD reset / VADD_EN             | PA00 / PA01 |
| VSYS / VCC_3V3 power gates      | PA38 / PA26 |
| KEY1 / KEY2                     | PA34 / PA43 |
| RGB LED (PWM)                   | PA32 |

The 22-pin LCD FPC pinout follows the LCKFB Huangshan Pi schematic
(equivalent to the SiFli reference design). LCKFB silkscreens QSPI D0
as `PB05`, but it is the same physical net as PA05.

## Build

```bash
cmake -B cmake_out/lckfb_huangshan_pi -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/lckfb_huangshan_pi
```

The build produces `cmake_out/lckfb_huangshan_pi/nuttx.bin` (~1.5 MB), to
be flashed to the on-package NOR at offset `0x12010000`.

## Flash

The board uses the SiFli ROM bootloader (SFBL) followed by a flat XIP
image at `0x12010000`. **No separate bootloader or partition table.**

The CH340N USB-UART bridge has its **RTS line wired to the SF32LB52
reset signal** (active low). This is deliberate: it lets PC-side tools
toggle hardware reset without a physical button press, mirroring the
auto-reset pattern that ESP-IDF / `esptool.py` use on ESP32 boards.
**Automated flashing and self-testing rely on this wiring** — do not
cut or pull-up the trace if you intend to use `sftool` from CI.

```bash
# Erase + write nuttx.bin to NOR @ 0x12010000, then soft-reset into NSH
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
       --before default_reset --after soft_reset \
       write_flash cmake_out/lckfb_huangshan_pi/nuttx.bin@0x12010000
```

If `sftool` reports `Failed to connect to the chip`, the SoC's ROM
bootloader missed the ~2 s `ATSF32` listen window after RTS reset —
re-plug the USB cable and retry. If the board boot-loops printing only
`SFBL`, the AMOLED panel is browning out the USB rail; use a 5 V/2 A
wall charger or a battery, not a high-impedance laptop USB-A port.

## First Boot & Quick Tests

Open the UART1 console at 1 000 000 8N1 (no flow control). Because of
the RTS-to-RST wiring, terminal choice matters:

```bash
# Recommended for interactive use — keeps RTS deasserted at open():
picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyUSB0
```

`minicom` will reset the SoC once at connect (RTS asserted on `open()`)
but stays running afterwards. `screen` and `cu` cannot deassert RTS and
keep the chip held in reset — avoid them.

After RTS reset you should see SFBL then NuttX boot:

```
SFBL
ABCD
ADC calibration data missing, use defaults
INFO: NOR MTD registered at /dev/config0 (offset=2464 blocks=1024)

NuttShell (NSH)
nsh> ls /dev
/dev:
 adc0      buttons   config0   console   fb0       gpio0     gpio1
 gpio2     i2c0      i2c1      input0    lcd0      pwm0      ram0
 rtc0      spi1      timer0    ttyACM0   ttyS0     urandom   watchdog0
nsh> uname -a
NuttX 0.0.0 ... arm lckfb_huangshan_pi
```

Per-peripheral hand-test (all from `nsh>`):

```
free                    # confirm 8 MB OPI-PSRAM in user heap
ps                      # task list
adc -n 1                # one-shot ADC sample
date                    # RTC read
i2c dev 0x10 0x77       # FT6146 ACKs at 0x38
i2c dev 0x40 0x4f       # AW32001 ACKs at 0x49 on /dev/i2c1
buttons 5               # press KEY1 / KEY2 within 5 events
fb                      # 6 nested rectangles to AMOLED
lvgldemo widgets        # full LVGL widgets demo + FT6146 touch
ostest                  # NuttX kernel regression suite
```

## Customising the Configuration

```bash
cd cmake_out/lckfb_huangshan_pi
ninja menuconfig                 # tweak interactively
ninja savedefconfig
cp defconfig \
   ../../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig
```

To create an additional config variant, copy `configs/nsh/` to
`configs/<your-name>/` and pass the new path to `-DBOARD_CONFIG=`.

## Automated Test Mode

The RTS-to-RST wiring is what enables fully automated CI / pytest /
expect harnesses. The test driver behaves as a remote reset button —
no physical interaction required:

```python
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 1_000_000, timeout=0.5)
ser.rts = True;  time.sleep(0.05)        # assert hardware reset
ser.rts = False; time.sleep(0.5)         # release, board boots
ser.read_until(b"nsh> ")                 # wait for NSH prompt
ser.write(b"ostest\r\n")
out = ser.read_until(b"PASSED\n")
assert b"PASSED" in out
```

## Known Limitations

1. The CO5300 panel does not always reply to QSPI ID query reliably on
   USB-only power; the LCD driver logs `[co5300] ReadID=0x0 expected
   0x331100, init anyway` and proceeds. This is benign — the panel
   accepts the SLPOUT / DISPON sequence and renders correctly.
2. `/dev/uorb` is empty — sensor uORB integration is queued for a future
   PR. The charger is exposed as raw `/dev/i2c1` only.
3. SD card is not wired on this board.
4. RTS-to-RST means a Linux serial open with default `termios` keeps
   the SoC in reset. Use `picocom --lower-rts --lower-dtr` (or the
   pyserial recipe above) for interactive console access.

## License

All files in this directory are licensed under Apache-2.0 (SPDX
identifier `Apache-2.0`); see individual files for full headers.
