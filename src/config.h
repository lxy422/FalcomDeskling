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
constexpr double PEER_PENALTY       = 500.0;  // ScoreFlee 里同伴那一档

constexpr double DEFAULT_SCALE   = 2.0;      // MainWindow ScaleFactor
// WPF 那边 DispatcherTimer 是 15ms，但实测这一拍的成本全花在窗口调用上：
// 66fps 要 6.3% CPU，30fps 只要 2.3%，而贴图本身才 12fps —— 多出来的三十拍
// 一帧都没用上，纯粹是白挪窗口。所以默认 33ms，想贴回 WPF 的节奏就 --tick=15
constexpr int    DEFAULT_TICK_MS = 33;
constexpr double DT_CLAMP        = 0.2;

enum class State { Walk, Run };

struct Vec2 {
    double x = 0.0, y = 0.0;
};

inline bool IsNaN(Vec2 v) { return std::isnan(v.x) || std::isnan(v.y); }

}  // namespace fd
