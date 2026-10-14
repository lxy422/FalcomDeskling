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
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")

namespace fd {
namespace {

LayeredWindow g_win;
Runner g_runner;
Tray g_tray;
WNDPROC g_origProc = nullptr;
bool g_quit = false;
bool g_paused = false;
bool g_stats = false;
double g_statsClock = 0.0;
uint64_t g_usPhysics = 0;
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
    const Runner::Stats& s = g_runner.GetStats();

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

    // 窗口在屏幕上的**真实**矩形，加上 work area 边界。这是绕开一切坐标推导
    // 的硬证据：脚有没有被屏幕/任务栏切掉，看这两个数就知道
    RECT wr{};
    const bool haveRect = GetWindowRect(g_win.Handle(), &wr) != FALSE;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    wchar_t line[640];
    swprintf(line, 640,
             L"t=%.1f ticks=%llu up=%llu mv=%llu bounce=%llu cpu=%.2fs ws=%zuKB "
             L"physics=%.2fms move=%.2fms upload=%.2fms state=%d dir=%d pos=%.0f,%.0f\n"
             L"   blit src=%d,%d %dx%d -> dst=%dx%d  crop=%d,%d %dx%d frame=%d cell=%d k=%.3f\n"
             L"   surf win=%dx%d first=%d last=%d rowsBelowLast=%d\n"
             L"   rect=(%ld,%ld)-(%ld,%ld) %ldx%ld  work=(%ld,%ld)-(%ld,%ld) overBottom=%ld\n",
             elapsed,
             (unsigned long long)s.ticks, (unsigned long long)s.uploads,
             (unsigned long long)s.moves, (unsigned long long)s.bounces, cpuSec, ws,
             (double)g_usPhysics / 1000.0, (double)s.usMove / 1000.0,
             (double)s.usUpload / 1000.0,
             (int)g_runner.GetState(), g_runner.Dir(), g_runner.Pos().x, g_runner.Pos().y,
             s.blitSrcX, s.blitSrcY, s.blitSrcW, s.blitSrcH,
             s.blitDstW, s.blitDstH,
             s.cropX, s.cropY, s.cropW, s.cropH, s.frameIndex, s.cellW, s.k,
             s.winW, s.winH, s.surfFirst, s.surfLast,
             s.winH - 1 - s.surfLast,
             (long)wr.left, (long)wr.top, (long)wr.right, (long)wr.bottom,
             (long)(wr.right - wr.left), (long)(wr.bottom - wr.top),
             (long)work.left, (long)work.top, (long)work.right, (long)work.bottom,
             (long)(haveRect ? wr.bottom - work.bottom : 0));

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring path = (fs::path(exe).parent_path() / L"falcomdeskling_stats.txt").wstring();
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const std::wstring out = line;
    DWORD n = 0;
    WriteFile(h, out.c_str(), (DWORD)(out.size() * sizeof(wchar_t)), &n, nullptr);
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
    const wchar_t* folders[] = { L"1.Run", L"2.Walk" };
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

LRESULT CALLBACK MainWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case Tray::WM_TRAY:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            g_tray.Popup();
        } else if (LOWORD(lp) == WM_LBUTTONUP) {
            g_paused = !g_paused;
        }
        return 0;
    case WM_COMMAND:
        if (wp == Tray::IDM_QUIT) {
            g_quit = true;
            PostQuitMessage(0);
            return 0;
        }
        if (wp == Tray::IDM_TOGGLE) {
            g_paused = !g_paused;
            return 0;
        }
        break;
    case WM_DESTROY:
        g_quit = true;
        break;
    default:
        break;
    }
    return CallWindowProcW(g_origProc, h, msg, wp, lp);
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
            L"  --skin=<名字>   指定角色目录（默认取第一个有 2.Walk 的）\n"
            L"  --scale=<百分比> 显示倍率，默认 200（=WPF 的 ScaleFactor 2.0）\n"
            L"  --tick=<毫秒>   主循环周期，默认 15；想再省 CPU 就 33\n"
            L"  --wander        走路时也每 1.5 秒自己换向\n"
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
    const std::wstring skin = PickSkin(root, skinArg);
    if (skin.empty()) {
        Fail((L"这个目录里没有带 2.Walk 的人：\n" + (skinArg.empty() ? root : skinArg)).c_str());
        return 1;
    }
    if (dumpCrop) {
        DumpCrops(skin);
        return 0;
    }

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    const UINT dpi = GetDpiForSystem();
    const double dpiScale = (double)dpi / 96.0;
    const double k = (scalePct / 100.0) * dpiScale;
    const int cell = (int)std::lround((double)CELL_PX * k);

    if (!g_win.Create(cell, cell, !interactive, !notop)) {
        wchar_t msg[160];
        swprintf(msg, 160, L"分层窗口创建失败 step=%hs gle=%lu cell=%d",
                 g_win.FailStep(), (unsigned long)g_win.LastError(), cell);
        Fail(msg);
        return 1;
    }

    Runner::Options opt;
    opt.skinDir = skin;
    opt.k = k;
    opt.dpi = dpiScale;
    opt.wander = wander;
    opt.smooth = smooth;
    opt.noPresent = noPresent;
    opt.uploadEveryTick = !movePath;
    opt.forceDir = forceDir;
    opt.forceFrame = forceFrame;
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
    if (!g_runner.Create(&g_win, opt)) {
        Fail((L"贴图加载失败：\n" + skin).c_str());
        return 1;
    }

    g_origProc = (WNDPROC)SetWindowLongPtrW(g_win.Handle(), GWLP_WNDPROC,
                                            (LONG_PTR)MainWndProc);
    const std::wstring icon = FindIcon();
    g_tray.Create(g_win.Handle(), icon.c_str(), L"FalcomDeskling 桌面小人");

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

    g_runner.Present();

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
                const uint64_t t0 = (uint64_t)now.QuadPart;
                g_runner.Tick(dt, m);
                g_usPhysics += (uint64_t)((double)(now.QuadPart - t0) * 1e6 / (double)freq.QuadPart);
                g_runner.Present();
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
    g_tray.Destroy();
    g_win.Destroy();
    CoUninitialize();
    return 0;
}

