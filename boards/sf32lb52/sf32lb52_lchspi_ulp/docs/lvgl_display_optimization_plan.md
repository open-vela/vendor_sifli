# SF32LB52 LCHSPI ULP：LVGL 推屏机制对齐与优化计划

本文定义 openVela 在 `sf32lb52_lchspi_ulp` 上向 SiFli SDK `drv_lcd_fb` 对齐的
实施顺序。每一阶段都以显示正确性为前提；benchmark 只用于量化通用机制变化，
不允许按 demo 名称、图像格式或像素数量加入特判。

相关已验证基线和结论见
[`lvgl_display_benchmark.md`](lvgl_display_benchmark.md)。

## 目标状态机

当前采用单 retained framebuffer，因此状态机应使用单 buffer 的真实所有权，
而不是形式上照搬 SDK 的双 buffer index：

```text
LVGL draw buffer
    │  copy 完成且 cache 已发布
    ▼
shadow 写入中 ──(last slice)──> ready + dirty clip
    ▲                                │
    │  仅等待与下一写区域相交的行       │ 同一 LVGL/display-safe context
    │                                ▼
line-valid <──── LCDC reading <── submitted / pending
    │                                │
    └──── LCDC line IRQ ─────────────┤
                                     ▼
                              transfer complete
                              全部 shadow 再次可写
```

以下不变量必须始终成立：

1. `lv_display_flush_ready()` 只在该 slice 已复制到 retained framebuffer、原 LVGL
   draw buffer 可以再次被 LVGL 使用时调用；它不等待物理面板完成。
2. 当 LCDC 正读取 retained framebuffer 的某一行时，后续 copy 只能写入已由
   line completion 发布的行，或不相交区域。
3. `PUTAREA_ASYNC` 必须从已验证的 LVGL/display-safe context 发起；不可移到
   NuttX worker task。此前该实验会黑屏。
4. `pending` 只表示 LCDC 已经成功接受了一笔 transfer；`deferred_submit` 只表示
   已 ready 但尚未从 LVGL async callback 实际提交。这两个状态不能混用。
5. LCDC completion 后才可把整个 shadow 视为完全可写。任何 cache clean 必须在
   LCDC 读取前完成。

## 分阶段计划与验收

## 2026-07-15：direct 双画布实测与 SDK 架构复核

P3 已先完成一个必要的、可回退的中间验证：Vela 以两张 390 x 450 x
RGB565 PSRAM draw buffer 运行 `LV_DISPLAY_RENDER_MODE_DIRECT`，LCDC 直接读取
当前 LVGL draw buffer。实板已确认正常显示；它消除了 retained-shadow copy，短窗口
统计为 `submit=complete=595`、`error=timeout=0`、`shadow copy=0`，实际 panel transfer
和等待约 8 ms/笔。

这不是 SDK `LCD_FB_USING_TWO_UNCOMPRESSED` 的完整等价实现。SDK 同时拥有：

```text
LVGL render buffer[0/1]  -- async copy -->  drv_lcd_fb scanout[0/1]
                                                │
                                                └--> LCDC
```

其中 `write_fb_idx` 属于 scanout manager，不是 LVGL draw buffer index；copy 完成即可
解除 LVGL 的 flushing，而 `flush_fb_idx` 仍可由 LCDC 消费。SDK 另用
`ready`、`fb_flushing_lcd`、`fb_valid_y1` 和 line-complete event 防止覆盖。

Vela 当前只有上图左侧两张画布，因此必须在 LCDC completion 后才调用
`lv_display_flush_ready()`，不能得到 SDK 的 render/copy/transfer overlap。这一限制是
当前约 8 ms panel wait 的根因，不是 TE 的额外等待。

本轮也试验了 SDK `LV_EVENT_REFR_START` 的 full-invalidate 片段；在 Vela 当前
“LCDC 直接读 draw buffer”的拓扑中，它把每笔传输扩大为完整 390 x 450 帧，实测
`15 FPS / 62 ms`、`render=48 ms`、`flush=14 ms`，故已经撤回，不能作为优化方案。

后续 P3 的正确实现应是四张全屏画布（新增两张 scanout，额外约 702,000 B PSRAM），
而非继续修改 LVGL 的全屏 invalidation：

1. scanout slot 独立记录 `ready`、`flushing`、`fb_clip` 和自上次使用以来的 stale
   region；
