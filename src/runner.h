#pragma once

#include "charconf.h"
#include "config.h"
#include "emote.h"
#include "wic.h"
#include "window.h"

#include <random>
#include <string>
#include <vector>

namespace fd {

// 物理那边报得上来的事现在统一走 charconf.h 的 GameEvent：以前这里另有一份
// EmoteEvent，两边各数各的，xml 里写的事件名和代码里的枚举早晚会对不上。
// GameEvent 是它的超集，名字也一样，不必再留一层别名。

// 一个坐在桌面上乱走的人：位置、方向、状态机、撞墙挑向全在这儿。
// 逻辑照搬 RunnerEntity.vb，只砍掉了 Phase 1 用不到的那几支（遭遇/跪姿）。
// 武器这一档这轮只做手动开关，自动抽剑/收剑那套要等敌人系统，先不写。
// 表情这轮只接已经有触发点的那几项，随机冒脸要等 character.xml 那张表。
class Runner {
public:
    struct Options {
        std::wstring skinDir;   // ...\Chr\科洛丝
        // 这个人的身份证。遭遇表认名字优先，靠的就是这一项 —— 号数原本只是
        // 内置几个人的巧合编号，谁搬一次目录就会错位，名字才是文件夹名
        std::wstring skinName;
        // xml 里没写 id= 时该用哪个号。AddEntity 从 SkinInfo 带进来 ——
        // 名单已经排过序了，这里只是把那个序号交给配置层，别在这儿另算一遍
        int idHint = 0;
        // --id=<N>：强行把这个人当 N 号。区间和阵营那两条规则光看默认号数
        // 试不出来 —— 桌上全是 1..14，永远撞不到 101..200 那一行
        int forceId = 0;
        // --enc=near/far：钉死这一拍落在哪一档。遭遇的判据是"谁 + 多远"，
        // 光靠随机走位复现不了一条规则，测试就变成玄学
        int forceBand = 0;   // 0=不干预 1=far 2=near
        // --state=<名字>：钉住不许自己换态。测试某一态的时候得让它停在那儿，
        // 不然它半秒就溜走了，什么都看不出来。-1 = 不干预
        int forceState = -1;
        // --hit：隔一会儿打后面的人一下，专门验 <react> 那张表。光靠两个人
        // 自己挤到一起，什么时候挨上完全看运气，测不出东西
        bool hitProbe = false;
        // 任务栏漫步：只在一根水平线上来回走，慢速，而且完全不跟鼠标打交道。
        // 这一档整个绕开走/跑那套迟滞和遭遇表 —— 它问的不是"这人什么脾气"，
        // 是"屏幕上只剩一个人，让他安静地散会儿步"
        bool taskbar = false;
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
    // 任务栏漫步的总开关，顺带把这条线钉在哪儿、左右到哪儿说清楚。
    //
    // 这三个数必须由 main 给：Runner 自己不知道屏幕有多大，更不知道任务栏
    // 在哪儿 —— 拿 SystemParametersInfo 的工作区去推会推出一根比任务栏高
    // 一截的线，人就走进任务栏里去了
    void SetTaskbar(bool on, double footY, double loX, double hiX);
    bool TaskbarMode() const { return _opt.taskbar; }
    // 全局装备开关会逐个叫到每个 Runner 头上。缺武器图的时候这里自己吞掉，
    // 不报错也不崩 —— 托盘那个勾还是全局的，只是这个人没得可换
    void SetArmed(bool flag);
    bool Armed() const { return _armed; }

    // ---- 表情 ----
    // 直接放一枚。返回 False = 这号不存在或者图集没解出来，调用方得听得见这句
    bool ShowEmote(int id, double seconds = 2.4);
    // 事件唯一的入口：状态、表情、日志三路都从这里派生。以前物理那边直接
    // SetState() 一处、别处又直接 ShowEmote() 一处，两件事各走各的，
    // "撞上之后到底该换态还是只换个脸"就没人说得清了
    void Emit(GameEvent ev, int byId = 0);
    // 物理那边自己换的态：事件照报（表情要看它），但不许回头查表 ——
    // 不然走/跑那套迟滞会被表里同一件事再驱动一遍
    void EmitPhysical(GameEvent ev);
    // 只走表情那一半。留着是因为托盘和大部分调用点都只要这一半。
    // byId 是"谁逼出来的"，0 = 说不清
    void NotifyEmote(GameEvent ev, int byId = 0);
    void HideEmote();
    bool EmoteBusy() const { return _bubble.Busy(); }
    const wchar_t* EmoteTitle() const { return _bubble.Title(); }
    // 气泡窗口现在在哪儿。写进 stats 用：两个矩形一起看才知道泡泡摆没摆对
    bool BubbleRect(int& x, int& y, int& w, int& h) const { return _bubble.Rect(x, y, w, h); }
    // 头顶线：泡泡的底沿该压在这一条上。它和窗口上边界是同一个来源，
    // 别在别处拿 BodyLen 现算一遍 —— 那是身体高度，不是窗口往上伸了多少
    double HeadTop() const { return _pos.y - ExtUp(); }
    bool FaceLeft() const { return DirDX()[_dir] < 0.0; }

