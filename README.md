# FalcomDeskling

一个 Windows 桌面小人程序，使用 C++17、Win32 和 WIC 实现。角色可以在桌面活动、沿任务栏散步、显示表情气泡，也可以与屏幕上出现的敌人自动战斗。

## 功能

- **桌面活动**：八方向行走与跑步、鼠标避让、边界转向、多角色同场及碰撞分离。
- **任务栏漫步**：沿所选屏幕的工作区底沿移动，偶尔停步、展示胜利动作或冒出表情。
- **表情气泡**：18 组动画表情，支持托盘手动显示、XML 事件映射。
- **武器与动作**：九套动作，包括持武器待机、攻击、受击、跪倒和胜利；缺少可选动作时尝试使用已有素材替代。
- **三阵营战斗**：chr、Enemy1、Enemy2 互相敌对，支持手动添加与定时生成敌人。
- **多屏与 DPI**：可指定活动屏幕，适配显示缩放及工作区变化。
- **托盘控制**：暂停/继续、切换漫步模式、添加/移除角色、装备武器、显示表情、管理敌人和退出。

默认点击穿透、窗口置顶，不抢输入焦点。当前不保存托盘偏好，也不自动配置开机启动。

## 快速开始

使用 Windows 10/11。取得编译版本，完整解压到有写入权限的目录，并准备人物素材。双击 `FalcomDeskling.exe`，程序会随机选择一个角色。

在 exe 所在目录打开终端，也可以这样启动：

```bat
REM 名字使用 chr 下的文件夹名
FalcomDeskling.exe --skin=科洛丝

REM 多角色同场
FalcomDeskling.exe --skin=科洛丝,艾丝蒂尔 --max=8

REM 任务栏漫步，显示倍率 150%
FalcomDeskling.exe --taskbar --scale=150

REM 加入敌人，之后每 10 秒自动增加一只
FalcomDeskling.exe --taskbar --enemy1=1 --enemy2=2 --enemyauto=10
```

托盘图标左键暂停/继续，右键打开菜单。自动生成默认关闭，也可以在“敌人”子菜单开启。

## 素材目录

程序默认在 **exe 旁边** 查找素材，与启动终端的当前目录无关。源码仓库不包含完整的人物和敌人素材包。

```text
FalcomDeskling/
├─ FalcomDeskling.exe
├─ chr/
│  ├─ 科洛丝/
│  │  ├─ character.xml          可选，当前角色配置
│  │  ├─ 1.Run/
│  │  │  ├─ dir0.png
│  │  │  └─ ... dir7.png
│  │  ├─ 2.Walk/
│  │  └─ ... 其他动作目录
│  └─ 艾丝蒂尔/
├─ Enemy1/
│  ├─ monster.xml              可选，Enemy1 公共配置
│  └─ CH04500/
│     ├─ character.xml         可选，皮肤独立配置
│     └─ ... 动作目录
└─ Enemy2/
   ├─ monster.xml              可选，Enemy2 公共配置
   └─ CH10000/
      └─ ... 动作目录
```

程序已内嵌 `falcom.ico` 和表情图集，正常运行无需单独放置这两个文件。

### 动作和方向

| 目录 | 动作 |
|---|---|
| `1.Run` | 无武器跑步 |
| `2.Walk` | 行走 |
| `3.kneel` | 休息 |
| `4.standby` | 持武器待机 |
| `5.run2` | 持武器移动 |
| `6.Attack` | 攻击 |
| `7.BeAttacked` | 受击 |
| `8.Kneel2` | 跪倒/倒地 |
| `9.Victory` | 胜利 |

每套动作使用 `dir0.png`～`dir7.png`，依次为：左、左上、上、右上、右、右下、下、左下。

PNG 是透明背景的横向帧条带，每帧为正方形，边长等于整张图片的高度；图片宽度应为高度的整数倍。中心和脚底锚点按原始 256 格素材比例换算，支持其他格径。

