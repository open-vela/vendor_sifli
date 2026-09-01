# SF32LB52 LCHSPI ULP：LVGL 显示路径基线与结论

本文记录 `sf32lb52_lchspi_ulp` 在实板上的 LVGL benchmark 基线，用于指导
openVela 显示提交（framebuffer handoff）适配。目标是与 SiFli SDK 的通用显示
机制对齐，而不是为某个 benchmark 场景加入特判或绕行。

最后更新：2026-07-15。

## 新增实测：Vela direct 双 draw-buffer 中间基线

在原 retained-shadow 路径之后，Vela 已实板验证两张完整 PSRAM LVGL draw buffer 的
`DIRECT` 路径。它直接把最终 dirty clip 的 LVGL buffer 提交给 LCDC，因此 shadow copy
为零；20 秒 benchmark 窗口得到：

```text
submit=595 complete=595 error=0 timeout=0
xfer avg=8 ms, waitarea avg=8 ms, waitwrite=0
LVGL shadow: copy=0 bytes=0 state_error=0
```

这证明 direct 双画布的缓存发布和 panel ownership 正确，但也揭示它尚未拥有 SDK
`drv_lcd_fb` 的独立 scanout queue：LVGL buffer 就是 LCDC source，故 frame completion
仍必须等待本笔 panel transfer。SDK 的高吞吐路径还多出两张 scanout framebuffer 及其
copy/ready/flush state machine；它不能被简化成“两个 LVGL direct buffer”。

曾短暂试验 SDK adapter 中的 full-invalidate 代码以避免 LVGL 的 draw-buffer sync；由于
Vela 当前直接扫描 draw buffer，它把每笔推屏变为全屏，得到 `15 FPS / 62 ms`、
`render=48 ms`、`flush=14 ms`，且平均 LCDC transfer 为 15 ms。该实验已从固件撤回，
不计入任何基线或优化结论。

## 测试范围与可比性

- 面板和 SoC：SF32LB52 + CO5300，390 x 450，WCH 串口，1 Mbaud。
- `CPU-only` 指 LVGL 的 EPIC draw unit 未接管绘制任务；LCDC 仍正常用于把已经
  绘制完成的像素发送到面板。这不是“完全不用硬件”的显示路径。
- SDK 基线使用 SDK 原生 `drv_lcd_fb` framebuffer manager 和官方 LVGL `v9.1.0`。
- Vela 数据使用当前 openVela LVGL 9.1、single retained shadow framebuffer、
  line-valid 写保护和异步 LCDC 提交路径。
- SDK 另有一组 SDK 自带 LVGL 9.4 的结果，它只用于说明 SDK 显示路径的大致上限，
  不能与 Vela 9.1 的每一个 benchmark 条目逐项比较。
- 官方 LVGL 9.1 benchmark 没有 Vela 当前应用里的 `BIN image`、`BIN I8/A8 image`
  和 `Span text` 等扩展条目。因此比较以双方共有场景及总 `render/flush` 结构为准。

## 已验证的汇总基线

| 固件与显示路径 | LVGL | 绘制后端 | All scenes FPS | 每帧总时间 | render | flush |
|---|---:|---|---:|---:|---:|---:|
| SiFli SDK 原生 framebuffer 路径 | 9.4 | CPU | 43 | 24 ms | 18 ms | 6 ms |
| SiFli SDK 原生 `drv_lcd_fb` 路径 | 官方 9.1.0 | CPU | 43 | 26 ms | 22 ms | 4 ms |
| 当前 openVela retained-FB 路径 | 9.1 | CPU | 36 | 19 ms | 14 ms | 5 ms |
| 当前 openVela retained-FB 路径 | 9.1 | CPU + 已启用 EPIC evaluator | 31 | 32 ms | 18 ms | 14 ms |

SDK 官方 LVGL 9.1 的完整 CPU 基线：

```text
Benchmark Summary (9.1.0 )
Name, Avg. CPU, Avg. FPS, Avg. time, render time, flush time
Empty screen, 81, 53, 12, 2, 10
Moving wallpaper, 92, 60, 13, 10, 3
Single rectangle, 79, 60, 12, 0, 12
Multiple rectangles, 92, 57, 14, 3, 11
Multiple RGB images, 79, 60, 11, 3, 8
Multiple ARGB images, 90, 43, 21, 11, 10
Rotated ARGB images, 96, 11, 81, 81, 0
Multiple labels, 92, 60, 13, 6, 7
Screen sized text, 97, 16, 58, 57, 1
Multiple arcs, 15, 61, 0, 0, 0
Containers, 33, 61, 9, 9, 0
Containers with overlay, 93, 24, 37, 34, 3
Containers with opa, 52, 58, 14, 14, 0
Containers with opa_layer, 84, 28, 38, 38, 0
Containers with scrolling, 96, 24, 38, 36, 2
Widgets demo, 97, 14, 57, 57, 0
All scenes avg.,79, 43, 26, 22, 4
```

