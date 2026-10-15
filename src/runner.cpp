#include "runner.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace fd {

namespace {
// 目录名那张表搬到 wic.cpp 的 FolderOf(Art) 去了：现在取哪套动作不只取决于
// 走还是跑，还取决于有没有装备武器，一个 State 说不清

inline uint64_t NowUs() {
    static LARGE_INTEGER f = [] {
        LARGE_INTEGER x{};
        QueryPerformanceFrequency(&x);
        return x;
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e6 / (double)f.QuadPart);
}

int RandDir(std::mt19937& g) {
    std::uniform_int_distribution<int> d(0, DIR_COUNT - 1);
    return d(g);
}
}  // namespace

bool Runner::Create(LayeredWindow* win, const Options& opt) {
    if (!win || !win->Valid()) return false;
    _win = win;
    _opt = opt;
    _k = opt.k;
    _dpi = opt.dpi;
    _ox = opt.ox;
    _oy = opt.oy;
    _worldW = opt.worldW;
    _worldH = opt.worldH;

    // 桌上站好几个人时，它们是在几毫秒之内连着构造的。光拿 tick 混种子，
    // 几个人的随机序列会高度相关 —— 表现为两个人同时往同一个方向走、
    // 或者同时撞同一面墙。把窗口指针和 this 都搅进去，地址天然不一样
    std::random_device rd;
    const uint32_t addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(win) >> 8) ^
                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this) << 1);
    _rng.seed(rd() ^ static_cast<uint32_t>(GetTickCount64()) ^ addr);

    // 开机扫一遍全部条带求并集框。这一步只解码不缓存，峰值内存还是一条的量。
    // 必须排在挑出生点之前：可行走区是从 _box 推出来的，框还没算出来就夹位置，
    // 用的是回退值，等于出生点按错的边界挑
    if (!ComputeSkinBBox(opt.skinDir, _box)) {
        _box = BBox{};  // 求不出来就退回按单条的框走，别把自己撑死
        OutputDebugStringW(L"global bbox unavailable, falling back to per-strip crop\n");
    }

    _dir = RandDir(_rng);
    if (_opt.forceDir >= 0 && _opt.forceDir < DIR_COUNT) _dir = _opt.forceDir;
    _state = State::Walk;
    _armed = false;

    // 落在可行走区里随便一个点上，别一上来就贴着墙
    const double clr = Clearance();
    double loX = _ox + PadLeft() + clr, hiX = _ox + _worldW - PadRight() - clr;
    double loY = _oy + PadTop() + clr, hiY = _oy + _worldH - PadBottom() - clr;
    if (hiX <= loX) { loX = CenterX(); hiX = loX + 1.0; }
    if (hiY <= loY) { loY = CenterY(); hiY = loY + 1.0; }
    std::uniform_real_distribution<double> ux(loX, hiX), uy(loY, hiY);
    _pos.x = ux(_rng);
    _pos.y = uy(_rng);

    LoadFrames();
    if (_frameCount > 0) _frameIndex = RandDir(_rng) % _frameCount;
    if (_opt.forceFrame >= 0 && _opt.forceFrame < _frameCount) _frameIndex = _opt.forceFrame;
    _animClock = 0.0;
    _needUpload = true;
    return true;
}

void Runner::Tick(double dt, Vec2 mouse) {
    _mouse = mouse;
    ++_stats.ticks;

    if (_bounceCool > 0) _bounceCool -= dt;
    if (_dirLock > 0) _dirLock -= dt;
    if (_touchCool > 0) _touchCool -= dt;

    // 时间到还不算，得真让开那条边才放锁；正往这条边回冲的时候也不许放
    if (_lockEdge != 0 && _dirLock <= 0 && ClearOfEdge(_lockEdge) &&
        !TowardEdge(_dir, _lockEdge) && !TowardEdge(DirAwayFromMouse(mouse), _lockEdge)) {
        _lockEdge = 0;
    }

    // MouseMode.Off：远近不许改变它的任何打算，这里一次定死，底下全读同一个说法
    const double dist = _opt.wander ? std::numeric_limits<double>::infinity() : BodyDistance(mouse);

    // 规则 3：被鼠标蹭到，就往当前朝向的反方向 ±45 度弹开
    if (_touchCool <= 0 && BodyHit(mouse)) {
        FlickAway();
        _touchCool = TOUCH_COOLDOWN_SEC;
    }

    UpdateState(dt, dist);
    UpdateFlee(dt);
    Move(dt);
    UpdateFrame(dt);
}

// 规则 4 的迟滞：追进 2 个身长开跑，退到 4 个身长才换回走路
void Runner::UpdateState(double dt, double dist) {
    (void)dt;
    // 拿了剑就不走路：WpfTest 那边是 SetArmed 里直接把状态摁成 Run，这里照做。
    // 不然会出现"用待机那套图在走"——待机是站定的动作，画出来就是脚不沾地
    // 在地上滑
    if (_armed) {
        if (_state != State::Run) SetState(State::Run);
        return;
    }
    const double runIn = RUN_START_LEN * BodyLen();
    const double runOut = RUN_END_LEN * BodyLen();
    if (_state == State::Walk) {
        if (dist < runIn) SetState(State::Run);
    } else {
        if (dist > runOut && _runTravel >= runOut) SetState(State::Walk);
    }
}

