#include "tray.h"
#include "../resource/resource.h"
#include "emote.h"   // EmoteTitle：表情子菜单那些名字只有这里一份

#include <cstring>
#include <cwchar>
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
    HICON ico = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_FALCOM), IMAGE_ICON, side, side, 0));
    if (!ico && iconPath && *iconPath) {
        ico = static_cast<HICON>(LoadImageW(nullptr, iconPath, IMAGE_ICON, side, side,
                                            LR_LOADFROMFILE));
    }
    if (!ico) ico = CopyIcon(LoadIcon(nullptr, IDI_APPLICATION));
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

    // 任务栏漫步：一个人、一根线、慢速来回走。它和"满桌乱逛"是两种活法，
    // 所以做成打勾项而不是又开一层子菜单
    {
        UINT chk = MF_STRING;
        if (isTaskbar && isTaskbar()) chk |= MF_CHECKED;
        AppendMenuW(menu, chk, IDM_TASKBAR, L"任务栏漫步");
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // ---- 表情 ----
    // 18 枚直接列出来，点谁放谁。逐张验的时候这比改代码重编有用得多
    HMENU emo = CreatePopupMenu();
    {
        for (int i = 1; i <= 18; ++i) {
            wchar_t label[64];
            swprintf(label, 64, L"%2d  %s", i, EmoteTitle(i));
            AppendMenuW(emo, MF_STRING, IDM_EMOTE_BASE + (i - 1), label);
        }
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)emo, L"表情");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // ---- 添加 ----
    // 名单会随中途加人和往目录里丢夹而变，所以每次开菜单现编。
    //
    // 顺序按号数排，不在这里现编：菜单里那个人是几号，和遭遇表里 id= 说的是
    // 不是同一个，全看这份顺序 —— 两处排法早晚会对不上
    HMENU add = CreatePopupMenu();
    {
        std::vector<std::wstring> names;
        if (namesAddable) names = namesAddable();
        const bool blocked = canAdd && !canAdd();
        if (names.empty()) {
            AppendMenuW(add, MF_STRING | MF_GRAYED, 0, L"没有可添加的人");
        } else {
            int i = 0;
            for (const std::wstring& n : names) {
                if (i >= IDM_ID_RANGE) break;   // 命令号就这一段，别越界
                // 漫步模式下桌上只能有一个人，所以整段都灰着 —— 让人点了
                // 才发现加不上，比点了报错好
                AppendMenuW(add, MF_STRING | (blocked ? MF_GRAYED : 0), IDM_ADD_BASE + i,
                            n.c_str());
                ++i;
            }
            // "添加全部"放在最后，用一条线把自己和逐个人的那一段分开
            AppendMenuW(add, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(add, MF_STRING | (blocked ? MF_GRAYED : 0), IDM_ADD_ALL,
                        L"添加全部");
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

    HMENU enemies = CreatePopupMenu();
    AppendMenuW(enemies, MF_STRING, 400, L"随机加入一只");
    AppendMenuW(enemies, MF_STRING, 401, L"加入 Enemy1（10 HP）");
    AppendMenuW(enemies, MF_STRING, 402, L"加入 Enemy2（5 HP）");
    AppendMenuW(enemies, MF_STRING | ((enemyAuto && enemyAuto()) ? MF_CHECKED : 0), 403, L"自动增加");
    HMENU interval = CreatePopupMenu();
    const int seconds[] = {1, 5, 10, 20, 30, 60, 120};
    for (int i = 0; i < 7; ++i) {
        const auto title = std::to_wstring(seconds[i]) + L" 秒";
        AppendMenuW(interval, MF_STRING | ((enemyInterval && enemyInterval() == seconds[i]) ? MF_CHECKED : 0), 410 + i, title.c_str());
    }
    AppendMenuW(enemies, MF_POPUP, (UINT_PTR)interval, L"生成间隔");
    AppendMenuW(enemies, MF_STRING, 404, L"清空所有敌人");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)enemies, L"敌人");
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
    DestroyMenu(emo);
    DestroyMenu(menu);
    if (cmd == 0) return;
    if (cmd >= 400 && cmd <= 402) { if (onEnemyAdd) onEnemyAdd(cmd - 400); return; }
    if (cmd == 403) { if (onEnemyAuto) onEnemyAuto(!(enemyAuto && enemyAuto())); return; }
    if (cmd == 404) { if (onEnemyClear) onEnemyClear(); return; }
    if (cmd >= 410 && cmd <= 416) { if (onEnemyInterval) onEnemyInterval(seconds[cmd - 410]); return; }

    switch (cmd) {
    case IDM_TOGGLE:
        if (onToggle) onToggle();
        return;
    case IDM_ARMED:
        if (onArmed) onArmed(!(isArmed && isArmed()));
        return;
    case IDM_TASKBAR:
        if (onTaskbar) onTaskbar(!(isTaskbar && isTaskbar()));
        return;
    case IDM_QUIT:
        if (onQuit) onQuit();
        return;
    default:
        break;
    }

    if (cmd == IDM_ADD_ALL) {
        if (onAddAll && (!canAdd || canAdd())) onAddAll();
        return;
    }

    // 剩下三段都是"基址 + 下标"，下标还得现问一遍名单 —— 名单在菜单开起来
    // 之后可能已经变了，拿菜单里那个序号去对上一次的表会点错人
    if (cmd >= IDM_EMOTE_BASE && cmd < IDM_EMOTE_BASE + 18) {
        if (onEmote) onEmote(cmd - IDM_EMOTE_BASE + 1);
    } else if (cmd >= IDM_ADD_BASE && cmd < IDM_ADD_BASE + IDM_ID_RANGE) {
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
}

}  // namespace fd
