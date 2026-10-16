# Standalone check for the shallow XML reader.
#
# The parser is new and hand-rolled, so it gets its own tiny console build
# rather than being exercised through the app: a wrong attribute read would
# otherwise show up much later as "my config is being ignored".
#
# NOTE: entity references are assembled from pieces in the C++ source. Writing
# them literally here gets them decoded on the way into the file, which makes
# the test compile as garbage instead of failing as a test.
#
# Keep ASCII-only: PS 5.1 decodes .ps1 with the system codepage.
param([int]$Keep = 0)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root "src"
$obj = Join-Path $root "build\obj"
$exe = Join-Path $obj "test_xml.exe"

if (-not (Test-Path $obj)) { New-Item -ItemType Directory -Path $obj | Out-Null }

# Pick a complete MSVC install the same way build.cmd does
$vcvars = $null
$vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsw) {
    foreach ($p in (& $vsw -all -products * -property installationPath)) {
        $c = Join-Path $p "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $c) { $vcvars = $c; break }
    }
}
if (-not $vcvars) { Write-Host "no usable vcvars64.bat found"; exit 1 }

$cpp = Join-Path $obj "test_xml.cpp"

# Single-quoted here-string: nothing gets interpolated by PowerShell.
$code = @'
#include "xml.h"
#include <cstdio>
#include <string>
using namespace fd;

static int fails = 0;
static void Check(bool ok, const char* what) {
    printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) ++fails;
}

// The ampersand is built from its code point so this source never contains a
// literal entity reference -- the file gets entity-decoded on the way in,
// which would otherwise turn the test into garbage that compiles wrong.
static std::string Ent(const char* name) {
    return std::string(1, (char)0x26) + name + ";";
}

int main() {
    XmlElem r; std::wstring err;

    // 1. declaration, comment, self-closing, attributes
    Check(ParseXml("<?xml version=\"1.0\"?>\n<!-- hi -->\n<a x=\"1\" y=\"two\"/>", r, err),
          "decl + comment + self-closing");
    Check(r.name == L"a" && r.AttrOr(L"x", L"") == L"1", "attrs on self-closing");
    Check(r.AttrOr(L"y", L"") == L"two", "second attr");
    Check(r.Attr(L"z") == nullptr, "missing attr is null, not empty");

    // 2. nesting
    Check(ParseXml("<c><o id=\"1\"/><o id=\"2\"/><o id=\"3\"/></c>", r, err), "nested");
    Check(r.Kids(L"o").size() == 3, "three kids");
    Check(r.Kids(L"o")[2]->AttrOr(L"id", L"") == L"3", "kid attr");

    // 3. entities: lt gt amp quot
    const std::string amp = Ent("amp");
    const std::string lt  = Ent("lt");
    const std::string gt  = Ent("gt");
    const std::string qu  = Ent("quot");
    const std::string doc = "<a v=\"" + lt + "x" + gt + " " + amp + " " + qu + "q" + qu + " \"/>";
    Check(ParseXml(doc, r, err), "entities parse");
    Check(r.AttrOr(L"v", L"") == L"<x> & \"q\" ", "entities decoded");

    // 4. spaces inside a quoted value survive
    Check(ParseXml("<a v=\"  two words  \"/>", r, err), "quoted spaces");
    Check(r.AttrOr(L"v", L"") == L"  two words  ", "quoted spaces kept");

    // 5. Chinese in a UTF-8 value
    Check(ParseXml("<character name=\"\xE4\xB8\xAD\xE6\x96\x87\"/>", r, err), "utf8 value");
    Check(r.Attr(L"name")->size() == 2, "utf8 decoded to 2 wchars");

    // 6. attribute name is case-insensitive (config files are hand-written)
    Check(ParseXml("<a Far=\"chase\"/>", r, err), "mixed-case attr");
    Check(r.AttrOr(L"far", L"") == L"chase", "attr name case-insensitive");

    // 7. mismatched close must fail, not silently pass
    Check(!ParseXml("<a><b/></c>", r, err), "mismatched close fails");
    Check(!err.empty(), "mismatch gives a reason");

    // 8. unterminated value must fail
    Check(!ParseXml("<a v=\"oops>", r, err), "unterminated value fails");

    // 9. text between elements is skipped, not mistaken for a child
    Check(ParseXml("<a>  hello  <b/></a>", r, err), "text node skipped");
    Check(r.Kids(L"b").size() == 1, "child after text found");

    // 10. the shape step 3 will actually read
    Check(ParseXml(
        "<character name=\"\xE9\x87\x91\" id=\"1\">\n"
        "  <speed walk=\"300\" run=\"900\"/>\n"
        "  <encounters nearLen=\"1.5\">\n"
        "    <other name=\"\xE7\xA7\x91\xE6\xB4\x9B\xE4\xB8\x9D\" far=\"chase\" near=\"attack\"/>\n"
        "    <other idRange=\"201..1000\" far=\"chase\" near=\"attack\" armed=\"true\"/>\n"
        "  </encounters>\n"
        "</character>\n", r, err), "full character.xml shape");
    Check(r.AttrOr(L"id", L"") == L"1", "root id");
    Check(r.Kid(L"speed")->AttrOr(L"run", L"") == L"900", "speed child");
    Check(r.Kid(L"encounters")->Kids(L"other").size() == 2, "two encounter rows");
    Check(r.Kid(L"encounters")->Kids(L"other")[1]->AttrOr(L"idRange", L"") == L"201..1000",
          "idRange attr");

    printf("\n%s\n", fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
'@
Set-Content -Path $cpp -Value $code -Encoding UTF8

# The test .cpp lives in build\obj, so the include has to come from /I, not
# from a relative path off the source file.
$cl = "cl /nologo /std:c++17 /EHsc /utf-8 /W3 /DUNICODE /D_UNICODE /I`"$src`" /Fo`"$obj\\`" " +
      "`"$src\xml.cpp`" `"$cpp`" /Fe`"$exe`" /link /SUBSYSTEM:CONSOLE"
$out = cmd /c "call `"$vcvars`" >nul 2>&1 & $cl" 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "---- compile failed ----"
    Write-Host ($out -join "`n")
    exit 1
}

& $exe
$rc = $LASTEXITCODE
if ($Keep -eq 0) { Remove-Item $exe -Force -ErrorAction SilentlyContinue }
exit $rc
