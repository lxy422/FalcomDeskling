#include "emote.h"
#include "emotio_data.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fd {
namespace {

constexpr int kCell = 32;

// slot(n)：图集里第 n 格。8 列 8 行，行优先
struct Slot {
    int x, y, w, h;
};

constexpr Slot slot(int n) {
    return Slot{(n % 8) * kCell, (n / 8) * kCell, kCell, kCell};
}

// 狂怒最后两帧的火星画在上半行里，只取 slot 会缺一截 —— 得把它上面那格带进来
constexpr Slot slotUp(int n) {
    return Slot{(n % 8) * kCell, (n / 8) * kCell - kCell, kCell, kCell * 2};
}

// 帧表。坐标只有这里一份，和 WpfTest 的 EmotionSheet.BuildTable 逐条对过
struct EmoteDef {
    const wchar_t* title;
    Slot frames[6];
    int count;
};

const EmoteDef kTable[EMOTE_LAST_ID + 1] = {
    {L"", {slot(0)}, 0},
    {L"疑问", {slot(0), slot(1)}, 2},
    {L"惊叹", {slot(2), slot(3), slot(4), slot(5), slot(6), slot(7)}, 6},
    {L"高兴", {slot(8), slot(9)}, 2},
    {L"喜欢", {slot(10), slot(11)}, 2},
    {L"愤怒", {slot(12), slot(13)}, 2},
    {L"一团麻", {slot(14), slot(15)}, 2},
    {L"流汗", {slot(16), slot(17), slot(18), slot(19)}, 4},
    {L"黑线", {slot(20), slot(21), slot(22), slot(23)}, 4},
    {L"无语", {slot(24), slot(25), slot(26), slot(27)}, 4},
    {L"瞌睡", {slot(28), slot(29), slot(30), slot(31), slot(32), slot(33)}, 6},
    {L"灯泡", {slot(34), slot(35), slot(36)}, 3},
    {L"注意", {slot(40), slot(41), slot(42), slot(43)}, 4},
    {L"狂怒", {slot(44), slot(45), slotUp(46), slotUp(47)}, 4},
    {L"眩晕", {slot(48), slot(49), slot(50)}, 3},
    {L"混乱", {slot(51), slot(52), slot(53)}, 3},
    {L"闪耀", {slot(54), slot(55), slot(56), slot(57)}, 4},
    {L"说话", {slot(59), slot(60)}, 2},
    {L"任务", {slot(61), slot(62)}, 2},
};

// 泡泡字号 = 图上像素 * k。想整体放大/缩小就改这一个数
constexpr double kBubbleScale = 1.0;
// 尾巴往头顶上方压多少（按"脚底到头顶"这段真实高度），完全不叠会看着飘在头上
constexpr double kNeckSink = 0.16;
// 整块往面朝那侧让一点，别正正堵在脸上
constexpr double kSideShift = 0.3;
// 三帧以上单向走完，三帧以内循环，两条支路都是 300 毫秒一张
constexpr double kSeqFrameSec = 0.3;
constexpr double kLoopFrameSec = 0.3;
constexpr int kLoopTimes = 5;

}  // namespace

const wchar_t* EmoteTitle(int id) {
    if (id >= 1 && id <= EMOTE_LAST_ID) return kTable[id].title;
    return L"?";
}

int EmoteIdOfTitle(const std::wstring& name) {
    if (name.empty()) return 0;
    for (int i = 1; i <= EMOTE_LAST_ID; ++i) {
        if (name == kTable[i].title) return i;
    }
    return 0;
}

EmotionSheet& EmotionSheet::Shared() {
    static EmotionSheet s;
    return s;
}

bool EmotionSheet::Load() {
    if (_loaded) return true;
    if (!DecodePngMemory(kEmotioPng, kEmotioPngLen, _strip)) {
        _fault = L"内嵌表情墙解码失败";
        return false;
    }
    if (_strip.width < 8 * kCell || _strip.height < 8 * kCell) {
        _fault = L"表情墙尺寸不对，应该是 256x256";
        return false;
    }
    _loaded = true;
    return true;
}

