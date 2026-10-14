# FalcomDeskling

低资源的 Win32 桌面小人，用来替换 `WpfTest` 那套 VB.NET/WPF 实现。
Phase 1 只做一件事：**一个角色在屏幕里随机走、撞边拐弯、靠近鼠标就跑**。

## 构建

```
build.cmd          Release（/O2 /MT，静态 CRT）
build.cmd debug    Debug
```

产物在 `build\bin\FalcomDeskling.exe`，图标会一并拷过去。
`Chr` 资源目录默认在 exe 旁边找，也可以 `--root=` 指到 `WpfTest\Resource\Chr`。

> 这台机器上的 VS2022 Community 的 C++ 组件是半残的（`cl.exe` 在，
> `vcvarsall.bat` 和 include 不在），所以 `build.cmd` 靠**文件存在性**
> 去挑可用的 MSVC 装件，而不是信 vswhere 的 `isComplete`。

## 运行

```
FalcomDeskling.exe                      默认：33ms 一拍，200% 尺寸，鼠标避让
FalcomDeskling.exe --tick=15            贴回 WPF 的 66fps 节奏
FalcomDeskling.exe --scale=100          半尺寸
FalcomDeskling.exe --skin=科洛丝        指定角色
FalcomDeskling.exe --stats              每秒往 exe 旁写一行自采样
```

托盘图标：左键暂停/继续，右键退出。

完整参数列表见 `FalcomDeskling.exe --help`。

## Phase 1 移植了什么

行为全部对着 `WpfTest` 抄，常量都集中在 `src/config.h`：

| 项目 | 值 | 出处 |
|---|---|---|
| 方向数 | 8 | `RunnerEntity.vb` |
| 走路速度 | 300 px/s | 同上 |
| 跑步速度 | 900 px/s | 同上 |
| 显示倍率 | 2.0 | `MainWindow.xaml` ScaleFactor |
| 帧率 | 走 12fps / 跑 20fps | `SpriteSheet.vb` |
| 贴图格 | 256×256，中心 x=128，脚底 y=175 | 同上 |
| 鼠标迟滞 | 2 个身位内跑，4 个身位外走回 | `RunnerEntity.vb` |
| 撞边 | 加权随机选向 + 角落锁定 0.9s | 同上 |

`--wander` 对应 WPF 的 `MouseMode.Off`：这条路上**只逛不躲**，距离被强制成
无穷，所以永远不会进跑步态。默认不开，也就是 Escape 模式——走路时不主动转向，
只有撞墙或被鼠标蹭到才改方向，这点和 `UpdateFlee` 的 `Select Case` 一致。

## 架构：为什么和 WPF 长得不一样

WPF 那版是一个铺满屏幕的透明窗口加 `DrawingVisual`。这里换成
**每个角色一个小分层窗口**：

- 位置变了 → `UpdateLayeredWindow` 顺带把 `ptDst` 定过去
- 帧变了 → 重拷一次表面
- 什么都不变 → 什么都不做

PNG 解码走 WIC，只解当前方向那条横向条带，最多缓存 2 条（LRU），
避免八方向全解码把内存吃光。

窗口尺寸和锚点**不按单条 strip 算**，而是开机扫全部 16 条求一个全局并集框，
一次定死 —— 见下面性能实测第 2 条。

**方格边长从图自己读，不是写死 256。** 横向拼帧的约定是「格高 == 图高」，
所以 `cell = h`、`frames = w / h`；中心线、脚底线、身长、半宽这些按 256 定的
常量全部乘 `cell/256` 换算。以前 `cellH = min(256, h)` 的写法，塞进一个 512
格径的角色就会把 cell 压成 256、frames 算成 16，`AlphaBox` 只扫最上面 256 行
—— 放大后的脚底线在 350 行，**整条腿连框都进不去**。
`tools\probe_mkskin.ps1` 能把任意皮肤放大 N 倍造出不同格径的测试皮肤，
`--root=...\testdata --skin=ChrBig` 就能验。

## 性能实测（这台机器，3840×2400 @200%）

`tools\bench_final.ps1` 跑出来的，单位是单核百分比：

