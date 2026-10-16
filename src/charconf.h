#pragma once

#include "config.h"
#include "emote.h"
#include "wic.h"   // Art：转移表里要指哪套动作

#include <cstdint>
#include <string>
#include <vector>

namespace fd {

// 一个人的一整套定义：速度、动作开关、表情、鼠标怎么应、撞上谁怎么办，
// 全在这一张表上。
//
// 为什么是 XML 而不是接着往 config.h 里加常量：第 10 项那张"遇到谁怎么反应"
// 的表有八行、每行两档，行式写法会变成八组前缀套前缀，谁都没法一眼看完；
// 而说明必须挂在字段旁边 —— 只写在文档里的那一份，改一个人就得同时翻两处，
// 早晚对不上。
//
// 最重要的一条：**一个文件都没有的时候，这张表上的每个数都必须等于
// config.h 里那几个常量**。老行为是默认行为，配置文件只写差异，
// 不许悄悄把人换成另一套脾气。

// ---- 阵营：从 id 派生，不是第二个字段 ----
//
// 路线图第 4 步原本写的是"Entity 加一个 bool isEnemy"。这里**故意偏离**：
// 那个 bool 只能是 FactionOf(id) != Ally 的别名，两处定义迟早会对不上，
// 出现"血条有、遭遇表里却当她是我方"这类查不动的账。
enum class Faction : int {
    Wild      = -1,  // 只能出现在查找规则里，真 id 不会用到负数
    Unset     = 0,   // 没写过
    Ally      = 1,   // 1..100
    EnemyChar = 2,   // 101..200
    Monster   = 3,   // 201..1000
};

// 区间的唯一出处。将来想加一段"友方 NPC 1001..2000"只动这张表，
// 不许在别处出现字面量 100 / 200 / 1000。
struct FactionBand {
    int     lo = 0;
    int     hi = 0;
    Faction f  = Faction::Unset;
};

// 顺序即优先级，第一个命中的算。
const FactionBand* FactionBands();

// 阵营的唯一出处。别的地方一律不许再写 if (id >= 101 && id <= 200)
Faction FactionOf(int id);
bool  IsHostile(int id);   // EnemyChar || Monster
bool  HasHp(int id);       // 语义是"有没有血条"，不是"是不是敌人"

// ---- 三态字段 ----
//
// 判"这个字段有没有被写过"不能用"值等于默认"：walk="300" 明明写了、
// 和根本没写，在生效值上一模一样，但在排障上是两回事。--dumpconfig
// 必须能说出每一项是哪一层给的。
template <class T>
struct Opt {
    bool set = false;
    T    v{};
    T valueOr(T dflt) const { return set ? v : dflt; }
    void Assign(T x) { v = x; set = true; }
};

// ---- 遭遇 ----
//
// 撞上别的角色时的态度。separate 就是"不当回事"，只剩 Separate() 那一下硬挤
enum class EncounterKind : int {
    Ignore   = 0,
    Separate = 1,
    Combat   = 2,   // 站定摆出战斗姿态
    Chase    = 3,
    Attack   = 4,
    Flee     = 5,
};

// 距离档位。按**她自己的身长**算，缩放的时候不用重调
enum class EncounterBand : int { None = 0, Far = 1, Near = 2 };

struct Encounter {
    // 字段不能叫 far / near：windows.h 通过 windef.h 把这俩定义成了空宏，
    // 直接写就变成 "EncounterKind  = EncounterKind::Ignore;" 这种断掉的东西
    EncounterKind farKind  = EncounterKind::Ignore;
    EncounterKind nearKind = EncounterKind::Ignore;
    std::vector<int> emoteFar;
    std::vector<int> emoteNear;
    // 这一档要不要把武器自己掏出来。**不是**一种态度，是一种同时生效的要求：
    // 站定还是追上去，仍旧看 far/near 那两格 —— 这一行只管"顺手把剑抽了"
    bool armed = false;