void Runner::SetState(State st) {
    if (_state == st) return;
    _state = st;
    if (st == State::Run) {
        _fleeForce = true;
        _fleeClock = 0.0;
        _runTravel = 0.0;
    }
    _animClock = 0.0;
    _frameIndex = -1;
    LoadFrames();
}

// 装备武器只换动作集和腿速，不动状态机：走/跑那套迟滞照旧在跑。
// 缺武器图的人这里直接吞掉 —— 托盘那个勾是全局的，不能因为一个人没图
// 就整条命令报错，或者让他黑一块
void Runner::SetArmed(bool flag) {
    if (flag && !_opt.hasWeaponArt) flag = false;
    if (_armed == flag) return;
    _armed = flag;
    // 换了动作集就是换了一批条带，帧号得重头数，不然会拿走跑的帧号去套武器跑
    _animClock = 0.0;
    _frameIndex = -1;
    LoadFrames();
    _needUpload = true;
}

void Runner::SetDirection(int dir) {
    // 八方向之外的一律不接：漏出 -1 就会去开一个 dir-1.png
    if (dir < 0 || dir >= DIR_COUNT) return;
    if (_dir == dir) return;
    _dir = dir;
    LoadFrames();
}

void Runner::FlickAway() {
    const int away = (_dir + DIR_COUNT / 2) % DIR_COUNT;
    std::uniform_int_distribution<int> d(-1, 1);
    const int pick = (away + d(_rng) + DIR_COUNT) % DIR_COUNT;
    _dirReason = "touch";
    SetDirection(pick);
}

// 鼠标那一侧的走法：躲开（默认）或每隔一会儿自己换向。
// 对应 RunnerEntity.vb 的 Select Case _cfg.MouseMode：Off 一路只逛不躲，
// Escape 一路只在 Run 态里转向，走路时一个字都不动。
void Runner::UpdateFlee(double dt) {
    _hold = false;
    if (_opt.wander) {
        UpdateWander(dt);
        return;
    }
    if (_state == State::Run) UpdateEscape(dt);
}

void Runner::UpdateEscape(double dt) {
    // 刚撞完这一下先撑住，别这拍就被逃鼠标掰回原来那面墙上
    if (_dirLock > 0) return;
    if (IsNaN(_mouse)) return;
    _fleeClock += dt;
    if (!_fleeForce && _fleeClock < FLEE_STEER_SEC) return;
    _fleeClock = 0.0;
    _fleeForce = false;

    const int go = BestFleeDir(_mouse);
    if (std::strcmp(_dirReason, "corner") == 0) _dirLock = CORNER_COMMIT_SEC;
    SetDirection(go);
}

void Runner::UpdateWander(double dt) {
    _wanderClock += dt;
    if (_wanderClock < WANDER_SEC) return;
    _wanderClock = 0.0;
    _dirReason = "wander";
    SetDirection(RandDir(_rng));
}

// 朝当前方向推一步，越界就夹回来，并把撞到的边记下来
void Runner::Move(double dt) {
    if (_worldW <= 0 || _worldH <= 0) return;
    if (_hold) return;

    const double speed = CurrentSpeedPx();
    if (_state == State::Run) _runTravel += speed * dt;

    _pos.x += DirDX()[_dir] * speed * dt;
    _pos.y += DirDY()[_dir] * speed * dt;

    // 四个方向各用自己的窗口伸出量夹。以前上边拿 BodyLen(=82，身体高度) 当
    // "窗口往上伸出多少"用、下边干脆什么都不减，于是窗口会一边伸到屏幕外、
    // 一边缩回来 —— 头顶和脚底都会被切。这里夹的是"窗口矩形"落在工作区内，
    // 不是"脚底"落在工作区内
    const double loX = _ox + PadLeft(), hiX = _ox + _worldW - PadRight();
    const double loY = _oy + PadTop(), hiY = _oy + _worldH - PadBottom();
    int breached = 0;
    if (_pos.x < loX) {
        _pos.x = (loX > hiX) ? CenterX() : loX;
        breached |= EDGE_LEFT;
    } else if (_pos.x > hiX) {
        _pos.x = (loX > hiX) ? CenterX() : hiX;
        breached |= EDGE_RIGHT;
    }
    if (_pos.y < loY) {
        _pos.y = (loY > hiY) ? CenterY() : loY;
        breached |= EDGE_TOP;
    } else if (_pos.y > hiY) {
        _pos.y = (loY > hiY) ? CenterY() : hiY;
        breached |= EDGE_BOTTOM;
    }
    if (breached == 0) return;
    ++_stats.bounces;
    // 只要这一拍还在压这条边就重新记进锁，不然它反复撞同一面墙不回头
    _lockEdge |= breached;
    if (_bounceCool > 0) return;

    if (_state == State::Run) {
        // 逃跑途中撞墙不能交给随机反射，不然它挑一条朝鼠标的那条线，规则就毁了
        SetDirection(BestFleeDir(_mouse));
        _fleeForce = true;
        _fleeClock = 0.0;
    } else {
        SetDirection(PickBounce(breached));
    }
    _bounceCool = BOUNCE_COOLDOWN_SEC;
    _dirLock = BOUNCE_COMMIT_SEC;
    _lockEdge = breached;
}

