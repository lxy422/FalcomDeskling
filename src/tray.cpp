#include "tray.h"

#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace fd {

bool Tray::Create(HWND hwnd, const wchar_t* iconPath, const wchar_t* tip) {
    if (_active) return true;
    _hwnd = hwnd;

    ZeroMemory(&_nid, sizeof(_nid));
    _nid.cbSize = sizeof(_nid);
    _nid.hWnd = hwnd;
    _nid.uID = 1;
    _nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    _nid.uCallbackMessage = WM_TRAY;
    wcsncpy_s(_nid.szTip, tip ? tip : L"FalcomDeskling", _TRUNCATE);

    const int side = GetSystemMetrics(SM_CXSMICON);
    HICON ico = nullptr;
    if (iconPath && *iconPath) {
        ico = static_cast<HICON>(LoadImageW(nullptr, iconPath, IMAGE_ICON, side, side,
                                            LR_LOADFROMFILE));
    }
    if (!ico) ico = LoadIcon(nullptr, IDI_APPLICATION);
    _nid.hIcon = ico;

    if (Shell_NotifyIconW(NIM_ADD, &_nid) == FALSE) {
        if (ico) DestroyIcon(ico);
        return false;
    }
    _active = true;
    return true;
}

void Tray::Destroy() {
    if (!_active) return;
    _nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_DELETE, &_nid);
    if (_nid.hIcon) {
        DestroyIcon(_nid.hIcon);
        _nid.hIcon = nullptr;
    }
    _active = false;
}

void Tray::Popup() {
    if (!_hwnd) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, L"暂停 / 继续");

    // 装备武器是个全局开关，做成打勾项比开一层子菜单省事
    {
        UINT chk = MF_STRING;
        if (isArmed && isArmed()) chk |= MF_CHECKED;
        AppendMenuW(menu, chk, IDM_ARMED, L"装备武器模式");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // ---- 添加 ----
    // 名单会随中途加人和往目录里丢夹而变，所以每次开菜单现编
    HMENU add = CreatePopupMenu();
    {
        std::vector<std::wstring> names;
        if (namesAddable) names = namesAddable();
        if (names.empty()) {
            AppendMenuW(add, MF_STRING | MF_GRAYED, 0, L"没有可添加的人");
        } else {
            int i = 0;
            for (const std::wstring& n : names) {
                if (i >= IDM_ID_RANGE) break;   // 命令号就这一段，别越界
                AppendMenuW(add, MF_STRING, IDM_ADD_BASE + i, n.c_str());
                ++i;
            }
        }
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)add, L"添加");

    // ---- 移除 ----
    HMENU del = CreatePopupMenu();
    {
        std::vector<std::wstring> names;
        if (namesOnscreen) names = namesOnscreen();
        if (names.empty()) {
            AppendMenuW(del, MF_STRING | MF_GRAYED, 0, L"没有站在桌上的");
        } else {
            int i = 0;
            for (const std::wstring& n : names) {
                if (i >= IDM_ID_RANGE) break;
                AppendMenuW(del, MF_STRING, IDM_DEL_BASE + i, n.c_str());
                ++i;
            }
        }
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)del, L"移除");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"退出");

    POINT p{};
    GetCursorPos(&p);
    // 不先抢一次前台，菜单会点了没反应、也不会自己消失
    SetForegroundWindow(_hwnd);
    // TPM_RETURNCMD：关完菜单才把编号交回来，回调于是落在菜单栈已经空掉的那一秒。
    // 走 WM_COMMAND 的话，加人这条分支会在 TrackPopupMenu 还压着栈的时候
    // 去 CreateWindowExW，新窗口会来抢这条消息队列
    const int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, p.x, p.y, 0,
                                   _hwnd, nullptr);
    DestroyMenu(add);
    DestroyMenu(del);
    DestroyMenu(menu);
    if (cmd == 0) return;

    switch (cmd) {
    case IDM_TOGGLE:
        if (onToggle) onToggle();
        break;
    case IDM_ARMED:
        if (onArmed) onArmed(!(isArmed && isArmed()));
        break;
    case IDM_QUIT:
        if (onQuit) onQuit();
        break;
    default:
        if (cmd >= IDM_ADD_BASE && cmd < IDM_ADD_BASE + IDM_ID_RANGE) {
            if (namesAddable && onAdd) {
                const std::vector<std::wstring> names = namesAddable();
                const int i = cmd - IDM_ADD_BASE;
                if (i < (int)names.size()) onAdd(names[i]);
            }
        } else if (cmd >= IDM_DEL_BASE && cmd < IDM_DEL_BASE + IDM_ID_RANGE) {
            if (namesOnscreen && onRemove) {
                const std::vector<std::wstring> names = namesOnscreen();
                const int i = cmd - IDM_DEL_BASE;
                if (i < (int)names.size()) onRemove(names[i]);
            }
        }
        break;
    }
}

}  // namespace fd
