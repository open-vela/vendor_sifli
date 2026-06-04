# SF32LB52-DevKit-LCD Board Support for openvela

[ English | [简体中文](README_zh-cn.md) ]

## Introduction

This directory provides openvela support for the SiFli **SF32LB52-DevKit-LCD**
development board on the `dev-ai-contest-2026` branch.

The DevKit-LCD shares the SF32LB52 wearable / IoT MCU and the same 1.85"
390×450 CO5300 AMOLED panel + FT6146 capacitive touch as the
[SF32LB52-LCHSPI-ULP](../sf32lb52_lchspi_ulp) reference and the
[LCKFB Huangshan Pi](../lckfb_huangshan_pi) derivative, but it is laid
out as a **bench-top development board, not a wearable**:

- **No on-board battery / charger.** The AW32001 charger and the I2C2
  charger bus that LCHSPI-ULP / Huangshan Pi populate are removed; the
  board is USB-powered through the on-package USB CDC ACM device
  (vendor ID `0x38F4`).
- **Different LCD power topology.** `LCD_VADD_EN` is on **PA37** (vs
  PA01 on LCHSPI-ULP), and PA01 is repurposed as `GPTIM1_CH4` for
  backlight PWM. VSYS (PA38) / VCC_3V3 (PA26) software gates are
  unused — those rails are always on.
- **Pin-map shifts** to free up the AUDIO_PA_CTRL and KEY2 pads that
  LCHSPI-ULP uses for I2C2: AUDIO_PA_CTRL → **PA10**, KEY2 → **PA11**,
  touch IRQ → **PA31**, touch I2C1 SCL → **PA30** (vs PA37).
- **A second debug UART** is wired out: UART2 RX = **PA20** / TX =
  **PA27** (LCHSPI-ULP repurposes PA27 as TF-card detect).
- **xTS-oriented default config.** LVGL is disabled by default; instead
  the `nsh` defconfig enables the openvela testing-suite apps
  (`getprime`, `scanftest`, `fstest`, `ramtest`, `cmocka`,
  `testsuites`, `cm_mm_test`, `cm_sched_test`, `popen`, `pipe`, …) so
  this board doubles as the SF32LB52 side of the **Vela xTS** rig.
  LVGL can be re-enabled via `menuconfig` if a graphical demo is
  needed.
- **NOR filesystem partition** sits at flash offset `0x008A0000` on
  this board (LCHSPI-ULP uses `0x009A0000`); the link-script XIP entry
  (`0x12010000`) and the `sftool` flash workflow are otherwise
  identical.

The CO5300 panel-init race fixed in `vendor_sifli` commit `0a3cd0a` is
shared with all three SF32LB52 boards via `boards/sf32lb52/drivers/lcd/`;
no board-local workaround is required.

For board hardware details, schematics and the official getting-started
guide, see the upstream SiFli documentation:

