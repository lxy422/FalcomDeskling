#pragma once

#include "config.h"   // Vec2
#include "wic.h"
#include "window.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fd {

// 表情号数。名字沿用 WpfTest 的 EmotionSheet.vb，只是这里用英文标识符，
// 中文那套写法留给配置文件 —— 两边靠 EmoteTitle/EmoteIdOfTitle 这层翻译。
enum class EmotionId : int {
    Question = 1,   // 疑问
    Exclaim,        // 惊叹
    Happy,          // 高兴
    Like,           // 喜欢
    Angry,          // 愤怒
    Mess,           // 一团麻
    Sweat,          // 流汗
    Deadpan,        // 黑线
    Speechless,     // 无语
    Sleepy,         // 瞌睡
    Bulb,           // 灯泡
    Notice,         // 注意
    Rage,           // 狂怒
    Dizzy,          // 眩晕
    Confused,       // 混乱
    Sparkle,        // 闪耀
    Talk,           // 说话
    Quest           // 任务
};

constexpr int EMOTE_LAST_ID = 18;

const wchar_t* EmoteTitle(int id);
// 认不出来只给 0，不猜 —— 配置里写错一枚表情名，得让它响一声而不是换一张脸
int EmoteIdOfTitle(const std::wstring& name);

// 一帧 = 图集里的一个矩形
struct EmoteFrame {
    int x = 0, y = 0, w = 0, h = 0;
};

// 内嵌在 exe 里的那张表情墙。
//
// 它不走文件：PNG 由 tools/embed_emotio.ps1 转成字节数组编进二进制，
// 启动时从内存解一次。理由和 WpfTest 那边一样 —— 这是一张整图，
// 用不着摆成目录，更不该让发布时多带一个会丢的文件。
class EmotionSheet {
public:
    static EmotionSheet& Shared();

    // 解那张内嵌 PNG。失败不算致命：气泡从此一声不响，Fault() 里说清为什么
    bool Load();
    bool Ready() const { return _loaded; }
    const std::wstring& Fault() const { return _fault; }

    int FrameCount(int id) const;
    const EmoteFrame* Frames(int id) const;
    bool DrawFrame(HDC dc, int id, int frame, int x, int y, int w, int h) const;

    // 整组最大框。**泡泡窗口的尺寸只从它算**，不能拿当前这一帧的高：
    // 狂怒前两帧 32 高、后两帧 64 高，按帧定位置会让它每 300 毫秒往上蹿一次
    void GroupBox(int id, int& w, int& h) const;

    const uint32_t* Pixels() const { return _strip.pixels.data(); }
    int Stride() const { return _strip.stride; }
    int AtlasW() const { return _strip.width; }
    int AtlasH() const { return _strip.height; }

private:
    EmotionSheet() = default;
    Strip _strip;
    bool _loaded = false;
    std::wstring _fault;
};

// 头顶那枚表情泡泡：自己一个分层窗口，自己知道该画在哪、什么时候收。
//
// 走"一人一个额外窗口"而不是画进角色自己的表面，是因为后者得让气泡内容
// 也进 ComputeSkinBBox 的并集框 —— 那正是上一轮刚修完的那类"无声裁掉"。
// 窗口数翻倍的代价用另一条压住：静默时 Show(false)，一个像素都不传。
class EmoteBubble {
public:
    ~EmoteBubble() { Destroy(); }

    bool Create();
    void Destroy();

    // 放一枚表情。返回 False = 这号不存在或者图集没解出来，
    // 调用方得能听见这句失败，别让它变成"点了没反应"
    bool Show(int id, double seconds);
    void Hide();

    // ownerFeet = 角色的脚底锚点，headTop = 真的头顶线（不是 BodyLen 那把尺子）
    void Tick(double dt, Vec2 ownerFeet, double headTop, double k, bool faceLeft);
    void Present();

    bool Busy() const { return _id != 0 && _life > 0.0; }
    int Id() const { return _id; }
    const wchar_t* Title() const { return EmoteTitle(_id); }
    void SetVisible(bool on);
    // 气泡窗口现在摆在屏幕哪儿。写进 stats 用：泡泡到底画没画对位置，
    // 光看角色的 rect 是问不出来的，得两个矩形一起看
    bool Rect(int& x, int& y, int& w, int& h) const;

private:
    enum Mode { Static = 0, Loop, Seq };

    void Plan(int n);
    int FrameIndex() const;
    // 窗口该摆在哪、多大。尺寸按整组最大框，位置按这一拍真的头顶
    void Layout(Vec2 ownerFeet, double headTop, double k, bool faceLeft);

    std::unique_ptr<LayeredWindow> _win;
    int _id = 0;
    int _frame = 0;
    int _maxW = 32, _maxH = 32;
    int _slots = 1;
    Mode _mode = Static;
    double _step = 0.0;
    double _clock = 0.0;
    double _life = 0.0;
    double _k = 1.0;
    bool _needUpload = false;
    bool _shown = false;
    // Layout 算出来的这一拍该摆在哪、多大；Present 只照着它搬
    int _boxW = 0, _boxH = 0;
    int _x = 0, _y = 0;
    bool _havePos = false;
    int _lastX = -1, _lastY = -1, _lastW = 0, _lastH = 0;
    // 最近一次知道自己在谁头上。Show() 只换内容，位置得当下就能定 ——
    // 不然第一拍会拿上一个泡泡留下的座标，闪一下才归位
    Vec2 _lastFeet{};
    double _lastHeadTop = 0.0, _lastK = 1.0;
    bool _lastFaceLeft = false;
    bool _hasPose = false;
};

}  // namespace fd
