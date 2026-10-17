#include "runner.h"
#include "store.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>

namespace fd {

// 桌上都有谁。名单是 main 的，Runner 只看不改
const std::vector<Runner*>* Runner::s_peers = nullptr;
bool Runner::s_separate = true;

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
    // 配置从缓存里现取，这里只留目录名。见 runner.h 上 Cfg() 那条注释
    _cfgDir = opt.skinDir;
    static uint64_t nextEntity = 0;
    _entityId = ++nextEntity;
    _faction = opt.faction == Faction::Unset ? DirectoryFaction(opt.skinDir) : opt.faction;
    _hp = StartingHp(_faction);
    for (int a = 0; a < ART_COUNT; ++a) {
        for (int dir = 0; dir < DIR_COUNT; ++dir) {
            std::error_code ec;
            _available[a][dir] = std::filesystem::is_regular_file(
                std::filesystem::path(opt.skinDir) / FolderOf((Art)a) / (L"dir" + std::to_wstring(dir) + L".png"), ec);
        }
    }

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
    // --state= 钉住这一态。它得在建起来的第一拍就生效，晚一拍表就会把它换走
    _stateLocked = false;
    if (_opt.forceState >= 0 && _opt.forceState < STATE_COUNT) {
        _state = (State)_opt.forceState;
        _stateLocked = true;
    }
    _stateClock = 0.0;
    _stateLimit = StateHold(_state);
    _returnTo = _state;
    _stats.state = (int)_state;
    _stats.stateChanges = 0;
    // 探针从命令行接进来，起步就给它一点余量，别第一拍就动手
    _hitProbe = _opt.hitProbe ? HIT_PROBE_SEC : 0.0;
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
    // 同伴那两条是秒表不是一拍计数：躲一次和挤一次谁也不该压着谁不放
    if (_peerCool > 0) _peerCool -= dt;
    if (_peerLogCool > 0) _peerLogCool -= dt;
    if (_hitCool > 0) _hitCool -= dt;
    // --hit=：桌上第一个人隔一会儿打后面的人一下。测试得让它稳定地打得到，
    // 太快就全糊在冷却里了
    // 探针只让名单里第一个去发。人人都发就成了八×八次互殴，
    // "谁挨了这一下"当场就说不清了
    if (_hitProbe > 0) {
        _hitProbe -= dt;
        if (_hitProbe <= 0) {
            _hitProbe = HIT_PROBE_SEC;
            if (s_peers && !s_peers->empty() && s_peers->front() == this) {
                for (const Runner* o : *s_peers) {
                    if (!o || o == this) continue;
                    const_cast<Runner*>(o)->Hit(const_cast<Runner*>(this));
                    break;
                }
            }
        }
    }

    if (TickBattle(dt)) {
        UpdateFrame(dt);
        _bubble.Tick(dt, _pos, HeadTop(), _k, FaceLeft());
        return;
    }

    // ---- 任务栏漫步 ----
    //
    // 这一档整条走自己的路：鼠标、同伴、遭遇表、撞墙那套全都不接。
    // 拿 if 去每个函数里挡一遍迟早会漏一处 —— 比如 BodyHit 那一下弹开，
    // 或者 SeparateAll 把两个人推开 —— 所以在这里一次分岔，底下全跳过
    if (_opt.taskbar) {
        UpdateTaskbar(dt);
        UpdateFrame(dt);
        _bubble.Tick(dt, _pos, HeadTop(), _k, FaceLeft());
        return;
    }

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
        _touchCool = Cfg().touchCoolSec.valueOr(TOUCH_COOLDOWN_SEC);
        Emit(GameEvent::Touched);
    }

    UpdateState(dt, dist);
    UpdateEncounter(dt);
    UpdateChase(dt);
    TickState(dt);
    UpdateFlee(dt);
    Move(dt);
    UpdateFrame(dt);

    // 泡泡排在最后：它得跟着这一拍真的脚底和头顶走，早一拍就是上一拍的位置
    _bubble.Tick(dt, _pos, HeadTop(), _k, FaceLeft());
}

// 规则 4 的迟滞：追进 2 个身长开跑，退到 4 个身长才换回走路
void Runner::UpdateState(double dt, double dist) {
    (void)dt;
    // 迟滞只管走/跑这两态。人要是被表叫去追人、挨打、歇着，这里一句都不许插嘴
    if (_stateLocked) return;
    if (_state != State::Walk && _state != State::Run) return;

    // 拿了剑就不走路：WpfTest 那边是 SetArmed 里直接把状态摁成 Run，这里照做。
    // 不然会出现"用待机那套图在走"——待机是站定的动作，画出来就是脚不沾地
    // 在地上滑
    if (_armed) {
        if (_state != State::Run) SetState(State::Run);
        return;
    }
    const double runIn = Cfg().nearLen.valueOr(RUN_START_LEN) * BodyLen();
    const double runOut = Cfg().farLen.valueOr(RUN_END_LEN) * BodyLen();
    const State before = _state;
    if (_state == State::Walk) {
        if (dist < runIn) SetState(State::Run);
    } else {
        if (dist > runOut && _runTravel >= runOut) SetState(State::Walk);
    }
    // 换态在这儿，报脸也在这儿：EnterState 不该自己发事件，那是 Emit 的活。
    // 走 EmitPhysical 而不是 Emit —— 这套迟滞带着 _runTravel 这个条件，
    // 表里同一件事再驱动一遍就成了两处说法
    if (_state == before) return;
    if (_state == State::Run) EmitPhysical(GameEvent::MouseNear);
    else EmitPhysical(GameEvent::MouseFar);
}