chr 应提供完整的行走、跑步八方向素材。程序用 `2.Walk/dir0.png` 识别角色目录，但仅有这个文件不代表全部动画可用。装备武器需要同时具有 `4.standby` 与 `5.run2` 素材。

敌人加入前会校验 **4～8 五套动作的全部八个方向**；缺文件或无法解码的皮肤会被跳过。运行时只能使用已有动作替代，无法生成缺失的动画帧。

## 配置人物：character.xml

文件放在 `chr/角色文件夹/character.xml`，保存为 **UTF-8**。修改后退出并重新启动程序，目前没有配置热重载。

配置是可选的，只写需要修改的部分即可，未填写的属性使用内置值。标签和属性名请按照示例拼写，尤其注意 `bodylen`、`armedRun`、`attackCoolSec` 的大小写。布尔值建议用 `true` / `false`。

### 可直接参考的完整示例

下面设置一个走路较慢、感知范围较大，并定制事件表情的人物。示例参数为自定义值，不全部等于程序默认值。

```xml
<?xml version="1.0" encoding="UTF-8"?>
<character name="科洛丝" id="1">
  <!-- 身长是感知、鼠标距离及击退距离的计算基准 -->
  <body bodylen="82" />

  <!-- 移动速度和动画帧率分别设置 -->
  <speed walk="220" run="700" armedRun="460"
         walkFrame="12" runFrame="20" />

  <!-- 鼠标进入 2 个身长内开始跑，远离至 4 个身长后再走 -->
  <mouse mode="flee" nearLen="2" farLen="4" keepLen="1.2" touchCool="0.8" />

  <!-- 随机漫游模式下，改变方向的时间间隔 -->
  <steer wander="2" />
  <rest allow="true" after="300" farSec="3" duration="20" />
  <armed default="false" />

  <!-- 距离单位为自身身长，冷却单位为秒 -->
  <encounters nearLen="1.5" farLen="6"
              attackLen="0.8" attackCoolSec="1.2" />

  <emotes random="高兴,喜欢,疑问" gap="30" showSec="2.4" />
  <emoteEvents default="all">
    <event name="BumpWalk" emotes="疑问,惊叹" />
    <event name="MouseNear" emotes="愤怒,无语" />
    <event name="Touched" emotes="黑线" />
    <event name="AttackedBy" emotes="惊叹" />
  </emoteEvents>

  <!-- 受击仍扣血，这里只调整反应和击退 -->
  <react>
    <hit name="*" kind="hit" kbLen="0.5" emote="惊叹" />
  </react>
</character>
```

根元素的 `name` 是配置描述；`--skin` 和按名字匹配的规则使用**文件夹名**。chr 的 `id` 可供旧遭遇规则引用，不决定阵营。运行时阵营由 chr、Enemy1、Enemy2 目录决定，敌人身份编号使用扫描结果。

### 速度、距离和装备

| 配置项 | 内置值 | 作用 |
|---|---|---|
| `body/bodylen` | `82` | 256 格基准下的身长，影响距离计算，不是显示尺寸 |
| `speed/walk` | `300` | 普通行走速度 |
| `speed/run` | `900` | 无武器跑步速度 |
| `speed/armedRun` | `520` | 持武器移动及自动战斗追击速度 |
| `speed/walkFrame` | `12` | 普通行走动画帧率，fps |
| `speed/runFrame` | `20` | 普通跑步动画帧率，fps |
| `mouse/nearLen` | `2` | 鼠标靠近至此身长范围内时，触发走转跑 |
| `mouse/farLen` | `4` | 鼠标远离且满足跑动距离后，允许跑转走 |
| `steer/wander` | `1.5` | `--wander` 随机转向间隔，秒 |
| `armed/default` | `false` | 初始装备武器，需有素材；也可用 `--armed` 开启 |
| `encounters/nearLen` | `1.5` | 旧遭遇规则的近距离档位 |
| `encounters/farLen` | `4` | 自动战斗目标感知范围，也用于旧遭遇规则 |
| `encounters/attackLen` | `0.8` | 攻击距离；战斗还会保留身体碰撞所需的最小距离 |
| `encounters/attackCoolSec` | `1.2` | 出手最小冷却时间，秒；实际频率还受动画时长影响 |

