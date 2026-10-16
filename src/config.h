#pragma once

#include <cmath>
#include <cstdint>

// 所有数值都从 WpfTest 的 RunnerEntity.vb / SpriteSheet.vb 逐条核对过来。
// 注释里标了出处，改这里之前先去那边看一眼，别两头对不上。
namespace fd {

// 方向编号沿用贴图文件名：0左 1左上 2上 3右上 4右 5右下 6下 7左下，y 向下为正
constexpr int DIR_COUNT = 8;
constexpr double SQRT2_HALF = 0.70710678118654752;

inline const double* DirDX() {
    static const double v[DIR_COUNT] = { -1.0, -SQRT2_HALF, 0.0, SQRT2_HALF,
                                         1.0,  SQRT2_HALF, 0.0, -SQRT2_HALF };
    return v;
}
inline const double* DirDY() {
    static const double v[DIR_COUNT] = { 0.0, -SQRT2_HALF, -1.0, -SQRT2_HALF,
                                         0.0,  SQRT2_HALF, 1.0,  SQRT2_HALF };
    return v;
}

// RunnerEntity.vb:39-43
constexpr int EDGE_LEFT = 1, EDGE_RIGHT = 2, EDGE_TOP = 4, EDGE_BOTTOM = 8;

// ---- 贴图几何（SpriteSheet.vb:25-31, CoreHalfWidth 的 Walk 档）----
constexpr int   CELL_PX            = 256;
constexpr int   SPRITE_FEET_Y      = 175;
constexpr int   SPRITE_CENTER_X    = 128;
constexpr int   SPRITE_BODY_HEIGHT = 82;
constexpr int   HALF_WALK_PX       = 21;

// ---- 物理常量（RunnerEntity.vb:45-96）----
constexpr double WALK_PX          = 300.0;   // 走路腿速，DIU/秒
constexpr double RUN_PX           = 900.0;   // = WALK_PX * RunSpeedMultiplier(3)
// 带武器跑是独立一条腿速，不走倍数：RunnerEntity.vb:48 WeaponRunSpeed
constexpr double ARMED_RUN_PX     = 520.0;
constexpr double WALK_FPS         = 12.0;
constexpr double RUN_FPS          = 20.0;
// 武器那两套的播放速度另有一张表（SpriteSheet.vb:66-87）：武器跑比无武器跑
// 慢一档，待机更慢，照搬走/跑那两个数会让剑影糊成一团
constexpr double STANDBY_FPS      = 7.0;
constexpr double RUN2_FPS         = 12.0;
constexpr double EDGE_PAD         = 6.0;     // 离边界留一点余量，免得贴墙来回抖
constexpr double BOUNCE_CLEARANCE = 6.0;     // 撞完往里让开几个像素
constexpr double BOUNCE_COOLDOWN_SEC = 0.15;
constexpr double BOUNCE_COMMIT_SEC   = 0.45;
constexpr double BOUNCE_ESCAPE_PX    = 180.0;
constexpr double RUN_START_LEN    = 2.0;     // 追进 2 个身长开跑
constexpr double RUN_END_LEN      = 4.0;     // 退到 4 个身长才换回走路
constexpr double WANDER_SEC       = 1.5;     // CharacterConfig.DfltWanderSec
constexpr double FLEE_STEER_SEC   = 0.06;
constexpr double TOUCH_COOLDOWN_SEC = 0.8;
constexpr int    PICK_NEAR_PCT    = 60;
constexpr int    PICK_MID_PCT     = 30;
constexpr int    PICK_FAR_PCT     = 10;
constexpr double CORNER_LEN         = 1.0;
constexpr double CORNER_HOLD_BONUS  = 3.0;
constexpr double CORNER_COMMIT_SEC  = 0.9;
// ---- 第 3 步配置表的默认值 ----
//
// 这一组是"还留在代码里的那些每人参数"的默认值。它们唯一的出处就是上面
// 那几个常量 —— 搬进 xml 之后这条等式不许断，否则"没有文件 = 今天的她"
// 这条基线就不成立了。
constexpr double KEEP_LEN         = 1.2;    // 追上了还留一点距离，别贴死
constexpr double ENC_NEAR_LEN     = 1.5;    // 遭遇的近档
constexpr double ENC_FAR_LEN      = 4.0;    // 遭遇的远档
constexpr double ATTACK_LEN       = 0.8;    // 出手距离
constexpr double ATTACK_COOL_SEC  = 1.2;    // 多久才允许再打一下
constexpr double EMOTE_GAP_SEC    = 30.0;   // 自己冒脸的间隔，只管随机那一半
constexpr double EMOTE_SHOW_SEC   = 2.4;    // 一张脸默认停多久
constexpr double REST_AFTER_SEC   = 300.0;  // 逛多久才允许跪
constexpr double REST_FAR_SEC     = 3.0;
constexpr double REST_DURATION_SEC = 20.0;
constexpr double DISARM_SEC       = 6.0;    // 看见怪多久之后自己把武器收回去
constexpr double IDLE_SEC         = 4.0;    // 附近没人，站多久再挪窝
constexpr int    DEFAULT_HP       = 5;      // 敌人默认五下

// ---- 同伴推开（RunnerEntity.vb:104-111）----
// 躲的时候同伴这一档：比撞墙(1000)轻、比不脱离(100)重。堵在墙角时还靠它绕得开，
// 又不至于为了躲一个人就僵在原地不动
constexpr double PEER_PENALTY       = 500.0;  // PeerPenalty
// 同伴身体框再让开这么多像素：两个人看着就没叠在一起，也不用挤到一次才反应过来
constexpr double PEER_PAD           = 10.0;   // PeerPad
// 往前探多远算"这条线上有个人的身体"，按身长说话
constexpr double PEER_LOOK_LEN      = 1.2;    // PeerLookLen
// 挨着推一下多久才允许再报一次脸，跟被鼠标拨那次的冷却一个规格
constexpr double PEER_BUMP_COOL_SEC = 0.8;    // PeerBumpCoolSec
// 身体框的半宽比碰撞用的 HalfW 再宽 6 个像素（TouchRect 里的 +6.0）。
// 鼠标命中和推开必须共用这一个框，别画第二套
constexpr double TOUCH_PAD_X        = 6.0;
// 身体框上下各留的那点余量：顶上是 -BodyLen-4，底下是 +4
constexpr double TOUCH_PAD_TOP      = 4.0;
constexpr double TOUCH_PAD_BOTTOM   = 4.0;

constexpr double DEFAULT_SCALE   = 2.0;      // MainWindow ScaleFactor
// WPF 那边 DispatcherTimer 是 15ms，但实测这一拍的成本全花在窗口调用上：
// 66fps 要 6.3% CPU，30fps 只要 2.3%，而贴图本身才 12fps —— 多出来的三十拍
// 一帧都没用上，纯粹是白挪窗口。所以默认 33ms，想贴回 WPF 的节奏就 --tick=15
constexpr int    DEFAULT_TICK_MS = 33;
constexpr double DT_CLAMP        = 0.2;

// 运行态。Walk/Run 是今天真在跑的那两支，后面几支是第 3 步先把名字立起来，
// 这一刀才接转移逻辑 —— 先立名字是为了让 character.xml 里的 <states> 有地方写。
enum class State { Walk, Run, Idle, Chase, Attack, BeAttacked, Stun, Rest, Dead, Victory };
inline constexpr int STATE_COUNT = (int)State::Victory + 1;

// 抢占等级。数值大的是赢家，同分 = 不许打断，等这一态放完。
//
// 这张表**故意不进 xml**：它问的不是"这个人什么脾气"，而是"谁敢打断谁"——
// 那是全局的规矩。写进个人配置里，八个人八份等级，最后就成了"谁先算谁赢"
constexpr int InterruptRank(State s) {
    switch (s) {
    case State::Dead:       return 100;
    case State::Stun:       return 80;
    case State::BeAttacked: return 70;
    case State::Attack:     return 50;
    case State::Chase:      return 40;
    case State::Rest:       return 30;
    case State::Victory:    return 30;
    case State::Run:        return 20;
    case State::Walk:       return 10;
    case State::Idle:       return 5;
    }
    return 0;
}

// 这几态是站着不动的。列在这儿而不是散到各处 if 里，是因为"动不动"和
// "用哪套图"是两件事，两处各判一遍早晚会对不上
inline bool StandsStill(State s) {
    return s == State::Idle || s == State::Rest || s == State::Attack ||
           s == State::BeAttacked || s == State::Stun || s == State::Dead ||
           s == State::Victory;
}

// 这几态放完自己就回去了，不用谁去叫它。挨一下和跪一下都算在这一类里 ——
// 它们必须记得"回来之后去哪"，否则追人追到一半挨一下，回来就变成乱逛
inline bool OneShot(State s) {
    return s == State::BeAttacked || s == State::Stun || s == State::Attack ||
           s == State::Victory;
}

// 每一态至少待够这么久才许被叫走。没有这一条，一帧里连着来两件事就会把
// 一次性动画切成两帧闪过去，画面上什么都看不见
constexpr double STATE_MIN_SEC       = 0.15;
constexpr double STUN_SEC            = 1.2;
constexpr double BEATTACKED_SEC      = 0.8;
constexpr double ATTACK_SEC          = 0.6;
constexpr double VICTORY_SEC         = 3.0;
constexpr double CHASE_LOST_SEC      = 2.0;   // 追丢了多久才放弃

// ---- 任务栏漫步 ----
//
// 这一档整个不走鼠标那套：人只在一根水平线上来回蹭，所以速度单独定，
// 比走路还慢一档才像"在任务栏上散步"而不是"在赶路"
constexpr double TB_WALK_PX          = 55.0;
constexpr double TB_PAUSE_MIN_SEC    = 1.5;   // 偶尔停下来，停这么短
constexpr double TB_PAUSE_MAX_SEC    = 4.0;   // 停这么久
constexpr double TB_POSE_SEC         = 2.2;   // 姿势摆多久
constexpr double TB_POSE_CHANCE      = 0.45;  // 停下来之后顺手摆个姿势的比例
constexpr double TB_EMOTE_MIN_SEC    = 6.0;   // 冒脸之间的空档
constexpr double TB_EMOTE_MAX_SEC    = 16.0;
constexpr double TB_EDGE_PAD         = 24.0;  // 两头各留一点，别贴着屏幕边翻面
constexpr int    TB_TURN_PAUSE_SEC   = 1;     // 翻面之后先站一下再走
// 摆姿势的播放速度。出拳那套比走路快、胜利那套慢，分开定才不像在原地抖
constexpr double TB_ATTACK_FPS       = 10.0;
constexpr double TB_VICTORY_FPS      = 6.0;
// 挨一下往后推几个身长。xml 里 kbLen=-1 就回到这个数 —— 别拿 0 当"没写"，
// 0 的意思真的是"一步都不退"
constexpr double HIT_KB_LEN          = 1.0;
// 同一个人多久才许再挨一下。没有这一条，两个人挤在一块会每拍推一次，
// 看着就像在互相弹球
constexpr double HIT_COOL_SEC        = 0.6;
// --hit= 的间隔：测试里得让它稳定地打得到，太快就全糊在冷却里了
constexpr double HIT_PROBE_SEC       = 2.5;

struct Vec2 {
    double x = 0.0, y = 0.0;
};

// 浮点矩形：左上角 + 尺寸。同伴的身体框和"这点落在框里没有"都靠它，
// 免得推开和命中测试各摆一套坐标算法
struct RectD {
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
    bool Contains(double px, double py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    RectD Inflated(double pad) const {
        return RectD{x - pad, y - pad, w + pad * 2.0, h + pad * 2.0};
    }
};

inline bool IsNaN(Vec2 v) { return std::isnan(v.x) || std::isnan(v.y); }

}  // namespace fd