2. copy completion 在 LVGL/display-safe task context 发布该 slot 并立即释放 render
   source；LCDC submit 仍只在该 context 发生；
3. LCDC completion 回收对应 slot、自动发起另一张已 ready 的 scanout；无可写 slot
   时只在当前帧 render 已完成后背压；
4. 因 Vela 保留 direct-mode 的 dirty rendering，每张 scanout 必须补齐其遗漏的区域
   （可先用正确的 joined stale clip，再决定是否需要 region-list），不能只复制本帧
   dirty clip 后假定另一张 scanout 也已同步；
5. 必须新增 copy/ready/queued/flushing/back-pressure 统计，并以首帧、滚动、overlay、
   widgets 和长跑 benchmark 验证显示正确。

这一路径才是把 SDK 的 ownership 语义迁移到 Vela；不会提前复用 LCDC 正在读取的
memory，也不依赖 benchmark 场景特判。

**实施前置验证（2026-07-15，已通过）**：RTS 重启后 `free` 报告可用 Umem
`8,174,616 B`；现有 direct 两 draw buffer 已分配时，仍足以加入两张合计
`702,000 B` 的 scanout buffer。扩展后的 `dmacopyctl` 已在实板通过：

```text
dmacopy psram-to-psram async stride: PASS rows=64 words=96
dmacopy psram-to-psram full async: PASS words=19500 bytes=78000
```

这两项分别覆盖 dirty clip 的逐行、保留 destination stride 形式，及 390 x 100
RGB565 连续行块形式。DMA provider 会清理 source、提交前 flush destination、完成后
invalidate destination；它可作为 render-buffer 至 scanout-buffer copy 的候选后端。
仍不能因 self-test 通过就在 ISR 内调用 LVGL 或 `PUTAREA_ASYNC`：completion 的状态
转换和 LCDC 提交仍必须回到 LVGL/display-safe task context。

### P0：建立不可改变路径上的观测能力

**改动范围**：当前 `sf32lb52_lvgl.c` 和 `sf32lb_lcd.c`；不改变回调上下文、不改变
buffer 数量、不引入 copy engine。

**实现项**：

- 为每个 panel transfer 记录：dirty clip、是否发生整屏扩展、copy 字节数与耗时、
  submit 到 completion 的耗时。
- 为 `WAITWRITE`、`WAITAREA` 分别记录次数、总耗时、最大耗时、超时/恢复次数。
- 为 lower-half 记录 line IRQ 次数、首次/末次 `draw_valid_y1`、实际 transfer area。
- 输出或提供可读的统计快照，并保证默认只在显式诊断时打印，避免日志改变时序。

**验收**：

- 固件可构建，实板冷启动和 RTS 重启后均有正确显示；
- 连续运行 benchmark 后无花屏、黑屏、LCDC timeout；
- 可以把 14 ms flush 分解为 copy、wait-write、wait-area、LCDC transfer 四类时间。

**当前进展（2026-07-14）**：`lcdstat` 已同时覆盖 lower-half 和 upper-half，并在
实板通过。它证明 LCDC completion 和 line-valid 等待不是当前主要成本，而
shadow copy 累计约 12.9 ms/frame（见基线文档的 P0 实测）；统计读取不会改变
submit context 或 callback 时序。

### P1：对齐 SDK 的 single-FB ready / fb_clip / line-valid 语义

**改动范围**：当前 stable single retained shadow 路径。

**实现项**：

- 用显式状态字段替代仅凭 `dirty_valid/pending` 推断所有权，分别表示 copying、
  ready、deferred、submitted 和 line-valid range；每个非法转换触发诊断断言。
- 使 dirty-clip 合并语义等同 SDK：若新 slice 覆盖一张已 ready、未开始 LCDC
  transfer 的 retained FB，保留并合并原 clip；开始 transfer 时原子地取走 clip，
  为下一帧清空 clip。
- 保持 line-valid 的区域相交判断。下一块 copy 只在它会覆盖 LCDC 尚未读完行时
  等待，不将无关区域串行化。
- 按 SDK 的实际 `fb_clip` 语义发送 joined dirty clip。旧的“bounding box 达到
  半屏即扩展为全屏”规则已经移除；如未来数据表明它对某类硬件接口确有普适收益，
  也只能作为独立、可量化且默认关闭的策略重新评估。

**验收**：

