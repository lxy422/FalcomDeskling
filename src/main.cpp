#include "config.h"
#include "runner.h"
#include "tray.h"
#include "wic.h"
#include "window.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <psapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")

namespace fd {
namespace {

// 一个上场的角色 = 一个分层窗口 + 一个 Runner。
// 用 unique_ptr 而不是直接 vector<Entity>：Runner::Create 里存的是
// LayeredWindow*，vector 扩容一搬对象那个指针就废了
struct Entity {
    std::unique_ptr<LayeredWindow> win;
    std::unique_ptr<Runner> runner;
    std::wstring name;
    bool hasWeaponArt = false;
};

std::vector<std::unique_ptr<Entity>> g_entities;
Tray g_tray;
// 托盘必须挂在一个拆不掉的窗口上：角色窗口随时能加能删，要是拿第一个人的
// 窗口当宿主，删掉那个人托盘图标就跟着没了
HWND g_msg = nullptr;
bool g_quit = false;
bool g_paused = false;
bool g_stats = false;
double g_statsClock = 0.0;
uint64_t g_usPhysics = 0;

// 建下一个实体时照抄这份 Options，省得每个角色重解析一遍命令行
Runner::Options g_opt;
std::vector<SkinInfo> g_skins;
// 全局装备开关：勾一次，桌上所有人一起抽剑/收剑
bool g_armed = false;
int g_maxEntities = 8;
bool g_interactive = false;
bool g_notop = false;
bool g_dumpCell = false;
int g_forceDir = -1, g_forceFrame = -1;
// 这几个必须是全局：AddEntity 建第二个人的时候要拿它们判"dump 只让第一个
// 角色写"。只在局部 opt 上设，后面加进来的人就全都没了
bool g_surfaceProbe = false;
bool g_dumpScreen = false;
std::wstring g_dumpScreenPath;
bool g_dumpSurface = false;
std::wstring g_dumpSurfacePath;

// --key=value 取等号后面那段
std::wstring ValueOf(const std::wstring& s, const wchar_t* key) {
    const std::wstring k = key;
    if (s.size() > k.size() + 1 && s[k.size()] == L'=' && s.compare(0, k.size(), k) == 0) {
        return s.substr(k.size() + 1);
    }
    return L"";
}

bool IsFlag(const std::wstring& s, const wchar_t* key) { return s == key; }

// 资源目录的找法照 CharacterStore.RootDirCore：先看 exe 旁边，再往上翻，
// 最后兜到旁边那个 WpfTest 仓库 —— Phase 1 有意不往新仓库拷那 16MB 图
std::wstring FindChrRoot(const std::wstring& explicitRoot) {
    namespace fs = std::filesystem;
    std::vector<std::wstring> cands;
    if (!explicitRoot.empty()) cands.push_back(explicitRoot);

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path dir = fs::path(exe).parent_path();
    for (int i = 0; i < 8 && !dir.empty(); ++i) {
        cands.push_back((dir / L"Chr").wstring());
        cands.push_back((dir / L"Resource" / L"Chr").wstring());
        dir = dir.parent_path();
    }
    cands.push_back(L"C:\\Users\\lxy_42\\source\\repos\\WpfTest\\Resource\\Chr");

    for (const std::wstring& c : cands) {
        std::error_code ec;
        if (!c.empty() && fs::is_directory(c, ec)) return c;
    }
    return L"";
}

bool HasWalkSet(const std::wstring& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::is_regular_file(fs::path(dir) / L"2.Walk" / L"dir0.png", ec);
}

// 名单排过序，随机定主角这件事才不会每次启动换个样子
std::wstring PickSkin(const std::wstring& root, const std::wstring& wanted) {
    namespace fs = std::filesystem;
    if (!wanted.empty()) {
        std::wstring p = (fs::path(root) / wanted).wstring();
        return HasWalkSet(p) ? p : L"";
    }
    std::vector<std::wstring> names;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        std::wstring p = it->path().wstring();
        std::wstring leaf = it->path().filename().wstring();
        if (leaf == L"Enemy" || leaf == L"Resource" || leaf == L"Chr") continue;
        if (HasWalkSet(p)) names.push_back(leaf);
    }
    std::sort(names.begin(), names.end());
    if (names.empty()) return L"";
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    const size_t i = (size_t)(c.QuadPart ^ (ULONGLONG)GetTickCount64()) % names.size();
    return (fs::path(root) / names[i]).wstring();
}

// ---- 实体管理 ----

// 建一个角色：窗口 + Runner。失败就把自己收拾干净再回来，不留半个窗口
bool AddEntity(const SkinInfo& si) {
    if ((int)g_entities.size() >= g_maxEntities) return false;

    auto win = std::make_unique<LayeredWindow>();
    // 先按整格开一个尺寸，第一拍 Present 会照它自己那套皮肤的框 Resize 一次。
    // 这里不能用别的算法推尺寸：每个角色的框不一样
    const int cell = (int)std::lround((double)CELL_PX * g_opt.k);
    // 点击穿透和置顶是窗口那一层的事，不是 Runner 的参数，走独立全局
    if (!win->Create(cell, cell, !g_interactive, !g_notop)) {
        return false;
    }

    auto e = std::make_unique<Entity>();
    e->name = si.name;
    e->hasWeaponArt = si.hasStandby;
    e->win = std::move(win);

    Runner::Options o = g_opt;
    o.skinDir = si.dir;
    o.hasWeaponArt = si.hasStandby;
    // 钉方向/帧是给调试截图用的，多角色时只该管第一个，别把所有人都冻住
    o.forceDir = g_entities.empty() ? g_forceDir : -1;
    o.forceFrame = g_entities.empty() ? g_forceFrame : -1;
    // 落盘的几个只让第一个角色写，否则同一个文件名被 N 个人抢着写
    o.dumpSurface = g_entities.empty() && g_dumpSurface;
    o.dumpScreen = g_entities.empty() && g_dumpScreen;
    o.dumpCell = g_entities.empty() && g_dumpCell;
    // 探针不落盘，只是扫一遍自己的表面。多角色回归时要看的就是"每个人
    // 自己的框对不对"，所以这个得人人都开
    o.surfaceProbe = g_surfaceProbe;

    e->runner = std::make_unique<Runner>();
    if (!e->runner->Create(e->win.get(), o)) {
        return false;
    }
    e->runner->SetArmed(g_armed);
    e->runner->Present();
    g_entities.push_back(std::move(e));
    return true;
}

// 删一个角色。顺序要紧：Runner 持有 LayeredWindow*，得先让它撒手
void RemoveEntity(const std::wstring& name) {
    for (auto it = g_entities.begin(); it != g_entities.end(); ++it) {
        if ((*it)->name == name) {
            (*it)->runner.reset();
            (*it)->win.reset();
            g_entities.erase(it);
            return;
        }
    }
}

// 还没上场的名单。每次菜单开起来现问一遍，中途往目录里丢个夹也不用重启
std::vector<std::wstring> NamesAddable() {
    std::vector<std::wstring> out;
    for (const SkinInfo& s : g_skins) {
        bool on = false;
        for (const auto& e : g_entities) {
            if (e->name == s.name) { on = true; break; }
        }
        if (!on) out.push_back(s.name);
    }
    return out;
}

std::vector<std::wstring> NamesOnscreen() {
    std::vector<std::wstring> out;
    for (const auto& e : g_entities) out.push_back(e->name);
    return out;
}

// 全局开关：逐个叫过去。缺武器图的人自己会在 SetArmed 里吞掉
void SetAllArmed(bool flag) {
    g_armed = flag;
    for (auto& e : g_entities) e->runner->SetArmed(flag);
}

std::wstring FindIcon() {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    fs::path dir = fs::path(exe).parent_path();
    const wchar_t* locals[] = { L"falcom.ico", L"resource\\falcom.ico" };
    for (int i = 0; i < 4 && !dir.empty(); ++i) {
        for (const wchar_t* l : locals) {
            std::error_code ec;
            fs::path p = dir / l;
            if (fs::is_regular_file(p, ec)) return p.wstring();
        }
        dir = dir.parent_path();
    }
    std::error_code ec;
    fs::path other = L"C:\\Users\\lxy_42\\source\\repos\\WpfTest\\WpfTest\\ed6_win_DX9_00001.ico";
    if (fs::is_regular_file(other, ec)) return other.wstring();
    return L"";
}

// 失败时先落到 exe 旁边的日志里，再弹窗 —— 只在弹窗里写原因的话，
// 无人值守地跑就永远看不见它到底死在哪一步
void Fail(const wchar_t* detail) {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring log = (fs::path(exe).parent_path() / L"falcomdeskling_error.txt").wstring();
    HANDLE h = CreateFileW(log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        const std::wstring line = std::wstring(detail) + L"\r\n";
        DWORD n = 0;
        WriteFile(h, line.c_str(), (DWORD)(line.size() * sizeof(wchar_t)), &n, nullptr);
        CloseHandle(h);
    }
    MessageBoxW(nullptr, detail, L"FalcomDeskling", MB_ICONERROR);
}

// --stats：每秒往 exe 旁边写一行自采样。外部光看任务管理器分不清
// "多少拍在搬窗口、多少拍在拷表面"，这一行就是为了让这两个数分开
void WriteStatsLine(double elapsed) {
    namespace fs = std::filesystem;

    FILETIME k{}, u{}, cri{}, cpo{};
    GetProcessTimes(GetCurrentProcess(), &k, &u, &cri, &cpo);
    ULARGE_INTEGER cpu{};
    cpu.LowPart = cri.dwLowDateTime;
    cpu.HighPart = cri.dwHighDateTime;
    const double cpuSec = (double)cpu.QuadPart / 1e7;

    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    size_t ws = 0;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        ws = (size_t)(pmc.WorkingSetSize / 1024);
    }

    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    // 桌上好多个人的时候，光看进程总 CPU 分不出是谁在吃。所以汇总一行、
    // 再逐个角色各一行 —— 少了后面那些，"这个卡是因为他图大"就永远说不清
    wchar_t head[320];
    swprintf(head, 320,
             L"t=%.1f n=%zu armed=%d cpu=%.2fs ws=%zuKB physics=%.2fms\n",
             elapsed, g_entities.size(), g_armed ? 1 : 0, cpuSec, ws,
             (double)g_usPhysics / 1000.0);