void Runner::UpdateFrame(double dt) {
    if (_frameCount < 1) return;
    // 站住了就别让腿还在转，看着像踩在原地不动的跑步机上
    if (_hold) return;
    // --frame=N 钉死：动画时钟继续走，但帧号不许变，省得截图的时候还在漂
    if (_opt.forceFrame >= 0 && _opt.forceFrame < _frameCount) {
        _animClock = 0.0;
        if (_frameIndex != _opt.forceFrame) {
            _frameIndex = _opt.forceFrame;
            _needUpload = true;
        }
        return;
    }
    _animClock += dt;
    // 帧率按动作集取，不是按走/跑：武器跑比无武器跑慢一档，照搬那两个数
    // 会让剑影糊成一团
    double rate = WALK_FPS;
    switch (ArtOf()) {
        case Art::Run:     rate = RUN_FPS;     break;
        case Art::Standby: rate = STANDBY_FPS; break;
        case Art::Run2:    rate = RUN2_FPS;    break;
        case Art::Walk:    rate = WALK_FPS;    break;
    }
    const int idx = (int)std::fmod(std::floor(_animClock * rate), (double)_frameCount);
    if (idx != _frameIndex) {
        _frameIndex = idx;
        _needUpload = true;
    }
}

std::wstring Runner::StripPath(Art a, int dir) const {
    std::wstring p = _opt.skinDir;
    p += L'\\';
    p += FolderOf(a);
    p += L"\\dir";
    p += std::to_wstring(dir);
    p += L".png";
    return p;
}

void Runner::LoadFrames() {
    const std::wstring path = StripPath(ArtOf(), _dir);
    if (path == _stripPath && _strip) return;

    StripPtr s = _cache.Get(path);
    if (!s) {
        if (!_stripFailed) {
            _stripFailed = true;
            OutputDebugStringW((L"sprite 取不到 " + path + L"\n").c_str());
        }
        return;
    }
    _stripFailed = false;
    _strip = s;
    _stripPath = path;
    _frameCount = s->frames;
    if (_frameIndex >= _frameCount) _frameIndex = _frameCount - 1;
    _needUpload = true;
}


// ==================== 边界与几何 ====================

// 到身体中心的直线距离，读不到鼠标就当作很远
double Runner::BodyDistance(Vec2 mouse) const {
    if (IsNaN(mouse)) return 1e300;
    const double dx = mouse.x - _pos.x;
    const double dy = mouse.y - (_pos.y - BodyLen() * 0.5);
    return std::sqrt(dx * dx + dy * dy);
}

bool Runner::BodyHit(Vec2 mouse) const {
    if (IsNaN(mouse)) return false;
    const double hw = HalfW();
    const double top = _pos.y - BodyLen() - 4.0 * _dpi;
    const double bot = _pos.y + 4.0 * _dpi;
    return std::fabs(mouse.x - _pos.x) <= hw && mouse.y >= top && mouse.y <= bot;
}

int Runner::DirFromVector(double vx, double vy) const {
    if (std::fabs(vx) < 0.01 && std::fabs(vy) < 0.01) return _dir;
    // 极角八分区 0=东 1=东南 …，而贴图编号的零点在西边，所以整体要加 4
    const int k = ((int)std::lround(std::atan2(vy, vx) * 180.0 / 3.14159265358979323846 / 45.0)
                   % DIR_COUNT + DIR_COUNT) % DIR_COUNT;
    return (k + 4 + DIR_COUNT) % DIR_COUNT;
}

int Runner::DirAwayFromMouse(Vec2 mouse) const {
    if (IsNaN(mouse)) return _dir;
    return DirFromVector(_pos.x - mouse.x, (_pos.y - BodyLen() * 0.5) - mouse.y);
}

// 这个方向走出去，算不算还在离鼠标越来越远
bool Runner::AwayFromMouse(int dir) const {
    if (_state != State::Run) return true;
    if (IsNaN(_mouse)) return true;
    const double vx = _pos.x - _mouse.x;
    const double vy = (_pos.y - BodyLen() * 0.5) - _mouse.y;
    return DirDX()[dir] * vx + DirDY()[dir] * vy >= 0.0;
}

// 这个方向会不会把小人继续压进正贴着的那条墙。判据必须和 Move() 里的夹紧
// 用同一套边界，否则它会一边把位置夹回来、一边又认为"还在墙里"不肯放锁
bool Runner::IntoWall(int dir) const {
    const double clr = Clearance();
    if (DirDX()[dir] < 0.0 && _pos.x <= _ox + PadLeft() + clr + 1.0) return true;
    if (DirDX()[dir] > 0.0 && _pos.x >= _ox + _worldW - PadRight() - clr - 1.0) return true;
    if (DirDY()[dir] < 0.0 && _pos.y <= _oy + PadTop() + clr + 1.0) return true;
    if (DirDY()[dir] > 0.0 && _pos.y >= _oy + _worldH - PadBottom() - clr - 1.0) return true;
    return false;
}

bool Runner::TowardEdge(int dir, int edge) const {
    const Vec2 n = EdgeNormalSum(edge);
    return DirDX()[dir] * n.x + DirDY()[dir] * n.y <= 0.01;
}

