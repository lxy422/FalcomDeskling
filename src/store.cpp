#include "store.h"
#include "wic.h"
#include "config.h"
#include "charconf.h"
#include <cwctype>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace fd {
namespace {

namespace fs = std::filesystem;

// exe 旁边那一级就是全部：联接由 build.cmd 建，程序自己不去翻目录树
std::wstring ExeDir() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::error_code ec;
    cached = fs::path(exe).parent_path().wstring();
    return cached;
}

std::wstring GuessFor(const wchar_t* leaf) {
    return (fs::path(ExeDir()) / leaf).wstring();
}

// 找到的东西缓存一次就定下来：中途往目录里丢夹不算数，那是第 4 步"现扫现加"
// 才有的待遇，这里只管启动那一刻有什么
struct Root {
    std::wstring guess;
    std::wstring found;
    bool tried = false;
};

Root g_chr;
Root g_enemy;
Root g_enemy1, g_enemy2;
std::wstring g_chrOverride;
std::wstring g_enemyOverride;

const std::wstring& Resolve(Root& r, const wchar_t* leaf, const std::wstring& overridePath) {
    if (r.tried) return r.found;
    r.tried = true;
    r.guess = overridePath.empty() ? GuessFor(leaf) : overridePath;

    std::error_code ec;
    if (!r.guess.empty() && fs::is_directory(r.guess, ec)) {
        r.found = r.guess;
        return r.found;
    }
    // 没找到也要把 guess 留着，报错的时候好说"我看了哪儿"
    r.found.clear();
    return r.found;
}

// 这个夹想不想当一个人：底下至少有一层是 <数字>.<动作> 那种名字。
// 光看目录名会把 runtimes 之类不相干的夹也报一遍
bool LooksLikeSkinRoot(const fs::path& dir) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const std::wstring n = it->path().filename().wstring();
        if (n.size() > 1 && n[0] >= L'1' && n[0] <= L'9' && n[1] == L'.') return true;
    }
    return false;
}

}  // namespace

void SetRootOverrides(const std::wstring& chr, const std::wstring& enemy) {
    g_chrOverride = chr;
    g_enemyOverride = enemy;
    // 改了就得重新找一遍，别让先前的猜测把后面的显式路径吃掉
    g_chr = Root{};
    g_enemy = Root{};
    g_enemy1 = Root{}; g_enemy2 = Root{};
}

const std::wstring& ChrRoot() { return Resolve(g_chr, L"chr", g_chrOverride); }
const std::wstring& EnemyRoot() { return Resolve(g_enemy, L"Enemy", g_enemyOverride); }

// 报错的时候要说"我看了哪儿"，所以这里得先把 Resolve 走一遍 —— guess 只在
// Resolve 里填，光读字段就会在"谁都没找过"的时候返回一个空串
const std::wstring& ChrRootGuess() { (void)ChrRoot(); return g_chr.guess; }
const std::wstring& EnemyRootGuess() { (void)EnemyRoot(); return g_enemy.guess; }

bool HasWalkSet(const std::wstring& dir) {
    std::error_code ec;
    return fs::is_regular_file(fs::path(dir) / L"2.Walk" / L"dir0.png", ec);
}

std::vector<SkinInfo> ScanChr(std::wstring* diagnostic) {
    std::vector<SkinInfo> found;
    if (diagnostic) diagnostic->clear();
    const std::wstring& root = ChrRoot();
    if (root.empty()) return found;

    std::error_code ec;
    size_t directories = 0;
    std::wstring skipped;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        ++directories;
        const fs::path p = it->path();
        const std::wstring leaf = p.filename().wstring();
        // 这几个不是人：敌人那摊、资源、还有联接自己套上来的同名夹
        if (leaf == L"Enemy" || leaf == L"Resource" || leaf == L"Chr") continue;
        if (!HasWalkSet(p.wstring())) {
            if (directories <= 3) {
                std::error_code walkError;
                const auto walk = p / L"2.Walk" / L"dir0.png";
                (void)fs::is_regular_file(walk, walkError);
                skipped += L"\n  " + walk.wstring();
                if (walkError) skipped += L"（系统错误 " + std::to_wstring(walkError.value()) + L"）";
            }
            continue;
        }

        SkinInfo si;
        si.name = leaf;
        si.dir = p.wstring();
        // 武器那两套都得齐才算数：少一套的话装备态会去开一个不存在的文件
        // Optional art must not change the directory iterator's error state.
        std::error_code artError;
        si.hasStandby = fs::is_regular_file(p / L"4.standby" / L"dir0.png", artError) &&
                        fs::is_regular_file(p / L"5.run2" / L"dir0.png", artError);
        found.push_back(si);
    }
    if (diagnostic) {
        *diagnostic = L"已枚举人物子目录：" + std::to_wstring(directories) +
                      L"；可用角色：" + std::to_wstring(found.size());
        if (ec) *diagnostic += L"\n目录枚举失败，系统错误：" + std::to_wstring(ec.value());
        if (found.empty()) {
            *diagnostic += L"\n需要的结构：chr\\角色名\\2.Walk\\dir0.png";
            if (HasWalkSet(root)) *diagnostic += L"\n当前 --root 指到了角色目录，请改为它的上一级。";
            if (!skipped.empty()) *diagnostic += L"\n未找到的文件（最多列出 3 个）：" + skipped;
        }
    }
    std::sort(found.begin(), found.end(),
              [](const SkinInfo& a, const SkinInfo& b) { return a.name < b.name; });

    // 发号：排序序号，从 0 开始。用序号而不是目录名 hash —— 247 个样本
    // 塞进几百个格子，碰撞是必然不是可能，而且 hash 出来的号还会随目录变。
    // 夹里那份 character.xml 要是明写了 id=，ConfigFor() 会盖掉这里，
    // 顺序就不作数了 —— 这里只是"没人说过"时的兜底
    for (size_t i = 0; i < found.size(); ++i) found[i].id = (int)i;
    return found;
}