    std::wstring all = head;
    for (const auto& e : g_entities) {
        const Runner::Stats& s = e->runner->GetStats();
        RECT wr{};
        const bool haveRect = GetWindowRect(e->win->Handle(), &wr) != FALSE;
        wchar_t line[512];
        swprintf(line, 512,
                 L"  [%s] up=%llu mv=%llu bounce=%llu upload=%.2fms state=%d dir=%d "
                 L"pos=%.0f,%.0f armed=%d\n"
                 L"     blit src=%d,%d %dx%d -> dst=%dx%d crop=%d,%d %dx%d "
                 L"frame=%d cell=%d k=%.3f\n"
                 L"     surf win=%dx%d first=%d last=%d rowsBelowLast=%d\n"
                 L"     rect=(%ld,%ld)-(%ld,%ld) %ldx%ld overBottom=%ld\n",
                 e->name.c_str(),
                 (unsigned long long)s.uploads, (unsigned long long)s.moves,
                 (unsigned long long)s.bounces, (double)s.usUpload / 1000.0,
                 (int)e->runner->GetState(), e->runner->Dir(),
                 e->runner->Pos().x, e->runner->Pos().y,
                 e->runner->Armed() ? 1 : 0,
                 s.blitSrcX, s.blitSrcY, s.blitSrcW, s.blitSrcH,
                 s.blitDstW, s.blitDstH,
                 s.cropX, s.cropY, s.cropW, s.cropH, s.frameIndex, s.cellW, s.k,
                 s.winW, s.winH, s.surfFirst, s.surfLast, s.winH - 1 - s.surfLast,
                 (long)wr.left, (long)wr.top, (long)wr.right, (long)wr.bottom,
                 (long)(wr.right - wr.left), (long)(wr.bottom - wr.top),
                 (long)(haveRect ? wr.bottom - work.bottom : 0));
        all += line;
    }

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring path = (fs::path(exe).parent_path() / L"falcomdeskling_stats.txt").wstring();
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD n = 0;
    WriteFile(h, all.c_str(), (DWORD)(all.size() * sizeof(wchar_t)), &n, nullptr);
    CloseHandle(h);
}