// 是不是已经从刚才撞到的那条边让开足够远了
bool Runner::ClearOfEdge(int edge) const {
    if (edge == 0) return true;
    const double esc = BOUNCE_ESCAPE_PX * _dpi;
    if ((edge & EDGE_LEFT) != 0 && _pos.x < _ox + PadLeft() + esc) return false;
    if ((edge & EDGE_RIGHT) != 0 && _pos.x > _ox + _worldW - PadRight() - esc) return false;
    if ((edge & EDGE_TOP) != 0 && _pos.y < _oy + PadTop() + esc) return false;
    if ((edge & EDGE_BOTTOM) != 0 && _pos.y > _oy + _worldH - PadBottom() - esc) return false;
    return true;
}

// 两条边同时挨着才算角，单侧贴墙不算：沿墙滑本来就不该被拦下来
int Runner::CornerEdges() const {
    const double r = CORNER_LEN * BodyLen();
    int hit = 0;
    if (_pos.x <= _ox + PadLeft() + r) hit |= EDGE_LEFT;
    if (_pos.x >= _ox + _worldW - PadRight() - r) hit |= EDGE_RIGHT;
    if (_pos.y <= _oy + PadTop() + r) hit |= EDGE_TOP;
    if (_pos.y >= _oy + _worldH - PadBottom() - r) hit |= EDGE_BOTTOM;
    return hit;
}

// 撞到的那条边朝场内的法线。画布 y 往下长，所以下墙朝内是负 y
Vec2 Runner::EdgeNormalSum(int breached) const {
    Vec2 n{ 0.0, 0.0 };
    if ((breached & EDGE_LEFT) != 0) n.x += 1.0;
    if ((breached & EDGE_RIGHT) != 0) n.x -= 1.0;
    if ((breached & EDGE_TOP) != 0) n.y += 1.0;
    if ((breached & EDGE_BOTTOM) != 0) n.y -= 1.0;
    return n;
}

// 角里那条线就是两个分量都朝场内的 45 度对角线，不加随机
int Runner::CornerDir(int breached) const {
    if (breached == 0) return -1;
    const Vec2 n = EdgeNormalSum(breached);
    // 窗口太扁时两条竖边会一起命中，法线自己抵消了；这种时候别硬指一条走不通的线
    if (std::fabs(n.x) < 0.01 || std::fabs(n.y) < 0.01) return -1;
    return DirFromVector(n.x, n.y);
}

// 一条墙给出的 3 个候选方向
void Runner::ReflectPool(int edge, int out[3]) {
    int n = 0;
    if (edge == EDGE_LEFT) n = 4;          // 左墙，得朝右
    else if (edge == EDGE_RIGHT) n = 0;    // 右墙，得朝左
    else if (edge == EDGE_TOP) n = 6;      // 上墙，得朝下
    else n = 2;                            // 下墙，得朝上
    out[0] = n;
    out[1] = (n + 1) % DIR_COUNT;
    out[2] = (n + DIR_COUNT - 1) % DIR_COUNT;
}

// ==================== 撞墙挑方向 ====================

// 撞墙后只能在"这条墙的法线 ±45 度"那 3 条线里挑一条
int Runner::PickBounce(int breached) {
    const int cd = CornerDir(breached);
    if (cd >= 0) {
        _dirReason = "corner";
        _lockEdge |= breached;
        return cd;
    }

    std::vector<int> pool;
    const int edges[4] = { EDGE_LEFT, EDGE_RIGHT, EDGE_TOP, EDGE_BOTTOM };
    for (int e : edges) {
        if ((breached & e) == 0) continue;
        int one[3];
        ReflectPool(e, one);
        if (pool.empty()) {
            pool.assign(one, one + 3);
        } else {
            // 角落同时撞两条边，就取两边候选的交集
            std::vector<int> both;
            for (int d : pool) {
                if (std::find(one, one + 3, d) != (one + 3)) both.push_back(d);
            }
            if (!both.empty()) pool = both;
        }
    }
    if (pool.empty()) return (_dir + DIR_COUNT / 2) % DIR_COUNT;

    // 正贴着的那条墙就别再撞回去了
    std::vector<int> clean;
    for (int d : pool) {
        if (!IntoWall(d)) clean.push_back(d);
    }
    if (clean.empty()) {
        // 一条干净的都不剩，说明人挤进角落里了：照墙朝场内的法线挑
        const Vec2 nrm = EdgeNormalSum(breached);
        if (std::fabs(nrm.x) < 0.01 && std::fabs(nrm.y) < 0.01) {
            clean.push_back((_dir + DIR_COUNT / 2) % DIR_COUNT);
        } else {
            double bestScore = -1e300;
            for (int d : pool) {
                const double s = DirDX()[d] * nrm.x + DirDY()[d] * nrm.y;
                if (s > bestScore) bestScore = s;
            }
            for (int d : pool) {
                if (DirDX()[d] * nrm.x + DirDY()[d] * nrm.y >= bestScore - 0.01) clean.push_back(d);
            }
        }
    }

    // 正在逃的时候，撞墙挑的方向还得顺带远离鼠标，不然它一撞完就回头奔着你
    std::vector<int> best;
    for (int d : clean) {
        if (AwayFromMouse(d)) best.push_back(d);
    }
    _dirReason = "bounce";
    if (best.empty()) best = clean;
    // 两道过滤都过了才轮到权重，反过来就会挑出一条"很朝场心但冲着鼠标去"的路
    return WeightedPick(RankToCenter(best));
}