    EncounterKind KindAt(EncounterBand b) const {
        return b == EncounterBand::Near ? nearKind : farKind;
    }
    const std::vector<int>* EmoteAt(EncounterBand b) const {
        return b == EncounterBand::Near ? &emoteNear : &emoteFar;
    }
};

// 匹配方式。加第六种（按距离段、按血量……）只要往 rules 里加一条 kind，
// 不用碰查找函数本身 —— 这就是留的接口
enum class RuleKind : int {
    ByName = 0,
    ById,
    ByIdRange,
    ByFaction,
    ByWild,
};

struct EncRule {
    RuleKind kind = RuleKind::ByName;
    std::wstring name;      // ByName
    int lo = 0, hi = 0;     // ByIdRange；ById 时 lo == hi
    Faction faction = Faction::Unset;   // ByFaction
    Encounter act;
};

// ---- 挨打反应 ----
//
// **这不是第七种态度**。态度说的是"我要理不理他"，这一条说的是
// "他打到我我长成什么样"。两件事混在一张表里，"追上去打"和
// "被打之后怎么挨"就会互相改写。
enum class ReactKind : int {
    Normal = 0,   // 代码那一条走什么还走什么：等于今天
    Hit    = 1,   // 只演 7.BeAttacked 那一下
    Stun   = 2,   // 跪一下，走 8.Kneel2
    None   = 3,   // 挺住，什么都不演，只扣血
};

struct Reaction {
    ReactKind kind = ReactKind::Normal;
    std::vector<int> emote;
    // 往后退几个身长。-1 = 不置一声，还按代码算的那个距离
    double kbLen = -1.0;
};

// 一条反应规则 = 匹配方式 + 反应。匹配方式复用 RuleKind，和遭遇表同一套
struct ReactRule {
    RuleKind kind = RuleKind::ByName;
    std::wstring name;
    int lo = 0, hi = 0;
    Faction faction = Faction::Unset;
    Reaction act;
};

// ---- 状态机 ----
//
// State 这个枚举住在 config.h：Runner 已经拿它当成员用了，两处各有一份
// 迟早会对不上。这里只留名字之间的来回翻译。
const wchar_t* StateName(State s);
bool StateFromName(const std::wstring& s, State& out);

// 事件。物理 / 计时器 / 遭遇 三个来源最后都汇到这一个入口
enum class GameEvent : int {
    None = 0,
    BumpWalk,
    BumpRun,
    MouseNear,
    MouseFar,
    Touched,
    PeerBump,
    Armed,      // 装备开关换了动作集
    EncounterFar,
    EncounterNear,
    InRange,
    Lost,
    AttackedBy,
    AttackHit,
    HpZero,
    RestBegin,
    RestEnd,
    StunBegin,
    TimerExpired,
    Count,
};

const wchar_t* EventName(GameEvent e);

// 一行转移。from == "any" 是通配行，优先级排在具体行之后
struct StateRule {
    State from = State::Idle;
    GameEvent ev = GameEvent::None;
    State to = State::Idle;
    bool anyFrom = false;
    // true = 不往某个固定态去，而是回到"进这一态之前"那个。xml 里写 state="return"
    // 就落在这里。挨一下和跪一下都靠它 —— 不然追人追到一半挨一下，回来就变乱逛
    bool back = false;
    // 空 = 这一态不换动作集
    Opt<Art> art;
    Opt<std::vector<int>> emote;
    Opt<double> cooldownSec;
};

// 内置那张表。一个字都没写过的时候就是它
const std::vector<StateRule>& BuiltinStates();

// ---- 一张表 ----
struct CharacterConfig {
    // 是哪一类，不是哪一个：怪物可以共用一个号，遇到谁就照那一行反应
    int  id = 0;
    Faction faction = Faction::Unset;
    std::wstring name;

    // 速度
    Opt<double> walkPx, runPx, armedRunPx;
    Opt<double> walkFrameRate, runFrameRate;

    // 鼠标
    Opt<int> mouseMode;      // 0=flee 1=follow 2=off
    Opt<double> nearLen, farLen, keepLen;
    Opt<double> touchCoolSec;

    // 逛／歇
    Opt<double> wanderSec;
    Opt<bool>   allowRest;
    Opt<double> restAfterSec, restFarSec, restDurationSec;

    // 遭遇的尺子
    Opt<double> encNearLen, encFarLen, attackLen, attackCoolSec;

    // 动作开关：没写的那几种就是开 —— 关掉的那种永远不会出现在画面上。
    // 关掉之后拿什么顶见 ArtKind()：只有"得一直动着的那几种"需要替身
    std::vector<Art> artOff;

