# SF32LB52-DevKit-LCD 开发板对 openvela 的支持

[ [English](README.md) | 简体中文 ]

## 简介

本目录为思澈 **SF32LB52-DevKit-LCD** 开发板提供 openvela 支持，基于
`dev-ai-contest-2026` 分支。

DevKit-LCD 与 [SF32LB52-LCHSPI-ULP](../sf32lb52_lchspi_ulp) 参考板和
[立创·黄山派](../lckfb_huangshan_pi) 衍生板共用 SF32LB52 MCU 与同一块
1.85" 390×450 CO5300 AMOLED + FT6146 触摸面板，但 PCB 定位是
**桌面型开发板，不是可穿戴形态**：

- **不带电池 / 充电芯片**：LCHSPI-ULP / 黄山派上的 AW32001 与 I2C2 充电
  总线在本板被移除；整板由片内 USB CDC ACM（VID `0x38F4`）+ USB 直供。
- **LCD 电源拓扑不同**：`LCD_VADD_EN` 接到 **PA37**（LCHSPI-ULP 是
  PA01），**PA01 重映射为 `GPTIM1_CH4`，用作背光 PWM**；VSYS（PA38）
  / VCC_3V3（PA26）软切换被废弃 —— 这两路电源始终在线。
- **引脚迁移**让出了 LCHSPI-ULP 用作 I2C2 的 PA10/PA11：
  AUDIO_PA_CTRL → **PA10**、KEY2 → **PA11**、触摸 IRQ → **PA31**、
  触摸 I2C1 SCL → **PA30**（而非 PA37）。
- **多了一路调试串口**：UART2 RX = **PA20** / TX = **PA27**
  （LCHSPI-ULP 把 PA27 占用作 TF 卡 detect）。
- **默认面向 xTS 测试**：DevKit-LCD 默认 ship 一份面向 xTS 的配置
  （CMocka、TESTS_TESTSUITES、GETPRIME、SCANFTEST、FSTEST、RAMTEST、
  CM_MM_TEST、CM_SCHED_TEST、popen / pipe 示例……），便于把这块板
  复用为 SF32LB52 的 **openvela xTS** 硬件挂机工位。需要图形化 demo 时
  可在 `menuconfig` 中重新打开 LVGL。
- **NOR 文件系统分区**位于 flash2 的 `0x008A0000` 偏移
  （LCHSPI-ULP 是 `0x009A0000`）；链接脚本 XIP 入口（`0x12010000`）
  和 `sftool` 烧录流程与 LCHSPI-ULP 一致。

`vendor_sifli` 提交 `0a3cd0a` 修复的 CO5300 面板初始化竞态通过
`boards/sf32lb52/drivers/lcd/` 共享驱动层覆盖三块板，DevKit-LCD 直接
受益，无需板级 workaround。

开发板硬件细节、原理图和官方上手指南请参考思澈官方文档：