移动速度是基准值，实际位移另乘显示倍率、DPI 和内部速度系数；默认 200%、100% DPI 时，`walk="300"` 约为 300 屏幕像素/秒。身长另随素材格径换算。建议填正数，并保持 `farLen` 大于 `nearLen`。

任务栏**闲逛**使用独立节奏：基准速度 55、走路动画 6fps，不能用普通行走参数调速。进入战斗后追击使用 `armedRun`，战斗动画使用各动作固定帧率。

### 事件表情

表情可写中文名或编号，用逗号分隔；多个候选通常随机选择一个，`none` 表示不显示。

```xml
<emotes showSec="3" />
<emoteEvents default="none">
  <event name="BumpWalk" emotes="1,2" />
  <event name="Touched" emotes="黑线,狂怒" />
  <event name="MouseNear" emotes="none" />
</emoteEvents>
```

`default="all"` 保留未配置事件的默认表情；`none` 只启用明确列出的事件表情。

| 常用事件名 | 含义 |
|---|---|
| `BumpWalk` / `BumpRun` | 走路/跑步撞边 |
| `MouseNear` / `MouseFar` | 鼠标距离引起走跑状态变化 |
| `Touched` | 鼠标触碰角色 |
| `PeerBump` | 与其他角色碰撞 |
| `Armed` | 装备切换 |
| `EncounterFar` / `EncounterNear` | 旧遭遇逻辑识别到远/近目标 |
| `AttackedBy` | 非致命受击且未被受击规则覆盖时 |

状态转移、受击规则可能接管同一事件。敌人和倒地角色不显示气泡。任务栏闲逛默认定时显示“高兴/喜欢”；可用 emotes 的 random、gap、showSec 覆盖，其事件表情仍独立处理。

完整编号见[表情对照说明](docs/emotes.md)；[图形对照表](docs/emotes.html)可下载后在浏览器打开。

### 受击反应

```xml
<react>
  <hit name="某个敌人的文件夹名" kind="stun" kbLen="1" emote="眩晕" />
  <hit name="*" kind="normal" kbLen="0.5" />
</react>
```

`kind` 可用 `normal`、`hit`、`stun`、`none`：当前战斗中 `normal` 和 `hit` 均播放受击动作；`stun` 使用跪倒动作；`none` 跳过非致命受击动画，**仍然扣血和击退**。HP 归零的倒地/死亡规则优先。

`kbLen` 为击退的身长倍数，`0` 不击退，省略或负数使用默认 0.5 个身长（半步）。`emote` 指定受击表情，当前受击路径使用列表的第一个。每次命中都添加击退，连续同侧命中不会丢掉尚未完成的后退距离；致命命中也会在倒下时后退。任务栏只沿水平方向，桌面按远离攻击者的方向移动，边界会限制位移。默认半步约在 0.175 秒内完成。

匹配字段可用 `name`、`id`、`idRange`（如 `10000..11000`）、`faction`（`ally` / `enemy1` / `enemy2`），每条选一种。先匹配名字，再按顺序匹配其他具体规则，最后用 `name="*"` 兜底。建议优先按文件夹名匹配，避免旧数字阵营映射的歧义。

### 动作开关与高级状态规则

关闭某个动作的显示：

```xml
<actions>
  <action folder="9.Victory" on="false" />
</actions>
```

开关影响绘制选择，不会取消伤害、改变阵营或阻止 HP 归零。请保留基本移动和待机动作；缺少替代素材时程序仍会尝试其他现有动作。

`states` 支持高级事件转移，语法示例：

```xml
<states default="builtin">
  <from state="beAttacked">
    <to event="TimerExpired" state="return" />
  </from>
</states>
```