    // 表情
    std::vector<int> emotePool;
    // 池子是不是真从文件里读出来的。没有这一项，--dumpconfig 会把"全开默认池"
    // 也标成 [xml]，于是 --no-config 那一遍里还剩几项挂着 xml，基线就说不清了
    bool emotePoolSet = false;
    Opt<double> emoteGapSec, emoteShowSec;
    // 没在这张表里出现过的事件还要不要按代码里那张默认映射去冒脸
    Opt<bool> eventDefault;
    std::vector<std::pair<std::wstring, std::vector<int>>> eventEmote;

    // 装备
    Opt<bool> armedDefault;

    // 尺寸：身长是所有距离的尺子
    Opt<double> bodyLen;

    // 敌人那几项。血条这件事只长在敌人身上，所以我方那份文件里没有这一项
    Opt<int> hp;
    Opt<double> idleSec;
    Opt<double> disarmSec;

    // 遭遇表。顺序即优先级，先写的先赢
    std::vector<EncRule> rules;

    // 挨打反应
    std::vector<ReactRule> reactRules;

    // 状态转移
    std::vector<StateRule> stateRules;
    Opt<bool> statesDefault;   // false = 没写过的转移就不许发生

    // 读文件时攒下来的问题，上桌那一行日志里一次报完
    std::vector<std::wstring> warnings;
    // 这个文件里认下来几项。0 = 没有文件或者全是默认
    int fields = 0;

    bool Empty() const { return fields == 0; }
};

// 按目录缓存。一处加载，实体、完整性判据、托盘三处都从这里读，别另开一扇门。
//
// idHint 是"xml 里没写 id= 时该用哪个号"：我方由 ScanChr() 的排序序号给，
// 敌人一律 201。xml 真写了 id= 就以它为准，这个 hint 只是兜底 ——
// 顺序 id 一增删就会全体错位，所以引用别人的时候一律先认名字
const CharacterConfig& ConfigFor(const std::wstring& dir, int idHint = 0);

// 全部失效，只走常量。用来证明"xml 那一路真的在起作用"
void SetConfigDisabled(bool off);
bool ConfigDisabled();

// 缓存换没换过。遭遇那一趟每拍要看八个人的身份，每次都去 map 里捞一遍
// 太贵 —— 实测八人同场 CPU 从 2.2% 掉到 4.2%。拿这个数当代次，
// 代次没变就说明手里那份还是新的，不用再去查
uint64_t ConfigGeneration();

// 阵营查找：名字 → 精确 id → id 区间 → 阵营 → 通配
const Encounter* MatchEncounter(const CharacterConfig& c,
                                const std::wstring& peerName, int peerId);
// 同一张表也可以按名字写：换一批人物时号数最容易撞车，而名字是各自夹的
// 文件夹名，天生不会撞 —— 所以认名字是首选，id 只当"类"用
const Reaction* MatchReaction(const CharacterConfig& c,
                              const std::wstring& attackerName, int attackerId);

EncounterBand BandFor(const CharacterConfig& c, double peerLen);

// 这个动作开不开
bool ActionOn(const CharacterConfig& c, Art a);
// 画面到底该用哪套图。这里只替"关掉的那一种"找替身，绝不动物理那一侧的
// 判定：拿它去换速度，半宽和腿速就会脱节
Art ArtKind(const CharacterConfig& c, Art a);

// --dumpconfig 用：把最终生效的整张表打出来，并标出每一项是哪一层给的
std::wstring DumpConfig(const CharacterConfig& c, const std::wstring& dir);

// ---- 事件 -> 表情 ----
//
// 交回来的两样东西得能分开：
//   nullptr = 这一栏压根没写过，该回到内置那张映射表
//   空表    = 这里就该安静，不是"没配过"
const std::vector<int>* EmotePoolFor(const CharacterConfig& c, GameEvent ev);
// 有 by= 那一层在，光"事件名被写过"已经说明不了全部问题
bool MentionsEvent(const CharacterConfig& c, GameEvent ev);
// 内置那张映射表。一个字都没写过的时候才用它
int BuiltinEmote(GameEvent ev, unsigned coin);

// 这一拍该走哪条转移。找不到给 nullptr。
//
// 查找顺序：xml 的具体行 → xml 的通流行 → 内置表（default="builtin" 才看）。
// default="none" 那位到第三层就停 —— 没写过的转移就不许发生，别拿内置表替他做主
const StateRule* MatchState(const CharacterConfig& c, State from, GameEvent ev);

}  // namespace fd