int EmotionSheet::FrameCount(int id) const {
    if (id < 1 || id > EMOTE_LAST_ID) return 0;
    return kTable[id].count;
}

const EmoteFrame* EmotionSheet::Frames(int id) const {
    if (id < 1 || id > EMOTE_LAST_ID) return nullptr;
    return reinterpret_cast<const EmoteFrame*>(kTable[id].frames);
}

void EmotionSheet::GroupBox(int id, int& w, int& h) const {
    w = kCell;
    h = kCell;
    const int n = FrameCount(id);
    const EmoteFrame* fr = Frames(id);
    if (!fr || n <= 0) return;
    for (int i = 0; i < n; ++i) {
        if (fr[i].w > w) w = fr[i].w;
        if (fr[i].h > h) h = fr[i].h;
    }
}

// ---- EmoteBubble ----

bool EmoteBubble::Create() {
    if (_win) return true;
    _win = std::make_unique<LayeredWindow>();
    // 气泡永远不参与命中测试：原版 EmoteBubble 也把 IsHitTestVisible 关了，
    // 左/右键还得留给小人本体
    if (!_win->Create(kCell * 2, kCell * 2, /*clickThrough=*/true)) {
        _win.reset();
        return false;
    }
    _win->Show(false);
    return true;
}

void EmoteBubble::Destroy() {
    if (_win) {
        _win->Destroy();
        _win.reset();
    }
    _id = 0;
    _life = 0.0;
    _shown = false;
}

void EmoteBubble::Plan(int n) {
    if (n <= 1) {
        _mode = Static;
        _step = 0.0;
        _slots = 1;
    } else if (n <= 3) {
        _mode = Loop;
        _step = kLoopFrameSec;
        _slots = n * kLoopTimes;
    } else {
        _mode = Seq;
        _step = kSeqFrameSec;
        _slots = n;
    }
}

bool EmoteBubble::Show(int id, double seconds) {
    EmotionSheet& sheet = EmotionSheet::Shared();
    if (!sheet.Ready() && !sheet.Load()) return false;
    const int n = sheet.FrameCount(id);
    if (n <= 0) return false;

    if (!_win && !Create()) return false;

    _id = id;
    _frame = 0;
    sheet.GroupBox(id, _maxW, _maxH);
    Plan(n);
    _clock = 0.0;
    _life = seconds > 0.6 ? seconds : 0.6;
    _needUpload = true;
    _lastW = 0;   // 逼着下一趟先 Resize 再上传
    _win->Show(true);
    // 姿态喂过的第一趟就得把位置定下来：Show() 只换内容，位置要当下就能算，
    // 不然第一帧会拿上一个泡泡留下的座标，闪一下才归位
    if (_hasPose) Layout(_lastFeet, _lastHeadTop, _lastK, _lastFaceLeft);
    return true;
}

void EmoteBubble::Hide() {
    _id = 0;
    _life = 0.0;
    if (_win) _win->Show(false);
    _shown = false;
}

bool EmoteBubble::Rect(int& x, int& y, int& w, int& h) const {
    if (_id == 0 || !_win || !_win->Valid()) return false;
    RECT r{};
    if (!GetWindowRect(_win->Handle(), &r)) return false;
    x = r.left;
    y = r.top;
    w = r.right - r.left;
    h = r.bottom - r.top;
    return w > 0 && h > 0;
}

void EmoteBubble::SetVisible(bool on) {
    if (_win) _win->Show(on);
}

int EmoteBubble::FrameIndex() const {
    if (_step <= 0.0 || _slots <= 1) return 0;
    int s = (int)std::floor(_clock / _step);
    if (s >= _slots) s = _slots - 1;
    if (s < 0) s = 0;
    const int n = EmotionSheet::Shared().FrameCount(_id);
    if (_mode == Loop && n > 0) return s % n;
    return s;
}

