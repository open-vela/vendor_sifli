# SF32LB52 LVGL 9.1 EPIC 适配与验证记录

本文记录 `sf32lb52_lchspi_ulp` 上 openvela LVGL 9.1 的 SiFli EPIC
适配边界、与 SiFli SDK 的对齐方式，以及 2026-07-12 的实板验证结果。

## 适配原则

- 行为参考为工作区只读的 `sifli-sdk`，本次审计使用其
  `origin/main`（`00ad700dc`）中的 LVGL v9 EPIC backend、single driver 和
  EPIC HAL。
- benchmark 只作为覆盖真实 LVGL task、decoder 和 layer 组合的回归场景，
  不用于增加像素门限、资源特判、格式转换缓存或专用内存池。
- 软件 draw unit 始终保留。只有能完整表达 LVGL 语义且满足硬件限制的任务
  才由 EPIC evaluator 接管；其余任务继续软件渲染。
- 不修改 `sifli-sdk` 工作树，也不覆盖 openvela 中与本适配无关的用户修改。

## 架构

适配分为三层：

1. LVGL draw unit
   `apps/graphics/lvgl/lvgl/src/draw/sifli/epic/`
   负责 task evaluator、FILL/BORDER/LABEL/IMAGE/LAYER 执行、decoder 生命周期、
   局部软件回退和逐类型统计。
2. NuttX SF32LB52 port
   `vendor/sifli/chips/sf32lb52/sf32lb52_epic.c`
   负责格式/层参数校验、IRQ、semaphore、mutex、cache 一致性、超时恢复和
   HAL 参数映射。
3. vendor EPIC HAL
   `vendor/sifli/chips/drivers/hal/bf0_hal_epic.c`
   包含从 SDK 最小 backport 的 transform、插值边界和寄存器范围修复。

当前 `CONFIG_LV_USE_OS=0`，LVGL 侧是同步 draw unit；底层仍使用中断启动 EPIC，
并通过 NuttX semaphore 等待完成。decoder、glyph 和 layer buffer 只会在硬件
完成后释放。

## PSRAM 是否必需

PSRAM 不是 EPIC transform 的前置条件，也没有为跑分保留专用 transform heap。

- SRAM、XIP flash 和 PSRAM 中的普通 source pointer 都可直接交给 EPIC；
  小尺寸 SRAM selftest 已覆盖 RGB565、ARGB8888、A8、scale、任意角度和
  非中心 pivot。
- 最终链接结果的静态 PSRAM 占用为 `0 B`。LVGL 显示 shadow buffer 和大尺寸
  测试可在运行时按正常 heap 策略使用 PSRAM。
- `epicctl transformtest 505 rotate` 动态分配两个 510050-byte buffer，实测地址
  位于 `0x60000000` PSRAM 窗口，505×505 旋转连续三轮通过。
- SiFli SDK 的 RT-Thread driver 只在 `PSRAM_CACHE_WB` 下 clean。此 NuttX
  端口做过实板 A/B：仅依赖 nominal write-through MPU 和 DSB 时，CPU 写入的
  PSRAM source 对 EPIC 仍会出现陈旧数据；恢复 source/output 活动行 clean 后，
  505×505 plain 和 rotate 均逐像素通过。因此当前 NuttX port 在 PSRAM 提交前
  clean，完成后 invalidate。这是平台 cache 一致性差异，不是性能调参。

## HAL 对齐

SF32LB52 的输入/输出单次坐标限制按 SDK 设为 505。仅修改 1010/512 常量不足以
安全开放 transform，本适配还成组 backport 了 SDK 的 `54300b46f` 和
`cb3e9459c` 等效修复：

- 将调用者的任意 pivot 等效替换为 source center，并把差值合并到整数和
  fractional layer offset；
- 保存 rotated visible area，正向与反向面积计算复用同一组 sin/cos/scale；
- 在有、无 scale 两条路径都保留 interpolation halo，消除旋转边缘黑线；
- 使用 d1/d2/d3/df 合法化 pivot、source origin 和 calculation origin；
- 分开处理可见像素尺寸与 `ROT_M_CFG1` inclusive span；
- 写 `ROT_M_CFG1/2/3` 前检查 unsigned/signed 寄存器范围，失败返回
  `HAL_ERROR`，不允许静默截断；