void Runner::SetState(State st) { EnterState(st, false); }

// 换态。fromRule=true 是转移表叫来的，得把"从哪儿来"记下来 —— 一次性动画
// 放完要回这儿。物理那边自己换的（走/跑那套迟滞）不许碰这个字段：
// 那是它自己的事，跟"挨一下之后回哪儿"无关
void Runner::EnterState(State st, bool fromRule) {
    if (_state == st) return;
    if (fromRule) _returnTo = _state;
    _state = st;
    ++_stats.stateChanges;
    _stats.state = (int)st;
    _stateClock = 0.0;
    _stateLimit = StateHold(st);
    if (st == State::Run) {
        _fleeForce = true;
        _fleeClock = 0.0;
        _runTravel = 0.0;
    }
    if (st == State::Chase) _chaseLost = 0.0;
    if (st == State::Walk || st == State::Run) _chaseTarget.clear();
    // 出手之后隔这么久才许再打一下。放在这儿而不是放在转移表里：表管的是
    // "什么时候换态"，这条管的是"什么时候再发那个事件"，两件事
    if (st == State::Attack) {
        _attackCool = Cfg().attackCoolSec.valueOr(ATTACK_COOL_SEC);
    }
    // 物理自己换态的时候不许把表里点的动作集留着 —— 那是表那一侧的事
    _artSet = false;
    _animClock = 0.0;
    _frameIndex = -1;
    LoadFrames();
}

// 每一态最多待多久。0 以外都靠它发 TimerExpired
double Runner::StateHold(State st) const {
    const CharacterConfig& c = Cfg();
    switch (st) {
    case State::BeAttacked: return BEATTACKED_SEC;
    case State::Stun:       return STUN_SEC;
    case State::Attack:     return ATTACK_SEC;
    case State::Victory:    return VICTORY_SEC;
    // 歇多久是配置里写的，不是这里定的
    case State::Rest:       return c.restDurationSec.valueOr(REST_DURATION_SEC);
    // 倒下不自己起来，除非哪天配了复活
    case State::Dead:       return 1e300;
    // 走/跑/追没有"放完"这一说，只有最小停留时间
    default:                return STATE_MIN_SEC;
    }
}

int Runner::DirToward(double dx, double dy) const {
    int best = 0;
    double bestDot = -1e300;
    const double n = std::sqrt(dx * dx + dy * dy);
    if (n <= 1e-9) return _dir;
    for (int d = 0; d < DIR_COUNT; ++d) {
        const double dot = (DirDX()[d] * dx + DirDY()[d] * dy) / n;
        if (dot > bestDot) { bestDot = dot; best = d; }
    }
    return best;
}

// 查表、看抢占等级、真换态。
//
// 抢占这条不许省：BeAttacked 打断 Chase 吗？Dead 打断一切吗？不问清楚一定会
// 写成"谁先算谁赢"。等级高的直接进，等级不够就得等这一态放完
bool Runner::ApplyStateRule(GameEvent ev, int byId) {
    if (_stateLocked) return false;
    if (_stateCool > 0) return false;
    const CharacterConfig& c = Cfg();
    const StateRule* r = MatchState(c, _state, ev);
    if (!r) return false;

    const State to = r->back ? (_returnTo == _state ? State::Walk : _returnTo) : r->to;
    // 这两条早退都交回 false："态没换成，但事还是发生了"，表情那一半还得让
    // 事件映射表去管 —— 等级不够拦住的只是换态，不是这件事本身
    if (to == _state) return false;
    if (InterruptRank(to) <= InterruptRank(_state) && _stateClock < StateHold(_state))
        return false;

    if (r->art.set) { _artOverride = r->art.v; _artSet = true; }
    else _artSet = false;

    if (r->cooldownSec.set) _stateCool = r->cooldownSec.v;
    else _stateCool = STATE_MIN_SEC;

    // 追人得先知道追谁：UpdateChase 只认这个名字，认不出来就按"跟丢了"算
    if (to == State::Chase && byId > 0) {
        if (s_peers) {
            for (const Runner* o : *s_peers) {
                if (o && o->Id() == byId) { _chaseTarget = o->Name(); break; }
            }
        }
    }

    // 表里点了名的表情优先于那张事件映射表
    if (r->emote.set) {
        if (!r->emote.v.empty()) {
            std::uniform_int_distribution<int> pick(0, (int)r->emote.v.size() - 1);
            ShowEmote(r->emote.v[pick(_rng)], c.emoteShowSec.valueOr(EMOTE_SHOW_SEC));
        }
        EnterState(to, !r->back);
        return true;   // 这里该不该安静，表说了算，别拿默认映射去补
    }
    // back=true 的那一行本来就是在"回到进态之前"，再写一次 _returnTo 只会把
    // 它换成刚放完那一态，下次回来就回错地方
    EnterState(to, !r->back);
    return false;
}

// 计时：最小停留、一次性动画放完、还有一态待太久自己散伙
void Runner::TickState(double dt) {
    if (_stateCool > 0) _stateCool -= dt;
    if (_attackCool > 0) _attackCool -= dt;
    _stateClock += dt;
    if (_stateLocked) return;
    if (_stateClock < _stateLimit) return;
    // 歇着的是等 RestEnd，不是等计时器 —— 到点就起来是它自己的规矩
    if (_state == State::Rest) { Emit(GameEvent::RestEnd); return; }
    if (OneShot(_state) || _state == State::Chase) Emit(GameEvent::TimerExpired);
}

