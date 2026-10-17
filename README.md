# FalcomDeskling

低资源的 Windows 桌面小人程序，使用 C++17、Win32 分层窗口和 WIC。支持八方向行走、鼠标避让、多角色同场、表情气泡、武器切换，以及任务栏漫步和三阵营自动战斗。

## 直接运行

解压便携包到有写入权限的目录，运行：

- `Taskbar.cmd`：一个角色沿工作区底沿慢速散步，偶尔停下、摆胜利姿势或冒表情。
- `Desktop.cmd` 或 `FalcomDeskling.exe`：一个随机角色在桌面活动，靠近鼠标会避让。

托盘左键暂停/继续；右键菜单可以切换任务栏漫步、装备武器、显示表情、添加/移除角色或退出。默认点击穿透，不抢输入焦点。

任务栏模式只保留第一个 chr，但允许多个 Enemy1/Enemy2 同场；关闭后回到桌面活动。被移除的其他 chr 需要从托盘重新添加，敌人的 HP 和倒地进度会保留。没有胜利素材时继续散步，不会用攻击动作替代。

## 构建

需要 Windows 10/11、包含 C++ 工具链和 Windows SDK 的 Visual Studio 或 Build Tools。

```bat
build.cmd          REM Release，/O2 /MT，静态 CRT
build.cmd debug    REM Debug
build.cmd test     REM Release + 显示几何、DPI、漫步和内存回归测试
build.cmd test build\alternate  REM 指定独立输出目录，避免覆盖运行中的程序
```

产物在 `build\bin\FalcomDeskling.exe`。测试使用仓库内的 `testdata\ChrBig`，无需完整外部人物包。脚本可从其他工作目录调用。

开发构建优先使用已有的 `build\bin\chr` 和 `Enemy`。不存在时，尝试为相邻 `..\WpfTest\Resource\Chr` / `Enemy` 建立目录联接。这只是开发便利；运行程序不依赖 WpfTest，也不会自行搜索开发机上的资源路径。

## 资源布局

```text
FalcomDeskling.exe
falcom.ico
chr/
  角色目录名/
    1.Run/dir0.png ... dir7.png
    2.Walk/dir0.png ... dir7.png
    character.xml                  可选
    ...                            其他可选动作
Enemy1/CH04500/                   编号 < 10000，10 HP
Enemy2/CH10000/                   编号 >= 10000，5 HP
```

角色素材应提供完整八方向的走/跑图；仅有 `2.Walk/dir0.png` 的目录也会被枚举，但缺少其他方向或动作会影响显示。PNG 是横向拼帧，单格边长取图片高度，支持不同格径；人物的中心线和脚底线按原始 256 格的比例换算。武器需同时具有 `4.standby` 和 `5.run2` 对应素材。

`character.xml` 可设置速度、鼠标模式、动作开关、表情、遭遇规则和状态转移；配置缺省时回到内置值。可用 `--dumpconfig` 查看最终值及其来源，`--no-config` 验证内置基线。九种动作均已接入；敌人缺少专用走路/跑步/休息/胜利素材时使用现有动作替代。[三阵营战斗说明](docs/combat.md)列出 HP、复起和生成规则。

## 常用参数

```bat
FalcomDeskling.exe --skin=科洛丝
FalcomDeskling.exe --skin=all --max=8
FalcomDeskling.exe --taskbar --monitor=2 --scale=150
FalcomDeskling.exe --root="D:\Falcom Art\Chr"
FalcomDeskling.exe --wander --tick=15
FalcomDeskling.exe --stats
FalcomDeskling.exe --enemy1=1 --enemy2=2 --enemyauto=10
FalcomDeskling.exe --taskbar --enemy=3 --enemyauto=10
```

| 参数 | 行为 |
|---|---|
| `--skin=名字` | 选择角色；支持逗号分隔或 `all`，默认随机一个 |
| `--root=路径` | 人物根目录，默认 exe 旁的 `chr` |
| `--enemyroot=路径` | 包含 `Enemy1`、`Enemy2` 的父目录，默认 exe 所在目录；兼容旧 Enemy 根目录 |
| `--enemy=N` | 启动时随机加入 N 个敌人 |
| `--enemy1=N` / `--enemy2=N` | 启动时加入指定阵营的敌人 |
| `--enemyauto=秒` | 开启自动生成，托盘也可开关和调整间隔 |
| `--taskbar` | 开启任务栏漫步 |
| `--monitor=N` | 选择活动屏幕；1 为主屏，其余按左坐标、上坐标排序 |
| `--scale=N` | 显示倍率百分比，默认 200，最大 800；再乘所在屏幕的 DPI 比例 |
| `--tick=N` | 更新周期毫秒，默认 33，最小 5；15 更流畅但开销更高 |
| `--wander` | 桌面模式主动随机转向，不躲鼠标 |
| `--armed` | 启动时装备武器，需有对应素材 |
| `--max=N` | 最大同场人数，默认 8 |
| `--smooth` | 使用 HALFTONE 缩放，默认最近邻 |
| `--notop` | 关闭置顶 |
| `--stats` | 每秒在 exe 旁写运行统计 |
| `--help` | 输出完整参数，包括诊断开关 |

参数同时支持 `--key=value` 与 `--key value`。

## 多屏与任务栏行为

