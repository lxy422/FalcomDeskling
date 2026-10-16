#pragma once

#include <string>
#include <vector>

namespace fd {

// 极浅的 XML 读取器。
//
// 为什么不用 XMLLite / MSXML：前者是流式 API，读个属性要转三圈；后者依赖
// MSXML 6.0 的 COM 注册，装不干净就是一句 NoClassRegistered。配置只有
// 一层根 + 几层子节点、全是属性，两百行读完比拖一个依赖进来划算，
// 也和这个项目"没有 CMake、纯 cl.exe"的脾气一致。
//
// 要处理的就四样：注释 <!-- -->、自闭合 <x/>、实体 " & < > '、
// 属性值里的空格。声明 <?xml ...?> 直接跳过。
struct XmlAttr {
    std::wstring name;
    std::wstring value;
};

struct XmlElem {
    std::wstring name;
    std::vector<XmlAttr> attrs;
    std::vector<XmlElem> kids;

    // 没有这个属性就给空串。属性名拼错只会静默失效，所以 --dumpconfig 必须
    // 把读到的东西原样打出来，光靠这个函数是看不出来的
    const std::wstring* Attr(const wchar_t* key) const;
    std::wstring AttrOr(const wchar_t* key, const wchar_t* dflt) const {
        const std::wstring* p = Attr(key);
        return p ? *p : std::wstring(dflt);
    }
    // 默认值本身是个 wstring 的那一支。少了它，调用方只能先转成 c_str()，
    // 而中间那个 wstring 一断就变成悬空指针
    std::wstring AttrOr(const wchar_t* key, const std::wstring& dflt) const {
        const std::wstring* p = Attr(key);
        return p ? *p : dflt;
    }
    bool Has(const wchar_t* key) const { return Attr(key) != nullptr; }

    // 第一个名字对得上的孩子。大小写不敏感 —— XML 是大小写敏感的，
    // 但配置文件是人写的，`<Other>` 和 `<other>` 该是同一个东西
    const XmlElem* Kid(const wchar_t* name) const;
    std::vector<const XmlElem*> Kids(const wchar_t* name) const;
};

// 读一个 UTF-8 文件。失败返回 false 并把原因写进 err —— 一个人读崩了
// 不许把整张桌子带下来，调用方得能只让那个人走全默认
bool LoadXmlFile(const std::wstring& path, XmlElem& out, std::wstring& err);

// 同一套解析，给测试和内嵌串用
bool ParseXml(const std::string& utf8, XmlElem& out, std::wstring& err);

}  // namespace fd