- 向上传播 transform/config error，避免被后续 background 配置覆盖。

同时同步 SDK `2b1c0b250`：continuous video-layer 的 `MAX_COL/MAX_LINE` 写入
`width - 1`、`height - 1`，避免最后一行/列黑点。

## 当前支持矩阵

| LVGL task/特性 | 当前处理 | 说明 |
|---|---|---|
| FILL：不透明纯色 | EPIC | RGB565、ARGB8565、RGB888、ARGB8888、XRGB8888 target |
| FILL：半透明纯色 | EPIC 或软件 | RGB/XRGB target 使用 SDK inverse-opacity 方法；带 pixel alpha 的 ARGB target 软件回退，避免透明 layer 被错误写成不透明 |
| FILL：两 stop HOR/VER gradient | 受限 EPIC | `radius=0`、两个 stop、frac `0/255`、宽高至少 3；变化轴保守限 128，以保证与 LVGL 软件插值的颜色误差边界 |
| BORDER | EPIC | 只接管 SDK evaluator 实际支持的 `side=FULL && radius=0`，拆成四个不重叠矩形；其他 border 软件渲染 |
| LABEL bitmap glyph | EPIC | A1/A2/A4/A8 glyph 由 LVGL 归一为 A8，再以固定文字色混合；IMAGE glyph 按 glyph 粒度软件回退 |
| LABEL vector/SVG/custom glyph | 软件 | evaluator 按 UTF-8 codepoint 扫描整段文本及 fallback font 的实际 glyph format；任一高级 glyph 都使整个 LABEL 留给相应 draw unit，避免混合字体漏字 |
| IMAGE RGB/ARGB | EPIC | RGB565、RGB565A8、ARGB8565、RGB888、ARGB8888、XRGB8888、A8；拒绝 premultiplied、skew、colorkey、非 normal blend、圆角 clip |
| IMAGE A1/A2/A4 | EPIC（A8） | 由 LVGL decoder 归一为 A8，不伪装成原生 A2/A4 backend |
| IMAGE I1/I2/I4/I8 | EPIC（ARGB8888 streaming） | 默认 decoder 参数把 indexed image 展开为 ARGB8888；当前不宣称未闭合的原生 L8/LUT 路径 |
| BIN RGB/I8 | EPIC streaming | 复用 `_lv_draw_image_normal_helper()`，逐行 decoder 输出按独立 chunk 提交；没有 benchmark 文件名或资源特判 |
| BIN A8 | EPIC | decoder 提供完整 A8 buffer |
| tiled image | EPIC 或软件 | 无 transform 时复用 `_lv_draw_image_tiled_helper()`，每个 tile/chunk 独立提交或局部软件回退；标准 widget 本就会清除 tiled transform，直接构造的 `tile + transform` 整 task 软件回退 |
| bitmap mask | EPIC | 当前 LVGL 9.1 API 的 variable A8 mask；硬件 `MASK` 乘 source pixel alpha 和 global opacity；建 task 时按 SDK 新版行为固化完整 `original_area`，streaming chunk 不会重复居中 mask |
| RGB565A8 | EPIC | RGB565 foreground + A8 mask；global opacity 保留 |
| LAYER | EPIC | 取 source layer draw buffer，复用 IMAGE 路径；空 draw buffer 为 no-op |
| 单层 RGB/ARGB/A8 transform | EPIC | HAL backport 和 SRAM/PSRAM 板测通过；支持 rotation、scale、任意 pivot 和裁剪 |
| RGB565A8 + transform | 软件 | 永久回退；两个 plane 中的 mask layer 不能 transform |
| bitmap mask + transform | 软件 | SF32LB52 只有一个不可 transform 的 mask 通道 |
| RGB565A8 + 外部 mask | 软件 | background + RGB + 内置 A8 + 外部 A8 超过可用 layer 数 |
| streaming source + transform | 软件 | evaluator 实际打开 decoder，只有确认提供完整 decoded buffer 才接管 transform；执行期再次拒绝任何 partial chunk，不能逐行独立变换 |
| ARC/LINE | 软件 | SDK non-render-list evaluator 中被 `#if 0`，不为 benchmark 单独实现 |
| BOX_SHADOW/TRIANGLE/MASK task/vector/复杂 blend | 软件 | 当前没有完整、低风险的硬件语义映射 |