当前 Vela 的同版本汇总数据（已启用当前 EPIC 适配）为：

```text
All scenes avg.,76%, 31 FPS, 32 ms,
render 18 ms, flush 14 ms,
GPU time 4 ms, GPU/render 30%, GPU/frame 16%
```

该 GPU 时间是已提交 EPIC 工作的累计墙钟时间，可能与 CPU 和 LCDC 并行，因此
不应将它与 `render` 或帧时长简单相加。它表示当前有一部分任务被 EPIC 接管，
而不是“GPU 已占用 30% 的整帧”。

## 2026-07-15 CPU-only 推屏对齐结果

按 SDK `drv_lcd_fb` 的语义重新收紧 CPU-only retained-FB 路径：

- `flush_ready()` 只表示 LVGL draw buffer 的 copy ownership 已释放，不再额外等待
  LCDC physical transfer completion；
- 对最终 full-width continuous DMA copy，按 SDK `dma_faster_than_lcdc` 思路在 DMA
  in-flight 后提前提交 LCDC，让 TE/LCDC 等待与 framebuffer copy 重叠；
- 非连续 dirty clip 仍走 CPU row copy，避免逐行 DMA setup/IRQ 造成长尾；
- EPIC draw unit 保持关闭，`GPU time` 全部为 0。

实板完整 benchmark：

```text
All scenes avg.,63%, 36, 19, 14, 5, 0, 0%, 0%
```

对应 `lcdstat`：

```text
LCD transfer: submit=2782 complete=2782 error=0 timeout=0 line_irq=77613 pixels=274746128
xfer: count=2782 total_ticks=28835 avg_ticks=10 max_ticks=24
waitarea: count=2781 total_ticks=404 avg_ticks=0 max_ticks=13
waitwrite: count=5000 total_ticks=176 avg_ticks=0 max_ticks=11
LVGL display: copy=10623 complete=10623 bytes=463526872 ready=2782 joins=7841
              full_clip=808 deferred=1684 early=1098 submit=2782
              pending_wait=2781 state_error=0
display_copy: count=10623 total_ticks=47992 avg_ticks=4 max_ticks=180
```

与上一版 CPU-only hybrid（约 `33 FPS / 22 ms / render 14 / flush 8`）相比，主要收益是
flush 从 8 ms 降到 5 ms，且 `WAITAREA`/`WAITWRITE` 的平均等待均低于 1 tick。该结果
说明当前推屏等待已经基本从 LVGL flush critical path 中移出；剩余差距更多来自
具体场景的软件 render、dirty clip 形态，以及 LCDC 实际传输本身。

## P0 推屏观测实测（当前 Vela 固件）

为把 `flush` 拆分为可验证的等待类型，当前 Vela lower-half 已提供 `lcdstat`
命令。执行 `lcdstat reset`、运行一次完整 `lvgldemo benchmark &` 后得到：

```text
LCD transfer: submit=1589 complete=1589 error=0 timeout=0 line_irq=40771
pixels=136718332
xfer:     count=1589 total_ticks=12395 avg_ticks=7 max_ticks=16
waitarea: count=1589 total_ticks=496   avg_ticks=0 max_ticks=12
waitwrite: count=0    total_ticks=0     avg_ticks=0 max_ticks=0
```

本板 `CONFIG_USEC_PER_TICK=1000`，所以一个 tick 为 1 ms。该次测试说明：

- LCDC 实际传输平均约 7 ms，且 1,589/1,589 均有 completion；没有 error 或 timeout；
- `WAITAREA` 总共只占 496 ms，平均不足 1 ms/笔；它不是当前 14 ms flush 的主因；
- `WAITWRITE` 为零，表明当前 async/deferred submit 的实际调用序列没有在 lower-half
  等待和新 copy 相交的 LCDC 读取行；
- 因此下一步不能仅继续调 TE/VSYNC，也不能把 line-valid 机制当作目前的主要开销。
  应量化并对齐 retained shadow copy、slice 合并、`ready` 发布和 LVGL callback
  调度的成本。

`lcdstat` 只读取 lower-half 状态，所有统计均使用 IRQ-consistent snapshot；它不改变
LCDC、TE 或 LVGL callback 的执行顺序。

随后增加 upper-half 同口径统计，在 benchmark 开始后的 8 秒取得：