| 配置 | CPU | 备注 |
|---|---|---|
| `--nopresent`（只算不画） | **0.4~0.7%** | 物理那部分是干净的 |
| 默认（33ms + 每拍重传） | **2.9%** | 16MB（全局框后不再每拍 Resize） |
| `--tick=15` | 7~8% | 66fps |
| `--move --tick=15` | 11% | 见下面那条 |
| **WpfTest Debug 版** | **55.16%** | **320MB** |

两个和预期不一样的结论，都是量出来的：

1. **`SetWindowPos` 挪一个分层窗口要 2.0~2.3ms，`UpdateLayeredWindow`
   整张重传只要 1.0ms。** 所以"多数帧只搬窗口不动像素"这条省资源的路
   在这台机器上是**反效果**，默认改成每拍都重传，快了一倍。
   `--move` 留着，想验证就切回去。
2. **256×256 那格里只有约 86×88 像素是真东西**，剩下八成是透明的。
   解码时按帧扫一遍 alpha 求并集包围盒，窗口和拷贝都只覆盖这一块，
   一次上传从 4MB 降到 0.5MB。这是单点最大的开销削减。

   但**不能按单条 strip 各自求框**：科洛丝八条 walk 的 cropH 在 83~91 之间跳，
   cropY 在 90~95 之间跳，每换一次方向就要 Resize 一次窗口，锚点 `cropY-175`
   跟着跳 —— 落在屏幕上就是"下半截没了"。`Resize` 里那句 `SetWindowPos` 带
   `SWP_NOMOVE`，尺寸变了位置不动，窗口从旧原点往下长出一截 memset 的透明区。
   现在改成开机扫全部 16 条求一个**全局框**，窗口尺寸和锚点全程恒定，
   `max(maxY, SPRITE_FEET_Y)` 那条兜底也一并删了。
   框比单条的大，但省掉了每拍的 Resize，CPU 反而从 3.3~4.0% 降到 2.9%。

   **注意**：上面这条 Resize 机制虽然真实存在，但它**不是**"缺脚"的真凶。
   真凶在 `Blit` 里：`StretchDIBits` 把源矩形的 y 当成 bottom-up 来定位，
   传 `srcY=95` 它实际从 `256-95-84=77` 行开始读，内容整体往下挪 18 行，
   脚底那一段被窗口下沿截掉。判据很硬：源格内容在 `y[103..174]`，按 `srcY=95`
   该落在表面 `y[32..316]`，实测却在 `y[104..335]` 且顶到底边，而
   `104 = (103-77)*4`。`--debugfull`（整格不裁，`srcY=0/srcH=256`）翻回去
   还是 0，所以那条路一直是对的 —— 这也解释了为什么只有裁剪会掉内容。
   现在 `srcY` 传 `height-(b.y+b.h)` 做反向补偿，GDI 再翻一次正好落回 `b.y`。
   改完之后 8 个角色 × 25 对逐帧比对，预测和实测偏差全为 0。

   缓存那边顺手修了一处：以前 `StripCache::Get` 发的是 `deque` 里的裸指针，
   `erase` 一挪槽位，"指针还有效、内容已经是另一条 strip"就会悄悄发生，而
   `LoadFrames` 里 `path == _stripPath` 又让它每拍都跳过修正。现在发
   `shared_ptr`，淘汰只放掉缓存自己那份引用。

物理本身（速度、撞边、避让、随机）几乎不花钱，`--nopresent` 那条就是它的地板。
剩下的全是窗口呈现，而目标 `<0.5%` 只有在这一档才够得到——只要还想每拍把画面
弄到屏上，就绕不开那 1ms 一次的重传。

## 工具

- `tools\bench_final.ps1` 主基准，直接读 `--stats` 的自采样算每次调用成本
- `tools\bench.ps1` 按 `--scale` 扫一遍
- `tools\bbox.ps1` 量贴图条带里 alpha 非零的范围

调试用的那批 `probe_*.ps1` 定位完已经清掉了。要再查几何问题，靠
`--debugframe` / `--debugmarks` 直接在屏幕上画线最快：品红是窗口四边、
绿是脚底线、青是中心线、黄/白是这一拍内容真正的首末行。

## 已知边界

- Phase 1 只有一个角色，多角色、点击交互、状态机其余分支还没接
- 33ms 一拍意味着 600px/s 的速度每步走 20px，比 WPF 的 66fps 略糙；
  介意的话 `--tick=15`，代价是 CPU 翻到 7%
- 撞边判定按物理像素做，多显示器混合 DPI 还没测