## decoder、tile 与局部回退

IMAGE 不再只接受“打开后已有完整 buffer”的资源。实现复用 LVGL 9.1 自带的
normal/tiled helper，core callback 可以收到：

- 完整 decoded buffer；
- streaming decoder 的一行或一个子区域；
- 一个 tile。

每个 chunk 在提交前验证格式、stride、data size、坐标、mask 和 layer 数。
非 transform 任务在提交前不满足条件时，只对当前独立 chunk 调用软件 renderer；
已经完成的 alpha chunk 不会被整 task 重放。transform 任务必须在 evaluator 的
decoder preflight 和执行期 callback 两处都确认 source 是完整 buffer；若观察到
partial chunk，则在任何 EPIC 提交前整 task 回退。`tile + transform` 也在 evaluator
阶段整 task 回退，避免局部软件恢复丢失 tile-cell clip 后造成重叠 alpha。

LVGL 9.1 原本只在 layer 的部分路径设置 `original_area`。本适配按 SDK 新版
`image_area` 行为，在普通 IMAGE/LAYER 建 task 时补齐完整、未裁剪区域，因此
streaming decoder 的行/子块 callback 仍能把外部 A8 mask 对齐到整张图，而不是
在每一行上重新居中。

若 HAL 已接受任务后失败，则记录 error 并禁止重画当前 chunk，避免二次 alpha。

## cache、超时与错误语义

- SRAM 为 non-cacheable；XIP flash 只读；PSRAM source/output 按活动行 clean，
  完成后 output invalidate；background 与 output 完全别名时不重复 clean。
- 提交前始终执行 DSB；IRQ EOF 后在 CPU 读取 output 前再次 DSB。
- `submitted` 只有在 `HAL_EPIC_*Start*_IT()` 返回成功后才置 true。
- mutex/init/preflight/HAL-start 失败可以安全软件回退；post-submit error/timeout
  不重放非幂等操作。
- SDK single driver 没有私有 500 ms 上限。本板 505×505 PSRAM transform
  单轮约 0.69 s，原 500 ms 会误判合法任务超时；NuttX port 使用 2 s 有界超时，
  既覆盖最大合法任务，也保留硬件恢复能力。

## 构建、烧录和复位

构建：

```sh
PATH="/opt/homebrew/opt/coreutils/libexec/gnubin:/opt/homebrew/bin:$PATH" \
./build.sh \
  vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh/ \
  --cmake -j8
```

烧录：

```sh
sftool \
  -c SF32LB52 \
  -p /dev/cu.wchusbserial130 \
  -b 1000000 \
  --before default_reset \
  --after soft_reset \
  write_flash \
  cmake_out/sf32lb52_lchspi_ulp_nsh/nuttx.bin@0x12010000
```

沁恒串口实测 VID:PID 为 `1A86:7523`，波特率 1000000。RTS 复位序列：

```text
RTS=true
等待 100 ms
RTS=false
```

板上命令：

```text
epicctl reset
epicctl selftest
epicctl transformtest 505 plain
epicctl transformtest 505 rotate
epicctl stat
lvgldemo benchmark &
epicctl stat
```

`transformtest` 也可指定 1..505 的尺寸；rotate 模式要求奇数边长：

```text
epicctl transformtest [odd-side] [plain|rotate]
```

## 2026-07-12 实板结果

最终固件：

```text
nuttx.bin size: 1285304 bytes
SHA-256: 6be9a8a51d6ad3a612449fc18fb269a1095dda210e8651988399e716c0ac4f94
flash address: 0x12010000
static PSRAM: 0 B / 8 MiB
```

片上 SRAM selftest：

