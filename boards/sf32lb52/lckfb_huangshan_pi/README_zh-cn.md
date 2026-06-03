# 立创·黄山派 (SF32LB52) 开发板对 openvela 的支持

\[ [English](README.md) | 简体中文 \]

## 简介

本目录为 **立创·黄山派（LCKFB Huangshan Pi）** 开发板提供 openvela 板级支持（BSP）。该板基于 **思澈微电子（SiFli）SF32LB52** 芯片，由嘉立创（LCKFB）推出，是 openvela AI 硬件大赛推荐的开发板之一。

> 开发板硬件说明、引脚定义、原理图等请参考思澈官方文档：[立创·黄山派开发板使用指南](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-%E9%BB%84%E5%B1%B1%E6%B4%BE.html)。

> 大赛参赛者请基于大赛分支 `dev-ai-contest-2026` 进行开发。

### 硬件规格

| 项目 | 说明 |
| ---- | ---- |
| 芯片 | 思澈 SF32LB52（Cortex-M33 @ 240 MHz） |
| 模组 | SF32LB525UC6（封装 16 MB NOR + 8 MB OPI-PSRAM） |
| 屏幕 | 1.85" 390×450 AMOLED（CO5300 控制器 + BV6802W PMIC） |
| 触摸 | FT6146 电容触摸（I²C1：PA37 SCL / PA33 SDA，中断 PA41） |
| 串口 | USB-UART：CH340 @ 1 Mbps |

> 以上参数来源于该板的开发板适配验证，最终以思澈官方文档为准。

## 已支持能力（以 `configs/nsh/defconfig` 实际为准）

| 类别 | 说明 |
| ---- | ---- |
| 显示 | LCD（CO5300，390×450）+ LCDC + framebuffer |
| 触摸 | FT6146 电容触摸屏（I²C1：PA37 SCL / PA33 SDA，中断 PA41） |
| 蓝牙 | BLE（zblue 协议栈，HCI 走 `/dev/ttyHCI0`） |
| 按键 | 板载按键（PA43_KEY2） |
| 串口 | UART1 / UART2（支持 DMA），UART 控制台 |
| 其他外设 | SPI、PWM、RTC（含闹钟、日期） |
| 存储与文件系统 | MTD、LittleFS、ROMFS、FAT |

## 目录结构

```
lckfb_huangshan_pi/
├── Kconfig            # 板级 Kconfig 选项
├── include/           # 板级头文件（board.h、drv_io.h 等）
├── src/               # 板级 bring-up 源码
│   ├── bsp_init.c       板级初始化
│   ├── bsp_pinmux.c     引脚复用配置
│   ├── bsp_lcd_tp.c     LCD + 触摸
│   ├── bsp_power.c      电源管理
│   ├── sifli_gpio.c     GPIO
│   ├── sf32lb52_buttons.c  按键
│   └── ...
├── scripts/           # 链接脚本与构建规则（ld.script、Make.defs）
└── configs/
    └── nsh            # 基础 NSH 命令行配置
```

## 编译

openvela 工程根目录的 `build.sh` 是统一编译入口：

```bash
# 可选：仅在切换配置或修改 menuconfig 后需要清理
./build.sh vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh -j8 distclean

# 编译
./build.sh vendor/sifli/boards/sf32lb52/lckfb_huangshan_pi/configs/nsh -j8
```

## 环境搭建、烧录与运行

编译完成后，固件位于 `cmake_out/.../nuttx.bin`（约 1.5 MB）。本板使用思澈官方烧录工具 **`sftool`** 烧录，示例命令如下（参数请按实际串口与环境调整）：

```bash
# 通过 USB-UART（CH340）烧录，烧写地址 0x12010000
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
    --after soft_reset write_flash \
    cmake_out/<你的构建目录>/nuttx.bin@0x12010000
```

烧录成功后，通过串口终端（1 Mbps）连接即可看到 `NuttShell (NSH)` 启动。

> 完整的环境搭建、`sftool` 安装、串口连接与上手教程**请以思澈官方文档为准**：

- [立创·黄山派开发板使用指南（思澈官方）](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-%E9%BB%84%E5%B1%B1%E6%B4%BE.html)

其他参考：

- [openvela 开发板适配案例](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/dev_board/Development_Board.md)
- [openvela 芯片移植（Chip Porting）章节](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/chip_porting/porting_guide.md)

## 许可协议

本目录下文件遵循各自文件头部声明的许可协议。
