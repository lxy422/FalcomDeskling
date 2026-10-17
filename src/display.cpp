#include "display.h"
#include "config.h"
#include <algorithm>

namespace fd {
bool ReadDisplay(HMONITOR monitor, DisplayArea& out) {
    MONITORINFO mi{sizeof(mi)};
    if (!monitor || !GetMonitorInfoW(monitor, &mi)) return false;
    out.handle = monitor;
    out.monitor = mi.rcMonitor;
    out.work = mi.rcWork;
    out.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    // Load dynamically so the fallback still works on systems without shcore.
    static HMODULE shcore = LoadLibraryW(L"shcore.dll");
    using GetDpi = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static auto getDpi = shcore ? reinterpret_cast<GetDpi>(GetProcAddress(shcore, "GetDpiForMonitor")) : nullptr;
    UINT x = 96, y = 96;
    if (!getDpi || FAILED(getDpi(monitor, 0, &x, &y)) || !x) x = 96;
    out.dpi = x;
    return true;
}

std::vector<DisplayArea> Displays() {
    std::vector<DisplayArea> out;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM data) -> BOOL {
        DisplayArea d;
        if (ReadDisplay(hm, d)) reinterpret_cast<std::vector<DisplayArea>*>(data)->push_back(d);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(), [](const DisplayArea& a, const DisplayArea& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.monitor.left != b.monitor.left) return a.monitor.left < b.monitor.left;
        return a.monitor.top < b.monitor.top;
    });
    return out;
}

StrollLine TaskbarLedge(const DisplayArea& area, double leftExtent, double rightExtent) {
    const double pad = TB_EDGE_PAD * area.dpi / 96.0;
    double lo = area.work.left + (std::max)(0.0, leftExtent) + pad;
    double hi = area.work.right - (std::max)(0.0, rightExtent) - pad;
    if (hi < lo) lo = hi = (area.work.left + area.work.right) * 0.5;
    const double y = area.work.bottom < area.monitor.bottom
        ? area.work.bottom : area.work.bottom - 2.0 * area.dpi / 96.0;
    return {y, lo, hi};
}
} // namespace fd