```text
EPIC selftest buffers: src=0x20016d40 dst=0x20016b40
EPIC selftest: PASS (fill + H/V gradient + RGB565/RGB888/ARGB8888/
RGB565A8/A8 images + ARGB8888/A8/arbitrary-pivot rotate + RGB565 scale)
```

PSRAM 与 505 极限：

```text
EPIC transformtest buffers: source=0x60055b40 target=0x600d23c0 bytes=510050
EPIC transformtest: PASS (505x505 RGB565, plain, 3 rounds)
port submit=3 complete=3 error=0 timeout=0 clean=6 invalidate=3

EPIC transformtest: PASS (505x505 RGB565, 180deg, 3 rounds)
```

505 rotate 三轮整条命令此前实测耗时 2.075 s；本轮依次执行 SRAM selftest、
505 plain 和 505 rotate 后的合并统计为：

```text
port submit=19 complete=19 error=0 timeout=0 clean=12 invalidate=6
```

完整 LVGL benchmark 仅作为回归覆盖，结果如下：

```text
Benchmark Summary (9.1.0 )
Name, Avg. CPU, Avg. FPS, Avg. time, render time, flush time
Empty screen, 86%, 18, 39, 9, 30
Moving wallpaper, 95%, 18, 50, 22, 28
Single rectangle, 88%, 55, 14, 0, 14
Multiple rectangles, 93%, 28, 31, 7, 24
Multiple RGB images, 93%, 30, 27, 8, 19
Multiple ARGB images, 93%, 28, 30, 11, 19
Rotated ARGB images, 96%, 10, 87, 70, 17
Multiple labels, 93%, 31, 28, 15, 13
Screen sized text, 96%, 11, 79, 53, 26
Multiple arcs, 89%, 52, 15, 3, 12
Span text, 95%, 14, 62, 46, 16
BIN image, 90%, 47, 17, 14, 3
BIN I8 image, 90%, 44, 20, 16, 4
BIN A8 image, 89%, 52, 16, 2, 14
Containers, 55%, 27, 16, 9, 7
Containers with overlay, 88%, 15, 59, 31, 28
Containers with opa, 62%, 28, 16, 10, 6
Containers with opa_layer, 65%, 27, 21, 15, 6
Containers with scrolling, 94%, 13, 69, 43, 26
Widgets demo, 96%, 12, 63, 42, 21
All scenes avg.,87%, 28, 37, 21, 16
```

最终任务与端口统计：

```text
EPIC ready=1 pixels=206578332
task,evaluated,accepted,completed,fallback,error
fill,20583,10613,10613,9970,0
border,3386,9,9,3377,0
box_shadow,936,0,0,936,0
label,15475,15175,15175,300,0
image,3494,2720,2720,774,0
layer,387,387,387,0,0
line,16908,0,0,16908,0
arc,2675,0,0,2675,0
triangle,288,0,0,288,0
port submit=164334 complete=164334 error=0 timeout=0 clean=0 invalidate=0
```

所有 EPIC task 均满足 `accepted == completed`，底层满足
`submit == complete`，draw-unit error、port error 和 timeout 均为 0。
benchmark 中 clean/invalidate 为 0，是因为本轮 EPIC 实际 source/target 位于
片上 SRAM/XIP；独立 PSRAM transformtest 已验证非零 cache maintenance 路径。

## 保留的软件路径

以下项目不是“为了跑分尚未打开”，而是当前不能证明硬件语义完整，故有意保留
软件实现：

- 原生 I8/L8 palette 提交；当前 indexed decoder 展开路径已经功能闭合；
- 原生 A2/A4 image 提交；当前统一为 A8；
- premultiplied、skew、colorkey、非 normal blend、圆角 image clip；
- RGB565A8 transform、mask transform、多 mask；
- vector/SVG/custom glyph、tiled transform、ARC、LINE、BOX_SHADOW、TRIANGLE 和独立 MASK task；
- SDK render-list backend、异步多任务流水和压缩/JPEG 专用 layer。

这些能力若后续接入，应先补通用像素正确性测试和 HAL 约束，再开放 evaluator；
不应通过 benchmark 资源特判或 FPS 门限绕过语义验证。