- [SF32LB52-DevKit-LCD Wiki](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-DevKit-LCD.html)
- [SF-DevKit-LCM-Adapter (LCD module adapter)](https://wiki.sifli.com/board/sf32lb52x/SF-DevKit-LCM-Adapter.html)
- [SF32LB52-DevKit-LCD on 立创开源硬件平台 (oshwhub)](https://oshwhub.com/sifli/sf32lb52-devkit-lcd)
- [SF32 auto-download / RTS reset design note](https://wiki.sifli.com/hardware/SF32-auto-download.html)

> ⚠️ **Branch dependency**
>
> This board overlay only builds on the `dev-ai-contest-2026` branch of
> `open-vela/nuttx` and `open-vela/vendor_sifli`. Building it from
> `trunk` or `dev` will fail because the chip-side dependencies are not
> yet upstream.

## Directory Structure

```
vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/
├── Kconfig                      # board-level Kconfig options
├── include/board.h              # clock / GPIO / MTD board definitions
├── include/drv_io.h             # SDK-style IO abstraction headers
├── src/                         # board bring-up sources
│   ├── sifli_ap.c               # board_late_initialize → bringup
│   ├── bsp_pinmux.c             # pinmux for UART / I2C / QSPI / LCD
│   ├── bsp_lcd_tp.c             # LCD power-up + touch reset glue
│   ├── bsp_power.c              # MPI2 NOR power gate (no charger IC)
│   ├── bsp_init.c               # MPU + early board hooks
│   ├── sf32lb52_buttons.c       # KEY1 / KEY2 button driver
│   └── sf32lb52_devkit_lcd.h    # board-specific declarations
├── configs/nsh/                 # NSH defconfig (xTS testing apps)
└── scripts/                     # link script (XIP @ 0x12010000)
```

## Supported Peripherals

| Peripheral | Driver | Device node |
|-----------|--------|-------------|
| 1.85" 390×450 AMOLED (CO5300, QSPI) | `co5300` + `sf32lb_lcd` | `/dev/lcd0`, `/dev/fb0` |
| FT6146 capacitive touch (I2C1)      | `ft6146`                | `/dev/input0` |
| Backlight PWM (GPTIM1_CH4)          | `sf32lb_pwm`            | `/dev/pwm0` |
| KEY1 (PA34) / KEY2 (PA11)           | `sf32lb52_buttons`      | `/dev/buttons` |
| ADC0 (8 channels, 12-bit)           | `sf32lb_adc`            | `/dev/adc0` |
| RTC                                 | `sf32lb_rtc`            | `/dev/rtc0` |
| Watchdog                            | `sf32lb_iwdg`           | `/dev/watchdog0` |
| Hardware timer                      | `sf32lb_tim`            | `/dev/timer0` |
| Internal NOR (16 MB, MPI2 bus)      | `sf32lb_flash` MTD      | `/dev/config0` |
| USB CDC ACM (on-package)            | `cdcacm`                | `/dev/ttyACM0` |
| UART1 console                       | `sf32lb_uart`           | `/dev/console`, `/dev/ttyS0` |
| UART2 debug log (optional)          | `sf32lb_uart`           | `/dev/ttyS1` |

> **Not populated on this board** (vs `sf32lb52_lchspi_ulp` /
> `lckfb_huangshan_pi`): AW32001 charger over I2C2. `bsp_power.c` only
> drives the MPI2 NOR power gate; `CONFIG_BSP_USING_I2C2` is left off.
> No `/dev/i2c1` charger node is registered.
>
> **LVGL is opt-in** on this board. Re-enable it via `menuconfig` if you
> want `lvgldemo widgets` (`CONFIG_GRAPHICS_LVGL=y`,
> `CONFIG_EXAMPLES_LVGLDEMO=y`, `CONFIG_LV_USE_NUTTX_LCD=y`,
> `CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y`).

## GPIO Pin Map (nsh defconfig)

| Function | GPIO |
|----------|------|
| UART1 RX / TX (console)         | PA18 / PA19 |
| UART2 RX / TX (debug log)       | **PA20 / PA27** |
| Touch I2C1 SDA / SCL            | PA33 / **PA30** |
| Touch INT / RST                 | **PA31** / PA09 |
| QSPI LCD CS / CLK / TE          | PA03 / PA04 / PA02 |
| QSPI LCD D0 / D1 / D2 / D3      | PA05 / PA06 / PA07 / PA08 |
| LCD reset / VADD_EN             | PA00 / **PA37** |
| LCD backlight PWM (GPTIM1_CH4)  | **PA01** |
| Audio PA EN                     | **PA10** |
| KEY1 / KEY2                     | PA34 / **PA11** |
| RGB LED                         | PA32 |

> **Differences from `sf32lb52_lchspi_ulp`** (and Huangshan Pi):
> - Touch I2C SCL: **PA30** (vs PA37 on -ULP)
> - Touch IRQ: **PA31** (`CONFIG_TOUCH_IRQ_PIN=31`, vs PA41 on -ULP)
> - KEY2: **PA11** (vs PA43 on -ULP)
> - LCD VADD_EN: **PA37** (vs PA01 on -ULP)
> - PA01 is **backlight PWM** here, not VADD_EN
> - AUDIO_PA EN: **PA10** (vs PA42 on -ULP)
> - UART2 debug log on PA20/PA27 (-ULP repurposes PA27 as TF-card detect)
>
> The 22-pin LCD FPC pinout follows the SiFli SF-DevKit-LCM-Adapter
> spec, which exposes both **QSPI** (D0..D3) and **8080 MCU**
> (DB0..DB7) wires. The current `nsh` defconfig only wires QSPI; the
> 8080 lines are reserved for alternate LCD modules through the
> SF-DevKit-LCM-Adapter.

## Build

```bash
cmake -B cmake_out/sf32lb52_devkit_lcd -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/sf32lb52_devkit_lcd
```

The build produces `cmake_out/sf32lb52_devkit_lcd/nuttx.bin` (~1 MB), to
be flashed to the on-package NOR at offset `0x12010000`.

## Flash

The board uses the SiFli ROM bootloader (SFBL) followed by a flat XIP
image at `0x12010000`. **No separate bootloader or partition table.**

The SF32LB52 SoC has **no dedicated reset pin**; reset is performed by
gating the chip's VCC supply through a load switch. SiFli wires the
USB-UART bridge's RTS line to that load switch's enable input
(see [SF32 auto-download design](https://wiki.sifli.com/hardware/SF32-auto-download.html)),
so toggling RTS hard-resets the SoC the same way `esptool.py` resets an
ESP32. **Automated flashing and self-testing rely on this wiring** —
do not cut or pull-up the trace if you intend to use `sftool` from CI.

```bash
# Erase + write nuttx.bin to NOR @ 0x12010000, then soft-reset into NSH
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
       --before default_reset --after soft_reset \
       write_flash cmake_out/sf32lb52_devkit_lcd/nuttx.bin@0x12010000
```

The DevKit-LCD also has a **physical Reset button** in addition to the
RTS-driven soft reset. If `sftool` reports `Failed to connect to the
chip`, the SoC's ROM bootloader missed the ~2 s `ATSF32` listen window
after RTS reset — re-plug the USB cable or press the Reset button and
retry.

## First Boot & Quick Tests

Open the UART1 console at 1 000 000 8N1 (no flow control). Because of
the RTS-to-load-switch wiring, terminal choice matters:

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
INFO: NOR MTD registered at /dev/config0 (offset=2208 blocks=1024)

NuttShell (NSH)
nsh> ls /dev
/dev:
 adc0      buttons   config0   console   fb0       gpio0     gpio1
 gpio2     i2c0      input0    lcd0      pwm0      ram0      rtc0
 spi1      timer0    ttyACM0   ttyS0     ttyS1     urandom   watchdog0
nsh> uname -a
NuttX 0.0.0 ... arm sf32lb52_devkit_lcd
```

### Vela xTS self-test

The `nsh` defconfig enables the openvela testing-suite apps. The full
14-case xTS self-test sweep can be exercised from `nsh>`:

```
ostest                   # NuttX kernel regression suite
getprime                 # kernel-prime stress
mm                       # memory manager stress
scanftest                # libc scanf coverage
hello                    # baseline app launch
popen                    # libc popen / pclose
pipe                     # NuttX pipe IPC
free                     # RAM resource check (8 MB Umem)
df -h                    # filesystem check (tmpfs/romfs/procfs)
fstest -n 3 -m /var      # /var read-write loop
ramtest -w -s 1024       # RAM marching / pattern
ls /dev/gpio*            # GPIO node enumeration
timer                    # /dev/timer0 expiration sweep
wdog -h                  # /dev/watchdog0 helper
```

### LCD / touch sanity check

```
fb                       # 6 nested rectangles to AMOLED
i2c dev 0x10 0x77        # FT6146 ACKs at 0x38 on I2C1
buttons 5                # press KEY1 / KEY2 within 5 events
adc -n 1                 # one-shot ADC sample
date                     # RTC read
free                     # 8 MB OPI-PSRAM in user heap
```

## Customising the Configuration

```bash
cd cmake_out/sf32lb52_devkit_lcd
ninja menuconfig                 # tweak interactively
ninja savedefconfig
cp defconfig \
   ../../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh/defconfig
```

To create an additional config variant, copy `configs/nsh/` to
`configs/<your-name>/` and pass the new path to `-DBOARD_CONFIG=`.

## Automated Test Mode

The RTS-to-load-switch wiring is what enables fully automated CI /
pytest / expect harnesses. The test driver behaves as a remote reset
button — no physical interaction required:

```python
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 1_000_000, timeout=0.5)
ser.rts = True;  time.sleep(0.05)        # gate VCC off → SoC resets
ser.rts = False; time.sleep(0.5)         # release, board boots
ser.read_until(b"nsh> ")                 # wait for NSH prompt
ser.write(b"ostest\r\n")
out = ser.read_until(b"PASSED\n")
assert b"PASSED" in out
```

This is the recommended driver pattern for the xTS self-test suite —
each case becomes a simple `write`/`read_until` pair without any
operator interaction.

## Known Limitations

1. **No charger IC populated.** Unlike `sf32lb52_lchspi_ulp` and
   `lckfb_huangshan_pi`, this board does not populate the AW32001
   charger. `bsp_power.c` only manages the MPI2 NOR power gate.
   `CONFIG_BSP_USING_I2C2` and the `/dev/i2c1` charger node are absent
   by design.
2. **LVGL disabled by default.** The `nsh` defconfig is xTS-oriented
   (CMocka + testsuites + getprime / scanftest / fstest / ramtest /
   cm_mm_test / cm_sched_test / popen / pipe). Enable LVGL via
   `menuconfig` if you need a graphical demo.
3. The CO5300 panel does not always reply to QSPI ID query reliably on
   USB-only power; the LCD driver logs `[co5300] ReadID=0x0 expected
   0x331100, init anyway` and proceeds. This is benign — the panel
   accepts the SLPOUT / DISPON sequence and renders correctly. The
   shared `boards/sf32lb52/drivers/lcd/` race fix landed in commit
   `0a3cd0a` covers this board too.
4. SD card is not wired on the default board layout (the `LCD_52J_SD`
   sub-variant exposes a TF socket on PA21 in deep sleep only).
5. RTS-to-load-switch means a Linux serial open with default `termios`
   keeps the SoC in reset. Use `picocom --lower-rts --lower-dtr` (or
   the pyserial recipe above) for interactive console access.

## License

All files in this directory are licensed under Apache-2.0 (SPDX
identifier `Apache-2.0`); see individual files for full headers.
