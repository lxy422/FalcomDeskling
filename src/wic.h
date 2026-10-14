#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace fd {

// 一条横向条带：宽 = 帧数 * cellW，高 = cellH，像素是 32bpp 预乘 BGRA
struct Strip {
    int frames = 0;
    int width = 0;
    int height = 0;
    int stride = 0;      // 字节/行
    int cellW = 0;
    int cellH = 0;
    // 这条 strip 自己所有帧并起来的可见包围盒，**格内**坐标。
    // 它现在只当 fallback 用：窗口尺寸和锚点走 ComputeSkinBBox 的全局框，
    // 因为单条的框每条都差几行，换向时 Resize + 锚点跳变会切掉半截身子
    int cropX = 0;
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;
    std::vector<uint32_t> pixels;

    const uint32_t* Cell(int i) const {
        return pixels.data() + (size_t)(i * cellW);
    }
};

// 懒解码 + 小容量 LRU：一套皮肤 72 条全解码是 110~170 兆，绝不能这么干
bool DecodePngStrip(const wchar_t* path, Strip& out);

// 一套皮肤所有条带合起来的可见范围。格内坐标，和 Strip 的 crop 同一套基准。
// cellW/cellH 是这套皮肤自己的方格尺寸 —— 中心线、脚底线这些基准都按它换算，
// 尺寸不同的角色进来才不会被拿 256 的尺子量错
struct BBox {
    int x = 0, y = 0, w = 0, h = 0;
    int cellW = 0, cellH = 0;  // 0 = 沿用 CELL_PX
};

// 开机时把 1.Run / 2.Walk 两个状态 × 8 个方向全扫一遍求并集。
// 窗口尺寸和锚点从此恒定，不再随换向 Resize —— 单条的框每条都差几行，
// 跳变落在屏幕上就是"下半截没了"
bool ComputeSkinBBox(const std::wstring& skinDir, BBox& out);

// 把一块 32bpp 预乘 BGRA 表面写成 PNG，用来回答"脚到底有没有画进表面"——
// 坐标推演只能证明"应该画进去"，这张图才证明"真的画进去了"。
// alpha 原样保留、一个像素都不合成：上一版写 BMP 时品红背景的 G 通道算错了，
// 透明像素变成近白，"最后一行有内容"的读数整个是假的
bool SaveSurfacePng(const wchar_t* path, const uint32_t* bits, int w, int h, int stride);

// 把屏幕上的一块矩形原样抓下来写成 PNG。和上面那个配对使用：同一拍里一个
// 抓"我以为画上去的东西"，一个抓"屏幕上真的有的东西"，两边一比就知道
// 丢的那一段是在表面里还是在窗口之外
bool SaveScreenPng(const wchar_t* path, int x, int y, int w, int h);

// 共享所有权。以前发出去的是裸指针，deque::erase 一挪槽位，"指针还有效、
// 内容已经是另一条 strip"这种别名失效就会悄悄发生，而且 LoadFrames 里
// path == _stripPath 会让它每拍都跳过修正。换成 shared_ptr 之后这条路没了：
// 淘汰只放掉缓存自己那份引用，外面还拿着的不会变、也不会悬空
using StripPtr = std::shared_ptr<Strip>;

class StripCache {
public:
    static constexpr size_t kMax = 2;

    // 命中返回已有的那份，未命中解码一份，失败返回空
    StripPtr Get(const std::wstring& path);
    void Clear() { _items.clear(); }
    size_t Bytes() const;

private:
    struct Entry {
        std::wstring path;
        StripPtr strip;
        uint64_t last = 0;
    };
    std::deque<Entry> _items;
    uint64_t _clock = 0;
};

}  // namespace fd
