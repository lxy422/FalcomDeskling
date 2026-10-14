#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace fd {

// 托盘只留两件事：暂停/继续、退出。Phase 1 不做"加人/换皮肤"
class Tray {
public:
    static constexpr UINT WM_TRAY = WM_APP + 1;
    enum { IDM_TOGGLE = 1, IDM_QUIT = 2 };

    ~Tray() { Destroy(); }

    bool Create(HWND hwnd, const wchar_t* iconPath, const wchar_t* tip);
    void Destroy();
    void Popup();

private:
    HWND _hwnd = nullptr;
    NOTIFYICONDATAW _nid{};
    bool _active = false;
};

}  // namespace fd