// 追人：朝目标挪，进攻击距离就发 InRange，跟久了发 Lost
void Runner::UpdateChase(double dt) {
    if (_state != State::Chase) return;
    if (_stateLocked) return;

    const Runner* target = nullptr;
    if (s_peers) {
        for (const Runner* o : *s_peers) {
            if (!o || o == this) continue;
            if (o->Name() == _chaseTarget) { target = o; break; }
        }
    }
    if (!target) {
        // 目标不在了。这里不许立刻散伙：追丢了先站两秒看看，是原版的规矩
        _chaseLost += dt;
        if (_chaseLost >= CHASE_LOST_SEC) Emit(GameEvent::Lost);
        return;
    }
    _chaseLost = 0.0;
    const Vec2 me = BodyCenter();
    const Vec2 p = target->BodyCenter();
    const double bl = BodyLen();
    const double len = std::sqrt((p.x - me.x) * (p.x - me.x) +
                                 (p.y - me.y) * (p.y - me.y)) / (bl > 0 ? bl : 1.0);
    // 进攻击距离不等于马上出手：attackCoolSec 没到就继续跟着，别每 0.6 秒
    // 换一次态空转
    if (len <= Cfg().attackLen.valueOr(ATTACK_LEN) && _attackCool <= 0) {
        Emit(GameEvent::InRange);
        return;
    }
    // 朝向得带迟滞，也不许每拍都重挑。两个人绕着圈走的时候这个数会连着翻，
    // 翻一次就是换一张条带 —— 八个人同场时缓存被冲穿，physics 从 200ms 涨到
    // 1500ms。隔 0.35 秒才看一次方向，加上"只比现在好出一点就不许换"，
    // 换向次数掉到原来的几十分之一
    if (_chaseSteer > 0.0) { _chaseSteer -= dt; return; }
    _chaseSteer = 0.35;
    int want = DirToward(p.x - me.x, p.y - me.y);
    if (want != _dir) {
        const double n = std::hypot(p.x - me.x, p.y - me.y);
        if (n > 1e-9) {
            const double cur = (DirDX()[_dir] * (p.x - me.x) +
                                DirDY()[_dir] * (p.y - me.y)) / n;
            const double best = (DirDX()[want] * (p.x - me.x) +
                                 DirDY()[want] * (p.y - me.y)) / n;
            // 只比现在这条好出 0.12 以上才真换。差得少就别动，让它继续走
            if (best - cur < 0.12) want = _dir;
        }
    }
    SetDirection(want);
}