- [SF32LB52-DevKit-LCD Wiki](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-DevKit-LCD.html)
- [SF-DevKit-LCM-Adapter（LCD 转接板）](https://wiki.sifli.com/board/sf32lb52x/SF-DevKit-LCM-Adapter.html)
- [SF32LB52-DevKit-LCD 立创开源硬件平台 (oshwhub)](https://oshwhub.com/sifli/sf32lb52-devkit-lcd)
- [SF32 自动下载 / RTS 复位设计说明](https://wiki.sifli.com/hardware/SF32-auto-download.html)

> ⚠️ **分支依赖**
>
> 本板适配仅在 `open-vela/nuttx` 与 `open-vela/vendor_sifli` 的
> `dev-ai-contest-2026` 分支上可编译。`trunk` 或 `dev` 分支由于尚未合入
> 芯片层依赖，无法编译。

## 目录结构

```
vendor/sifli/boards/sf32lb52/sf32lb52_devkit_epd/
├── Kconfig                      # 板级 Kconfig 选项
├── include/board.h              # 时钟 / GPIO / MTD 板级定义
├── include/drv_io.h             # SDK 风格 IO 抽象头文件
├── src/                         # 板级 bring-up 源码
│   ├── sifli_ap.c               # board_late_initialize → bringup
│   ├── bsp_pinmux.c             # UART / I2C / QSPI / LCD 引脚复用
│   ├── bsp_lcd_tp.c             # LCD 上电 + 触摸复位 glue
│   ├── bsp_power.c              # 仅管 MPI2 NOR 电源（无充电 IC）
│   ├── bsp_init.c               # MPU + early board 钩子
│   ├── sf32lb52_buttons.c       # KEY1 / KEY2 按键驱动
│   └── sf32lb52_devkit_epd.h    # 板级声明头
├── configs/nsh/                 # NSH defconfig（含 xTS 测试套）
└── scripts/                     # 链接脚本（XIP @ 0x12010000）
```

## 支持的外设

| 外设 | 驱动 | 设备节点 |
|------|------|----------|
| 1.85" 390×450 AMOLED（CO5300，QSPI） | `co5300` + `sf32lb_lcd` | `/dev/lcd0`、`/dev/fb0` |
| FT6146 电容触控（I2C1）              | `ft6146`                | `/dev/input0` |
| 背光 PWM（GPTIM1_CH4）               | `sf32lb_pwm`            | `/dev/pwm0` |
| KEY1（PA34）/ KEY2（PA11）           | `sf32lb52_buttons`      | `/dev/buttons` |
| ADC0（8 通道，12 位）                | `sf32lb_adc`            | `/dev/adc0` |
| RTC                                  | `sf32lb_rtc`            | `/dev/rtc0` |
| 看门狗                               | `sf32lb_iwdg`           | `/dev/watchdog0` |
| 硬件定时器                           | `sf32lb_tim`            | `/dev/timer0` |
| 内置 NOR（16 MB，MPI2 总线）         | `sf32lb_flash` MTD      | `/dev/config0` |
| USB CDC ACM（片内）                  | `cdcacm`                | `/dev/ttyACM0` |
| UART1 控制台                         | `sf32lb_uart`           | `/dev/console`、`/dev/ttyS0` |
| UART2 调试 log（可选）               | `sf32lb_uart`           | `/dev/ttyS1` |

> **本板未配** `sf32lb52_lchspi_ulp` / `lckfb_huangshan_pi` 上的
> AW32001 充电 IC（I2C2）。`bsp_power.c` 仅管 MPI2 NOR 电源开关；
> `CONFIG_BSP_USING_I2C2` 默认关闭，没有 `/dev/i2c1` 充电节点。
>
> **LVGL 默认未启用**，需要 `lvgldemo widgets` 时通过 `menuconfig`
> 打开（`CONFIG_GRAPHICS_LVGL=y`、`CONFIG_EXAMPLES_LVGLDEMO=y`、
> `CONFIG_LV_USE_NUTTX_LCD=y`、`CONFIG_LV_USE_NUTTX_TOUCHSCREEN=y`）。

## GPIO 引脚映射（nsh defconfig）

| 功能 | GPIO |
|------|------|
| UART1 RX / TX（控制台）        | PA18 / PA19 |
| UART2 RX / TX（调试 log）      | **PA20 / PA27** |
| 触摸 I2C1 SDA / SCL            | PA33 / **PA30** |
| 触摸 INT / RST                 | **PA31** / PA09 |
| QSPI LCD CS / CLK / TE         | PA03 / PA04 / PA02 |
| QSPI LCD D0 / D1 / D2 / D3     | PA05 / PA06 / PA07 / PA08 |
| LCD reset / VADD_EN            | PA00 / **PA37** |
| LCD 背光 PWM（GPTIM1_CH4）     | **PA01** |
| 音频 PA EN                     | **PA10** |
| KEY1 / KEY2                    | PA34 / **PA11** |
| RGB LED                        | PA32 |

> **与 `sf32lb52_lchspi_ulp`（含黄山派）的差异**：
> - 触摸 I2C SCL：**PA30**（-ULP 是 PA37）
> - 触摸 IRQ：**PA31**（`CONFIG_TOUCH_IRQ_PIN=31`，-ULP 是 PA41）
> - KEY2：**PA11**（-ULP 是 PA43）
> - LCD VADD_EN：**PA37**（-ULP 是 PA01）
> - PA01 在本板是**背光 PWM**，而不是 VADD_EN
> - 音频 PA EN：**PA10**（-ULP 是 PA42）
> - UART2 调试 log 走 PA20/PA27（-ULP 把 PA27 占用作 TF 卡 detect）
>
> 22-pin LCD FPC 定义遵循思澈 SF-DevKit-LCM-Adapter 规范，**同时**
> 引出了 **QSPI**（D0..D3）和 **8080 MCU**（DB0..DB7）走线。当前
> `nsh` defconfig 只用 QSPI；8080 数据线保留给通过 LCM-Adapter 接入
> 的其他 LCD 模组使用。

## 编译

```bash
cmake -B cmake_out/sf32lb52_devkit_epd -S "$PWD/nuttx" -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_epd/configs/nsh \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/sf32lb52_devkit_epd
```

`cmake_out/sf32lb52_devkit_epd/nuttx.bin`（约 1 MB）是最终烧录到内置
NOR 偏移 `0x12010000` 的镜像。

## 烧录

本板采用思澈 ROM bootloader（SFBL）+ 平面 XIP 镜像 @ `0x12010000` 的
架构，**没有独立 bootloader 或分区表**。

SF32LB52 SoC **没有专用的复位引脚**；复位通过外接负载开关切断 VCC
实现。思澈把 USB-UART 桥的 RTS 引脚直接连接到该负载开关的使能引脚
（参见 [SF32 自动下载设计](https://wiki.sifli.com/hardware/SF32-auto-download.html)），
toggle RTS 即可硬复位 SoC，与 ESP-IDF / `esptool.py` 在 ESP32 上的
机制完全相同。**自动烧录和自测都依赖这个走线** —— 如果要在 CI 中使用
`sftool`，请勿切断或上拉这条走线。

```bash
# 擦除 + 把 nuttx.bin 烧到 NOR @ 0x12010000，再 soft-reset 进入 NSH
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
       --before default_reset --after soft_reset \
       write_flash cmake_out/sf32lb52_devkit_epd/nuttx.bin@0x12010000
```

DevKit-LCD 在 RTS 软复位之外，还有一颗**物理 Reset 按键**作为后备。
如果 `sftool` 报 `Failed to connect to the chip`，说明 SoC 的 ROM
bootloader 错过了 RTS 复位后约 2 秒的 `ATSF32` 监听窗口 —— 重新插拔
USB 或按一下 Reset 键再重试即可。

## 首次启动 & 快速验证

打开 UART1 控制台，1 000 000 8N1，关闭流控。由于 RTS-to-VCC-load-switch
的走线设计，**串口工具的选择很关键**：

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
INFO: NOR MTD registered at /dev/config0 (offset=2208 blocks=1024)

NuttShell (NSH)
nsh> ls /dev
/dev:
 adc0      buttons   config0   console   fb0       gpio0     gpio1
 gpio2     i2c0      input0    lcd0      pwm0      ram0      rtc0
 spi1      timer0    ttyACM0   ttyS0     ttyS1     urandom   watchdog0
nsh> uname -a
NuttX 0.0.0 ... arm sf32lb52_devkit_epd
```

### openvela xTS 自测

`nsh` defconfig 默认启用 openvela 的 testing-suite app 集合，14 个
xTS 用例可直接在 `nsh>` 下手动跑一遍：

```
ostest                   # NuttX 内核回归测试
getprime                 # 内核质数压力测试
mm                       # 内存管理压力测试
scanftest                # libc scanf 覆盖
hello                    # 基线 app 启动
popen                    # libc popen / pclose
pipe                     # NuttX pipe IPC
free                     # 内存资源（应看到 8 MB Umem）
df -h                    # 文件系统列表（tmpfs / romfs / procfs）
fstest -n 3 -m /var      # /var 读写循环
ramtest -w -s 1024       # RAM marching / pattern
ls /dev/gpio*            # GPIO 节点枚举
timer                    # /dev/timer0 expiration 测试
wdog -h                  # /dev/watchdog0 helper
```

### LCD / 触摸功能验证

```
fb                       # 在 AMOLED 屏上画 6 个嵌套矩形
i2c dev 0x10 0x77        # FT6146 应当在 I2C1 的 0x38 处 ACK
buttons 5                # 5 次按键事件内按下 KEY1 / KEY2
adc -n 1                 # 单次 ADC 采样
date                     # RTC 读取
free                     # 8 MB OPI-PSRAM 已纳入用户堆
```

## 自定义配置

```bash
cd cmake_out/sf32lb52_devkit_epd
ninja menuconfig                 # 交互式调整
ninja savedefconfig
cp defconfig \
   ../../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_epd/configs/nsh/defconfig
```

如需新建配置变体，把 `configs/nsh/` 复制为 `configs/<新名字>/`，然后把
新路径传给 `-DBOARD_CONFIG=`。

## 自动化测试模式

RTS-to-VCC-load-switch 走线让全自动 CI / pytest / expect 测试成为可能。
测试驱动扮演远程复位按钮的角色，**无需任何物理交互**：

```python
import serial, time
ser = serial.Serial('/dev/ttyUSB0', 1_000_000, timeout=0.5)
ser.rts = True;  time.sleep(0.05)        # 切断 VCC，SoC 复位
ser.rts = False; time.sleep(0.5)         # 释放，板子开始 boot
ser.read_until(b"nsh> ")                 # 等待 NSH 提示符
ser.write(b"ostest\r\n")
out = ser.read_until(b"PASSED\n")
assert b"PASSED" in out
```

这是 xTS 自测套推荐的驱动方式 —— 每个用例都退化为简单的
`write` + `read_until` 配对，没有任何手工干预。

## 已知限制

1. **未配充电 IC**：与 `sf32lb52_lchspi_ulp` 和 `lckfb_huangshan_pi`
   不同，本板没有焊 AW32001 充电芯片。`bsp_power.c` 仅管 MPI2 NOR
   的电源开关；`CONFIG_BSP_USING_I2C2` 与 `/dev/i2c1` 充电节点都按
   设计省略。
2. **LVGL 默认关闭**：`nsh` defconfig 面向 xTS（CMocka + testsuites
   + getprime / scanftest / fstest / ramtest / cm_mm_test /
   cm_sched_test / popen / pipe）。需要图形 demo 时通过 `menuconfig`
   重新打开 LVGL。
3. 默认板不引出 SD 卡（`LCD_52J_SD` 子型号在 deep-sleep 中通过 PA21
   暴露 TF 接口）。
4. RTS-to-VCC-load-switch 意味着 Linux 默认 `termios` 串口 open 会让
   SoC 持续处于复位态。交互式控制台请使用 `picocom --lower-rts
   --lower-dtr`，或者上面的 pyserial 脚本。

## 许可协议

本目录下所有文件均使用 Apache-2.0 协议（SPDX 标识符
`Apache-2.0`）；详见各文件头部声明。