// 按"谁更朝场心"从大到小排队
std::vector<int> Runner::RankToCenter(const std::vector<int>& cands) const {
    const double ux = _ox + _worldW * 0.5 - _pos.x;
    const double uy = _oy + _worldH * 0.5 - _pos.y;
    std::vector<int> left = cands;
    std::vector<int> order;
    // 已经站在正中间就没得排，原样发回去让权重自己抽
    if (std::fabs(ux) + std::fabs(uy) < 0.01) return left;
    while (!left.empty()) {
        int hit = 0;
        double bestScore = -1e300;
        for (size_t i = 0; i < left.size(); ++i) {
            const double s = DirDX()[left[i]] * ux + DirDY()[left[i]] * uy;
            if (s > bestScore) { bestScore = s; hit = (int)i; }
        }
        order.push_back(left[hit]);
        left.erase(left.begin() + hit);
    }
    return order;
}

// 名次换成权重。只剩两条时没法摊，60/30 直接端上去就是 2:1
int Runner::WeightedPick(const std::vector<int>& ranked) {
    const int count = (int)ranked.size();
    if (count == 0) return _dir;
    if (count == 1) return ranked[0];

    auto weight = [count](int rank) {
        if (rank == 0) return PICK_NEAR_PCT;
        if (rank == 1) return PICK_MID_PCT;
        return PICK_FAR_PCT;
    };

    int total = 0;
    for (int i = 0; i < count; ++i) total += weight(i);
    std::uniform_int_distribution<int> d(0, total - 1);
    const int r = d(_rng);
    int acc = 0;
    for (int i = 0; i < count; ++i) {
        acc += weight(i);
        if (r < acc) return ranked[i];
    }
    return ranked.back();
}


// ==================== 逃向 ====================

int Runner::BestFleeDir(Vec2 mouse) {
    if (IsNaN(mouse)) return _dir;
    return BestFleeVec(_pos.x - mouse.x, (_pos.y - BodyLen() * 0.5) - mouse.y);
}

// 脱离这条矢量指过去。贴墙时直线脱离等于往墙上撞，干脆八个方向全试一遍打分
int Runner::BestFleeVec(double vx, double vy) {
    const int ce = CornerEdges();
    if (ce != 0) {
        // 必须先拿到 cd 再判号：CornerDir 拿 -1 当哨兵，直接返回就是 SetDirection(-1)
        const int cd = CornerDir(ce);
        if (cd >= 0 && TowardEdge(DirFromVector(vx, vy), ce)) {
            _dirReason = "corner";
            _lockEdge |= ce;
            return cd;
        }
    }
    const double len = std::sqrt(vx * vx + vy * vy);
    if (len < 0.01) return _dir;
    vx /= len;
    vy /= len;
    if (std::strcmp(_dirReason, "corner") != 0) _dirReason = "flee";
    return ScoreFlee(vx, vy);
}

// 八条线各打一遍分。撞墙 1000 > 刚撞过的边 800 > 不脱离 100
int Runner::ScoreFlee(double vx, double vy) {
    const int ideal = DirFromVector(vx, vy);
    int best = ideal;
    double bestScore = -1e300;
    for (int d = 0; d < DIR_COUNT; ++d) {
        double score = DirDX()[d] * vx + DirDY()[d] * vy;
        if (score <= 0.0) score -= 100.0;
        if (IntoWall(d)) score -= 1000.0;
        if (_lockEdge != 0 && TowardEdge(d, _lockEdge)) score -= 800.0;
        // 平时只给一点点不动的偏向就够；锁住的时候必须加大力度，否则墙角上会擦来擦去
        if (d == _dir) score += (_lockEdge == 0 ? 0.6 : CORNER_HOLD_BONUS);
        if (d == ideal) score += 1.0;
        if (score > bestScore) { bestScore = score; best = d; }
    }
    return best;
}

// ==================== 上屏 ====================

// 窗口尺寸和锚点唯一的来源。全局框求不出来才退回单条的框
BBox Runner::EffectiveBox() const {
    if (_box.w > 0 && _box.h > 0) return _box;
    if (_strip) {
        BBox b;
        b.x = _strip->cropX;
        b.y = _strip->cropY;
        b.w = _strip->cropW;
        b.h = _strip->cropH;
        b.cellW = _strip->cellW;
        b.cellH = _strip->cellH;
        return b;
    }
    BBox b;
    b.x = 0;
    b.y = 0;
    b.w = CELL_PX;
    b.h = CELL_PX;
    b.cellW = CELL_PX;
    b.cellH = CELL_PX;
    return b;
}