void EmoteBubble::Layout(Vec2 ownerFeet, double headTop, double k, bool faceLeft) {
    const double w = (double)_maxW * k * kBubbleScale;
    const double h = (double)_maxH * k * kBubbleScale;
    if (w <= 0.0 || h <= 0.0) return;

    // 底沿压在多长的脖子上，按**这一拍真的有多高**算，不能拿 BodyLen=82 那把尺子：
    // 金量出来是一百零九，还按八十二走，压的就是头顶那几行像素
    const double sink = (ownerFeet.y - headTop) * kNeckSink;
    const double bottom = headTop + sink;
    // 横向同样按整组最宽那一格定，序列里换到窄一格就不会左右跳
    const double cx = ownerFeet.x + (faceLeft ? -1.0 : 1.0) * w * kSideShift;

    _boxW = (int)std::lround(w);
    _boxH = (int)std::lround(h);
    _x = (int)std::lround(cx - w * 0.5);
    _y = (int)std::lround(bottom - h);
    _havePos = true;
    _lastFeet = ownerFeet;
    _lastHeadTop = headTop;
    _lastK = k;
    _lastFaceLeft = faceLeft;
    _hasPose = true;
}

void EmoteBubble::Tick(double dt, Vec2 ownerFeet, double headTop, double k, bool faceLeft) {
    if (_id == 0) {
        if (_shown) {
            _shown = false;
            if (_win) _win->Show(false);
        }
        return;
    }
    _life -= dt;
    if (_life <= 0.0) {
        Hide();
        return;
    }
    _clock += dt;
    const int n = EmotionSheet::Shared().FrameCount(_id);
    if (n > 1) {
        const int idx = FrameIndex();
        if (idx != _frame) {
            _frame = idx;
            _needUpload = true;
        }
    }
    Layout(ownerFeet, headTop, k, faceLeft);
    _k = k;
}

void EmoteBubble::Present() {
    if (_id == 0 || !_win) return;
    // 暂停的时候 Tick 不会叫，位置就停在上一拍 —— 那不要紧，要紧的是
    // "托盘点了表情却什么都不出现"。所以这里只要还没算过位置就现算一次
    if (!_havePos) {
        // 没有主人就没有脚底，等下一拍。这里只能安静地退回去
        return;
    }

    if (_boxW != _lastW || _boxH != _lastH) {
        if (!_win->Resize(_boxW, _boxH)) return;
        _needUpload = true;
        _lastW = _boxW;
        _lastH = _boxH;
    }

    if (_needUpload) {
        const EmotionSheet& sheet = EmotionSheet::Shared();
        const EmoteFrame* fr = sheet.Frames(_id);
        if (!fr) return;
        const int n = sheet.FrameCount(_id);
        int idx = _frame;
        if (idx > n - 1) idx = n - 1;
        if (idx < 0) idx = 0;
        const EmoteFrame& f = fr[idx];

        void* bits = _win->Bits();
        if (!bits) return;
        memset(bits, 0, (size_t)_win->Stride() * (size_t)_win->H());

        // Uniform + 贴底居中：矮一格的内容不许被拉满整个盒子，否则狂怒前两帧
        // 会被撑成 64 高，看着就是同一张脸忽然糊了
        const double sc = (std::min)((double)_boxW / (double)f.w, (double)_boxH / (double)f.h);
        const int dstW = (std::max)(1, (int)std::lround(f.w * sc));
        const int dstH = (std::max)(1, (int)std::lround(f.h * sc));
        const int dstX = (_boxW - dstW) / 2;
        const int dstY = _boxH - dstH;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = EmotionSheet::Shared().AtlasW();
        bi.bmiHeader.biHeight = -EmotionSheet::Shared().AtlasH();
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        HDC dc = _win->MemDC();
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, dstX, dstY, dstW, dstH, f.x, f.y, f.w, f.h,
                      EmotionSheet::Shared().Pixels(), &bi, DIB_RGB_COLORS, SRCCOPY);

        _win->Present(_x, _y);
        _needUpload = false;
        _shown = true;
    } else if (_x != _lastX || _y != _lastY) {
        _win->MoveTo(_x, _y);
    }
    _lastX = _x;
    _lastY = _y;
}

}  // namespace fd
