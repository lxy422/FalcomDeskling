#pragma once

#include "config.h"
#include "wic.h"
#include "window.h"

#include <random>
#include <string>
#include <vector>

namespace fd {

// 一个坐在桌面上乱走的人：位置、方向、状态机、撞墙挑向全在这儿。
// 逻辑照搬 RunnerEntity.vb，只砍掉了 Phase 1 用不到的那几支（遭遇/跪姿/表情）。
// 武器这一档这轮只做手动开关，自动抽剑/收剑那套要等敌人系统，先不写。
class Runner {
public:
    struct Options {
        std::wstring skinDir;   // ...\Chr\科洛丝
        // 这套皮肤有没有武器那两套图。没有的话 SetArmed 会退回走/跑，
        // 免得兴冲冲切过去却去开一个不存在的文件
        bool hasWeaponArt = false;
        double k = 1.0;         // scale * dpiScale，身长/半宽/脚底都从它现算
        double dpi = 1.0;       // dpiScale，把 DIU 常量换算成物理像素
        bool wander = false;    // 走路时也每隔 1.5 秒自己换向
        bool smooth = false;    // 放大用 HALFTONE 滤波，否则最近邻
        bool noPresent = false; // 对照实验：什么都不往屏上画
        // 分层窗口挪一次位置比整张表面重传一遍还贵（实测 2.2ms vs 1.1ms），
        // 所以默认每拍都走 UpdateLayeredWindow；--move 才退回"只搬窗口"那条路
        bool uploadEveryTick = true;
        // 排查"缺脚"用：每拍上传后把刚画好的表面扫一遍，量最上/最下一行
        // 有非透明像素的行号。坐标推导演出来的余量对不对，看这个数就知道
        bool surfaceProbe = false;
        // 把上传前那一帧表面原样落盘成 PNG。"脚到底有没有画进表面"这个问题，
        // 只有这张图能回答
        bool dumpSurface = false;
        std::wstring dumpSurfacePath;
        // 同一拍里再抓一次窗口矩形覆盖下的屏幕。两张图一比：表面有、屏幕没有
        // 的那一段，就是丢在窗口之外 —— 摆放或合成，而不是解码
        bool dumpScreen = false;
        std::wstring dumpScreenPath;
        // ---- 肉眼诊断三件套：把"截断发生在哪一层"变成看得见的东西 ----
        // 沿窗口最外圈画一圈品红。窗口要是被裁或被挪出工作区，这圈会缺一块，
        // 缺哪条边直接说明是哪一侧的问题
        bool debugFrame = false;
        // 完全不裁剪：源矩形取整个方格而不是全局框，窗口也跟着放大到整格。
        // 角色因此显示完整 => 丢的内容在框里；照旧缺 => 框是清白的，
        // 问题在 StretchDIBits 或窗口摆放
        bool debugFull = false;
        // 画脚底线（绿）和中心线（青），再画一遍"如果不裁会用的整格框"（红）。
        // 有了这三条线，就能看出角色是贴在脚底线上还是掉到线下面去了
        bool debugMarks = false;
        // 把这一拍真正用的那一格原样落成 PNG，和 surface_dump 成对看：
        // 一张是"源里长什么样"，一张是"表面里长什么样"
        bool dumpCell = false;
        std::wstring dumpCellPath;
        // 调试开关：把朝向和帧钉死，方便逐方向逐帧回归截图比对。
        // 默认 -1 = 不干预，还是随机起步。
        int forceDir = -1;
        int forceFrame = -1;
        double ox = 0, oy = 0, worldW = 0, worldH = 0;  // 绝对物理像素
    };

    bool Create(LayeredWindow* win, const Options& opt);
    void Tick(double dt, Vec2 mouse);
    void Present();

    State GetState() const { return _state; }
    int Dir() const { return _dir; }
    Vec2 Pos() const { return _pos; }
    // 全局装备开关会逐个叫到每个 Runner 头上。缺武器图的时候这里自己吞掉，
    // 不报错也不崩 —— 托盘那个勾还是全局的，只是这个人没得可换
    void SetArmed(bool flag);
    bool Armed() const { return _armed; }
    // 这个人到底有没有武器图，供托盘标"缺武器"用
    bool HasWeaponArt() const { return _opt.hasWeaponArt; }
    // 现在这一拍在放哪套动作，取图的路径只从这里出
    Art ArtOf() const {
        if (_armed) return (_state == State::Run) ? Art::Run2 : Art::Standby;
        return (_state == State::Run) ? Art::Run : Art::Walk;
    }
    // 身长按皮肤自己的格径缩放：82 是按 256 格定的，换成 512 格就该是 164
    double BodyLen() const { return SPRITE_BODY_HEIGHT * _k * CellScale(); }

