#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fd {

// 一个角色一个分层窗口，只盖住它自己那格贴图。
// 位置变了只调 MoveTo（零像素上传），只有换帧才 Present 上传一次 —— 这就是低消耗的全部来源。
class LayeredWindow {
public:
    ~LayeredWindow() { Destroy(); }

    bool Create(int w, int h, bool clickThrough, bool topmost = true);
    void Destroy();

    HWND Handle() const { return _hwnd; }
    bool Valid() const { return _hwnd != nullptr; }

    void MoveTo(int x, int y);
    bool Present(int x, int y);

    // 裁剪框换了尺寸就得重做表面，这是唯一一处会改窗口大小的地方
    bool Resize(int w, int h);

    void Show(bool on);
    bool Visible() const;

    // 失败在哪一步、GetLastError 是多少：排查创建问题就靠这两个
    const char* FailStep() const { return _failStep; }
    DWORD LastError() const { return _gle; }

    // 直接往这块表面上画，省一次拷贝
    void* Bits() const { return _bits; }
    int Stride() const { return _stride; }
    int W() const { return _w; }
    int H() const { return _h; }
    HDC MemDC() const { return _memDc; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp);

    HWND _hwnd = nullptr;
    HDC _memDc = nullptr;
    HBITMAP _dib = nullptr;
    HBITMAP _oldBmp = nullptr;
    void* _bits = nullptr;
    int _w = 0, _h = 0, _stride = 0;
    int _lastX = -1, _lastY = -1;
    const char* _failStep = "none";
    DWORD _gle = 0;
};

}  // namespace fd
