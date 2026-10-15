#include "window.h"

#include <cstring>

namespace fd {

namespace {
const wchar_t* kClass = L"FalcomDeskling.Layered";
bool g_classRegistered = false;
}  // namespace

LRESULT CALLBACK LayeredWindow::WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    LayeredWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<LayeredWindow*>(cs->lpCreateParams);
        // 这一步非做不可：CreateWindowExW 还没返回，成员 _hwnd 仍是空的。
        // 不先把句柄认下来，下面任何一处拿 _hwnd 去调 DefWindowProc 都是 1400
        self->_hwnd = h;
        SetWindowLongPtr(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(h, msg, wp, lp);
    }
    self = reinterpret_cast<LayeredWindow*>(GetWindowLongPtr(h, GWLP_USERDATA));
    if (self) return self->OnMessage(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT LayeredWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        // 分层窗口也得把无效区消化掉，不然消息循环会被 WM_PAINT 一直叫醒
        PAINTSTRUCT ps;
        BeginPaint(_hwnd, &ps);
        EndPaint(_hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        // 这里不许再 PostQuitMessage：多角色时"删掉一个人"会走 DestroyWindow，
        // 那等于把整个程序一起关了。退出只从托盘那条路来
        _hwnd = nullptr;
        return 0;
    case WM_WINDOWPOSCHANGED: {
        // 被别的窗口挪过就别再拿旧坐标做差分了
        auto wp2 = reinterpret_cast<WINDOWPOS*>(lp);
        if (!(wp2->flags & SWP_NOMOVE)) { _lastX = wp2->x; _lastY = wp2->y; }
        break;
    }
    default:
        break;
    }
    return DefWindowProc(_hwnd, msg, wp, lp);
}

bool LayeredWindow::Create(int w, int h, bool clickThrough, bool topmost) {
    if (_hwnd) return true;
    _w = w;
    _h = h;
    _failStep = "none";
    _gle = 0;

    HINSTANCE inst = GetModuleHandle(nullptr);
    if (!g_classRegistered) {
        WNDCLASSEX wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClass;
        if (!RegisterClassEx(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                _failStep = "RegisterClassEx";
                _gle = GetLastError();
                return false;
            }
        }
        g_classRegistered = true;
    }

    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    if (topmost) ex |= WS_EX_TOPMOST;
    if (clickThrough) ex |= WS_EX_TRANSPARENT;

    _hwnd = CreateWindowExW(ex, kClass, L"", WS_POPUP, 0, 0, w, h, nullptr, nullptr, inst, this);
    if (!_hwnd) {
        _failStep = "CreateWindowExW";
        _gle = GetLastError();
        return false;
    }

    _memDc = CreateCompatibleDC(nullptr);
    if (!_memDc) {
        _failStep = "CreateCompatibleDC";
        _gle = GetLastError();
        Destroy();
        return false;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 自顶向下，和内存布局一致
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    _dib = CreateDIBSection(_memDc, &bi, DIB_RGB_COLORS, &_bits, nullptr, 0);
    if (!_dib || !_bits) {
        _failStep = "CreateDIBSection";
        _gle = GetLastError();
        Destroy();
        return false;
    }
    _oldBmp = (HBITMAP)SelectObject(_memDc, _dib);
    _stride = w * 4;
    memset(_bits, 0, (size_t)_stride * (size_t)h);

    ShowWindow(_hwnd, SW_SHOWNOACTIVATE);
    if (topmost) {
        SetWindowPos(_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return true;
}

void LayeredWindow::Destroy() {
    if (_hwnd) {
        DestroyWindow(_hwnd);
        _hwnd = nullptr;
    }
    if (_oldBmp && _memDc) {
        SelectObject(_memDc, _oldBmp);
        _oldBmp = nullptr;
    }
    if (_dib) { DeleteObject(_dib); _dib = nullptr; }
    if (_memDc) { DeleteDC(_memDc); _memDc = nullptr; }
    _bits = nullptr;
}

void LayeredWindow::MoveTo(int x, int y) {
    if (!_hwnd) return;
    if (x == _lastX && y == _lastY) return;
    // NOCOPYBITS：分层窗口的内容来自表面而不是窗口位图，搬旧像素纯属多余
    // ASYNCWINDOWPOS：实测把一次 2.3 毫秒的同步挪动压下去了，别在这儿等合成
    SetWindowPos(_hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                     SWP_NOCOPYBITS | SWP_ASYNCWINDOWPOS);
    _lastX = x;
    _lastY = y;
}

bool LayeredWindow::Present(int x, int y) {
    if (!_hwnd) return false;
    POINT dst{ x, y };
    SIZE sz{ _w, _h };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    if (!UpdateLayeredWindow(_hwnd, nullptr, &dst, &sz, _memDc, &src, 0, &bf, ULW_ALPHA)) {
        return false;
    }
    _lastX = x;
    _lastY = y;
    return true;
}

bool LayeredWindow::Resize(int w, int h) {
    if (!_hwnd) return false;
    if (w == _w && h == _h) return true;

    if (_oldBmp && _memDc) {
        SelectObject(_memDc, _oldBmp);
        _oldBmp = nullptr;
    }
    if (_dib) { DeleteObject(_dib); _dib = nullptr; }
    _bits = nullptr;

    _w = w;
    _h = h;
    _stride = w * 4;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    _dib = CreateDIBSection(_memDc, &bi, DIB_RGB_COLORS, &_bits, nullptr, 0);
    if (!_dib || !_bits) {
        _failStep = "Resize:CreateDIBSection";
        _gle = GetLastError();
        return false;
    }
    _oldBmp = (HBITMAP)SelectObject(_memDc, _dib);
    memset(_bits, 0, (size_t)_stride * (size_t)h);

    // 尺寸一变，原来那张表面就不作数了：逼着下一拍走 UpdateLayeredWindow
    SetWindowPos(_hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    _lastX = -1;
    _lastY = -1;
    return true;
}

void LayeredWindow::Show(bool on) {
    if (_hwnd) ShowWindow(_hwnd, on ? SW_SHOWNOACTIVATE : SW_HIDE);
}

bool LayeredWindow::Visible() const {
    return _hwnd && ::IsWindowVisible(_hwnd) != FALSE;
}

}  // namespace fd