    struct Stats {
        uint64_t ticks = 0;
        uint64_t uploads = 0;   // 真的拷了表面的那一拍
        uint64_t moves = 0;     // 只搬窗口的那一拍
        uint64_t bounces = 0;
        // 三笔累计微秒：到底是物理在转，还是 SetWindowPos / UpdateLayeredWindow 在等
        uint64_t usPhysics = 0;
        uint64_t usMove = 0;
        uint64_t usUpload = 0;
        // 调试用：最近一次 Blit 真正用的源矩形和窗口尺寸，对账用
        int blitSrcX = 0, blitSrcY = 0, blitSrcW = 0, blitSrcH = 0;
        int blitDstW = 0, blitDstH = 0;
        int cropX = 0, cropY = 0, cropW = 0, cropH = 0;
        int frameIndex = 0, cellW = 0;
        double k = 1.0;
        // 表面实测：窗口尺寸 + 真正有像素的第一/最后一行
        int winW = 0, winH = 0;
        int surfFirst = -1, surfLast = -1;
    };
    const Stats& GetStats() const { return _stats; }
    void SetStripCacheBytes(size_t* bytes, size_t* items) const {
        if (bytes) *bytes = _cache.Bytes();
        if (items) *items = _cache.Bytes() ? 1 : 0;
    }

private:
    void SetState(State st);
    void SetDirection(int dir);
    void UpdateState(double dt, double dist);
    void UpdateFlee(double dt);
    void UpdateEscape(double dt);
    void UpdateWander(double dt);
    void UpdateFrame(double dt);
    void Move(double dt);
    void FlickAway();

    int PickBounce(int breached);
    int BestFleeDir(Vec2 mouse);
    int BestFleeVec(double vx, double vy);
    int ScoreFlee(double vx, double vy);
    int DirFromVector(double vx, double vy) const;
    int DirAwayFromMouse(Vec2 mouse) const;
    bool AwayFromMouse(int dir) const;
    bool IntoWall(int dir) const;
    bool TowardEdge(int dir, int edge) const;
    bool ClearOfEdge(int edge) const;
    int CornerEdges() const;
    int CornerDir(int breached) const;
    Vec2 EdgeNormalSum(int breached) const;
    static void ReflectPool(int edge, int out[3]);
    std::vector<int> RankToCenter(const std::vector<int>& cands) const;
    int WeightedPick(const std::vector<int>& ranked);

