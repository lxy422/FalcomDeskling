#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>
#include <vector>

namespace fd {

// 托盘是这个程序唯一的控制台：窗口本身不占任务栏也永远不抢焦点。
// 名单是会变的（中途加过人、或者有人往目录里丢了个夹），所以菜单每次
// 开起来之前现编，不在构造时定死 —— 这一点照的是 WpfTest 的 TrayMenu。
//
// 命令不是走 WM_COMMAND 发出去的：TrackPopupMenu 用 TPM_RETURNCMD 在关完
// 菜单之后才把编号交回来，回调于是落在菜单栈已经空掉的那一秒。省掉一整类
// "在菜单里开新窗口会咬到消息队列"的毛病。
class Tray {
public:
    static constexpr UINT WM_TRAY = WM_APP + 1;

    // 加/删那两段把数组下标加进基址，所以下面限了 IDM_ID_RANGE 个
    enum {
        IDM_TOGGLE = 1,
        IDM_QUIT = 2,
        IDM_ARMED = 3,
        IDM_TASKBAR = 4,
        IDM_ADD_ALL = 5,      // 一次把还没上场的都叫上来
        IDM_EMOTE_BASE = 20,  // 20 + (id-1)，id 是 1..18 那枚表情
        IDM_ADD_BASE = 100,   // 100 + i  -> 加名单里第 i 个
        IDM_DEL_BASE = 200,   // 200 + i  -> 删桌上第 i 个
        IDM_ID_RANGE = 100
    };

    using NamesFn = std::function<std::vector<std::wstring>()>;
    using AddFn = std::function<void(const std::wstring&)>;
    using AddAllFn = std::function<void()>;
    using RemoveFn = std::function<void(const std::wstring&)>;
    using ArmedFn = std::function<void(bool)>;
    using IsArmedFn = std::function<bool()>;
    using ActionFn = std::function<void()>;
    using EmoteFn = std::function<void(int)>;
    // 任务栏漫步是个模式，不是开关一个角色：它管的是"桌上只能有一个人"
    using TaskbarFn = std::function<void(bool)>;
    using IsTaskbarFn = std::function<bool()>;
    // 菜单自己不许知道"能不能加"这件事：桌上满人了、或者进了漫步模式，
    // 都该由 main 说，不然两处各判一遍早晚会对不上
    using CanAddFn = std::function<bool()>;

    ~Tray() { Destroy(); }

    bool Create(HWND hwnd, const wchar_t* iconPath, const wchar_t* tip);
    void Destroy();
    void Popup();

    // 谁在场上是这一瞬间的事，所以编名单放在 Popup 里而不是 Create 里
    NamesFn namesAddable;
    NamesFn namesOnscreen;
    AddFn onAdd;
    AddAllFn onAddAll;
    RemoveFn onRemove;
    ArmedFn onArmed;
    IsArmedFn isArmed;
    TaskbarFn onTaskbar;
    IsTaskbarFn isTaskbar;
    CanAddFn canAdd;
    ActionFn onToggle;
    ActionFn onQuit;
    // 点谁放谁：桌上不止一个人的时候一次全放，逐张验才不用挨个戳
    EmoteFn onEmote;
    std::function<void(int)> onEnemyAdd;
    ActionFn onEnemyClear;
    std::function<void(bool)> onEnemyAuto;
    std::function<bool()> enemyAuto;
    std::function<void(double)> onEnemyInterval;
    std::function<double()> enemyInterval;

private:
    HWND _hwnd = nullptr;
    NOTIFYICONDATAW _nid{};
    bool _active = false;
};

}  // namespace fd
