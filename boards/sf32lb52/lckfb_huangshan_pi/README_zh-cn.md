# 立创·黄山派 开发板对 openvela 的支持

[ [English](README.md) | 简体中文 ]

## 简介

本目录为
[**立创·黄山派**（LCKFB Huangshan Pi）](https://wiki.lckfb.com/zh-hans/hspi-sf32lb52/)
提供 openvela 支持，基于 `dev-ai-contest-2026` 分支。

黄山派是立创开发板（LCKFB）发售的基于 SF32LB52 的学习 / 可穿戴套件，
源自思澈 **SF32LB52-LCHSPI-ULP** 参考设计 —— PCB 布局、CO5300 AMOLED
屏、FT6146 电容触控、AW32001 充电 IC 都完全相同。思澈参考板的 openvela
适配请见 [`../sf32lb52_lchspi_ulp`](../sf32lb52_lchspi_ulp)。

开发板硬件细节、原理图和官方上手指南请参考立创开发板文档：

- [黄山派 Wiki](https://wiki.lckfb.com/zh-hans/hspi-sf32lb52/)
- [硬件介绍（引脚、原理图）](https://wiki.lckfb.com/zh-hans/hspi-sf32lb52/hardware/board.html)

> ⚠️ **分支依赖**
>
> 本板适配仅在 `open-vela/nuttx` 与 `open-vela/vendor_sifli` 的
> `dev-ai-contest-2026` 分支上可编译。`trunk` 或 `dev` 分支由于尚未合入
> 芯片层依赖，无法编译。

## 目录结构

```
vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/
├── Kconfig                   # 板级 Kconfig 选项
├── include/board.h           # 时钟 / GPIO / MTD 板级定义
├── include/drv_io.h          # SDK 风格 IO 抽象头文件
├── src/                      # 板级 bring-up 源码
│   ├── sifli_ap.c            # board_late_initialize → bringup
│   ├── bsp_pinmux.c          # UART / I2C / QSPI / LCD 引脚复用
│   ├── bsp_lcd_tp.c          # LCD 上电 + 触摸复位 glue
│   ├── bsp_power.c           # AW32001 充电 I2C2 + NOR DPD
│   ├── bsp_init.c            # MPU + early board 钩子
│   ├── sf32lb52_buttons.c    # KEY1 / KEY2 按键驱动
│   └── ...
├── configs/nsh/              # NSH defconfig（含 LVGL + LCD + 触摸）
└── scripts/                  # 链接脚本（XIP @ 0x12010000）
```

## 支持的外设

| 外设 | 驱动 | 设备节点 |
|------|------|----------|
| 1.85" 390×450 AMOLED（CO5300，QSPI） | `co5300` + `sf32lb_lcd` | `/dev/lcd0`、`/dev/fb0` |
| FT6146 电容触控（I2C1）              | `ft6146`                | `/dev/input0` |
| AW32001 充电 IC（I2C2 @0x49）        | `bsp_power.c`（内核态） | `/dev/i2c1`（裸 I2C） |
| KEY1 / KEY2 按键                     | `sf32lb52_buttons`      | `/dev/buttons` |
| ADC0（8 通道，12 位）                | `sf32lb_adc`            | `/dev/adc0` |
| RTC                                  | `sf32lb_rtc`            | `/dev/rtc0` |
| 看门狗                               | `sf32lb_iwdg`           | `/dev/watchdog0` |
| 硬件定时器                           | `sf32lb_tim`            | `/dev/timer0` |
| 内置 NOR（16 MB，MPI2 总线）         | `sf32lb_flash` MTD      | `/dev/config0` |
| USB CDC ACM 设备                     | `cdcacm`                | `/dev/ttyACM0` |
| UART1 控制台（CH340N，1 Mbps）       | `sf32lb_uart`           | `/dev/console`、`/dev/ttyS0` |

## GPIO 引脚映射（nsh defconfig）

| 功能 | GPIO |
|------|------|
| UART1 RX / TX（控制台）        | PA18 / PA19 |
| 触摸 I2C1 SDA / SCL            | PA33 / PA37 |
| 触摸 INT / RST                 | PA41 / PA09 |
| 充电 I2C2 SDA / SCL            | PA11 / PA10 |
| QSPI LCD CS / CLK / TE         | PA03 / PA04 / PA02 |
| QSPI LCD D0 / D1 / D2 / D3     | PA05 / PA06 / PA07 / PA08 |
| LCD reset / VADD_EN            | PA00 / PA01 |
| VSYS / VCC_3V3 电源开关        | PA38 / PA26 |
| KEY1 / KEY2                    | PA34 / PA43 |
| RGB LED（PWM）                 | PA32 |

22-pin LCD FPC 引脚定义遵循立创·黄山派原理图（与思澈参考设计等价）。
立创丝印把 QSPI D0 标为 `PB05`，但与 PA05 是同一物理网络。

## 编译

```bash
cmake -B cmake_out/lckfb_huangshan_pi -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/lckfb_huangshan_pi
```

`cmake_out/lckfb_huangshan_pi/nuttx.bin`（约 1.5 MB）是最终烧录到内置
NOR 偏移 `0x12010000` 的镜像。

## 烧录

本板采用思澈 ROM bootloader（SFBL）+ 平面 XIP 镜像 @ `0x12010000` 的
架构，**没有独立 bootloader 或分区表**。

CH340N USB-UART 桥接芯片的 **RTS 引脚直连到 SF32LB52 的复位信号**
（低电平有效）。这是**有意为之的设计**：让 PC 端工具可以软件触发硬件
复位，无需手动按按键，与 ESP-IDF / `esptool.py` 在 ESP32 板上的 auto-
reset 机制完全相同。**自动烧录和自测都依赖这个走线** —— 如果要在 CI 中
使用 `sftool`，请勿切断或上拉这条走线。

```bash
# 擦除 + 把 nuttx.bin 烧到 NOR @ 0x12010000，再 soft-reset 进入 NSH
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
       --before default_reset --after soft_reset \
       write_flash cmake_out/lckfb_huangshan_pi/nuttx.bin@0x12010000
```

如果 `sftool` 报 `Failed to connect to the chip`，说明 SoC 的 ROM
bootloader 错过了 RTS 复位后约 2 秒的 `ATSF32` 监听窗口 —— 重新插拔
USB 后重试。如果板子只打印 `SFBL` 反复重启，说明 AMOLED 屏幕拉低了
USB 电源轨；改用 5 V/2 A 充电头或电池供电，不要用阻抗较高的笔记本
USB-A 口。

## 首次启动 & 快速验证

打开 UART1 控制台，1 000 000 8N1，关闭流控。由于 RTS-to-RST 走线，
**串口工具的选择很关键**：

```bash
# 交互式使用推荐：让 RTS 在 open() 时保持 deasserted
picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyUSB0
```

`minicom` 会在连接时复位 SoC 一次（open() 时拉 RTS），但之后保持运行；
`screen` 和 `cu` 没有 deassert RTS 的能力，会让芯片一直停留在复位状态
—— 请避免使用。

RTS 复位后应该看到 SFBL，然后 NuttX 启动：

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

各外设手动验证命令（全部在 `nsh>` 下执行）：

```
free                    # 确认 8 MB OPI-PSRAM 已纳入用户堆
ps                      # 任务列表
adc -n 1                # 单次 ADC 采样
date                    # RTC 读取
i2c dev 0x10 0x77       # FT6146 应当在 0x38 处 ACK
i2c dev 0x40 0x4f       # AW32001 应当在 /dev/i2c1 的 0x49 处 ACK
buttons 5               # 5 次按键事件内按下 KEY1 / KEY2
fb                      # 在 AMOLED 屏上画 6 个嵌套矩形
lvgldemo widgets        # 完整 LVGL widgets demo + FT6146 触摸
ostest                  # NuttX 内核回归测试套件
```

## 自定义配置

```bash
cd cmake_out/lckfb_huangshan_pi
ninja menuconfig                 # 交互式调整
ninja savedefconfig
cp defconfig \
   ../../vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig
```

如需新建配置变体，把 `configs/nsh/` 复制为 `configs/<新名字>/`，然后把
新路径传给 `-DBOARD_CONFIG=`。

## 自动化测试模式

RTS-to-RST 走线让全自动 CI / pytest / expect 测试成为可能。测试驱动
扮演远程复位按钮的角色，**无需任何物理交互**：

```python
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 1_000_000, timeout=0.5)
ser.rts = True;  time.sleep(0.05)        # 触发硬件复位
ser.rts = False; time.sleep(0.5)         # 释放，板子开始 boot
ser.read_until(b"nsh> ")                 # 等待 NSH 提示符
ser.write(b"ostest\r\n")
out = ser.read_until(b"PASSED\n")
assert b"PASSED" in out
```

## 已知限制

1. CO5300 屏在仅 USB 供电场景下不一定可靠回应 QSPI ID 查询；LCD 驱动会
   打印 `[co5300] ReadID=0x0 expected 0x331100, init anyway` 然后继续
   初始化。这是预期行为 —— 屏幕能正确接受 SLPOUT / DISPON 命令并正常
   显示。
2. `/dev/uorb` 暂为空 —— sensor uORB 集成排在后续 PR。充电 IC 当前仅以
   裸 `/dev/i2c1` 形式暴露。
3. 该板未引出 SD 卡。
4. RTS-to-RST 意味着 Linux 默认 `termios` 串口 open 会让 SoC 持续处于
   复位态。交互式控制台请使用 `picocom --lower-rts --lower-dtr`，或者
   上面的 pyserial 脚本。

## 许可协议

本目录下所有文件均使用 Apache-2.0 协议（SPDX 标识符
`Apache-2.0`）；详见各文件头部声明。
