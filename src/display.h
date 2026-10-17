#pragma once
#include <windows.h>
#include <vector>

namespace fd {
struct DisplayArea {
    HMONITOR handle = nullptr;
    RECT monitor{};
    RECT work{};
    UINT dpi = 96;
    bool primary = false;
};

// Primary first, then left/top order. Coordinates are physical pixels.
std::vector<DisplayArea> Displays();
bool ReadDisplay(HMONITOR monitor, DisplayArea& out);

struct StrollLine { double y, left, right; };
// Horizontal ledge at the bottom of the work area, including side taskbars.
StrollLine TaskbarLedge(const DisplayArea& area, double leftExtent, double rightExtent);
} // namespace fd