- 静态界面、滚动、overlay、全屏重绘都显示正确；
- `ready` 不会在 data/cache 尚未完成时被 LCDC 消费；
- 单 buffer 下 `WAITWRITE` 只覆盖真正重叠的行；
- flush 下降必须伴随同样的显示正确性和无超时，而非以跳帧换取。

**当前进展（2026-07-14）**：已显式区分 `ready`、`deferred` 与 `pending`，并在
`lcdstat` 中观察其转换；前 8 秒 benchmark 为 `ready=236`、`deferred=236`、
`submit=236`、`state_error=0`。half-screen full-frame heuristic 已删除并完成小窗口
A/B；该窗口中 full clip 本来就是完整帧，所以未产生可宣称的性能提升。

**当前进展（2026-07-15）**：CPU-only retained-FB 路径已进一步对齐 SDK handoff
语义。`flush_wait_cb` 在等待 copy completion 后立即释放 LVGL，不再把 LCDC physical
completion 串到 LVGL flush critical path；最终连续 DMA copy 可在 source ownership
仍受保护时提前启动 LCDC。完整 benchmark 达到 `36 FPS / 19 ms`，其中 `render=14 ms`、
`flush=5 ms`，`state_error=0`、LCDC `error=timeout=0`。这属于通用推屏流程优化，
没有按 benchmark 场景、图片格式或面积阈值特判。

### P2：对齐 copy completion 的异步语义（先证明，再启用）

**前置条件**：P0 统计证明 shadow copy 是可观的通用时间，并且 P1 的状态机已在
实板稳定。

**实现项**：

- 抽象 copy completion callback：CPU copy 先作为同步实现，回调在 cache clean 完成
  后触发；这样 `ready` 的定义与未来 DMA/EPIC copy 一致。
- 只为连续同宽区域评估 SiFli SDK 已使用的通用 copy engine；非连续矩形必须有
  正确的 stride/row 处理或继续 CPU copy，不能用错误的连续 memcpy。
- 将 LVGL draw buffer 的释放绑定在“source 已不再被 copy engine 访问”之后；
  将 LCDC submit 绑定在“destination 完整且对 LCDC 可见”之后。
- 无法同时保证 source lifetime、PSRAM cache clean/invalidate、IRQ callback context
  时，继续使用 CPU copy，不以跑分为理由开启异步引擎。

**当前进展（2026-07-14）**：已将 Vela 的同步 CPU copy 重构为
`shadow_copy_submit()` 与 `shadow_copy_complete()` 两段。`flush_ready()`、dirty clip
合并、`ready` 发布与 LCDC submit 全部只在 copy-complete 之后发生；当前 CPU backend
同步调用 completion，故显示时序不变。下一步才可在同一 contract 内选择经过验证的
DMA/EPIC backend，且必须保持 `copy == complete` 与 `state_error == 0`。

**引擎审计结果（2026-07-14）**：当前不得将 display copy 直接提交给已有的
`sf32lb52_epic`。该 EPIC handle 已由 LVGL EPIC draw unit 的异步 worker 使用，HAL
只提供单一 in-flight 的 `XferCpltCallback` / `IntXferCpltCallback`；display copy 会与
draw task 争用 completion ownership。通用 DMA 硬件可做 memory-to-memory，但 Vela 尚未
提供 display 专用 channel、IRQ handler、跨 1 MiB 分段、cache maintenance 和 callback
封装。故 CPU backend 是当前唯一满足 lifecycle contract 的实现；启用 DMA 前必须先补齐
这套独立资源层及逐像素 copy self-test，不能为了 benchmark 共享 EPIC handle。

SDK 对普通 GP-DMA 的处理也确认了“不能硬编码 channel”：`drv_lcd_fb.c` 默认注释掉
`ENABLE_GP_DMA_COPY`，并在该选项手工指定通道时以
`#error "Need to allocate DMA channel automatically!"` 阻止编译。进一步核对本板 HAL
后确认，SF32LB52 HCPU 已启用 `DMA_SUPPORT_DYN_CHANNEL_ALLOC`，并提供
`HAL_DMA_AllocChannel()` / `HAL_DMA_FreeChannel()`：以 DMA2 的一个合法起点初始化后，
HAL pool 会在 transfer 开始时选择未被占用的 channel、设置对应 IRQ，并在 completion
时释放。因此下一步 provider 应使用该动态机制，绝不固定某个 DMAC2 channel；仍需先
完成 IRQ dispatch、cache/1 MiB 边界和逐像素 self-test，才能接入显示。