角色在所选显示器的工作区内活动，支持位于主屏左边或上边的负坐标屏幕。显示器、工作区和 DPI 发生变化时重新计算尺寸与边界；检查周期最多约半秒。移除当前显示器后，角色回到最近的可用显示器，新增角色也使用更新后的活动区。

任务栏漫步和战斗以工作区底沿作为脚底线。底部任务栏时踩在任务栏上沿；顶部、侧面任务栏或无任务栏时，沿工作区底部散步。左右端点为整个角色留出空间。自动隐藏任务栏时按 Windows 报告的工作区定位，不追随任务栏弹出动画。

漫步中更新几何不会重播动作；暂停时仍能适应显示变化，刷怪、复起和死亡计时同时冻结。走路动画为 6fps，胜利动作只播放一次后停留。角色过宽、没有可走距离时保持原地。若缩放大到角色本身超过屏幕尺寸，仍可能超出屏幕，需减小 `--scale`。

当前选择屏幕通过启动参数完成，不支持跨屏自由游走或托盘内切屏。真实混合 DPI 多屏与热插拔仍建议在目标设备上验收；自动化回归覆盖合成几何、运行时 DPI 切换和当前连接屏幕的枚举。

## 制作独立便携包

```powershell
powershell -ExecutionPolicy Bypass -File tools\package.ps1
powershell -ExecutionPolicy Bypass -File tools\package.ps1 -ChrRoot "D:\Art\Chr"
powershell -ExecutionPolicy Bypass -File tools\package.ps1 -EnemyRoot "D:\Art"
```

输出为 `dist\FalcomDeskling-时间-随机后缀.zip`，包含 exe、图标、人物素材、启动脚本、README 和 SHA256 校验清单和历史记录文档。复制实际文件，不携带开发目录联接；每次生成新包，不覆盖旧版本。默认同时包含 Enemy1、Enemy2 素材；`-OmitEnemies` 可制作纯角色包。`-EnemyRoot` 指向这两个文件夹的父目录。已有可执行文件时使用 `-SkipBuild` 跳过构建。

使用独立输出目录时，给打包脚本加 `-BuildRoot build\alternate -SkipBuild`，从该目录读取 exe。独立构建目录和生成的便携包均可在使用完毕后删除。

分发包只需解压即可运行，不要求安装 Visual Studio、WpfTest 或单独的 VC 运行库。请使用自己有权使用和分发的素材。

## 实现与验证

每个角色一个小分层窗口。启动时扫描可用动作，计算统一的非透明包围盒；绘制只覆盖裁剪区域。PNG 条带按需解码，缓存按字节预算淘汰，角色用共享引用持有正在播放的条带。表情图集内嵌 exe；[表情编号与事件对应表](docs/emotes.md)及[逐帧图形对照表](docs/emotes.html)列出全部 18 组。

移动时默认用 `UpdateLayeredWindow` 同时上传和定位；静止且未换帧时跳过上传。`--move` 可改走移动窗口路径进行对照，`--nopresent` 仅运行逻辑。

- `build.cmd test`：显示/DPI、表情 236 组逐像素比较、三阵营 HP/寻敌/复起/任务栏战斗、刷怪调度、实际敌人素材校验及 1000 次内存解码回归。
- `tools\check_battle_live.ps1`：使用真实素材实跑桌面和任务栏战斗、检查自动生成、HP 与任务栏脚底线。
- `tools\check_package.ps1 -Zip <zip路径>`：解压到独立的含空格路径，校验所有文件并验证配置加载和漫步运行。
- `tools\check_taskbar.ps1`：实际运行下验证单角色、水平移动、慢速和端点转向。
- 其他 `tools\check_*.ps1`：配置、表情、状态和渲染诊断。部分旧脚本会停止所有同名进程，运行前留意已有桌面角色。
- [历史性能与裁剪排障记录](docs/performance-history.md)：早期测量，不能直接代表当前版本。

托盘“敌人”菜单支持随机加入、指定阵营加入、自动增加、间隔选择和清场。自动生成默认关闭，间隔默认 10 秒，可选 1/5/10/20/30/60/120 秒。尚未实现用户偏好保存和开机启动。

## 启动时未识别到角色

当前版本也修正了旧版随机选人时在遍历中反复抽签、误报“没有带 2.Walk 的人”的问题；连续 30 次随机启动已验证。程序默认查找 exe 旁的 `chr\角色名\2.Walk\dir0.png`，不是从命令行当前目录找。便携包需要完整解压；只复制 exe 会丢失外部人物素材。`--root` 应指向包含多个角色目录的 `chr`，不能指向单个角色或 `2.Walk`。

可运行 `powershell -ExecutionPolicy Bypass -File tools\check_startup.ps1`，验证当前 exe 从不同工作目录都能识别人物。报错会记录到 exe 旁的 `falcomdeskling_error.txt`，新记录包含时间、实际 exe 路径、启动参数、枚举数量和缺失文件样例。报告问题时保留完整路径与最新一段日志，便于区分不同目录中的程序副本。

旧 Enemy 目录可用 `powershell -ExecutionPolicy Bypass -File tools\split_enemies.ps1` 分组，脚本保存移动清单，CH10000 归入 Enemy2。仅移动指定 bin 内的真实资源目录；不会移动外部目录联接的目标。