// --debugfull：把源矩形从"全局框"换成"整个方格"，窗口尺寸跟着放大到整格。
// 这一步是决定性的：如果角色因此显示完整，丢的那一段就出在框的计算上；
// 如果照旧缺，框就是清白的，得往 StretchDIBits 或窗口摆放那边找
BBox Runner::DebugBox() const {
    if (!_opt.debugFull) return EffectiveBox();
    BBox b = EffectiveBox();
    const int cw = _strip ? _strip->cellW : b.cellW;
    const int ch = _strip ? _strip->cellH : b.cellH;
    if (cw > 0 && ch > 0) {
        b.x = 0;
        b.y = 0;
        b.w = cw;
        b.h = ch;
        b.cellW = cw;
        b.cellH = ch;
    }
    return b;
}

// 在刚拷完的表面 DC 上叠诊断线。GDI 画出来的像素 alpha 是 255，一定会显示，
// 不会被底下那些透明像素吃掉
void Runner::DrawOverlay(HDC dc, const BBox& b) {
    const int W = _win->W();
    const int H = _win->H();
    if (W < 3 || H < 3) return;

    // 先扫一遍刚拷好的表面，量真正有 alpha 的首/末行。这一步必须在画线之前，
    // 否则线本身会被算成内容
    int first = -1, last = -1;
    {
        const uint32_t* bits = static_cast<const uint32_t*>(_win->Bits());
        const int rowWords = _win->Stride() / 4;
        if (bits) {
            for (int y = 0; y < H; ++y) {
                const uint32_t* row = bits + (size_t)y * rowWords;
                bool any = false;
                for (int x = 0; x < W; ++x) {
                    if ((row[x] & 0xFF000000u) != 0) { any = true; break; }
                }
                if (!any) continue;
                if (first < 0) first = y;
                last = y;
            }
        }
    }
    _stats.winW = W; _stats.winH = H;
    _stats.surfFirst = first; _stats.surfLast = last;

    const int old = GetMapMode(dc);
    SetMapMode(dc, MM_TEXT);

    // 窗口最外圈：品红。窗口被裁、被挪出工作区、或者尺寸算错，这圈会缺
    HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 0, 255));
    HPEN oldPen = (HPEN)SelectObject(dc, pen);
    HBRUSH oldBr = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    // Rectangle 不含右/下边界，所以直接画四条边，一条都不许少
    MoveToEx(dc, 1, 1, nullptr);       LineTo(dc, W, 1);
    MoveToEx(dc, 1, H - 2, nullptr);   LineTo(dc, W, H - 2);
    MoveToEx(dc, 1, 1, nullptr);       LineTo(dc, 1, H);
    MoveToEx(dc, W - 2, 1, nullptr);   LineTo(dc, W - 2, H);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    // 实测内容边界：黄 = 第一个有像素的行，白 = 最后一个有像素的行。
    // 白线要是贴着窗口底边，内容就是顶到窗口边界了，下面再有一点就得出事
    if (first >= 0) {
        pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 0));
        oldPen = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, 0, first, nullptr);
        LineTo(dc, W, first);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        pen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
        oldPen = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, 0, last, nullptr);
        LineTo(dc, W, last);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
    }

    if (_opt.debugMarks) {
        // 当前变换：dst = (src - b.xy) * (win / b.wh)
        const double sx = b.w > 0 ? (double)W / (double)b.w : 1.0;
        const double sy = b.h > 0 ? (double)H / (double)b.h : 1.0;
        const int feetY = (int)((FeetPx() - (double)b.y) * sy);
        const int centerX = (int)((CenterPx() - (double)b.x) * sx);

        // 脚底线：绿。角色的鞋底应该正好踩在这条线上
        pen = CreatePen(PS_SOLID, 3, RGB(0, 255, 0));
        oldPen = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, 0, feetY, nullptr);
        LineTo(dc, W, feetY);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        // 中心线：青
        pen = CreatePen(PS_SOLID, 3, RGB(0, 255, 255));
        oldPen = (HPEN)SelectObject(dc, pen);
        MoveToEx(dc, centerX, 0, nullptr);
        LineTo(dc, centerX, H);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        // 整格在这个变换下会占多大：红。超出窗口的部分就是"被框掉"的部分
        const int cw = _strip ? _strip->cellW : b.cellW;
        const int ch = _strip ? _strip->cellH : b.cellH;
        if (cw > 0 && ch > 0) {
            const int x0 = (int)(-(double)b.x * sx);
            const int y0 = (int)(-(double)b.y * sy);
            const int x1 = (int)(((double)cw - (double)b.x) * sx);
            const int y1 = (int)(((double)ch - (double)b.y) * sy);
            pen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
            oldPen = (HPEN)SelectObject(dc, pen);
            MoveToEx(dc, x0, y0, nullptr);   LineTo(dc, x1, y0);
            MoveToEx(dc, x0, y1, nullptr);   LineTo(dc, x1, y1);
            MoveToEx(dc, x0, y0, nullptr);   LineTo(dc, x0, y1);
            MoveToEx(dc, x1, y0, nullptr);   LineTo(dc, x1, y1);
            SelectObject(dc, oldPen);
            DeleteObject(pen);
        }
    }

    SelectObject(dc, oldBr);
    SetMapMode(dc, old);
}