// --dumpcrop：把每条贴图的裁剪框和逐帧 alpha 包围盒落到 exe 旁边的日志里。
// 以后再说"脚被切掉了"，先在这儿看一眼 C++ 自己量出来的是多少，别拿截图猜。
void DumpCrops(const std::wstring& skin) {
    namespace fs = std::filesystem;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring path = (fs::path(exe).parent_path() / L"falcomdeskling_dump.txt").wstring();
    HANDLE h = CreateFileW(path.c_str(), FILE_WRITE_DATA, FILE_SHARE_READ, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    // 和 ComputeSkinBBox 同一套：扫动作目录而不是写死两个，武器那两套也得进来，
    // 否则"dump 里看着没问题"和"实际会裁到"之间会差出一截
    const wchar_t* folders[] = { L"1.Run", L"2.Walk", L"4.standby", L"5.run2" };
    for (const wchar_t* folder : folders) {
        for (int d = 0; d < DIR_COUNT; ++d) {
            const std::wstring p =
                (fs::path(skin) / folder / (L"dir" + std::to_wstring(d) + L".png")).wstring();
            Strip s;
            if (!DecodePngStrip(p.c_str(), s)) continue;
            wchar_t line[256];
            DWORD n = 0;
            swprintf(line, 256, L"%s\\dir%d  %dx%d cell=%dx%d frames=%d  crop=%d,%d %dx%d\r\n",
                     folder, d, s.width, s.height, s.cellW, s.cellH, s.frames,
                     s.cropX, s.cropY, s.cropW, s.cropH);
            WriteFile(h, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &n, nullptr);

            const int rowWords = s.stride / 4;
            for (int f = 0; f < s.frames; ++f) {
                int minX = INT_MAX, minY = INT_MAX, maxX = -1, maxY = -1;
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
                swprintf(line, 256, L"    f%d  x[%d..%d] y[%d..%d]\r\n", f, minX, maxX, minY, maxY);
                WriteFile(h, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &n, nullptr);
            }
        }
    }
    CloseHandle(h);
}

// 托盘和"退出"这两件事都得有个拆不掉的宿主，所以专门开一个窗口：它不上屏、
// 不进 alt-tab，只收 WM_TRAY。角色窗口是随时能加能删的，靠不住
LRESULT CALLBACK MsgWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == Tray::WM_TRAY) {
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            g_tray.Popup();
        } else if (LOWORD(lp) == WM_LBUTTONUP) {
            g_paused = !g_paused;
        }
        return 0;
    }
    if (msg == WM_DESTROY) {
        g_quit = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool CreateMsgWindow() {
    namespace fs = std::filesystem;
    static bool registered = false;
    const wchar_t* kClass = L"FalcomDeskling.Msg";
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = MsgWndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        registered = true;
    }
    // 看得见但看不见：SetForegroundWindow 只理可见窗口，而托盘菜单要先抢一次
    // 前台才会在"点到别处"时自己关掉。所以拿 1x1 摆到 -32000 这个所有屏幕都
    // 够不着的位置 —— 它既算可见，又一个像素都不会画到屏上
    g_msg = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClass,
                            L"FalcomDeskling", WS_POPUP, -32000, -32000, 1, 1,
                            nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!g_msg) return false;
    ShowWindow(g_msg, SW_SHOWNOACTIVATE);
    return true;
}

}  // namespace
}  // namespace fd

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    using namespace fd;

    // 先声明 DPI 感知，后面拿到的坐标才全是物理像素，换算只有一站
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring rootArg, skinArg;
    double scalePct = DEFAULT_SCALE * 100.0;
    bool wander = false, interactive = false, smooth = false;
    bool noPresent = false, movePath = false, notop = false, help = false;
    bool dumpCrop = false;
    int forceDir = -1, forceFrame = -1;
    bool surfaceProbe = false;
    bool dumpScreen = false;
    bool dumpSurface = false;
    bool dumpCell = false;
    bool debugFrame = false, debugFull = false, debugMarks = false;
    int tickMs = DEFAULT_TICK_MS;
    // --key value 这种空格写法以前会被 ValueOf 判成空 —— 于是 --skin 形同没传，
    // PickSkin 走 QueryPerformanceCounter^GetTickCount64 随机挑一套图。调试时
    // 拿这套东西测出来的结论全是假的，所以先在本地把两个 token 并成一个。
    // 不能去动 argv 本身：下面还等着 LocalFree(argv)，改了就是未定义行为
    std::vector<std::wstring> tokens;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        const bool bareKey = (a.size() > 2 && a[0] == L'-' && a[1] == L'-' &&
                              a.find(L'=') == std::wstring::npos);
        if (bareKey && i + 1 < argc) {
            const std::wstring nxt = argv[i + 1];
            if (!nxt.empty() && nxt[0] != L'-') {
                tokens.push_back(a + L"=" + nxt);
                ++i;
                continue;
            }
        }
        tokens.push_back(a);
    }
    for (const std::wstring& a : tokens) {
        std::wstring v;
        if (!(v = ValueOf(a, L"--root")).empty()) rootArg = v;
        else if (!(v = ValueOf(a, L"--skin")).empty()) skinArg = v;
        else if (!(v = ValueOf(a, L"--scale")).empty()) scalePct = _wtof(v.c_str());
        else if (!(v = ValueOf(a, L"--tick")).empty()) tickMs = (int)_wtoi(v.c_str());
        else if (IsFlag(a, L"--wander")) wander = true;
        else if (IsFlag(a, L"--armed")) g_armed = true;
        else if (IsFlag(a, L"--smooth")) smooth = true;
        else if (IsFlag(a, L"--stats")) g_stats = true;
        else if (IsFlag(a, L"--nopresent")) noPresent = true;
        else if (IsFlag(a, L"--move")) movePath = true;
        else if (IsFlag(a, L"--notop")) notop = true;
        else if (IsFlag(a, L"--dumpcrop")) dumpCrop = true;
        else if (IsFlag(a, L"--surfaceprobe")) surfaceProbe = true;
        else if (IsFlag(a, L"--dumpsurface")) dumpSurface = true;
        else if (IsFlag(a, L"--dumpscreen")) dumpScreen = true;
        else if (IsFlag(a, L"--dumpcell")) dumpCell = true;
        else if (IsFlag(a, L"--debugframe")) debugFrame = true;
        else if (IsFlag(a, L"--debugfull")) debugFull = true;
        else if (IsFlag(a, L"--debugmarks")) debugMarks = true;
        else if (!(v = ValueOf(a, L"--dir")).empty()) forceDir = (int)_wtoi(v.c_str());
        else if (!(v = ValueOf(a, L"--frame")).empty()) forceFrame = (int)_wtoi(v.c_str());
        else if (IsFlag(a, L"--interactive")) interactive = true;
        else if (IsFlag(a, L"--help") || IsFlag(a, L"-h")) help = true;
    }
    if (help) {
        static const wchar_t* usage =
            L"FalcomDeskling —— 低资源桌面小人\n\n"
            L"  --root=<路径>   Chr 资源目录（默认在 exe 旁边找）\n"
            L"  --skin=<名字>   指定角色目录，逗号分开可一次起好几个人（默认随机一个）\n"
            L"  --scale=<百分比> 显示倍率，默认 200（=WPF 的 ScaleFactor 2.0）\n"
            L"  --tick=<毫秒>   主循环周期，默认 15；想再省 CPU 就 33\n"
            L"  --wander        走路时也每 1.5 秒自己换向\n"
            L"  --armed         起场就装备武器（缺那两套图的人自己退回走/跑）\n"
            L"  --smooth        放大用 HALFTONE 滤波，默认最近邻\n"
            L"  --interactive   窗口参与命中测试，可以拿鼠标戳\n"
            L"  --move          改回\"只搬窗口\"那条路（实测比每拍重传贵一倍）\n"
            L"  --stats         每秒往 exe 旁写一行 falcomdeskling_stats.txt\n"
            L"  --nopresent     只跑物理不画屏，用来量渲染开销\n"
            L"  --notop         不置顶\n"
            L"  --dir=<0-7>     钉死朝向，调试截图比对用\n"
            L"  --frame=<0-7>   钉死第几帧，同上\n"
            L"  --dumpcrop      把每条贴图的裁剪框和逐帧包围盒写进 falcomdeskling_dump.txt 后退出\n"
            L"  --surfaceprobe  每拍扫一遍刚画好的表面，把最上/最下有像素的行号记进 stats\n"
            L"  --dumpsurface   把第一拍上传前的表面落成 surface_dump.png，直接看脚在不在里面\n"
            L"  --dumpscreen    晚一拍把窗口矩形覆盖下的屏幕落成 screen_dump.png，和上面配对看\n"
            L"  --dumpcell      把这一拍真正用的那一格原样落成 cell_dump_NNNN.png\n"
            L"  --debugframe    沿窗口最外圈画一圈品红，判断丢的东西是不是在窗口之外\n"
            L"  --debugmarks    画脚底线(绿)/中心线(青)/整格框(红)三条参考线\n"
            L"  --debugfull     完全不裁剪：源矩形取整个方格，窗口放大到整格\n";
        DWORD n = 0;
        HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE);
        if (so && so != INVALID_HANDLE_VALUE) {
            WriteFile(so, usage, (DWORD)(wcslen(usage) * sizeof(wchar_t)), &n, nullptr);
        }
        return 0;
    }
    if (argc > 0 && argv) LocalFree(argv);
    if (!(scalePct > 0.0)) scalePct = DEFAULT_SCALE * 100.0;
    if (tickMs < 5) tickMs = 5;

    const std::wstring root = FindChrRoot(rootArg);
    if (root.empty()) {
        Fail(L"找不到 Chr 资源目录，用 --root=<路径> 指一下");
        return 1;
    }
    // 名单一次扫干净：托盘里"添加"要用的就是这份表
    g_skins = ScanSkins(root);
    if (g_skins.empty()) {
        Fail((L"这个目录里没有带 2.Walk 的人：\n" + (skinArg.empty() ? root : skinArg)).c_str());
        return 1;
    }
    // --skin 可以写成逗号串，也能直接写 all，一次起好好几个人。回归多角色
    // 时靠这个，不然每次都得拿鼠标去戳托盘
    std::vector<SkinInfo> starters;
    {
        std::wstring rest = skinArg;
        while (!rest.empty()) {
            std::wstring one = rest;
            const size_t c = rest.find_first_of(L",;");
            if (c != std::wstring::npos) {
                one = rest.substr(0, c);
                rest = rest.substr(c + 1);
            } else {
                rest.clear();
            }
            // 前后空格都可能有：从命令行里抄过来的串不会讲礼貌
            size_t b = one.find_first_not_of(L" \t");
            size_t e = one.find_last_not_of(L" \t");
            if (b == std::wstring::npos) continue;
            one = one.substr(b, e - b + 1);
            if (one.empty()) continue;
            if (one == L"all" || one == L"*") {
                // all 排在最前面，后面再点名字也能把它挤掉
                starters.clear();
                for (const SkinInfo& s : g_skins) starters.push_back(s);
                continue;
            }
            for (const SkinInfo& s : g_skins) {
                if (s.name == one) { starters.push_back(s); break; }
            }
        }
    }
    if (starters.empty()) {
        const std::wstring skin = PickSkin(root, skinArg);
        if (skin.empty()) {
            Fail((L"这个目录里没有带 2.Walk 的人：\n" + (skinArg.empty() ? root : skinArg)).c_str());
            return 1;
        }
        namespace fs = std::filesystem;
        const std::wstring leaf = fs::path(skin).filename().wstring();
        for (const SkinInfo& s : g_skins) {
            if (s.name == leaf) { starters.push_back(s); break; }
        }
        if (starters.empty()) starters.push_back(SkinInfo{ leaf, skin, true });
    }
    if (dumpCrop) {
        DumpCrops(starters.front().dir);
        return 0;
    }

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const UINT dpi = GetDpiForSystem();
    const double dpiScale = (double)dpi / 96.0;
    const double k = (scalePct / 100.0) * dpiScale;

    // 托盘和"退出"都得挂在一个拆不掉的窗口上：角色窗口随时能加能删，靠不住
    if (!CreateMsgWindow()) {
        Fail(L"消息窗口创建失败");
        return 1;
    }

    // 建实体时照抄这份 Options，省得每个角色重解析一遍命令行
    Runner::Options& opt = g_opt;
    opt.k = k;
    opt.dpi = dpiScale;
    opt.wander = wander;
    opt.smooth = smooth;
    opt.noPresent = noPresent;
    opt.uploadEveryTick = !movePath;
    // 探针会每拍扫一遍整张表面，量 CPU 的时候开它就是自己污染自己的数 ——
    // 所以 --stats 不再自动带上它，必须单独 --surfaceprobe
    opt.surfaceProbe = surfaceProbe;
    opt.dumpSurface = dumpSurface;
    if (dumpSurface) {
        namespace fs = std::filesystem;
        wchar_t exepath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exepath, MAX_PATH);
        g_dumpSurfacePath = (fs::path(exepath).parent_path() / L"surface_dump.png").wstring();
        opt.dumpSurfacePath = g_dumpSurfacePath;
    }
    opt.dumpScreen = dumpScreen;
    if (dumpScreen) {
        namespace fs = std::filesystem;
        wchar_t exepath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exepath, MAX_PATH);
        g_dumpScreenPath = (fs::path(exepath).parent_path() / L"screen_dump.png").wstring();
        opt.dumpScreenPath = g_dumpScreenPath;
    }
    opt.dumpCell = dumpCell;
    if (dumpCell) {
        namespace fs = std::filesystem;
        wchar_t exepath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exepath, MAX_PATH);
        opt.dumpCellPath = (fs::path(exepath).parent_path() / L"cell_dump.png").wstring();
    }
    opt.debugFrame = debugFrame;
    opt.debugFull = debugFull;
    opt.debugMarks = debugMarks;
    opt.ox = (double)wa.left;
    opt.oy = (double)wa.top;
    opt.worldW = (double)(wa.right - wa.left);
    opt.worldH = (double)(wa.bottom - wa.top);
    // 钉方向/帧、抓屏抓表面这些只该管第一个角色，别把所有人都冻住
    g_forceDir = forceDir;
    g_forceFrame = forceFrame;
    g_interactive = interactive;
    g_notop = notop;
    g_dumpCell = dumpCell;
    g_dumpSurface = dumpSurface;
    g_dumpScreen = dumpScreen;
    g_surfaceProbe = surfaceProbe;

    // 起场这些人：一个都建不起来才算失败，后面几个建不起来只是少几个人
    if (!AddEntity(starters.front())) {
        Fail(L"第一个角色没能上场：窗口或贴图没起来");
        return 1;
    }
    for (size_t i = 1; i < starters.size(); ++i) {
        AddEntity(starters[i]);
    }

    const std::wstring icon = FindIcon();
    if (!g_tray.Create(g_msg, icon.c_str(), L"FalcomDeskling 桌面小人")) {
        Fail(L"托盘图标创建失败");
        return 1;
    }
    g_tray.namesAddable = [] { return NamesAddable(); };
    g_tray.namesOnscreen = [] { return NamesOnscreen(); };
    g_tray.isArmed = [] { return g_armed; };
    g_tray.onArmed = [](bool f) { SetAllArmed(f); };
    g_tray.onToggle = [] { g_paused = !g_paused; };
    // 光设 g_quit 不算：主循环正压在 MsgWaitForMultipleObjects 上，得往队列里
    // 丢点什么才醒得过来
    g_tray.onQuit = [] {
        g_quit = true;
        if (g_msg) PostMessageW(g_msg, WM_NULL, 0, 0);
    };
    // 加/删是在 TrackPopupMenu 关完、编号交回来之后才叫到这里的，菜单栈早空了，
    // 这时候开新窗口不会跟谁抢消息队列
    g_tray.onAdd = [](const std::wstring& name) {
        for (const SkinInfo& s : g_skins) {
            if (s.name != name) continue;
            if (!AddEntity(s)) {
                wchar_t m[256];
                swprintf(m, 256, L"加不了 %s：场上最多 %d 个人，或者窗口/贴图没起来",
                         name.c_str(), g_maxEntities);
                // 点了没反应又什么都不说，只会让人以为菜单坏了
                MessageBoxW(nullptr, m, L"FalcomDeskling", MB_ICONWARNING);
            }
            return;
        }
    };
    g_tray.onRemove = [](const std::wstring& name) { RemoveEntity(name); };

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (!timer) {
        Fail(L"定时器创建失败");
        return 1;
    }
    LARGE_INTEGER due{};
    due.QuadPart = -10000;  // 立刻先来一拍
    SetWaitableTimer(timer, &due, (LONG)tickMs, nullptr, nullptr, FALSE);

    LARGE_INTEGER freq{}, last{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    const LARGE_INTEGER boot = last;

    for (;;) {
        const DWORD r = MsgWaitForMultipleObjects(1, &timer, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) {
            // 定时器到了：先把消息消化干净，别让 WM_PAINT 白叫醒下一轮
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { g_quit = true; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (g_quit) break;

            QueryPerformanceCounter(&now);
            double dt = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
            last = now;
            if (dt <= 0.0) continue;
            if (dt > DT_CLAMP) dt = DT_CLAMP;

            if (!g_paused) {
                POINT cp{};
                Vec2 m{ std::nan(""), std::nan("") };
                if (GetCursorPos(&cp)) m = Vec2{ (double)cp.x, (double)cp.y };
                // t0 得真的去取一次计数器：拿 now 当起点等于拿终点减起点，
                // 差永远是 0，stats 里那行 physics 就一直是 0.00ms
                LARGE_INTEGER t0{}, t1{};
                QueryPerformanceCounter(&t0);
                // 先全员 Tick 再全员 Present：物理那一笔才量得准，也不会出现
                // "前一个画完了后一个还在算"的错位
                for (auto& e : g_entities) e->runner->Tick(dt, m);
                QueryPerformanceCounter(&t1);
                for (auto& e : g_entities) e->runner->Present();
                g_usPhysics += (uint64_t)((double)(t1.QuadPart - t0.QuadPart) * 1e6 /
                                          (double)freq.QuadPart);
            }

            if (g_stats) {
                const double elapsed = (double)(now.QuadPart - boot.QuadPart) / (double)freq.QuadPart;
                if (elapsed - g_statsClock >= 1.0) {
                    g_statsClock = elapsed;
                    WriteStatsLine(elapsed);
                }
            }
        } else {
            // 有消息插进来了；WAIT_FAILED 也走这一支，下面 PeekMessage 会把它取出来
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { g_quit = true; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (g_quit) break;
        }
    }

    CancelWaitableTimer(timer);
    CloseHandle(timer);
    // 先拆实体再拆托盘：顺序反了倒也不要紧，但窗口的 WM_DESTROY 现在不许
    // 再往队列里丢 WM_QUIT 了，退出全靠 g_quit
    g_entities.clear();
    g_tray.Destroy();
    if (g_msg) { DestroyWindow(g_msg); g_msg = nullptr; }
    CoUninitialize();
    return 0;
}

