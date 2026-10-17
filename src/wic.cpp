#include "wic.h"
#include "config.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace fd {

namespace {
IWICImagingFactory* Factory() {
    static IWICImagingFactory* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&f)))) {
            f = nullptr;
        }
    }
    return f;
}

// 逐帧量一遍 alpha，把每帧的可见框并成格内坐标的总包围盒。
// 必须按帧折算回格内：条带是横向拼的，直接扫整条会把第 0 帧的左和第 7 帧的右
// 当成一个框，量出来的"裁剪"等于没裁
bool AlphaBox(const Strip& s, int& minX, int& minY, int& maxX, int& maxY) {
    const int rowWords = s.stride / 4;
    minX = INT_MAX; minY = INT_MAX; maxX = -1; maxY = -1;
    for (int f = 0; f < s.frames; ++f) {
        const int base = f * s.cellW;
        for (int y = 0; y < s.cellH; ++y) {
            const uint32_t* row = s.pixels.data() + (size_t)y * rowWords + base;
            for (int x = 0; x < s.cellW; ++x) {
                if ((row[x] & 0xFF000000u) != 0) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
    }
    return maxX >= minX && maxY >= minY;
}

void ComputeCrop(Strip& s) {
    int minX, minY, maxX, maxY;
    if (!AlphaBox(s, minX, minY, maxX, maxY)) {
        // 整条全透明：给个整格，别让它退化成 1x1 然后每拍都在换尺寸
        s.cropX = 0;
        s.cropY = 0;
        s.cropW = s.cellW;
        s.cropH = s.cellH;
        return;
    }
    s.cropX = minX;
    s.cropY = minY;
    s.cropW = maxX - minX + 1;
    s.cropH = maxY - minY + 1;
}
}  // namespace

namespace {

// 解码的公共一段：拿到一个已经开好的 decoder，把第一帧读成 Strip。
// 走文件的和解内存的都要过这里 —— cell/frames 那套推法只许有这一处，
// 表情墙要是自己另算一遍，早晚和角色图对不上
bool StripFromDecoder(IWICImagingFactory* f, IWICBitmapDecoder* dec, Strip& out) {
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(dec->GetFrame(0, &frame))) return false;

    bool ok = false;
    IWICFormatConverter* conv = nullptr;
    if (SUCCEEDED(f->CreateFormatConverter(&conv))) {
        // 32bppPBGRA 正好是分层窗口要的那一路：预乘 alpha，通道序 BGRA
        if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                       WICBitmapDitherTypeNone, nullptr, 0.0f,
                                       WICBitmapPaletteTypeCustom))) {
            UINT w = 0, h = 0;
            conv->GetSize(&w, &h);
            if (w > 0 && h > 0) {
                const UINT stride = w * 4;
                const size_t bytes = (size_t)stride * h;
                out.pixels.resize(bytes / 4);
                if (SUCCEEDED(conv->CopyPixels(nullptr, stride, (UINT)bytes,
                                               reinterpret_cast<BYTE*>(out.pixels.data())))) {
                    out.width = (int)w;
                    out.height = (int)h;
                    out.stride = (int)stride;
                    // 方格边长从图自己读，不是写死 256：横向拼帧的约定是
                    // 格高 == 图高，所以 cell = h、frames = w / h。
                    // 以前用 min(CELL_PX, w/h) 的做法，遇到非 256 的图会把
                    // cell 压成 256、frames 算多，AlphaBox 只扫最上面 256 行，
                    // 下半身连框都进不去 —— 那才是真正的"裁腿"
                    int cell = (int)h;
                    if (cell < 1) cell = CELL_PX;
                    if ((int)w % cell != 0) {
                        // 不整除说明这图不是横向拼帧，退回按 256 切，
                        // 至少不会算出 0 帧
                        cell = (std::min)(CELL_PX, (int)w);
                        if (cell < 1) cell = 1;
                    }
                    out.cellW = cell;
                    out.cellH = cell;
                    int n = out.width / out.cellW;
                    out.frames = n < 1 ? 1 : n;
                    ComputeCrop(out);
                    ok = true;
                }
            }
        }
        conv->Release();
    }

    frame->Release();
    return ok;
}

}  // namespace

bool DecodePngStrip(const wchar_t* path, Strip& out) {
    out = Strip{};

    IWICImagingFactory* f = Factory();
    if (!f) return false;

    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(f->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &dec))) {
        return false;
    }
    const bool ok = StripFromDecoder(f, dec, out);
    dec->Release();
    return ok;
}