    double CurrentSpeedPx() const {
        // 带武器跑用另一条腿速：300 和 900 之间取 520，不然拿了剑反而像在冰上滑
        if (_state == State::Run) {
            return (_armed ? ARMED_RUN_PX : RUN_PX) * _k * 0.5;
        }
        return WALK_PX * _k * 0.5;
    }
    // 窗口从"脚底锚点"往四个方向各伸出多少物理像素。这四个数全部从 _box 现推：
    // 窗口原点 = 脚底 + (box.y - 脚底线)，所以 box 的四条边就是窗口在屏幕上的
    // 真实边界。以前这里拿 BodyLen(=82) 当"窗口往上伸出多少"用，那是身体高度，
    // 两者只在"内容刚好从第 93 行开始"时才相等；金的内容从第 59 行就开始，
    // 窗口实际往上伸 464 像素，夹紧却只让开 352 —— 差出来的那 112 像素就是
    // 头发被屏幕顶切掉的直接原因
    double ExtUp() const {
        const BBox b = EffectiveBox();
        return (FeetPx() - (double)b.y) * _k;
    }
    // 注意下/右这两条没有 "-1"。框的最后一行虽然是 b.y+b.h-1，但窗口的边
    // 落在 b.y+b.h 这个**排他**的位置上：窗口原点是脚底往上 (feet-b.y)*k，
    // 窗口高 b.h*k，所以底边 = 脚底 + (b.y+b.h-feet)*k。减掉那个 1 会让
    // 夹紧余量凭空少掉一整行（k 像素），角色贴底边时脚就顶到任务栏了
    double ExtDown() const {
        const BBox b = EffectiveBox();
        return ((double)b.y + (double)b.h - FeetPx()) * _k;
    }
    double ExtLeft() const {
        const BBox b = EffectiveBox();
        return (CenterPx() - (double)b.x) * _k;
    }
    double ExtRight() const {
        const BBox b = EffectiveBox();
        return ((double)b.x + (double)b.w - CenterPx()) * _k;
    }
    // 脚底能走到的极限 = 窗口伸出量 + 离边余量。四个方向分开算，因为
    // box 一般不是以中心线/脚底线为对称的，左右上下这四个数互不相同
    double PadLeft() const { return ExtLeft() + EDGE_PAD * _dpi; }
    double PadRight() const { return ExtRight() + EDGE_PAD * _dpi; }
    double PadTop() const { return ExtUp() + EDGE_PAD * _dpi; }
    double PadBottom() const { return ExtDown() + EDGE_PAD * _dpi; }
    double Clearance() const { return BOUNCE_CLEARANCE * _dpi; }
    // 这两个只服务鼠标命中测试和"追进几个身长开跑"那种距离，跟窗口边界无关，
    // 别拿它们去夹位置
    double HalfW() const { return HALF_WALK_PX * _k * CellScale(); }
    // 窗口比工作区还高/还宽的时候，两边夹是无解的：这时候让窗口在那个轴上
    // 居中，至少头顶和脚底被切的比例一样，不至于整个跑到屏幕外面去
    static double ClampAxis(double v, double lo, double hi, double center) {
        if (hi < lo) return center;
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }
    double CenterX() const { return _ox + _worldW * 0.5; }
    double CenterY() const { return _oy + _worldH * 0.5; }
    bool BodyHit(Vec2 mouse) const;
    double BodyDistance(Vec2 mouse) const;
    std::wstring StripPath(Art a, int dir) const;
    void LoadFrames();
    void Blit();
    // 窗口尺寸和锚点唯一的来源。全局框求不出来才退回单条的框
    BBox EffectiveBox() const;
    // 诊断模式下的"有效框"：--debugfull 时换成整个方格，其余原样
    BBox DebugBox() const;
    // 在刚拷好的表面上面叠一圈诊断线。GDI 画上去的东西 alpha 是 255，
    // 所以一定会显示出来，不会被下面的透明像素吃掉
    void DrawOverlay(HDC dc, const BBox& b);
    void DumpCell();
    // 这套皮肤自己的方格边长，取不到时退回 CELL_PX。横向拼帧的约定是格高
    // 等于图高，所以这里取 cellH
    int CellPx() const {
        const int c = EffectiveBox().cellH;
        return c > 0 ? c : CELL_PX;
    }
    // 相对 256 基准的比例。config.h 里 128/175/82/21 那几个常量都是按 256
    // 格定的，皮肤换了格径就必须一起缩放，否则碰撞框和贴图会脱节
    double CellScale() const { return (double)CellPx() / CELL_PX; }
    // 中心线和脚底线按 cell 等比换算，不是写死的 128/175
    double CenterPx() const { return CellPx() * (double)SPRITE_CENTER_X / CELL_PX; }
    double FeetPx() const { return CellPx() * (double)SPRITE_FEET_Y / CELL_PX; }
    void ProbeSurface();

    LayeredWindow* _win = nullptr;
    Options _opt;
    // 缓存不再是成员：进程里就一份，两个人用同一张图就只解一次
    StripCache& _cache = StripCache::Shared();

    Vec2 _pos{};
    Vec2 _mouse{};
    State _state = State::Walk;
    // 装备武器是个开关，不是状态机里的一态：它只换动作集和腿速，
    // 走/跑的迟滞那套照旧在跑
    bool _armed = false;
    int _dir = 0;
    int _frameIndex = -1;
    int _frameCount = 0;
    StripPtr _strip;
    std::wstring _stripPath;
    bool _stripFailed = false;

    double _animClock = 0.0;
    double _bounceCool = 0.0;
    double _dirLock = 0.0;
    double _touchCool = 0.0;
    double _wanderClock = 0.0;
    double _fleeClock = 0.0;
    double _runTravel = 0.0;
    bool _fleeForce = false;
    bool _hold = false;
    int _lockEdge = 0;
    const char* _dirReason = "init";

    double _ox = 0, _oy = 0, _worldW = 0, _worldH = 0;
    double _k = 1.0, _dpi = 1.0;

    // 全部条带合起来的可见框，格内坐标。窗口尺寸和锚点只从它算，一次定死 ——
    // 单条的框每条都差几行，跳变落在屏幕上就是"下半截没了"
    BBox _box{};

    bool _needUpload = true;
    // 抓表面不再只抓第一拍 —— 掉腿是"走起来之后"才出现的，第一拍必然干净，
    // 只抓一次等于专门挑了个 bug 还不发作的时刻
    int _surfaceDumps = 0;
    // --dumpcell 的序号，和 surface_dump 用同一套计数方式
    int _cellDumps = 0;
    // 抓屏要晚一拍：UpdateLayeredWindow 只保证窗口表面更新了，DWM 把它合成到
    // 屏幕上还得等一帧，当场抓容易拿到空内容
    int _screenDumpLeft = 0;
    int _lastX = -1, _lastY = -1;
    Stats _stats{};

    std::mt19937 _rng;
};

}  // namespace fd