```text
LCD transfer: submit=235 complete=235 error=0 timeout=0 line_irq=6380
pixels=21824100
xfer:        count=235 total_ticks=1912 avg_ticks=8 max_ticks=16
waitarea:    count=234 total_ticks=114  avg_ticks=0 max_ticks=12
waitwrite:   count=0   total_ticks=0    avg_ticks=0 max_ticks=0
LVGL shadow: copy=681 bytes=41680584 ready=235 joins=446 fullscreen=115
             deferred=235 submit=235 pending_wait=234 state_error=0
shadow_copy: count=681 total_ticks=3021 avg_ticks=4 max_ticks=6
```

这意味着每个 panel transfer 平均有约 2.9 次 shadow slice copy，copy 累计约
`3021 / 235 = 12.9 ms/frame`，已经接近原 benchmark 的 `flush=14 ms`。当前
优化主线因此进一步收敛为：减少不必要的 shadow copy/dirty union 覆盖面积，并让
SDK 风格 `fb_clip` 按真实 dirty clip 提交。统计还发现旧 Vela 规则会在 joined
clip 的 bounding box 达到半屏时强制发送完整面板（115/235 次）；该规则不属于 SDK
`drv_lcd_fb` 语义，现已移除，后续必须以实板 A/B 验证其对正确性和总帧时长的影响。

该规则移除后的同一 8 秒窗口 A/B 为：

```text
submit=236 complete=236 error=0 timeout=0 line_irq=6391
xfer:       total_ticks=1914 avg_ticks=8 max_ticks=16
waitarea:   total_ticks=110  avg_ticks=0 max_ticks=12
waitwrite:  total_ticks=0
LVGL shadow: copy=682 bytes=41707776 ready=236 joins=446
             full_clip=115 deferred=236 submit=236 pending_wait=235
             state_error=0
shadow_copy: total_ticks=3032 avg_ticks=4 max_ticks=6
```

`full_clip=115` 表示移除阈值后仍然有 115 笔真实 joined clip 恰好是完整屏幕。它与
旧路径的 115 相同，故这段 benchmark 窗口中的 115 笔并非由阈值额外扩张；本次
语义对齐没有宣称帧率收益，也没有改变正确性状态。它保留了真实 `fb_clip` 行为，
之后应在实际 UI 的局部大区域更新中继续观察其收益。

## 结论

## 2026-07-15 scanout manager 实验

按 SDK two-uncompressed 拓扑实现了一个独立 scanout manager 原型：

```text
LVGL direct render buffer[0/1] -> scanout[0/1] -> LCDC
```

该原型保留了正确的所有权边界：copy 完成后才 `flush_ready()`，LCDC 只读取
scanout，不再读取 LVGL source；slot 记录 `ready/flushing/stale_area/fb_clip`，
且无可写 slot 时只在 LVGL/display-safe context 背压等待。实板短窗口验证中
`submit == complete`、`error == timeout == state_error == 0`，显示链路没有出现
非法状态。

但本板当前 copy backend 不足以支撑该拓扑默认开启：

```text
DMA2 PSRAM->PSRAM full scanout copy: display_copy avg ~= 130 ms
CPU PSRAM->PSRAM scanout copy:       display_copy avg ~= 22 ms
EXTDMA continuous copy:              display_copy avg ~= 28 ms
```

因此 scanout manager 已保留为独立 feature flag（当前默认关闭），板上重新烧录为
已验证稳定的 direct 双 LVGL buffer 基线。这个结果说明下一步不能仅增加 framebuffer
数量；必须先找到接近 SDK 的高效 copy 引擎，或减少 stale bounding copy 的面积
（例如 region-list/line-list），否则 render/push overlap 得不到收益，反而把瓶颈从
panel wait 转成 PSRAM-to-PSRAM copy。

1. **当前第一瓶颈是 Vela 的 flush / handoff，而不是平均 CPU render。**
   SDK + 官方 LVGL 9.1 为 43 FPS、26 ms/frame（render 22 ms、flush 4 ms）；
   Vela 为 31 FPS、32 ms/frame（render 18 ms、flush 14 ms）。Vela 的平均 CPU
   render 反而低 4 ms，但 flush 多约 10 ms/frame，足以解释主要帧率差异。

2. **flush 的 14 ms 不等于纯 QSPI 传输时间。**
   它包含 LVGL flush 回调内的 shadow 写入、上一笔 LCDC/TE 完成等待、
   line-valid 等待、LCDC 提交以及 LVGL bookkeeping。QSPI 时钟已到当前可用上限，
   P0 实测进一步表明，其中并没有大量时间停留在 `WAITAREA`/TE completion 或
   `WAITWRITE`。后续应优先明确 buffer 所有权并拆分 shadow copy 与 LVGL flush
   调度，而不是假定可继续提高总线时钟。