    // ---- 同伴 ----
    // 桌上现在都有谁。main 每拍填一次，PeerAhead / SeparateAll 都从这儿看人，
    // 不许两边各存一份名单 —— 早晚会对不上
    static void SetPeers(const std::vector<Runner*>* peers) { s_peers = peers; }
    // 对照实验的总开关：关掉之后推挤和绕人都没有，等于"这桌上没人互相看得见"。
    // 只关推挤不算对照 —— 绕人那一路还在起作用，量出来的差值说不清是谁的功劳
    static void SetSeparate(bool on) { s_separate = on; }
    // 两两看一眼，重叠了就把多的那一截一人推一半。这条推挤**不算撞墙**：
    // 不走 Move、不碰 IntoWall 的边界、也不留 lockEdge —— 它只把位置修回来，
    // 真上锁的话后面十几拍的放锁条件会一直被一处不该有的账牵着
    static void SeparateAll();

    // 身体框。鼠标命中和同伴推开共用这一份，不许画第二套
    RectD PeerBox() const;
    // 身体中心：躲人和推开都按这个点算，而不是脚底 —— 脚底差出来的那半个
    // 身长会把"站在旁边"误判成"挡在前面"
    Vec2 BodyCenter() const { return Vec2{_pos.x, _pos.y - BodyLen() * 0.5}; }
    // 被人推开的那一下：只挪位置，然后按新的落点重新夹一次。夹完贴图会跟着
    // 新的地方走，免得这一拍画的是推之前的旧位置
    void Shove(double dx, double dy);
    // 沿 d 这条线往前探一个身长，会不会走进哪个同伴的身体框里
    bool PeerAhead(int dir) const;
    // 桌上有没有别人，离我多近（按身长算）。一个人时给 -1
    double NearestPeerLen() const;
    // 位置唯一的入口，除了这里别处不许改 _pos
    void SetPos(Vec2 p) { _pos = p; }
    // "他打到我"那一路唯一的入口。反应那张表只回答这个问题，
    // 和"我要不要理他"（遭遇表）是两张表，混在一张里两边会互相改写
    void Hit(Runner* attacker);
    // 这个人到底有没有武器图，供托盘标"缺武器"用
    bool HasWeaponArt() const { return _opt.hasWeaponArt; }
    // 现在这一拍在放哪套动作，取图的路径只从这里出。
    //
    // 外面套一层 ArtKind()：配置里关掉的那一种不许出现在画面上，得让替身去顶。
    // 注意这里换的只是"取哪套图"，物理那一侧的 _armed/_state 一个字都不动 ——
    // 拿它去换速度，半宽和腿速就会脱节
    Art ArtOf() const {
        // 任务栏漫步那一档只认自己那两套：走的时候是走路，摆姿势的时候是
        // 挑好的那套。这里不许再去看 _armed —— 这一档压根没有武器这回事
        if (_opt.taskbar) {
            return _tbPhase == 2 ? _tbPoseArt : Art::Walk;
        }
        // 转移表点了名就听它的：表里写 art=7.BeAttacked，画面上就该是那套
        if (_artSet) return ArtKind(Cfg(), _artOverride);
        Art a = (_state == State::Run || _state == State::Chase)
                    ? (_armed ? Art::Run2 : Art::Run)
                    : (_armed ? Art::Standby : Art::Walk);
        return ArtKind(Cfg(), a);
    }
    // 身长按皮肤自己的格径缩放：82 是按 256 格定的，换成 512 格就该是 164。
    // 配置里写了 bodylen= 就按她自己的，没写还是那把公共尺子
    double BodyLen() const { return Cfg().bodyLen.valueOr(SPRITE_BODY_HEIGHT) * _k * CellScale(); }