**验收**：

- copy callback 与 LCDC completion 的时序可由统计和状态断言证明；
- 高刷新滚动、透明 overlay、重启后的首帧无撕裂/错行/旧帧残留；
- benchmark 和长期交互均无 DMA/EPIC timeout。

### P3：仅在数据证明需要时评估 two-uncompressed framebuffer

**前置条件**：P1/P2 后仍然主要因“单 shadow 与 LCDC 读取冲突”而等待，且有足够
PSRAM、cache、带宽和首帧初始化余量。

**当前实验结果（2026-07-15）**：四 framebuffer scanout manager 原型已经实现并以
feature flag 保留，但默认关闭。短窗口实板验证证明它的 ownership 状态是正确的
（copy/ready/queued/flush-complete 计数闭合，LCDC 无 error/timeout），但 copy backend
成为新瓶颈：DMA2 PSRAM-to-PSRAM full copy 约 130 ms，CPU copy 约 22 ms，EXTDMA
continuous copy 约 28 ms。这个结果达不到 SDK 路径的成本模型，所以不能把该原型作为
默认路径烧录给用户。后续 P3 必须先解决两个问题之一：

- 提供真正接近 SDK 的高速 display copy backend，且 completion 能在 LVGL-safe context
  及时释放 source；
- 或把每个 scanout 的 stale region 从单个 bounding box 改成 region-list/line-list，
  避免局部动画因为跨帧合并而退化成大面积 PSRAM copy。

**实现项**：

- 先以独立 feature flag 试验两张未压缩 retained framebuffer；明确 `write_idx`、
  `flush_idx`、每张的 `ready`、`fb_clip`、`flushing` 和 completion 回收规则。
- 不在 worker task 提交 LCDC；沿用 SDK callback 的 ownership 语义。
- 明确 full frame 的实际内存成本和 PSRAM cache 策略，并保留稳定单 FB 回退配置。

**验收**：

- 多帧背压时不覆盖正被 LCDC 读取的 buffer；
- 真实 UI、bench、冷启动和手工 RTS 重启都正确；
- 帧率改善来自可量化的 wait-area 减少，而不是减少有效更新或隐藏错误。

### P4：将 render / EPIC 优化作为独立工作流

只有 display handoff 已达到 P1 或 P2 的稳定状态后，才以 per-task 统计决定是否扩展
EPIC 支持。优先级应由真实软件热点决定：rotation、文本、layer/opacity/scrolling；
不应为某一 benchmark 名称专门优化。EPIC 的累计时间允许与 CPU/LCDC 重叠，评估时
应以帧时长和软件回退比例为准。

## 每次变更的验证程序

1. 增量构建：

   ```sh
   PATH="/opt/homebrew/opt/coreutils/libexec/gnubin:/opt/homebrew/bin:$PATH" \
   ./build.sh \
     vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh/ \
     --cmake -j8
   ```

2. 将 `cmake_out/sf32lb52_lchspi_ulp_nsh/nuttx.bin` 烧录到 `0x12010000`，使用当前
   已验证的 `/dev/cu.wchusbserial130`、1 Mbaud 配置。
3. 自动软复位后若无显示，按已验证的 WCH RTS 极性手工复位：pyserial
   `rts=True` 保持约 1 秒、`rts=False` 释放，随后等待约 2 秒读取日志。
4. 先确认首帧和手动交互的真实显示，再运行 `lvgldemo benchmark`；记录 P0 统计和
   benchmark 汇总。出现黑屏、花屏、LCDC timeout 或错误 completion 即停止推进，
   回退到上一个实板确认的状态，不继续叠加优化。

用于 P0 的最小命令序列为：

```text
lcdstat reset
lvgldemo benchmark &
lcdstat
```

## 明确不做的事项

- 不按 benchmark 名称、文件名、图像类型或像素阈值走不同显示路径；
- 不为了规避 flush 等待而跳过 dirty region、伪造 `flush_ready()` 或提前复用 source；
- 不在未经验证的 worker context 调用 `PUTAREA_ASYNC`；
- 不假定 PSRAM、双 framebuffer 或 EPIC copy 是必需方案；先由 P0 数据决定；
- 不把 SDK 的 RT-Thread 代码逐行搬到 NuttX。应对齐其 ownership 和 callback
  语义，同时遵守 NuttX/LVGL 的线程与 cache 约束。