状态名包括 `walk`、`run`、`idle`、`chase`、`attack`、`beAttacked`、`stun`、`rest`、`dead`、`victory`。`from state="any"` 是通配，`to state="return"` 返回进入当前状态之前的状态。`to` 还可设置 `art`（动作目录名）、`emote`、`cooldown`（秒）。

查找顺序为 XML 具体状态 → XML 通配 → 内置规则。建议保留 `default="builtin"`，`none` 会停用未声明的内置转移。**任务栏和三阵营战斗有独立控制流程，不能靠此表修改攻击优先级、复起时间或死亡流程。** 部分事件只由旧状态机路径触发。

旧 `encounters` 还支持 `<other name="文件夹名" far="ignore" near="separate" />`，行为值包括 `ignore`、`separate`、`combat`、`chase`、`attack`、`flee`。这些用于旧角色遭遇逻辑，不能改写当前三阵营的固定敌对关系，同阵营不会因 XML 产生伤害。

### 鼠标、自动休息和随机表情

以下配置已接入实际行为：

| 字段 | 默认值 | 作用 |
|---|---|---|
| `mouse/mode` | `flee` | `flee` 避让、`follow` 跟随、`off` 忽略鼠标并随机漫游；命令行 `--wander` 优先按 off 处理 |
| `mouse/keepLen` | `1.2` | follow 接近至此身长距离后停下，鼠标移远后继续跟随 |
| `mouse/touchCool` | `0.8` 秒 | flee 模式下两次鼠标触碰反应之间的冷却；follow/off 不会被鼠标弹开 |
| `rest/allow` | `true` | 是否允许自动休息；follow 模式不自动休息 |
| `rest/after` | `300` 秒 | 无战斗时累计活动多久后允许休息 |
| `rest/farSec` | `3` 秒 | 鼠标持续位于 farLen 之外的时间门槛；鼠标靠近后重新计时 |
| `rest/duration` | `20` 秒 | 自动休息时长；鼠标进入 farLen 范围也会提前唤醒 |
| `emotes/random` | 桌面全部 18 组 | 定时表情候选池，支持中文名、编号、all、none；none 禁止定时表情，但不禁止事件/手动表情 |
| `emotes/gap` | 桌面 30 秒 | 随机表情间隔，0 或负数关闭；气泡忙碌时不覆盖，多候选时避免连续重复 |
| `emotes/showSec` | 桌面 2.4 秒 | 表情显示时长，气泡最短显示 0.6 秒 |

follow 的走跑阈值与 flee 相反：超过 farLen 开始跑着跟随，回到 nearLen 内改为走路，到 keepLen 停住。

自动休息使用 `3.kneel`，退出休息后重新累计活动时间。战斗优先于休息，战斗中重置活动/远离计时；任务栏模式使用自己的停步节奏，不运行桌面自动休息。状态转移规则应保留内置 RestBegin/RestEnd 处理，否则需自行声明对应转移。

桌面随机表情在休息和倒地时暂停，不会顶掉正在显示的气泡。任务栏未显式设置 random/gap 时保留原默认：首次约 3 秒、之后间隔 6～16 秒，候选为高兴/喜欢；显式设置后使用配置池/间隔。敌人不显示气泡。

跟随鼠标的最小配置：

```xml
<character>
  <mouse mode="follow" nearLen="2" farLen="4" keepLen="1.2" />
  <emotes random="高兴,喜欢" gap="15" />
</character>
```

## 敌人和自动战斗

| 阵营目录 | 素材编号 | 默认 HP | 优先攻击 | HP 归零后 |
|---|---|---|---|---|
| `chr` | 不限制为敌人编号段 | 10 | Enemy2 → Enemy1 | 倒地约 5 秒，随后恢复满血 |
| `Enemy1` | CH 编号 < 10000 | 10 | Enemy2 → chr | 倒下约 2 秒，再淡出约 1 秒并移除 |
| `Enemy2` | CH 编号 ≥ 10000 | 5 | chr → Enemy1 | 同上 |