const std::wstring& EnemyRootFor(Faction faction) {
    const auto leaf = faction == Faction::Enemy1 ? L"Enemy1" : L"Enemy2";
    return Resolve(faction == Faction::Enemy1 ? g_enemy1 : g_enemy2, leaf,
                   g_enemyOverride.empty() ? L"" : (fs::path(g_enemyOverride) / leaf).wstring());
}

namespace {
bool Under(const std::wstring& dir, const std::wstring& root) {
    if (root.empty()) return false;
    std::error_code ec;
    auto a = fs::weakly_canonical(dir, ec).wstring();
    if (ec) return false;
    auto b = fs::weakly_canonical(root, ec).wstring();
    return !ec && a.size() > b.size() && _wcsnicmp(a.c_str(), b.c_str(), b.size()) == 0 &&
        (a[b.size()] == L'\\' || a[b.size()] == L'/');
}
int NumberOf(const std::wstring& name) {
    auto start = name.find_last_not_of(L"0123456789");
    if (start == name.size() - 1) return -1;
    try { return std::stoi(name.substr(start == std::wstring::npos ? 0 : start + 1)); }
    catch (...) { return -1; }
}
}
Faction DirectoryFaction(const std::wstring& dir) {
    if (Under(dir, EnemyRootFor(Faction::Enemy1))) return Faction::Enemy1;
    if (Under(dir, EnemyRootFor(Faction::Enemy2))) return Faction::Enemy2;
    // Read-only compatibility for a legacy Enemy tree, until split by the tool.
    std::error_code ec;
    const int number = NumberOf(fs::path(dir).filename().wstring());
    if (number >= 0 && !EnemyRoot().empty() && fs::equivalent(fs::path(dir).parent_path(), EnemyRoot(), ec))
        return number < 10000 ? Faction::Enemy1 : Faction::Enemy2;
    return Faction::Ally;
}

std::vector<SkinInfo> ScanEnemyIndex() {
    std::vector<SkinInfo> found;
    for (auto faction : {Faction::Enemy1, Faction::Enemy2}) {
        auto root = EnemyRootFor(faction);
        const bool split = !root.empty();
        if (!split) root = EnemyRoot();
        if (root.empty()) continue;
        std::error_code ec;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2) || !LooksLikeSkinRoot(it->path())) continue;
            const auto name = it->path().filename().wstring();
            const int number = NumberOf(name);
            if (number < 0 || (number < 10000 ? Faction::Enemy1 : Faction::Enemy2) != faction) continue;
            SkinInfo skin;
            skin.name = name; skin.dir = it->path().wstring(); skin.id = number;
            skin.faction = faction; skin.hasStandby = true;
            skin.enemyRepeat = ConfigFor(skin.dir, skin.id).enemyRepeat.valueOr(DefaultEnemyRepeat(faction));
            found.push_back(skin);
        }
    }
    std::sort(found.begin(), found.end(), [](const SkinInfo& a, const SkinInfo& b) { return a.name < b.name; });
    return found;
}

bool ValidateEnemySkin(const SkinInfo& skin, std::wstring& error) {
    for (auto art : {Art::Standby, Art::Run2, Art::Attack, Art::BeAttacked, Art::Kneel2}) {
        for (int dir = 0; dir < DIR_COUNT; ++dir) {
            const auto path = fs::path(skin.dir) / FolderOf(art) / (L"dir" + std::to_wstring(dir) + L".png");
            Strip strip;
            if (!DecodePngStrip(path.c_str(), strip) || strip.frames < 1 || strip.width % strip.height != 0) {
                error = L"敌人素材缺失或损坏：" + path.wstring(); return false;
            }
        }
    }
    error.clear(); return true;
}
bool IsEnemyDir(const std::wstring& dir) { return EnemyFaction(DirectoryFaction(dir)); }

} // namespace fd