// 装备武器只换动作集和腿速，不动状态机：走/跑那套迟滞照旧在跑。
// 缺武器图的人这里直接吞掉 —— 托盘那个勾是全局的，不能因为一个人没图
// 就整条命令报错，或者让他黑一块
void Runner::SetArmed(bool flag, bool automatic) {
    if (!automatic && flag != _armed) { _manualWeaponHold = 6.0; _autoArmed = false; }
    if (flag && !_opt.hasWeaponArt) flag = false;
    if (_armed == flag) return;
    _armed = flag;
    // 换了动作集就是换了一批条带，帧号得重头数，不然会拿走跑的帧号去套武器跑
    _animClock = 0.0;
    _frameIndex = -1;
    LoadFrames();
    _needUpload = true;
    // 装备这件事得让它自己说一声：SetArmed 只管改状态，后面那几路都是从这条事件来的
    Emit(GameEvent::Armed);
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

// 遭遇：这一拍该不该理谁、在第几档理。
//
// 躲和追都是"理"，所以挑对象和定档位只在这儿做一次，别在 HitPeer 那种地方
// 再判一遍距离 —— 两处判法早晚会对不上。
//
// 这一刀只把"谁 + 多远"这条线立起来并让它能报出来：真去追、真出手是状态机
// 那一刀的事，现在接进来等于改了今天的脾气
void Runner::UpdateEncounter(double dt) {
    if (_encCool > 0) _encCool -= dt;
    if (!s_peers) return;

    const CharacterConfig& c = Cfg();
    const Vec2 me = BodyCenter();
    const double bl = BodyLen();
    if (bl <= 0.0) return;

    const Runner* hit = nullptr;
    EncounterBand band = EncounterBand::None;
    double bestLen = 1e300;

    for (const Runner* o : *s_peers) {
        if (!o || o == this || !Opposed(MyFaction(), o->MyFaction())) continue;
        // 敌人之间不许互相追打：那是"理"的一支，得在这儿挡掉，
        // 别挪到挨打那一路里去判 —— 那边只管"这一下怎么挨"
        if (!Opposed(MyFaction(), o->MyFaction())) continue;
        const Vec2 p = o->BodyCenter();
        const double len = std::sqrt((p.x - me.x) * (p.x - me.x) +
                                     (p.y - me.y) * (p.y - me.y)) / bl;
        const EncounterBand real = BandFor(c, len);
        // 距离这道门不许被开关顶掉：--enc= 只是"这个人确实在范围内，但他落在
        // 哪一档我替他指定"，不是"多远都算近身"。真站到了范围外就是没遇到
        if (real == EncounterBand::None) continue;
        const EncounterBand b = (_opt.forceBand != 0) ? (EncounterBand)_opt.forceBand : real;
        // 判据在**我**这张表上：我要不要理他，是我的事。对面那份配置只回答
        // "他打到我我该怎么挨"，那是另一张表
        const Encounter* mine = MatchEncounter(c, o->Name(), o->Id());
        const EncounterKind k = mine->KindAt(b);
        if (k == EncounterKind::Ignore || k == EncounterKind::Separate) continue;
        if (len < bestLen) {
            bestLen = len;
            hit = o;
            band = b;
        }
    }

    if (!hit) {
        _encLast.clear();
        _encLastBand = 0;
        return;
    }

    // "换人或换档才算一次"：照面一次报一次脸太吵，可两个人换了个位置又得报
    const int b = (int)band;
    if (hit->Name() == _encLast && b == _encLastBand) return;
    _encLast = hit->Name();
    _encLastBand = b;
    if (_encCool > 0) return;
    _encCool = 0.6;
    ++_stats.encounters;

    Emit(band == EncounterBand::Near ? GameEvent::EncounterNear : GameEvent::EncounterFar,
         hit->Id());
}

void Runner::UpdateWander(double dt) {
    _wanderClock += dt;
    if (_wanderClock < Cfg().wanderSec.valueOr(WANDER_SEC)) return;
    _wanderClock = 0.0;
    _dirReason = "wander";
    SetDirection(RandDir(_rng));
}

// ---- 任务栏漫步 ----
//
// 这根线归 main 定：它才知道屏幕多大、任务栏在哪儿。Runner 只管沿着它走，
// 所以这里一个字都不碰 _ox/_worldW 那套可行走区 —— 那是给满屏乱逛用的，
// 拿它来夹这根线会把人夹到任务栏底下去
void Runner::SetDisplay(const RECT& work, double dpiScale) {
    if (_ox == work.left && _oy == work.top &&
        _worldW == work.right - work.left && _worldH == work.bottom - work.top &&
        _dpi == dpiScale) return;
    const double scale = _dpi > 0 ? _k / _dpi : _opt.k;
    _dpi = _opt.dpi = dpiScale;
    _k = _opt.k = scale * dpiScale;
    _ox = _opt.ox = work.left; _oy = _opt.oy = work.top;
    _worldW = _opt.worldW = work.right - work.left;
    _worldH = _opt.worldH = work.bottom - work.top;
    _bounceCool = _dirLock = 0.0;
    _lockEdge = 0;
    if (!_opt.taskbar) ClampPos();
    _needUpload = true;
    _bubble.Tick(0.0, _pos, HeadTop(), _k, FaceLeft());
}

void Runner::SetTaskbar(bool on, double footY, double loX, double hiX) {
    if (_opt.taskbar == on && _tbFootY == footY && _tbLoX == loX && _tbHiX == hiX) return;
    const bool wasOn = _opt.taskbar;
    _opt.taskbar = on;
    _tbFootY = footY;
    _tbLoX = loX;
    _tbHiX = hiX;
    _stateLocked = on || _opt.forceState >= 0;
    if (_battleActive || IsEnemy() || Entering() || Downed()) {
        if (on) { _pos.x = (std::clamp)(_pos.x, loX, hiX); _pos.y = footY; }
        else ClampPos();
        if (Entering()) { _enterLeft = 0; EnterFromEdge(_dir == 4 ? 0 : 1); }
        _needUpload = true;
        return;
    }
    // 关掉这一档得把之前钉的东西全数还回去：状态锁还锁着的话，这人会一辈子
    // 站在原地不动 —— 那是这一档自己设的，不是状态机设的
    if (!on) {
        if (!wasOn) return;
        _stateLocked = _opt.forceState >= 0;
        _state = _stateLocked ? (State)_opt.forceState : State::Walk;
        _returnTo = _state;
        _artSet = false;
        _tbPhase = 0;
        _animClock = 0.0;
        _frameIndex = -1;
        LoadFrames();
        ClampPos();
        _needUpload = true;
        _stats.state = (int)_state;
        _bubble.Tick(0.0, _pos, HeadTop(), _k, FaceLeft());
        return;
    }
    _pos.x = (std::clamp)(_pos.x, loX, hiX);
    _pos.y = footY;
    _needUpload = true;
    _bubble.Tick(0.0, _pos, HeadTop(), _k, FaceLeft());
    // A work-area/DPI change only moves the ledge; it must not restart a pose.
    if (wasOn) return;
    // 起场先走一段，别一上来就杵在那儿不动
    _tbPhase = 0;
    _tbFacing = 1;
    _tbTimer = 4.0;
    _tbEmote = 3.0;
    _pos.y = footY;
    _state = State::Walk;
    _stats.state = (int)_state;
    _stateLocked = true;   // 这一档不许转移表插嘴
    _dirReason = "taskbar";
    SetDirection(_tbFacing > 0 ? 4 : 0);
    _animClock = 0.0;
    _frameIndex = -1;
    LoadFrames();
}

// 这一套动作在这张皮肤里到底有没有图。摆姿势之前先看它 —— 不然会去开一个
// 不存在的文件，然后整个人黑一块
bool Runner::HasArt(Art a) const {
    std::error_code ec;
    return ActionOn(Cfg(), a) && std::filesystem::is_regular_file(StripPath(a, _dir), ec);
}

void Runner::BeginTaskbarPose() {
    // Quiet companion mode: use a victory gesture when available.
    Art pick = Art::Victory;
    if (!HasArt(pick)) {
        _tbPhase = 0;
        _tbTimer = 5.0;
        _animClock = 0.0;
        return;
    }
    _tbPoseArt = pick;
    _tbPhase = 2;
    _tbTimer = TB_POSE_SEC;
    _animClock = 0.0;
    _frameIndex = -1;
    LoadFrames();
    _tbTimer = (std::max)(_tbTimer, _frameCount / TB_VICTORY_FPS);
}

void Runner::UpdateTaskbar(double dt) {
    _pos.y = _tbFootY;
    _pos.x = (std::clamp)(_pos.x, _tbLoX, _tbHiX);
    // Oversized skins cannot traverse a negative-width ledge.
    if (_tbHiX <= _tbLoX) {
        _tbPhase = 1;
        _tbTimer = TB_TURN_PAUSE_SEC;
        return;
    }

    if (_tbEmote > 0) _tbEmote -= dt;
    // 冒脸：空档到了才冒，而且不许上一枚还挂着就再盖一张
    if (_tbEmote <= 0 && !_bubble.Busy()) {
        std::uniform_int_distribution<int> which(3, 4);   // 高兴 / 喜欢
        ShowEmote(which(_rng), 2.6);
        std::uniform_real_distribution<double> gap(TB_EMOTE_MIN_SEC, TB_EMOTE_MAX_SEC);
        _tbEmote = gap(_rng);
    }

    std::uniform_real_distribution<double> pause(TB_PAUSE_MIN_SEC, TB_PAUSE_MAX_SEC);
    std::uniform_real_distribution<double> walkDur(4.0, 11.0);
    std::uniform_real_distribution<double> chance(0.0, 1.0);

    switch (_tbPhase) {
    case 0: {
        _tbTimer -= dt;
        const double sp = TB_WALK_PX * _k * 0.5;
        _pos.x += _tbFacing * sp * dt;
        // 到端点就翻面。这是这一档里唯一会改朝向的地方
        if (_tbFacing > 0 && _pos.x >= _tbHiX) {
            _pos.x = _tbHiX;
            _tbFacing = -1;
            _dirReason = "taskbar";
            SetDirection(0);
            _tbPhase = 1;
            _tbTimer = (double)TB_TURN_PAUSE_SEC;
        } else if (_tbFacing < 0 && _pos.x <= _tbLoX) {
            _pos.x = _tbLoX;
            _tbFacing = 1;
            _dirReason = "taskbar";
            SetDirection(4);
            _tbPhase = 1;
            _tbTimer = (double)TB_TURN_PAUSE_SEC;
        } else if (_tbTimer <= 0) {
            _tbPhase = 1;
            _tbTimer = pause(_rng);
        }
        break;
    }
    case 1: {
        _tbTimer -= dt;
        if (_tbTimer > 0) break;
        // 停下来之后顺手摆个姿势，不是每次都摆 —— 全摆反而像在跳舞
        if (chance(_rng) < TB_POSE_CHANCE) {
            BeginTaskbarPose();
        } else {
            _tbPhase = 0;
            _tbTimer = walkDur(_rng);
            _animClock = 0.0;
        }
        break;
    }
    default: {
        _tbTimer -= dt;
        if (_tbTimer > 0) break;
        _tbPhase = 0;
        _tbTimer = walkDur(_rng);
        _animClock = 0.0;
        _frameIndex = -1;
        LoadFrames();
        break;
    }
    }
}

// 朝当前方向推一步，越界就夹回来，并把撞到的边记下来
void Runner::Move(double dt) {
    if (_worldW <= 0 || _worldH <= 0) return;
    if (_hold) return;
    // 站着不动的态就不许挪窝。这条放在这里而不是散到各处判速度：
    // "动不动"和"多快"是两件事，两处各判一遍早晚会对不上
    if (!Moving()) return;

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
    // 是不是"新的一次撞墙"得在这儿定：底下只按冷却放行，而冷却只有 0.15 秒 ——
    // 人贴着墙不动时会每 0.15 秒再报一次 BumpWalk，每次把泡泡的 _life 重置成
    // 2.4 秒，于是那一枚表情永远收不掉。撞墙是事件，不是"还在压着这条边"的状态
    const bool newBounce = (_lockEdge == 0);
    // 只要这一拍还在压这条边就重新记进锁，不然它反复撞同一面墙不回头
    _lockEdge |= breached;
    if (_bounceCool > 0) return;

    if (_state == State::Run) {
        // 逃跑途中撞墙不能交给随机反射，不然它挑一条朝鼠标的那条线，规则就毁了
        SetDirection(BestFleeDir(_mouse));
        _fleeForce = true;
        _fleeClock = 0.0;
        if (newBounce) Emit(GameEvent::BumpRun);
    } else {
        SetDirection(PickBounce(breached));
        if (newBounce) Emit(GameEvent::BumpWalk);
    }
    _bounceCool = BOUNCE_COOLDOWN_SEC;
    _dirLock = BOUNCE_COMMIT_SEC;
    _lockEdge = breached;
}

void Runner::UpdateFrame(double dt) {
    if (_frameCount < 1) return;
    // 站住了就别让腿还在转，看着像踩在原地不动的跑步机上
    if (_hold && !_battleActive) return;
    // --frame=N 钉死：动画时钟继续走，但帧号不许变，省得截图的时候还在漂
    if (_opt.forceFrame >= 0 && _opt.forceFrame < _frameCount) {
        _animClock = 0.0;
        if (_frameIndex != _opt.forceFrame) {
            _frameIndex = _opt.forceFrame;
            _needUpload = true;
        }
        return;
    }
    if (_battleActive) {
        _animClock += dt;
        const auto art = ArtOf();
        const double fps = art == Art::Kneel2 && Downed()
            ? _frameCount / 2.0 : AnimationRate(art);
        const int step = (int)std::floor(_animClock * fps);
        const int frame = _battleOnce ? (std::min)(step, _frameCount - 1) : step % _frameCount;
        if (frame != _frameIndex) { _frameIndex = frame; _needUpload = true; }
        return;
    }
    // 帧率按动作集取，不是按走/跑：武器跑比无武器跑慢一档，照搬那两个数
    // 会让剑影糊成一团。配置里写的 walkFrame/runFrame 只顶走和跑这两档 ——
    // 武器那两套的节拍是另一张表，没在 xml 里开过口子，别拿它去盖
    const CharacterConfig& c = Cfg();
    const double walkFps = c.walkFrameRate.valueOr(WALK_FPS);
    const double runFps = c.runFrameRate.valueOr(RUN_FPS);
    // 任务栏漫步：站住那一档腿不许转，只留第一帧 —— 不然就是踩在原地不动的
    // 跑步机上。摆姿势那一档反过来，必须让它把动作放完
    if (_opt.taskbar) {
        if (_tbPhase == 1) {
            if (_frameIndex != 0) { _frameIndex = 0; _needUpload = true; }
            return;
        }
        _animClock += dt;
        const double rate = _tbPhase == 2 ? TB_VICTORY_FPS : TB_WALK_FPS;
        const int step = (int)std::floor(_animClock * rate);
        // Poses play once and hold, walking loops.
        const int idx = _tbPhase == 2 ? (std::min)(step, _frameCount - 1) : step % _frameCount;
        if (idx != _frameIndex) {
            _frameIndex = idx;
            _needUpload = true;
        }
        return;
    }
    _animClock += dt;
    double rate = AnimationRate(ArtOf());
    switch (ArtOf()) {
        case Art::Run:     rate = runFps;      break;
        case Art::Standby: rate = STANDBY_FPS; break;
        case Art::Run2:    rate = RUN2_FPS;    break;
        case Art::Walk:    rate = walkFps;     break;
    }
    const bool once = _state == State::Attack || _state == State::BeAttacked || _state == State::Stun || _state == State::Victory;
    const int step = (int)std::floor(_animClock * rate);
    const int idx = once ? (std::min)(step, _frameCount - 1) : step % _frameCount;
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

// 身体框。鼠标命中和同伴推开共用这一份 —— 原版 TouchRect 和 BodyHit 就是同一个
// 矩形，两边各画一套早晚会对不上
RectD Runner::PeerBox() const {
    const double hw = HalfW() + TOUCH_PAD_X * _dpi;
    const double top = _pos.y - BodyLen() - TOUCH_PAD_TOP * _dpi;
    const double h = BodyLen() + (TOUCH_PAD_TOP + TOUCH_PAD_BOTTOM) * _dpi;
    return RectD{_pos.x - hw, top, hw * 2.0, h};
}

bool Runner::BodyHit(Vec2 mouse) const {
    if (IsNaN(mouse)) return false;
    return PeerBox().Contains(mouse.x, mouse.y);
}

// 沿 d 探一个身长，会不会走进哪个同伴的身体框里。这里用的就是 PeerBox()，
// 故意不再画第二套碰撞框
bool Runner::PeerAhead(int dir) const {
    if (!s_peers || !s_separate) return false;
    if (dir < 0 || dir >= DIR_COUNT) return false;
    const std::vector<Runner*>& list = *s_peers;
    if (list.size() < 2) return false;
    const double reach = PEER_LOOK_LEN * BodyLen();
    const double px = _pos.x + DirDX()[dir] * reach;
    const double py = _pos.y + DirDY()[dir] * reach;
    for (Runner* o : list) {
        if (!o || o == this) continue;
        if (o->PeerBox().Inflated(PEER_PAD * _dpi).Contains(px, py)) return true;
    }
    return false;
}

// 桌上有没有别人，离我多近（按身长算）。一个人时给 -1
double Runner::NearestPeerLen() const {
    if (!s_peers) return -1.0;
    const std::vector<Runner*>& list = *s_peers;
    const Vec2 me = BodyCenter();
    double best = -1.0;
    for (Runner* o : list) {
        if (!o || o == this) continue;
        const Vec2 c = o->BodyCenter();
        const double d = std::sqrt((c.x - me.x) * (c.x - me.x) + (c.y - me.y) * (c.y - me.y));
        if (best < 0.0 || d < best) best = d;
    }
    if (best < 0.0 || BodyLen() <= 0.0) return best;
    return best / BodyLen();
}

// 被人推开的那一下：只挪位置，然后按新的落点重新夹一次。夹完贴图会跟着新的
// 地方走，免得这一拍画的是推之前的旧位置
void Runner::Shove(double dx, double dy) {
    _pos.x += dx;
    _pos.y += dy;
    ClampPos();
    // 不用在这儿提醒 Present：它每拍都拿 _pos 现算窗口原点，再和 _lastX/_lastY
    // 比，位置一变自己就会搬。这里硬设 _needUpload 只会白白多拷一张表面
}

// 把脚底夹回可行走区。和 Move 用的是同一组边界，别在这儿另立一套
void Runner::ClampPos() {
    if (_worldW <= 0 || _worldH <= 0) return;
    const double loX = _ox + PadLeft(), hiX = _ox + _worldW - PadRight();
    const double loY = _oy + PadTop(), hiY = _oy + _worldH - PadBottom();
    _pos.x = ClampAxis(_pos.x, loX, hiX, CenterX());
    _pos.y = ClampAxis(_pos.y, loY, hiY, CenterY());
}

// 两两看一眼。这条推挤**不算撞墙**：不走 Move、不碰 IntoWall 的边界、也不留
// lockEdge —— 它只把位置修回来。真上锁的话后面十几拍的放锁条件会一直被一处
// 不该有的账牵着
void Runner::SeparateAll() {
    if (!s_peers || !s_separate) return;
    const std::vector<Runner*>& list = *s_peers;
    for (size_t i = 0; i + 1 < list.size(); ++i) {
        if (!list[i]) continue;
        for (size_t j = i + 1; j < list.size(); ++j) {
            if (!list[j]) continue;
            list[i]->SeparateFrom(list[j]);
        }
    }
}

// a 和 b 之间那一下，动作只从 a 这边发起一次，免得同一对事在两边各推一遍
void Runner::SeparateFrom(Runner* other) {
    if (!other || !Targetable() || !other->Targetable() || (_opt.taskbar && other->_opt.taskbar)) return;
    const Vec2 ca = BodyCenter();
    const Vec2 cb = other->BodyCenter();
    const double dx = cb.x - ca.x;
    const double dy = cb.y - ca.y;
    const double gap = HalfW() + other->HalfW() + PEER_PAD * _dpi;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d >= gap) return;

    double ux, uy;
    if (d < 0.5) {
        // 两人正好叠死，这条中心线推不出方向来：按下标定一个往左一个往右，
        // 不然下一拍还是老样子，看着就像钉在了同一个点上
        if (dx > 0.0) ux = 1.0;
        else if (dx < 0.0) ux = -1.0;
        else ux = (dy > 0.0) ? -1.0 : 1.0;
        uy = 0.0;
    } else {
        ux = dx / d;
        uy = dy / d;
    }

    const double push = (gap - d) * 0.5 + 1.0;
    Shove(-ux * push, -uy * push);
    other->Shove(ux * push, uy * push);
    NotifyPeerBump(other);
}

// "他打到我"那一路。
//
// 这张表只回答"我长成什么样"，和遭遇表那张"我要不要理他"是两张表 ——
// 混在一起，"追上去打"和"被打之后怎么挨"就会互相改写。
//
// 它不改血也不改伤害（这套程序本来就没有伤害），只管三件事：走哪段动画、
// 往后退几个身长、换哪张脸。撞上墙那一推**不看这张表**，还是 wall knock
// 那个数 —— 不然一定有人往 <react> 里加一行想调撞墙
void Runner::Hit(Runner* attacker) {
    if (!attacker || !Targetable() || !attacker->Targetable() || !Opposed(_faction, attacker->_faction)) return;
    --_hp;
    ++_stats.hits;
    _targetId = 0;
    if (_hp <= 0) {
        _hp = 0; _lifeAge = 0; _knockLeft = 0; _actionLeft = 0;
        HideEmote();
        BattleArt(Art::Kneel2, true, true);
        _state = _faction == Faction::Ally ? State::Stun : State::Dead;
    } else {
        const auto* reaction = MatchReaction(Cfg(), attacker->Name(), attacker->Id());
        if (reaction && reaction->kind == ReactKind::None) return;
        const bool wasHit = _battleActive && _battleArt == Art::BeAttacked && _actionLeft > 0;
        const auto me = BodyCenter(), from = attacker->BodyCenter();
        const double dx = me.x - from.x, dy = _opt.taskbar ? 0 : me.y - from.y;
        const double norm = std::hypot(dx, dy);
        const int facing = DirToward(-dx, -dy);
        if (!wasHit || facing != _dir) {
            _knockUnit = norm > 0.01 ? Vec2{dx / norm, dy / norm} : Vec2{1, 0};
            _knockLeft = (reaction && reaction->kbLen >= 0 ? reaction->kbLen : 1.0) * BodyLen();
        }
        SetDirection(facing);
        _lastHitter = attacker->EntityId();
        _actionLeft = IsEnemy() ? 2.0 : 0.35;
        const bool stunned = reaction && reaction->kind == ReactKind::Stun;
        BattleArt(stunned ? Art::Kneel2 : Art::BeAttacked, true, !wasHit);
        _state = stunned ? State::Stun : State::BeAttacked;
        if (reaction && !reaction->emote.empty()) ShowEmote(reaction->emote.front());
        else NotifyEmote(GameEvent::AttackedBy, attacker->Id());
    }
    _stats.state = (int)_state;
}

// 挤开之后报一次脸。两边各自有冷却，谁冷却完了谁说，都还冷却着就闭嘴
void Runner::NotifyPeerBump(Runner* other) {
    const bool mine = _peerCool <= 0.0;
    bool theirs = false;
    if (other) {
        theirs = other->_peerCool <= 0.0;
        if (theirs) other->_peerCool = PEER_BUMP_COOL_SEC;
    }
    if (mine) _peerCool = PEER_BUMP_COOL_SEC;
    if (!mine && !theirs) return;
    // 和撞墙那一下分得开：撞墙是懵，这个是被人挤到了，所以两张脸各一半
    if (mine) Emit(GameEvent::PeerBump);
    if (other && theirs) other->Emit(GameEvent::PeerBump);
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
    if (std::strcmp(_dirReason, "corner") != 0 && std::strcmp(_dirReason, "encounter") != 0) {
        _dirReason = "flee";
    }
    // 打两遍：一遍当桌上没有别人，一遍把同伴算进去。挑出来的不一样就说明这一拍
    // 真是在绕人 —— 那个差值才是"躲开"的证据，光看分数看不出来
    const int goNoPeer = ScoreFlee(vx, vy, false);
    const int goPeer = ScoreFlee(vx, vy, true);
    if (goPeer != goNoPeer) {
        ++_stats.peerBlocks;
        if (_peerLogCool <= 0.0) {
            _peerLogCool = PEER_BUMP_COOL_SEC;
            wchar_t msg[160];
            swprintf(msg, 160, L"peer 躲开 %d->%d 靠得近=%.2f\n",
                     goNoPeer, goPeer, NearestPeerLen());
            OutputDebugStringW(msg);
        }
    }
    return goPeer;
}

// 八条线各打一遍分。撞墙 1000 > 同伴 500 > 刚撞过的边 800 > 不脱离 100
int Runner::ScoreFlee(double vx, double vy, bool withPeer) {
    const int ideal = DirFromVector(vx, vy);
    int best = ideal;
    double bestScore = -1e300;
    for (int d = 0; d < DIR_COUNT; ++d) {
        double score = DirDX()[d] * vx + DirDY()[d] * vy;
        if (score <= 0.0) score -= 100.0;
        if (IntoWall(d)) score -= 1000.0;
        // 同伴这条比撞墙轻、比不脱离重：堵在角上时还能靠它绕开，
        // 又不至于为了躲人就僵住不动
        if (withPeer && PeerAhead(d)) score -= PEER_PENALTY;
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
    if (Entering() || _entryClip) {
        if (Entering()) {
            const int l = (std::clamp)((int)std::ceil(_ox - x), 0, winW);
            const int t = (std::clamp)((int)std::ceil(_oy - y), 0, winH);
            const int r = (std::clamp)((int)std::floor(_ox + _worldW - x), l, winW);
            const int btm = (std::clamp)((int)std::floor(_oy + _worldH - y), t, winH);
            SetWindowRgn(_win->Handle(), CreateRectRgn(l, t, r, btm), FALSE);
        } else SetWindowRgn(_win->Handle(), nullptr, FALSE);
        _entryClip = Entering();
    }
    const bool moved = (x != _lastX || y != _lastY);

    if (_opt.uploadEveryTick && moved) _needUpload = true;
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

    // 泡泡排在角色之后：它的位置是照这一拍真的脚底算的，先传它等于传个旧坐标
    _bubble.Present();
}

// ---- 表情 ----

bool Runner::ShowEmote(int id, double seconds) {
    if (IsEnemy() || Downed()) return false;
    if (!_bubble.Create()) return false;
    if (!_bubble.Show(id, seconds)) return false;
    _lastEmoteId = id;
    return true;
}

void Runner::HideEmote() {
    _bubble.Hide();
}

// 事件唯一的入口。三路都从这儿派生，别在物理那边直接 SetState() 又在别处
// 直接 ShowEmote() —— 那样"撞上之后到底该换态还是只换个脸"就没人说得清了
void Runner::Emit(GameEvent ev, int byId) {
    // 表里点了名的那一路自己就把脸放了，别再从事件映射表里补一张
    if (ApplyStateRule(ev, byId)) return;
    NotifyEmote(ev, byId);
}

// 物理自己撞出来的那一路：表情照报，但不许回头查表。
//
// 走/跑那套迟滞还带着 _runTravel 这个条件，表里再写一遍同一件事就成了两处
// 说法 —— 谁先跑起来就没准了
void Runner::EmitPhysical(GameEvent ev) { NotifyEmote(ev, 0); }

// 表情那一半。xml 写过就从那几张里等概率抽；写了但写了 none 就是这里该安静；
// 一个字都没写过才回到内置那张映射表
void Runner::NotifyEmote(GameEvent ev, int byId) {
    (void)byId;   // by= 那一层要等遭遇表接上才有"谁"可填，现在还不许猜
    const CharacterConfig& c = Cfg();
    const std::vector<int>* pool = EmotePoolFor(c, ev);
    if (!pool) {
        // default="none" 的那位要靠这一句才知道哪些事件是他自己认领过的：
        // 剩下的那些就别再拿代码那张映射表来替他做主
        if (!c.eventDefault.valueOr(true)) return;
        std::uniform_int_distribution<int> coin(0, 1);
        const int id = BuiltinEmote(ev, (unsigned)coin(_rng));
        if (id <= 0) return;
        ShowEmote(id, c.emoteShowSec.valueOr(EMOTE_SHOW_SEC));
        return;
    }
    if (pool->empty()) return;   // 空表 = "这里就该安静"，不是"没配过"
    std::uniform_int_distribution<int> pick(0, (int)pool->size() - 1);
    const int id = (*pool)[pick(_rng)];
    if (id <= 0) return;
    // 事件逼出来的脸不许因为"上一张也是它"就不放：撞了两次墙本来就该懵两次。
    // "别连着两次同一张"那是随机冒脸那一路的事，等 emoteGapSec 那条计时接上再说
    ShowEmote(id, c.emoteShowSec.valueOr(EMOTE_SHOW_SEC));
}

}  // namespace fd