bool DecodePngMemory(const void* data, size_t len, Strip& out) {
    out = Strip{};
    if (!data || len == 0) return false;

    IWICImagingFactory* f = Factory();
    if (!f) return false;

    // Write copies the static PNG into memory allocated by the stream.
    // Release must also free that allocation (the source array stays untouched).
    IStream* st = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &st))) return false;

    bool ok = false;
    ULONG written = 0;
    if (SUCCEEDED(st->Write(data, (ULONG)len, &written)) && written == (ULONG)len) {
        LARGE_INTEGER zero{};
        st->Seek(zero, STREAM_SEEK_SET, nullptr);
        IWICBitmapDecoder* dec = nullptr;
        if (SUCCEEDED(f->CreateDecoderFromStream(st, nullptr,
                                                 WICDecodeMetadataCacheOnLoad, &dec))) {
            ok = StripFromDecoder(f, dec, out);
            dec->Release();
        }
    }
    st->Release();
    return ok;
}

StripCache& StripCache::Shared() {
    static StripCache c;
    return c;
}

// 按字节淘汰最久没用的那一份。只放掉缓存自己那份引用，外面还攥着的
// shared_ptr 继续有效，也不会像裸指针那样被 erase 挪到别的槽位上去
void StripCache::EvictSkin(const std::wstring& dir) {
    const auto prefix = dir + L"\\";
    for (auto it = _items.begin(); it != _items.end();) {
        if (it->path.compare(0, prefix.size(), prefix) == 0) { _bytes -= it->bytes; it = _items.erase(it); }
        else ++it;
    }
}

void StripCache::Trim() {
    while (_bytes > _budget && _items.size() > 1) {
        size_t victim = 0;
        for (size_t i = 1; i < _items.size(); ++i) {
            if (_items[i].last < _items[victim].last) victim = i;
        }
        _bytes -= _items[victim].bytes;
        _items.erase(_items.begin() + (long)victim);
    }
}

StripPtr StripCache::Get(const std::wstring& path) {
    for (size_t i = 0; i < _items.size(); ++i) {
        if (_items[i].path == path) {
            _items[i].last = ++_clock;
            return _items[i].strip;
        }
    }

    StripPtr s = std::make_shared<Strip>();
    if (!DecodePngStrip(path.c_str(), *s)) return nullptr;

    const size_t bytes = s->pixels.size() * 4;
    _items.push_back(Entry{ path, std::move(s), ++_clock, bytes });
    _bytes += bytes;
    Trim();
    return _items.back().strip;
}

// ---- 动作集 ----
double AnimationRate(Art art) {
    switch (art) {
    case Art::Run: return 20;
    case Art::Down: return 1;
    case Art::Standby: return 7;
    case Art::Run2: return 12;
    case Art::Attack: return 16;
    case Art::BeAttacked: return 1;
    case Art::Kneel2: return 8;
    case Art::Victory: return 15;
    default: return 12;
    }
}
const wchar_t* FolderOf(Art a) {
    switch (a) {
        case Art::Run:     return L"1.Run";
        case Art::Walk:    return L"2.Walk";
        case Art::Standby: return L"4.standby";
        case Art::Run2:    return L"5.run2";
        case Art::Attack:  return L"6.Attack";
        case Art::Victory: return L"9.Victory";
        case Art::Down: return L"3.kneel";
        case Art::BeAttacked: return L"7.BeAttacked";
        case Art::Kneel2: return L"8.Kneel2";
    }
    return L"2.Walk";
}

// 反着找。目录名是配置里写的东西，认不出来就老实说认不出来
bool ArtFromFolder(const std::wstring& folder, Art& out) {
    for (int a = 0; a < ART_COUNT; ++a) {
        if (_wcsicmp(folder.c_str(), FolderOf(static_cast<Art>(a))) == 0) {
            out = static_cast<Art>(a);
            return true;
        }
    }
    return false;
}