不同阵营互相攻击，同阵营不造成伤害。每次有效出手扣 1 HP，出手开始即结算；受击硬直不提供额外无敌。优先级在感知范围内生效，同优先级选择较近目标，并避免频繁换目标。

chr 倒地使用 `8.Kneel2`，期间不会被选择为目标，不攻击、不受伤。入场中的敌人暂不参与攻击和碰撞。遇敌时自动装备可用武器，无目标一段时间后收起；手动装备切换会暂缓自动切换。

### 敌人公共配置

在 `Enemy1/monster.xml` 或 `Enemy2/monster.xml` 设置对应阵营的公共参数，根元素仍为 `character`：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<character>
  <speed walk="180" armedRun="400" />
  <encounters farLen="6" attackLen="0.8" attackCoolSec="1.5" />
  <enemy hp="10" repeat="1" idleSec="4" disarmSec="6" />
</character>
```

加载顺序为内置值 → 阵营 `monster.xml` → 皮肤 `character.xml`。普通数值由后者覆盖；规则列表会累计，重复匹配可能先命中公共规则，建议公共文件只放通用数值。chr 不读取敌人公共配置。

`idleSec` 为敌人无目标时站立等待时间，之后短暂走动；`disarmSec` 为自动装备后、无目标时的收武器等待时间，默认分别为 4 和 6 秒。示例 hp="10" 可作为 Enemy1 的默认配置；Enemy2 可用 hp="5"。hp 支持 1～1000000 的整数，由阵营公共配置提供默认值，也可在单个敌人 character.xml 覆盖。非法值会记录警告并保留公共值或阵营默认值。阵营和攻击优先级仍不可修改，chr 始终为 10 HP。

### 重复出现与同屏上限

在阵营公共 monster.xml 的 `<enemy>` 中设置 `repeat`，也可由具体皮肤的 character.xml 覆盖：

| repeat 值 | 行为 |
|---|---|
| `0` | 同一皮肤本次运行只生成一次，死亡退场后不再刷新 |
| `1` | 可以再次生成，同屏最多一只；前一只完全退场后才释放名额 |
| `N > 1` | 可以再次生成，同屏最多 N 只同皮肤实例 |

Enemy1 默认 `repeat="1"`，Enemy2 默认 `repeat="4"`。上限按**同一种皮肤/CH 文件夹**计数，不是整个阵营的总数量；在场实例跨桌面和任务栏模式统一计数，包括入场和死亡淡出阶段。每个实例的 HP、目标和动作独立。

例如 Enemy2 的配置可以写成：

```xml
<character>
  <enemy hp="5" repeat="4" idleSec="4" disarmSec="6" />