// 把这一拍真正用的那一格原样落盘。和 surface_dump 成对看：一张是"源里长什么
// 样"，一张是"表面里长什么样"，中间丢没丢东西一比就知道
void Runner::DumpCell() {
    if (!_strip || _frameCount <= 0) return;
    int idx = _frameIndex;
    if (idx < 0) idx = 0;
    if (idx >= _frameCount) idx = _frameCount - 1;

    wchar_t name[64];
    // 文件名里带上这一拍 Blit 真正用的源行区间和目的尺寸：
    // "期望"和"实际"对不上时，必须能证明两者是同一拍的同一组参数
    swprintf(name, 64, L"_%04d_sy%d_sh%d_dh%d", ++_cellDumps,
             _stats.blitSrcY, _stats.blitSrcH, _stats.blitDstH);
    const std::wstring p = _opt.dumpCellPath;
    const size_t dot = p.find_last_of(L'.');
    const std::wstring out = (dot == std::wstring::npos)
        ? (p + name) : (p.substr(0, dot) + name + p.substr(dot));
    SaveSurfacePng(out.c_str(), _strip->Cell(idx), _strip->cellW, _strip->cellH,
                   _strip->stride);
}

void Runner::Blit() {
    void* bits = _win->Bits();
    if (!bits) return;
    if (!_strip || _frameCount <= 0) {
        // 图没了也得把表面擦干净，不然会把上一套图的残影像留在屏上
        memset(bits, 0, (size_t)_win->Stride() * (size_t)_win->H());
        return;
    }

    int idx = _frameIndex;
    if (idx < 0) idx = 0;
    if (idx >= _frameCount) idx = _frameCount - 1;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = _strip->width;
    bi.bmiHeader.biHeight = -_strip->height;  // 源条带也是自顶向下
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = _win->MemDC();
    // 这里不许再 memset 一遍：SRCCOPY 会把整块表面逐像素盖满，擦一遍纯属白拷
    // 默认走最近邻——放大像素画本来就该是方块，HALFTONE 那套滤波贵一个数量级还没人看得出来
    SetStretchBltMode(dc, _opt.smooth ? HALFTONE : COLORONCOLOR);
    if (_opt.smooth) SetBrushOrgEx(dc, 0, 0, nullptr);

    // 只拷可见框那一小块，整格 256x256 里八成是透明像素，拷它纯属浪费。
    // 源框直接照抄全局 box：它和窗口尺寸是同一个来源，StretchDIBits 把整个
    // box 映进整个窗口，一行都掉不了。以前从窗口尺寸反推源尺寸那套是为了
    // 防"两次取整差出 1 行挤掉脚底"，现在 box 恒定，那个坑不存在了。
    // 注意这里必须和 Present 用同一个框，否则窗口尺寸和源矩形会脱节，
    // StretchDIBits 就得把一块东西拉伸到另一块尺寸上 —— 那正是丢内容的地方
    const BBox b = DebugBox();
    int srcW = b.w;
    int srcH = b.h;
    int srcX = idx * _strip->cellW + b.x;
    // 这里的 y 不是 b.y，是 b.y 的"反向补偿"，不是笔误。
    //
    // StretchDIBits 在这台机器上把源矩形的 y 当成 bottom-up 来定位：传进去
    // srcY，它实际从 height - srcY - srcH 那一行开始读，但行序仍然是正的
    // （不是镜像，只是起点被翻到下面去了）。所以传 b.y=95 时它读的是
    // 256-95-84=77 行，内容整体往下挪了 18 行，脚底那一段直接被窗口下沿
    // 截掉 —— 这就是"缺脚"的真身，和裁剪框、窗口尺寸、屏幕边界都无关。
    //
    // 反推的证据：源格内容在 y[103..174]，按 srcY=95 该落在表面 y[32..316]，
    // 实测却在 y[104..335]（且顶到底边）。104 = (103-77)*4，严丝合缝。
    // 而 --debugfull 走的是 srcY=0/srcH=256，翻回去还是 0，所以那条路一直是
    // 对的 —— 这也解释了为什么"整格渲染"看起来没问题、只有裁剪会掉内容。
    //
    // 补偿之后 GDI 再翻一次正好落回 b.y。x 方向没有这个毛病，别跟着翻。
    //
    // 这个补偿只在"方格铺满整张图的高度"时成立，也就是横向拼帧那种正常布局
    // （cellH == height）。万一哪天来了一张不是这种排布的图，格内坐标就不再
    // 是整图坐标了，硬翻会翻到别的地方去 —— 那种情况退回不翻，宁可画歪也别
    // 画到条带外面去
    int srcY = (_strip->cellH == _strip->height)
                   ? (_strip->height - (b.y + b.h))
                   : b.y;
    // 两个轴都得夹住。源框一旦越出条带末尾，StretchDIBits 会读到分配区之外，
    // 读到什么全看堆上接着的是什么 —— 之前只夹了 x，y 是漏的
    if (srcW > _strip->width) srcW = _strip->width;
    if (srcH > _strip->height) srcH = _strip->height;
    if (srcX + srcW > _strip->width) srcX = _strip->width - srcW;
    if (srcY + srcH > _strip->height) srcY = _strip->height - srcH;
    if (srcX < 0) srcX = 0;
    if (srcY < 0) srcY = 0;

    _stats.blitSrcX = srcX; _stats.blitSrcY = srcY;
    _stats.blitSrcW = srcW; _stats.blitSrcH = srcH;
    _stats.blitDstW = _win->W(); _stats.blitDstH = _win->H();
    _stats.cropX = b.x; _stats.cropY = b.y;
    _stats.cropW = b.w; _stats.cropH = b.h;
    _stats.frameIndex = idx; _stats.cellW = _strip->cellW;
    _stats.k = _k;

    StretchDIBits(dc, 0, 0, _win->W(), _win->H(), srcX, srcY, srcW, srcH,
                  _strip->pixels.data(), &bi, DIB_RGB_COLORS, SRCCOPY);

    // 诊断线画在拷贝之后：它要盖的是"这一拍真的画了什么"，不是元数据
    if (_opt.debugFrame || _opt.debugMarks) DrawOverlay(dc, b);
}