    // 这个人那份定义。走函数而不是存指针：--no-config 会把整张缓存清空，
    // 存了指针就悬在那儿了。std::map 的节点查找一次几十纳秒，八个人一帧
    // 才八次，犯不着为它省
    // 配置查一次记一次，靠代次判它还是不是新的。
    //
    // 为什么非要留这一层：遭遇那一趟每拍要看八个人的身份，次次去 map 里捞
    // 太贵 —— 实测八人同场 CPU 从 2.2% 掉到 4.2%。std::map 的引用在插入时
    // 不会失效，所以只有 SetConfigDisabled 那种"整张丢掉"才需要重新捞
    const CharacterConfig& Cfg() const {
        const uint64_t g = ConfigGeneration();
        if (!_cfgPtr || _cfgGen != g) {
            _cfgPtr = &ConfigFor(_cfgDir, _opt.idHint);
            _cfgGen = g;
        }
        return *_cfgPtr;
    }
    // 我是谁。遭遇表两边都要这个：一边查"我遇到他会怎样"，一边给对方看"我是谁"
    const std::wstring& Name() const { return _opt.skinName; }
    // --id= 走的是这里，不是改缓存里的那份配置：配置是所有人共享的，
    // 拿它当测试开关会把别的人也一起改了
    int Id() const { return _opt.forceId > 0 ? _opt.forceId : Cfg().id; }
    Faction MyFaction() const { return FactionOf(Id()); }

    struct Stats {
        uint64_t ticks = 0;
        uint64_t uploads = 0;   // 真的拷了表面的那一拍
        uint64_t moves = 0;     // 只搬窗口的那一拍
        uint64_t bounces = 0;
        // 这一拍为了绕开同伴换了向。它比撞墙次数更能说明"两个人真的在互相让"
        uint64_t peerBlocks = 0;
        // 照面了几次。"遭遇表到底有没有在跑"这件事，光看泡泡看不出来 ——
        // 冒脸的来源太多，得有个只数这一路的数
        uint64_t encounters = 0;
        // 现在停在哪一态。--state= 到底钉没钉住，看这个数最直接
        int state = (int)State::Walk;
        // 换过几次态。钉住的那一路应该是 0
        uint64_t stateChanges = 0;
        // 挨了几下。<react> 那张表到底有没有在跑，看这个数最快
        uint64_t hits = 0;
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
    // 换态并记下"从哪儿来"。fromRule=false 是物理那边自己换的（走/跑那套迟滞），
    // 不许去碰 returnTo —— 那是转移表那一侧的事
    void EnterState(State st, bool fromRule);
    // 查表、看抢占等级、真换态。Emit() 里叫一次。
    // 交回来的那个 bool 说"表情我已经管了"—— 表里点了名的那一路
    bool ApplyStateRule(GameEvent ev, int byId);
    // 每一态的最小停留时间和一次性动画的计时
    void TickState(double dt);
    // 追人：朝目标挪，跟丢了就放弃
    void UpdateChase(double dt);
    // 从 dir0..7 里挑最接近这个向量的那一个
    int DirToward(double dx, double dy) const;
    // 这一态到底动不动。StandsStill() 是定义，这里是问当前这个人
    bool Moving() const { return !StandsStill(_state); }
    double StateHold(State st) const;
    void UpdateState(double dt, double dist);
    void UpdateFlee(double dt);
    // 遭遇：这一拍桌上有没有该理的人，落在哪一档。躲和追都是"理"，
    // 所以这一趟只管挑出对象和档位，别在别处再判一遍距离
    void UpdateEncounter(double dt);
    void UpdateEscape(double dt);
    void UpdateWander(double dt);
    // 任务栏漫步那一档：自己走自己的，鼠标、同伴、遭遇表一概不掺和
    void UpdateTaskbar(double dt);
    // 停下来摆姿势：胜利或者出拳，随机挑一套有图的
    void BeginTaskbarPose();
    // 这一套动作有没有图。没有就别摆，不然会去开一个不存在的文件
    bool HasArt(Art a) const;
    void UpdateFrame(double dt);
    void Move(double dt);
    void FlickAway();
    // 只把脚底夹回可行走区，不记撞到的边、也不挑新方向。Shove 用它，
    // Move 里那一份是"夹 + 记账"，两件事别混
    void ClampPos();

    int PickBounce(int breached);
    int BestFleeDir(Vec2 mouse);
    int BestFleeVec(double vx, double vy);
    // withPeer=false 是"当桌上没有别人"那一遍。两遍各挑一次，挑出来的不一样
    // 就说明这一拍真是在绕人 —— 那个差值才是"躲开"的证据，光看分数看不出来
    int ScoreFlee(double vx, double vy, bool withPeer);
    // a 和 b 之间那一下。动作只从 a 这边发起一次，免得同一对事在两边各推一遍
    void SeparateFrom(Runner* other);
    // 挤开之后报一次脸。两边各自有冷却，谁冷却完了谁说，都还冷却着就闭嘴
    void NotifyPeerBump(Runner* other);
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
        const CharacterConfig& c = Cfg();
        // 追人和跑是同一条腿：都是"要去某个地方"，慢下来就追不上了
        if (_state == State::Run || _state == State::Chase) {
            return (_armed ? c.armedRunPx.valueOr(ARMED_RUN_PX)
                           : c.runPx.valueOr(RUN_PX)) * _k * 0.5;
        }
        return c.walkPx.valueOr(WALK_PX) * _k * 0.5;
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