</character>
```

手动添加、启动参数和自动生成均遵守此设置。达到上限时不会关闭自动生成，空位释放后按下一次间隔继续刷新。repeat 为 0 的出现记录在重启或托盘“清场”后重置。修改配置后需重启；合法值为 0～1000000 的整数，非法值保留公共配置或阵营默认值并记录警告。

### 生成规则

- 托盘“敌人”菜单支持随机加入、指定阵营、自动开关、间隔和清场。
- 自动默认关闭，默认间隔 10 秒；菜单可选 1、5、10、20、30、60、120 秒，每个间隔总共加入一只。
- 可用皮肤等概率抽取，不对阵营加权；Enemy1 素材较少时自然较少出现。
- 手动与自动添加都受 enemy/repeat 限制。允许重复的皮肤可再次随机选中，达到同屏上限时等待名额释放；仅当所有可用皮肤均不可再出现时自动关闭。
- 清场清空敌人和已出现记录、重置计时，保留自动开关。
- 桌面模式从四边入场，任务栏从左右入场。暂停冻结生成、战斗、复起和淡出计时。

更多行为说明见[三阵营战斗说明](docs/combat.md)。

## 任务栏与多屏

任务栏模式保留一个 chr，允许多个敌人同场；进入模式时移除的其他 chr，退出后需要重新添加。追击和击退沿水平脚底线进行，没有敌人后恢复漫步。切换模式保留存留角色的 HP、倒地/死亡进度与生成记录。

角色沿所选显示器的**工作区底沿**活动：底部任务栏时在任务栏上方，顶部/侧面任务栏或无任务栏时沿工作区下边缘。自动隐藏任务栏按 Windows 报告的工作区处理，不追随弹出动画。

`--monitor=1` 为主屏，其余屏幕按位置排序。支持负坐标屏幕和 DPI/工作区变化；不支持跨屏自由游走或托盘切屏。角色缩放过大时可能裁切，请减小 `--scale`。

## 启动参数

在 exe 所在目录执行，也可把参数加到 Windows 快捷方式“目标”的末尾。

| 参数 | 说明 |
|---|---|
| `--skin=名字` | chr 下的文件夹名，可用逗号分隔或 `all`，默认随机一个 |
| `--max=8` | chr 同场数量上限，默认 8，不限制敌人数量 |
| `--root="D:\Art\Chr"` | 包含多个角色文件夹的父目录 |
| `--enemyroot="D:\Art"` | Enemy1、Enemy2 的父目录，也兼容旧 Enemy 目录 |
| `--enemy=3` | 启动时随机加入敌人 |
| `--enemy1=1` / `--enemy2=2` | 启动时加入指定阵营敌人 |
| `--enemyauto=10` | 开启自动生成，间隔单位秒 |
| `--taskbar` | 开启任务栏漫步 |
| `--monitor=2` | 指定显示器，1 为主屏 |
| `--scale=150` | 百分比倍率，默认 200，最大 800，另乘系统 DPI |
| `--tick=33` | 更新周期毫秒，默认 33，最小 5，较小值开销更高 |
| `--wander` | 桌面随机转向，替代主动鼠标避让 |
| `--armed` | 初始装备武器，需对应素材 |
| `--smooth` | 平滑缩放，默认最近邻 |
| `--notop` | 取消置顶 |
| `--dumpconfig` | 输出所选 chr 的最终配置后退出 |
| `--no-config` | 完全跳过 character.xml 和 monster.xml，使用内置默认值 |
| `--stats` | 在 exe 旁写运行统计 |
| `--help` | 查看全部参数和诊断选项 |

## 常见问题

### 修改配置后没有变化

退出程序，确认文件路径和 UTF-8 编码，再运行：

```bat
FalcomDeskling.exe --skin=科洛丝 --dumpconfig
```

打开 exe 旁的 `falcomdeskling_config.txt`，检查读取路径、最终值和警告。该命令仅导出所选 chr，不会因传入 `--enemy` 而列出全部敌人。导出中出现字段代表已解析，不保证已接入所有行为，请参考字段限制。

### 提示没有带 2.Walk 的人物

确认存在 `chr/角色文件夹/2.Walk/dir0.png`。`--root` 应指向人物的父目录，不能指向单个角色或 `2.Walk`。仅复制 exe 不会携带人物素材。

### 敌人没有出现

确认已手动添加或开启自动生成，检查 Enemy1/Enemy2 编号和五套动作八方向文件。缺损素材会跳过，自动皮肤池耗尽后会停止。入场移动和首次加载可能需要等待。

### 程序无法启动

查看 exe 旁的 `falcomdeskling_error.txt`，其中记录实际路径、参数和资源识别问题。运行目录需要允许写入诊断文件。

## 从源码构建

需要 Visual Studio 或 Build Tools，安装 MSVC C++ 工具链与 Windows SDK。运行：

```bat
build.cmd
REM 或生成调试版本
build.cmd debug
```

输出为 `build/bin/FalcomDeskling.exe`，使用静态 CRT，将 `resource/falcom.ico` 编译为 exe 图标。构建后按上面的目录布局自行准备素材。

仓库只跟踪源码、必要资源、构建脚本和用户文档。测试工具、测试素材、开发计划、日志、编译产物及便携包通过白名单排除。编译版可作为 GitHub Release 附件单独提供。