// 排查"缺脚"用：直接扫刚画好的表面，量真正有非透明像素的第一/最后一行。
// 坐标推导演出来的余量对不对，看这个数就知道 —— 它绕过了 crop/round/StretchDIBits
// 整条链，是"屏上实际有什么"的最硬读数
void Runner::ProbeSurface() {
    const int w = _win->W(), h = _win->H();
    _stats.winW = w;
    _stats.winH = h;
    _stats.surfFirst = -1;
    _stats.surfLast = -1;
    const uint32_t* bits = static_cast<const uint32_t*>(_win->Bits());
    if (!bits || w < 1 || h < 1) return;
    const int rowWords = _win->Stride() / 4;
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = bits + (size_t)y * rowWords;
        bool any = false;
        for (int x = 0; x < w; ++x) {
            if ((row[x] & 0xFF000000u) != 0) { any = true; break; }
        }
        if (!any) continue;
        if (_stats.surfFirst < 0) _stats.surfFirst = y;
        _stats.surfLast = y;
    }
}

void Runner::Present() {
    if (_opt.noPresent) return;

    // 尺寸只由全局框决定，全程恒定：换方向、走↔跑切换都不再 Resize 窗口
    const BBox b = DebugBox();
    const int winW = (std::max)(1, (int)std::lround(b.w * _k));
    const int winH = (std::max)(1, (int)std::lround(b.h * _k));
    if (winW != _win->W() || winH != _win->H()) {
        if (!_win->Resize(winW, winH)) return;
        _needUpload = true;
    }

    // 裁剪框左上角相对"格中心 + 脚底"这个锚点差了多少，就把它补回窗口原点。
    // 中心线和脚底线都按这套皮肤自己的格径换算，不是写死的 128/175
    const double offX = (double)b.x - CenterPx();
    const double offY = (double)b.y - FeetPx();
    const int x = (int)std::lround(_pos.x + offX * _k);
    const int y = (int)std::lround(_pos.y + offY * _k);
    const bool moved = (x != _lastX || y != _lastY);

    if (_opt.uploadEveryTick) _needUpload = true;
    if (_needUpload) {
        // 只有换帧才上传这一整张表面，顺带把位置也定了
        const uint64_t t0 = NowUs();
        Blit();
        if (_opt.surfaceProbe) ProbeSurface();
        if (_opt.dumpCell) DumpCell();
        // 每拍都落盘，不再只抓第一拍。掉腿是走起来之后才出现的，第一拍必然
        // 干净 —— 之前那个 !_surfaceDumped 的守卫等于专门挑 bug 还不发作的时刻
        if (_opt.dumpSurface && _surfaceDumps < 2000) {
            ++_surfaceDumps;
            wchar_t name[64];
            // 同样把参数写进文件名，和 cell_dump 那边逐字段对得上才算同一拍
            swprintf(name, 64, L"_%04d_sy%d_sh%d_dh%d", _surfaceDumps,
                     _stats.blitSrcY, _stats.blitSrcH, _stats.blitDstH);
            const std::wstring p = _opt.dumpSurfacePath;
            const size_t dot = p.find_last_of(L'.');
            const std::wstring out = (dot == std::wstring::npos)
                ? (p + name) : (p.substr(0, dot) + name + p.substr(dot));
            SaveSurfacePng(out.c_str(),
                           static_cast<const uint32_t*>(_win->Bits()),
                           _win->W(), _win->H(), _win->Stride());
        }
        _win->Present(x, y);
        _stats.usUpload += NowUs() - t0;
        // 抓屏以前排在上传之后两拍，理由是"等 DWM 合成"。但那两拍里人已经
        // 走开了 —— 抓屏用的是当下的 _lastX/_lastY，画面却是两拍前画的，
        // 窗框和窗口实际边界必然错开，看起来就像两个矩形叠在一起。那是探针
        // 的毛病，不是渲染的毛病，而且它会把真问题一起盖掉。现在改成同一拍
        // 里上传完立刻抓，坐标就用刚提交的 x,y，两边严格对齐
        if (_opt.dumpScreen) {
            SaveScreenPng(_opt.dumpScreenPath.c_str(), x, y, _win->W(), _win->H());
        }
        _needUpload = false;
        _lastX = x;
        _lastY = y;
        ++_stats.uploads;
    } else if (moved) {
        // 剩下的四分之三拍只搬窗口，一个像素都不拷
        const uint64_t t0 = NowUs();
        _win->MoveTo(x, y);
        _stats.usMove += NowUs() - t0;
        _lastX = x;
        _lastY = y;
        ++_stats.moves;
    }
}

}  // namespace fd