    // 桌上都有谁。main 拥有这份名单，Runner 只看不改
    static const std::vector<Runner*>* s_peers;
    static bool s_separate;

    // 这份定义住在 charconf 的缓存里，这里只留目录名。存指针是不行的：
    // --no-config 会把整张缓存清空，光凭指针不知道它已经悬了，所以每次用之前
    // 先对一代
    std::wstring _cfgDir;
    mutable const CharacterConfig* _cfgPtr = nullptr;
    mutable uint64_t _cfgGen = 0;

    LayeredWindow* _win = nullptr;
    Options _opt;
    // 缓存不再是成员：进程里就一份，两个人用同一张图就只解一次
    StripCache& _cache = StripCache::Shared();
    // 头顶那枚泡泡自己一个窗口。没在放的时候它是隐藏的，一个像素都不传
    EmoteBubble _bubble;
    // 上一次放的是哪一枚，只为"别连着两次同一张"这一个目的存在
    int _lastEmoteId = 0;

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

    // 同伴那两条冷却都是秒表，不是一拍计数：躲一次和挤一次谁也不该压着谁不放
    double _peerCool = 0.0;
    double _peerLogCool = 0.0;
    // 遭遇那两档各自有冷却：照面一次报一次脸太吵，但"换人了"或"换档了"必须算一次
    double _encCool = 0.0;
    // 上一次理的是谁、在第几档。-1 表示这一拍谁都没理
    std::wstring _encLast;
    int _encLastBand = 0;

    // ---- 状态机 ----
    // 进这一态之前是哪一态。一次性动画全指望它：追人追到一半挨一下，
    // 回来还得是追人，不然就变成乱逛
    State _returnTo = State::Walk;
    // 这一待了多久。最小停留时间和一次性动画都按它算
    double _stateClock = 0.0;
    // 这一态最多待多久。0 = 不靠计时自己走，等事件叫
    double _stateLimit = 0.0;
    // 转移表给的冷却。同一件事连着撞两次不许每拍都换态
    double _stateCool = 0.0;
    // 追的是谁。空 = 没在追
    std::wstring _chaseTarget;
    // 多久没看见目标了。超过 CHASE_LOST_SEC 就发 Lost
    double _chaseLost = 0.0;
    // 出手的冷却。没这一条，两个人挤在一块就会 chase→attack→chase 每 0.6 秒
    // 翻一轮 —— 测出来 332 次换态，全是这个空转
    double _attackCool = 0.0;
    // 追人时隔多久才允许重新挑朝向。每拍都挑就会每 45 度翻一次条带，
    // 一次翻面就是解一张 2MB 的图 —— 八个人同场时缓存直接被冲穿，
    // physics 从 200ms 涨到 1500ms
    double _chaseSteer = 0.0;
    // 多久才许再挨一下。没有这一条，两个人挤在一块会每拍推一次，
    // 看着像在互相弹球
    double _hitCool = 0.0;
    // --hit= 的计时。测试里得让它稳定地打得到，太快就全糊在冷却里
    double _hitProbe = 0.0;
    // --state= 钉住不许自己换。测试的时候得让某一态停在那儿不动，
    // 不然它半秒就溜走了，什么都看不出来
    bool _stateLocked = false;
    // 转移表点了名的那套动作集。没有点名的态还是按"走/跑 × 有没有武器"自己推
    bool _artSet = false;
    Art _artOverride = Art::Walk;

    // ---- 任务栏漫步 ----
    //
    // 这一档和状态机没关系：压根不进那张表，也不看鼠标、不看同伴。
    // 拿 State 去套它反而会把走/跑那套迟滞拽进来，人走两步就自己跑起来了
    // 0=走 1=站着 2=摆姿势
    int _tbPhase = 0;
    double _tbTimer = 0.0;
    double _tbEmote = 0.0;
    // -1 朝左，1 朝右。翻面这件事只由这两条边界说了算，别处不许改
    int _tbFacing = 1;
    // 这一回摆的是哪套姿势。挑的时候先看哪套有图
    Art _tbPoseArt = Art::Victory;
    // 这根线本身：脚底的 y，和左右两个端点。都是物理像素，main 给的
    double _tbFootY = 0.0;
    double _tbLoX = 0.0;
    double _tbHiX = 0.0;
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
