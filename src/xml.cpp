#include "xml.h"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <cwchar>

namespace fd {
namespace {

// ---- UTF-8 解码 ----
//
// 配置文件里全是中文（人名、表情名），所以这一步不能省。BOM 直接跳过。
std::wstring Utf8ToWide(const char* p, size_t n) {
    if (n >= 3 && (unsigned char)p[0] == 0xEF &&
        (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) {
        p += 3; n -= 3;
    }
    std::wstring out;
    out.reserve(n);
    const unsigned char* s = reinterpret_cast<const unsigned char*>(p);
    const unsigned char* e = s + n;
    while (s < e) {
        unsigned int cp = 0;
        int extra = 0;
        if (*s < 0x80) { cp = *s++; extra = 0; }
        else if ((*s & 0xE0) == 0xC0) { cp = *s++ & 0x1Fu; extra = 1; }
        else if ((*s & 0xF0) == 0xE0) { cp = *s++ & 0x0Fu; extra = 2; }
        else if ((*s & 0xF8) == 0xF0) { cp = *s++ & 0x07u; extra = 3; }
        else { ++s; continue; }   // 孤立的续字节，跳过而不是把整份文件判死
        while (extra > 0 && s < e) {
            if ((*s & 0xC0) != 0x80) break;
            cp = (cp << 6) | (*s++ & 0x3Fu);
            --extra;
        }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

inline bool IsSpace(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L'\n' || c == L'\r';
}

inline bool IsNameStart(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
           c == L'_' || c == L':' || c > 0x7F;   // 中文标签也算名字的一部分
}

inline bool IsNameChar(wchar_t c) {
    return IsNameStart(c) || (c >= L'0' && c <= L'9') || c == L'-' || c == L'.';
}

struct Cursor {
    const wchar_t* s;
    size_t pos = 0;
    size_t len = 0;

    bool End() const { return pos >= len; }
    wchar_t Peek(size_t k = 0) const { return pos + k < len ? s[pos + k] : 0; }
    void SkipSpace() { while (!End() && IsSpace(s[pos])) ++pos; }
    bool Eat(const wchar_t* lit) {
        const size_t n = wcslen(lit);
        if (len - pos < n) return false;
        if (wcsncmp(s + pos, lit, n) != 0) return false;
        pos += n;
        return true;
    }
};

// 读到 end 之前，或者 < 之前 —— 属性值里的空格必须原样留着
std::wstring ReadName(Cursor& c) {
    std::wstring n;
    while (!c.End() && IsNameChar(c.Peek())) n.push_back(c.s[c.pos++]);
    return n;
}

inline size_t LenAt(const Cursor& c, size_t need) {
    return c.len - c.pos >= need ? need : 0;
}

// 解一个实体。返回 false 表示这不是合法实体，调用方按原文收
bool ReadEntity(Cursor& c, std::wstring& out) {
    if (c.Peek() != L'&') return false;
    const size_t start = c.pos;
    ++c.pos;
    if (c.Peek() == L'#') {
        ++c.pos;
        bool hex = (c.Peek() == L'x' || c.Peek() == L'X');
        if (hex) ++c.pos;
        unsigned int cp = 0;
        while (!c.End() && c.Peek() != L';') {
            const wchar_t d = c.s[c.pos++];
            unsigned int v = 0;
            if (d >= L'0' && d <= L'9') v = (unsigned)(d - L'0');
            else if (d >= L'a' && d <= L'f') v = (unsigned)(d - L'a') + 10;
            else if (d >= L'A' && d <= L'F') v = (unsigned)(d - L'A') + 10;
            else { c.pos = start; return false; }
            cp = cp * 16 + v;
        }
        if (c.Peek() != L';') { c.pos = start; return false; }
        ++c.pos;
        if (cp > 0x10FFFF) { c.pos = start; return false; }
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
        return true;
    }
    // 用码位写，不用引号字面量：这几个字符正好是引号本身，写进 L'...' 里
    // 极易变成 L'\'' 这种断掉的东西，编译器还会往后面一路报过去
    static const struct { const wchar_t* key; wchar_t ch; } kEnt[] = {
        {L"quot", (wchar_t)0x22},   // "
        {L"apos", (wchar_t)0x27},   // '
        {L"lt",   (wchar_t)0x3C},   // <
        {L"gt",   (wchar_t)0x3E},   // >
        {L"amp",  (wchar_t)0x26},   // &
    };
    for (const auto& e : kEnt) {
        const size_t n = wcslen(e.key);
        if (LenAt(c, n + 1) == n + 1 && wcsncmp(c.s + c.pos, e.key, n) == 0 &&
            c.s[c.pos + n] == L';') {
            c.pos += n + 1;
            out.push_back(e.ch);
            return true;
        }
    }
    c.pos = start;
    return false;
}

// 属性值：引号内原样收，只解实体
bool ReadAttrValue(Cursor& c, std::wstring& out, std::wstring& err) {
    c.SkipSpace();
    const wchar_t q = c.Peek();
    if (q != L'"' && q != L'\'') {
        err = L"属性值少了引号";
        return false;
    }
    ++c.pos;
    while (!c.End()) {
        const wchar_t ch = c.s[c.pos];
        if (ch == q) { ++c.pos; return true; }
        if (ch == L'&') {
            if (!ReadEntity(c, out)) {
                err = L"实体认不出来";
                return false;
            }
            continue;
        }
        out.push_back(ch);
        ++c.pos;
    }
    err = L"属性值没有收尾的引号";
    return false;
}

// 跳掉 <!-- ... -->、<?...?>、<!DOCTYPE ...>
bool SkipProlog(Cursor& c, std::wstring& err) {
    for (;;) {
        c.SkipSpace();
        if (c.Peek() != L'<') return true;
        if (c.Eat(L"<!--")) {
            while (!c.End() && !c.Eat(L"-->")) ++c.pos;
            if (c.End()) { err = L"注释没有收尾"; return false; }
            continue;
        }
        if (c.Eat(L"<?")) {
            while (!c.End() && !c.Eat(L"?>")) ++c.pos;
            if (c.End()) { err = L"声明没有收尾"; return false; }
            continue;
        }
        if (c.Eat(L"<!")) {
            while (!c.End() && !c.Eat(L">")) ++c.pos;
            if (c.End()) { err = L"<! 没有收尾"; return false; }
            continue;
        }
        return true;
    }
}

bool ParseElem(Cursor& c, XmlElem& out, std::wstring& err);

// 一个元素：开始标签、若干孩子、结束标签
bool ParseElem(Cursor& c, XmlElem& out, std::wstring& err) {
    if (!c.Eat(L"<")) { err = L"这里该有一个 <"; return false; }
    out.name = ReadName(c);
    if (out.name.empty()) { err = L"标签名是空的"; return false; }

    for (;;) {
        c.SkipSpace();
        if (c.End()) { err = L"元素<" + out.name + L"没收尾"; return false; }
        if (c.Eat(L"/>")) return true;                    // 自闭合
        if (c.Eat(L">")) break;                           // 进到孩子
        if (c.Eat(L"/>")) return true;
        // 属性
        const std::wstring an = ReadName(c);
        if (an.empty()) { err = L"<" + out.name + L"> 里属性名读不出来"; return false; }
        c.SkipSpace();
        if (c.Peek() != L'=') { err = L"属性 " + an + L" 后面没有等号"; return false; }
        ++c.pos;
        XmlAttr a;
        a.name = an;
        if (!ReadAttrValue(c, a.value, err)) {
            err = L"<" + out.name + L"> 的 " + err;
            return false;
        }
        out.attrs.push_back(std::move(a));
    }

    // 孩子层
    for (;;) {
        if (c.End()) { err = L"元素<" + out.name + L"> 没有结束标签"; return false; }
        if (c.Peek() == L'<') {
            if (c.Eat(L"</")) {
                const std::wstring close = ReadName(c);
                c.SkipSpace();
                if (!c.Eat(L">")) { err = L"</" + close + L" 没收尾"; return false; }
                if (close != out.name) {
                    err = L"结束标签对不上：<" + out.name + L"> 配上了 </" + close + L">";
                    return false;
                }
                return true;
            }
            if (!SkipProlog(c, err)) return false;
            if (c.Peek() == L'<') {
                XmlElem kid;
                if (!ParseElem(c, kid, err)) return false;
                out.kids.push_back(std::move(kid));
                continue;
            }
            continue;   // 吃掉的是文本，这里只关心属性
        }
        ++c.pos;   // 文本节点：配置全挂在属性上，跳过
    }
}

}  // namespace

const std::wstring* XmlElem::Attr(const wchar_t* key) const {
    for (const XmlAttr& a : attrs) {
        if (_wcsicmp(a.name.c_str(), key) == 0) return &a.value;
    }
    return nullptr;
}

const XmlElem* XmlElem::Kid(const wchar_t* name) const {
    for (const XmlElem& k : kids) {
        if (_wcsicmp(k.name.c_str(), name) == 0) return &k;
    }
    return nullptr;
}

std::vector<const XmlElem*> XmlElem::Kids(const wchar_t* name) const {
    std::vector<const XmlElem*> out;
    for (const XmlElem& k : kids) {
        if (_wcsicmp(k.name.c_str(), name) == 0) out.push_back(&k);
    }
    return out;
}

bool ParseXml(const std::string& utf8, XmlElem& out, std::wstring& err) {
    // 先清空。不洗的话同一个 XmlElem 连着解两份，第二份会带着第一份的属性和
    // 孩子 —— Attr() 找的是第一个同名属性，于是新值被旧值盖住，看起来就像
    // "配置读不到"。这个坑在测试里先撞上了
    out = XmlElem{};
    const std::wstring w = Utf8ToWide(utf8.data(), utf8.size());
    Cursor c{w.data(), 0, w.size()};
    if (!SkipProlog(c, err)) return false;
    if (c.Peek() != L'<') { err = L"这份文件里找不到元素"; return false; }
    if (!ParseElem(c, out, err)) return false;
    return true;
}

bool LoadXmlFile(const std::wstring& path, XmlElem& out, std::wstring& err) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = L"打不开 " + path;
        return false;
    }
    const DWORD n = GetFileSize(h, nullptr);
    std::string buf;
    buf.resize(n ? n : 1);
    DWORD got = 0;
    const BOOL ok = ReadFile(h, buf.data(), n, &got, nullptr);
    CloseHandle(h);
    if (!ok || got != n) {
        err = L"读到一半就断了 " + path;
        return false;
    }
    buf.resize(got);
    if (!ParseXml(buf, out, err)) {
        err = path + L" : " + err;
        return false;
    }
    return true;
}

}  // namespace fd