3. **重软件绘制场景仍是独立瓶颈。**
   `Rotated ARGB images`、`Screen sized text`、复杂 layer/scrolling 的 render 时间
   很高。framebuffer manager 只能降低显示提交阻塞，不能替代 transform、文本和
   复杂合成的 GPU/软件 renderer 优化。

4. **EPIC 利用率低不是当前主因的充分证据。**
   当前 EPIC evaluator 已能覆盖部分 fill/image/label/layer 工作；不过整个帧仍会
   包含软件回退、LCDC 发送和 TE 同步。应先让 display pipeline 的 copy、ready 和
   LCDC completion 可以重叠，之后再用按任务的 EPIC 统计决定扩展哪类 evaluator。

5. **真实显示正确性优先于吞吐。**
   过去将 LCDC `PUTAREA_ASYNC` 移入 worker，或直接搬运 two-full-framebuffer
   状态机，都出现过黑屏。当前唯一已实板确认稳定的基线是同 LVGL/display-safe
   context 提交、single retained framebuffer 加 line-valid 写保护；后续只能在该
   路径上小步验证。

## SDK 与 Vela 状态语义映射

| SDK `drv_lcd_fb` 语义 | 当前稳定 Vela 对应 | 需要继续对齐的点 |
|---|---|---|
| `write_fb_idx` / `flush_fb_idx` | 单一 `shadow`，写入与刷屏所有权隐含在 `pending` 中 | 将单 buffer 的写入、待提交、LCDC-reading 显式建模并输出断言/统计；不虚构双 buffer index |
| `fb_clip` | `dirty_area` 合并为最后一次 panel area，按真实 joined clip 提交 | 对齐“新写入覆盖已 ready 但尚未发送的 clip”语义，并持续量化大面积局部更新 |
| `ready` | 最后一 slice copy 完成后允许 LCDC submit | 明确 ready 的成立条件为“retained FB 数据与 cache 对 LCDC 可见”，而非物理发送完成 |
| `fb_flushing_lcd` | lower-half `draw_busy` / `pending` | 记录 submit 到 completion 的实际时长，避免把硬件 busy 与 deferred-but-not-submitted 混为一谈 |
| `fb_valid_y1` / line completion | `draw_valid_y1`、`line_busy`、`line_sem` | 保持只等待与即将 copy 区域相交、尚未被 LCDC 读完的行；验证 10-line IRQ 水位是否正常推进 |
| async copy completion | 当前 CPU 行拷贝同步完成 | 先记录 copy 耗时和连续/非连续 copy；只有生命周期和 cache 语义得到证明后，才评估异步 copy engine |
| LCDC completion callback | `SendLayerDataCpltCbk` 唤醒 `draw_sem` | completion 后必须发布整个 retained FB 可写，并且不从非 display-safe context 发起下一笔 submit |

SDK 的实质不是“必须两张全屏 framebuffer”。此板的 SDK 配置在非
`DRV_EPIC_NEW_API` 路径上可以使用单 uncompressed framebuffer 配合 line-valid
保护；two-uncompressed framebuffer 仅在证明单 buffer 的 copy/LCDC 时序无法满足
吞吐时才是备选策略。

当前也不能直接把 shadow copy 交给现有 LVGL EPIC instance：它的 asynchronous draw
worker 已拥有 HAL 的单 in-flight completion callback。没有独立 arbitration 的 display
copy 会破坏 draw task 的 completion ownership。CPU copy completion contract 已经建立；
后续若要迁移到硬件 copy，必须使用独立 DMA 资源和单独验证过的 cache/IRQ/lifetime
封装。

SDK 同样没有把 GP-DMA copy 作为可直接打开的默认路径：`ENABLE_GP_DMA_COPY` 被注释，
手工固定 channel 时会触发编译错误。SF32LB52 HCPU HAL 已有动态 DMA channel pool，
因此后续 display copy 必须使用 `HAL_DMA_AllocChannel()` 的动态 reservation，而不是
根据芯片“有 DMA”就硬编码某个 DMAC2 channel。

## 参考实现

- SDK manager：`sifli-sdk/rtos/rtthread/bsp/sifli/drivers/drv_lcd_fb.c`
- SDK LVGL v9 adapter：`sifli-sdk/middleware/lvgl/lv_drivers_v9/lv_lcd.c`
- Vela LVGL adapter：`../src/sf32lb52_lvgl.c`
- Vela LCD lower-half：`../../drivers/lcd/sf32lb_lcd.c`
- 后续实施与验收：[`lvgl_display_optimization_plan.md`](lvgl_display_optimization_plan.md)