// HasWalkSet / ScanSkins 搬去 store.cpp 了：那边管"上哪儿找人"，这里只管
// "一套图长什么样"。两处都留一份迟早会对不上
// 开机扫一遍全部条带求并集框。一条一条解，解完就扔，峰值内存只有一条的量
bool ComputeSkinBBox(const std::wstring& skinDir, BBox& out) {
    namespace fs = std::filesystem;
    int minX = INT_MAX, minY = INT_MAX, maxX = -1, maxY = -1;
    int cellW = 0, cellH = 0;
    int seen = 0;

    // 不写死目录列表：扫 skinDir 下所有存在的动作夹。武器那两套的图必须也
    // 进这个并集，否则那个动作的内容会超出框被无声裁掉
    for (int a = 0; a < ART_COUNT; ++a) {
        const std::wstring folder = FolderOf(static_cast<Art>(a));
        std::error_code ec;
        if (!fs::is_directory(fs::path(skinDir) / folder, ec)) continue;
        for (int d = 0; d < DIR_COUNT; ++d) {
            const std::wstring p =
                (fs::path(skinDir) / folder / (L"dir" + std::to_wstring(d) + L".png")).wstring();
            Strip s;
            if (!DecodePngStrip(p.c_str(), s)) continue;
            int x0, y0, x1, y1;
            if (!AlphaBox(s, x0, y0, x1, y1)) continue;
            // 取所有条带里最小的格径：Blit 是按每条自己的 cellW 去数帧的，
            // 框却只有一个，按大的那个算就会在小的那条上读到条带外面去
            if (seen == 0) { cellW = s.cellW; cellH = s.cellH; }
            else {
                if (s.cellW > 0 && s.cellW < cellW) cellW = s.cellW;
                if (s.cellH > 0 && s.cellH < cellH) cellH = s.cellH;
            }
            if (x0 < minX) minX = x0;
            if (y0 < minY) minY = y0;
            if (x1 > maxX) maxX = x1;
            if (y1 > maxY) maxY = y1;
            ++seen;
        }
    }
    if (seen == 0 || maxX < minX) return false;

    // 框不能越出格子，越出去 Blit 会读到条带外面去
    if (cellW > 0) {
        if (minX < 0) minX = 0;
        if (maxX > cellW - 1) maxX = cellW - 1;
    }
    if (cellH > 0) {
        if (minY < 0) minY = 0;
        if (maxY > cellH - 1) maxY = cellH - 1;
    }
    out.x = minX;
    out.y = minY;
    out.w = maxX - minX + 1;
    out.h = maxY - minY + 1;
    // 这套皮肤自己的方格尺寸。中心线、脚底线这些基准以后都从它换算，
    // 尺寸不同的角色进来才不会拿 256 的尺子量错
    out.cellW = cellW;
    out.cellH = cellH;
    return true;
}

// 表面原样写 PNG：alpha 一个像素都不动。上一版手写 BMP 时把"合成到品红底"
// 的 G 通道也算成了 255+inv，透明像素全变成近白，于是"最后一行有内容"这个
// 读数整个是假的 —— 想看清脚在不在，就得让 alpha 活着到查看器里
bool SaveSurfacePng(const wchar_t* path, const uint32_t* bits, int w, int h, int stride) {
    if (!bits || w < 1 || h < 1) return false;
    IWICImagingFactory* f = Factory();
    if (!f) return false;

    bool ok = false;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IWICMetadataQueryWriter* meta = nullptr;

    if (FAILED(f->CreateStream(&stream))) goto done;
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) goto done;
    if (FAILED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc))) goto done;
    if (FAILED(enc->Initialize(stream, WICBitmapEncoderNoCache))) goto done;
    if (FAILED(enc->CreateNewFrame(&frame, nullptr))) goto done;
    if (FAILED(frame->Initialize(nullptr))) goto done;
    if (FAILED(frame->SetSize((UINT)w, (UINT)h))) goto done;
    {
        // 表面本来就是 32bpp 预乘 BGRA，和分层窗口用的那一路一模一样
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppPBGRA;
        if (FAILED(frame->SetPixelFormat(&fmt))) goto done;
    }
    if (FAILED(frame->WritePixels((UINT)h, (UINT)stride,
                                  (UINT)((size_t)stride * (size_t)h),
                                  reinterpret_cast<BYTE*>(const_cast<uint32_t*>(bits))))) {
        goto done;
    }
    if (FAILED(frame->Commit())) goto done;
    if (FAILED(enc->Commit())) goto done;
    ok = true;

done:
    if (meta) meta->Release();
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    return ok;
}

bool SaveScreenPng(const wchar_t* path, int x, int y, int w, int h) {
    if (w < 1 || h < 1) return false;
    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    HDC mem = CreateCompatibleDC(screen);
    if (!mem) { ReleaseDC(nullptr, screen); return false; }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 自顶向下，和 SaveSurfacePng 那一路一致
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;
    if (dib && bits) {
        HBITMAP old = (HBITMAP)SelectObject(mem, dib);
        // CAPTUREBLT 才带得上分层窗口；SRCCOPY 在某些合成路径下会拿到空内容
        BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY | CAPTUREBLT);
        if (old) SelectObject(mem, old);
        ok = SaveSurfacePng(path, static_cast<const uint32_t*>(bits), w, h, w * 4);
        DeleteObject(dib);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

}  // namespace fd
