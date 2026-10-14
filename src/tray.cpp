#include "tray.h"

#include <cstring>

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
        if (ico && ico != LoadIcon(nullptr, IDI_APPLICATION)) DestroyIcon(ico);
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
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_QUIT, L"退出");

    POINT p{};
    GetCursorPos(&p);
    // 不先抢一次前台，菜单会点了没反应、也不会自己消失
    SetForegroundWindow(_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_NONOTIFY, p.x, p.y, 0, _hwnd, nullptr);
    PostMessage(_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

}  // namespace fd
