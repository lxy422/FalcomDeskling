#include "../src/display.h"
#include "../src/runner.h"
#include "../src/emotio_data.h"
#include "../src/store.h"
#include <psapi.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
void Check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool Near(double a, double b) { return std::abs(a - b) < 0.01; }
SIZE_T PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX m{};
    Check(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m)) != FALSE,
          "read private memory");
    return m.PrivateUsage;
}
}

#include "combat_cases.h"

int wmain(int argc, wchar_t** argv) {
    using namespace fd;
    Check(argc == 2, "supply test skin directory");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "COM init");

    {
        namespace fs = std::filesystem;
        const auto fixture = fs::path(argv[1]).parent_path().parent_path() / L"build" /
            (L"scan-fixture-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        for (const auto* name : {L"A", L"B"}) {
            const auto walk = fixture / name / L"2.Walk";
            fs::create_directories(walk);
            std::ofstream(walk / L"dir0.png", std::ios::binary).put('\0');
        }
        SetRootOverrides(fixture.wstring(), L"");
        std::wstring diagnostic;
        Check(ScanChr(&diagnostic).size() == 2, "missing optional weapon art must not stop character enumeration");
        Check(diagnostic.find(L"2") != std::wstring::npos, "report enumerated character count");
        SetRootOverrides((fixture / L"A").wstring(), L"");
        Check(ScanChr(&diagnostic).empty() && diagnostic.find(L"--root") != std::wstring::npos,
              "single-character root reports required parent directory");
        SetRootOverrides((fixture / L"missing").wstring(), L"");
        Check(ScanChr().empty(), "missing root does not yield phantom characters");
        SetRootOverrides(L"", L"");
        fs::remove_all(fixture);
    }

    {
        auto& sheet = EmotionSheet::Shared();
        Check(sheet.Load(), "load emote atlas");
        LayeredWindow surface;
        Check(surface.Create(128, 256, true, false), "create emote test surface");
        surface.Show(false);
        int checked = 0;
        for (int id = 1; id <= EMOTE_LAST_ID; ++id) {
            Check(EmoteIdOfTitle(EmoteTitle(id)) == id, "emote title/id round trip");
            for (int i = 0; i < sheet.FrameCount(id); ++i) {
                const auto& fr = sheet.Frames(id)[i];
                for (int scale : {1, 2, 3, 4}) {
                    Check(sheet.DrawFrame(surface.MemDC(), id, i, 0, 0, fr.w * scale, fr.h * scale), "draw emote frame");
                    GdiFlush();
                    const auto* pixels = static_cast<const uint32_t*>(surface.Bits());
                    for (int y = 0; y < fr.h * scale; ++y) {
                        for (int x = 0; x < fr.w * scale; ++x) {
                            const auto expected = sheet.Pixels()[(fr.y + y / scale) * (sheet.Stride() / 4) + fr.x + x / scale];
                            const auto actual = pixels[y * (surface.Stride() / 4) + x];
                            if (expected != actual) {
                                std::fprintf(stderr, "Emote crop mismatch: id=%d frame=%d scale=%d pixel=%d,%d\n", id, i, scale, x, y);
                                Check(false, "emote rendering matches atlas pixels");
                            }
                        }
                    }
                    ++checked;
                }
            }
        }
        std::printf("Emote frame/scale comparisons passed: %d\n", checked);
    }

    DisplayArea area;
    area.monitor = RECT{-1920, 0, 0, 1080};
    area.work = RECT{-1920, 0, 0, 1040};
    auto line = TaskbarLedge(area, 40, 60);
    Check(Near(line.y, 1040) && Near(line.left, -1856) && Near(line.right, -84), "negative-coordinate screen ledge");
    area.work = RECT{-1840, 0, 0, 1080};
    line = TaskbarLedge(area, 40, 60);
    Check(Near(line.left, -1776) && Near(line.y, 1078), "side taskbar keeps stroll in work area");
    area.dpi = 192;
    area.work.top = 80;
    line = TaskbarLedge(area, 40, 60);
    Check(Near(line.left, -1752) && Near(line.y, 1076), "scaled margins with top taskbar");
    line = TaskbarLedge(area, 2000, 2000);
    Check(Near(line.left, line.right), "oversized skin produces stationary ledge");
    const auto screens = Displays();
    Check(!screens.empty() && screens.front().primary && screens.front().dpi > 0, "enumerate real monitors, primary first");

    {
        LayeredWindow win;
        Check(win.Create(256, 256, true, false), "create runner window");
        win.Show(false);
        Runner runner;
        Runner::Options opt;
        opt.skinDir = argv[1]; opt.k = 1; opt.dpi = 1;
        opt.worldW = 1920; opt.worldH = 1080;
        Check(runner.Create(&win, opt), "create test runner");
        const double body = runner.BodyLen();
        runner.SetPos(Vec2{1e6, 1e6});
        RECT work{-1920, -1080, 0, 0};
        runner.SetDisplay(work, 2);
        runner.Present();
        RECT wr{};
        Check(GetWindowRect(win.Handle(), &wr) != FALSE, "read resized window");
        Check(Near(runner.BodyLen(), body * 2), "DPI scales existing runner");
        Check(wr.left >= work.left && wr.right <= work.right && wr.top >= work.top && wr.bottom <= work.bottom,
              "display change clamps complete surface into negative-coordinate work area");
        const int wide = win.W();
        runner.SetDisplay(work, 1);
        runner.Present();
        Check(std::abs(win.W() * 2 - wide) <= 1 && Near(runner.BodyLen(), body), "DPI round trip preserves user scale");

        runner.SetPos(Vec2{200, 500});
        runner.SetTaskbar(true, 800, 100, 1000);
        runner.Tick(0.2, Vec2{200, 800});
        runner.Present();
        Check(runner.GetStats().frameIndex == 1, "taskbar animation advances clock exactly once at 6fps");
        Check(Near(runner.Pos().x, 205.5) && Near(runner.Pos().y, 800), "stroll ignores nearby mouse and uses slow speed");
        runner.SetTaskbar(true, 790, 100, 1000);
        runner.Tick(0.2, Vec2{});
        runner.Present();
        Check(runner.GetStats().frameIndex == 2 && Near(runner.Pos().y, 790), "geometry refresh preserves animation phase");

        runner.SetPos(Vec2{999, 790});
        runner.Tick(0.1, Vec2{});
        Check(runner.Dir() == 0 && Near(runner.Pos().x, 1000), "right endpoint turns left");
        runner.SetTaskbar(true, 780, 100, 1000);
        runner.Tick(0.1, Vec2{});
        Check(runner.Dir() == 0 && Near(runner.Pos().x, 1000), "ledge update preserves turn pause and direction");
        for (int i = 0; i < 100 && runner.Pos().x >= 999; ++i) runner.Tick(0.1, Vec2{});
        Check(runner.Pos().x < 999, "walking resumes after endpoint pause");
        runner.SetPos(Vec2{100, 780});
        runner.Tick(0.1, Vec2{});
        Check(runner.Dir() == 4 && Near(runner.Pos().x, 100), "left endpoint turns right");

        runner.SetTaskbar(true, 790, 500, 500);
        runner.Tick(0.1, Vec2{});
        runner.Present();
        const auto uploads = runner.GetStats().uploads;
        for (int i = 0; i < 20; ++i) { runner.Tick(0.02, Vec2{}); runner.Present(); }
        Check(Near(runner.Pos().x, 500), "collapsed ledge never drifts");
        Check(runner.GetStats().uploads == uploads, "stationary unchanged sprite does not re-upload");
        runner.SetTaskbar(false, 0, 0, 0);
        runner.Present();
        Check(GetWindowRect(win.Handle(), &wr) != FALSE, "read post-stroll rect");
        Check(wr.right <= work.right && wr.bottom <= work.bottom, "leaving taskbar mode clamps to desktop");
    }
    // The old FALSE ownership leaked a fresh PNG-sized HGLOBAL on each call.
    for (int i = 0; i < 30; ++i) { Strip s; Check(DecodePngMemory(kEmotioPng, sizeof(kEmotioPng), s), "warm memory decoder"); }
    const auto before = PrivateBytes();
    for (int i = 0; i < 1000; ++i) { Strip s; Check(DecodePngMemory(kEmotioPng, sizeof(kEmotioPng), s), "repeat memory decoder"); }
    const auto after = PrivateBytes();
    const long long growth = static_cast<long long>(after) - static_cast<long long>(before);
    std::printf("1000 decodes private-memory growth: %lld bytes\n", growth);
    Check(growth < 8 * 1024 * 1024, "memory decoding stays bounded");
    std::puts("All display, DPI, taskbar and decoder regressions passed.");
    CombatRegression(argv[1]);
    CoUninitialize();
    return 0;
}
