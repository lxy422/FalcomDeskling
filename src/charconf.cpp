#include "charconf.h"
#include "store.h"
#include "xml.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <map>

namespace fd {
namespace fs = std::filesystem;

namespace {

// Legacy numeric XML selectors. Runtime combat uses resource-root factions.
const FactionBand kBands[] = {
    {0,   999,  Faction::Ally},
    {1000, 9999, Faction::Enemy1},
    {10000, 2147483647, Faction::Enemy2},
};

constexpr int kMonsterDefaultId = 201;

// WpfTest 那边 100 就是"怪物类"的号（MonsterId = 100），生成出来的
// character.xml 里那条底线写的就是 <other id="100" far="chase" near="attack"/>。
// 我们的区间里怪物落在 201..1000，按号数去找会一个都找不到 —— 于是"谁都不许
// 看不见敌人"这条底线会**静默**失效，人只会表现为"怪在旁边晃他当没看见"。
// 所以 100 在这里只当别名认，翻译成一整类。
constexpr int kLegacyMonsterClassId = 100;

// 属性名 -> 值，找不到给 false
bool GetDouble(const XmlElem& e, const wchar_t* key, double& out) {
    const std::wstring* v = e.Attr(key);
    if (!v) return false;
    out = wcstod(v->c_str(), nullptr);
    return std::isfinite(out);
}

bool GetInt(const XmlElem& e, const wchar_t* key, int& out) {
    const std::wstring* v = e.Attr(key);
    if (!v) return false;
    out = (int)std::lround(wcstod(v->c_str(), nullptr));
    return true;
}

bool GetBool(const XmlElem& e, const wchar_t* key, bool& out) {
    const std::wstring* v = e.Attr(key);
    if (!v) return false;
    std::wstring s = *v;
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    if (s == L"true" || s == L"1" || s == L"yes" || s == L"on") { out = true; return true; }
    if (s == L"false" || s == L"0" || s == L"no" || s == L"off") { out = false; return true; }
    return false;
}

// 逗号分隔的号数或表情名。"3,4,狂怒" 都认
std::vector<int> ParseEmoteList(const std::wstring& s) {
    std::vector<int> out;
    size_t b = 0;
    for (;;) {
        const size_t c = s.find_first_of(L",; ", b);
        std::wstring one = (c == std::wstring::npos) ? s.substr(b) : s.substr(b, c - b);
        if (!one.empty()) {
            if (one == L"all") {
                for (int i = 1; i <= EMOTE_LAST_ID; ++i) out.push_back(i);
            } else if (one != L"none") {
                // 先试号数，再试名字
                wchar_t* end = nullptr;
                const long n = wcstol(one.c_str(), &end, 10);
                if (end && *end == 0 && n >= 1 && n <= EMOTE_LAST_ID) {
                    out.push_back((int)n);
                } else {
                    const int id = EmoteIdOfTitle(one);
                    if (id > 0) out.push_back(id);
                }
            }
        }
        if (c == std::wstring::npos) break;
        b = c + 1;
        if (b > s.size()) break;
    }
    return out;
}

bool ParseEncounterKind(const std::wstring& s, EncounterKind& out) {
    std::wstring t = s;
    std::transform(t.begin(), t.end(), t.begin(), ::towlower);
    if (t == L"ignore")   { out = EncounterKind::Ignore; return true; }
    if (t == L"separate") { out = EncounterKind::Separate; return true; }
    if (t == L"combat")   { out = EncounterKind::Combat; return true; }
    if (t == L"chase")    { out = EncounterKind::Chase; return true; }
    if (t == L"attack")   { out = EncounterKind::Attack; return true; }
    if (t == L"flee")     { out = EncounterKind::Flee; return true; }
    return false;
}

bool ParseReactKind(const std::wstring& s, ReactKind& out) {
    std::wstring t = s;
    std::transform(t.begin(), t.end(), t.begin(), ::towlower);
    if (t == L"normal") { out = ReactKind::Normal; return true; }
    if (t == L"hit")    { out = ReactKind::Hit; return true; }
    if (t == L"stun")   { out = ReactKind::Stun; return true; }
    if (t == L"none")   { out = ReactKind::None; return true; }
    return false;
}

bool ParseFaction(const std::wstring& s, Faction& out) {
    std::wstring t = s;
    std::transform(t.begin(), t.end(), t.begin(), ::towlower);
    if (t == L"ally")       { out = Faction::Ally; return true; }
    if (t == L"enemy1" || t == L"enemychar" || t == L"enemy") { out = Faction::EnemyChar; return true; }
    if (t == L"enemy2" || t == L"monster")    { out = Faction::Monster; return true; }
    return false;
}

// "101..200" / "101-200" / 单一个 "201"
bool ParseIdRange(const std::wstring& s, int& lo, int& hi) {
    if (s.empty()) return false;
    const size_t dot = s.find(L"..");
    if (dot != std::wstring::npos) {
        lo = (int)wcstod(s.substr(0, dot).c_str(), nullptr);
        hi = (int)wcstod(s.substr(dot + 2).c_str(), nullptr);
        return hi >= lo;
    }
    const size_t dash = s.find(L'-');
    if (dash != std::wstring::npos) {
        lo = (int)wcstod(s.substr(0, dash).c_str(), nullptr);
        hi = (int)wcstod(s.substr(dash + 1).c_str(), nullptr);
        return hi >= lo;
    }
    lo = hi = (int)wcstod(s.c_str(), nullptr);
    return true;
}

// 一条规则上的匹配字段。name= / id= / idRange= / faction= / * 五选一
bool FillMatcher(const XmlElem& e, RuleKind& kind, std::wstring& name,
                 int& lo, int& hi, Faction& faction, std::wstring& warn) {
    if (e.Has(L"name")) {
        kind = RuleKind::ByName;
        name = e.AttrOr(L"name", L"");
        return true;
    }
    if (const std::wstring* v = e.Attr(L"idRange")) {
        kind = RuleKind::ByIdRange;
        if (!ParseIdRange(*v, lo, hi)) {
            warn = L"idRange 认不出来：" + *v;
            return false;
        }
        return true;
    }
    if (const std::wstring* v = e.Attr(L"id")) {
        // 通配既可以写 name="*"，也可以写 id="*"。后者漏判就会变成 id=0 的一条
        // ById —— wcstod("*") 安静地给 0，于是"谁的兜底"变成了"只理 0 号"
        if (*v == L"*") { kind = RuleKind::ByWild; return true; }
        const int id = (int)wcstod(v->c_str(), nullptr);
        if (id == kLegacyMonsterClassId) {
            kind = RuleKind::ByFaction;
            faction = Faction::Monster;
            return true;
        }
        kind = RuleKind::ById;
        lo = hi = id;
        return true;
    }
    if (const std::wstring* v = e.Attr(L"faction")) {
        kind = RuleKind::ByFaction;
        if (!ParseFaction(*v, faction)) {
            warn = L"faction 认不出来：" + *v;
            return false;
        }
        return true;
    }
    kind = RuleKind::ByWild;
    return true;
}

// 关掉一种动作之后拿什么顶。只有"得一直动着的那几种"需要替身：
// 一次性动画直接不放就对了
Art SubstituteFor(Art a) {
    switch (a) {
    case Art::Run:     return Art::Walk;
    case Art::Walk:    return Art::Run;
    case Art::Run2:    return Art::Run;
    case Art::Standby: return Art::Run2;
    }
    return a;
}

void ReadActions(const XmlElem& node, CharacterConfig& c) {
    for (const XmlElem* a : node.Kids(L"action")) {
        Art art;
        const std::wstring folder = a->AttrOr(L"folder", L"");
        if (!ArtFromFolder(folder, art)) {
            c.warnings.push_back(L"actions 里的目录名不是已知的动作：" + folder);
            continue;
        }
        bool on = true;
        if (GetBool(*a, L"on", on)) {
            if (!on) c.artOff.push_back(art);
            ++c.fields;
        }
    }
}

void ReadEncounters(const XmlElem& node, CharacterConfig& c) {
    // d 只声明一次。C++17 那个 if (double d; ...) 的声明范围只到这一支，
    // 后面几行再写 d 就是没声明过的标识符
    double d = 0.0;
    if (GetDouble(node, L"nearLen", d))        { c.encNearLen.Assign(d); ++c.fields; }
    if (GetDouble(node, L"farLen", d))         { c.encFarLen.Assign(d); ++c.fields; }
    if (GetDouble(node, L"attackLen", d))      { c.attackLen.Assign(d); ++c.fields; }
    if (GetDouble(node, L"attackCoolSec", d))  { c.attackCoolSec.Assign(d); ++c.fields; }

    for (const XmlElem* o : node.Kids(L"other")) {
        EncRule r;
        std::wstring warn;
        if (!FillMatcher(*o, r.kind, r.name, r.lo, r.hi, r.faction, warn)) {
            if (!warn.empty()) c.warnings.push_back(warn);
            continue;
        }
        if (const std::wstring* v = o->Attr(L"far")) {
            if (!ParseEncounterKind(*v, r.act.farKind)) {
                c.warnings.push_back(L"far= 认不出来：" + *v + L"，按 ignore 走");
            }
        }
        if (const std::wstring* v = o->Attr(L"near")) {
            if (!ParseEncounterKind(*v, r.act.nearKind)) {
                c.warnings.push_back(L"near= 认不出来：" + *v + L"，按 ignore 走");
            }
        }
        if (const std::wstring* v = o->Attr(L"emoteFar"))  r.act.emoteFar = ParseEmoteList(*v);
        if (const std::wstring* v = o->Attr(L"emoteNear")) r.act.emoteNear = ParseEmoteList(*v);
        bool armed = false;
        if (GetBool(*o, L"armed", armed)) r.act.armed = armed;
        c.rules.push_back(std::move(r));
        ++c.fields;
    }
}

void ReadReactions(const XmlElem& node, CharacterConfig& c) {
    for (const XmlElem* h : node.Kids(L"hit")) {
        ReactRule r;
        std::wstring warn;
        if (!FillMatcher(*h, r.kind, r.name, r.lo, r.hi, r.faction, warn)) {
            if (!warn.empty()) c.warnings.push_back(warn);
            continue;
        }
        if (const std::wstring* v = h->Attr(L"kind")) {
            if (!ParseReactKind(*v, r.act.kind)) {
                c.warnings.push_back(L"react kind 认不出来：" + *v +
                                    L"，按默认的挨一下走");
            }
        }
        if (const std::wstring* v = h->Attr(L"emote")) r.act.emote = ParseEmoteList(*v);
        if (double d; GetDouble(*h, L"kbLen", d)) r.act.kbLen = d;
        c.reactRules.push_back(std::move(r));
        ++c.fields;
    }
}

void ReadStates(const XmlElem& node, CharacterConfig& c) {
    std::wstring d = node.AttrOr(L"default", L"builtin");
    std::transform(d.begin(), d.end(), d.begin(), ::towlower);
    c.statesDefault.Assign(d != L"none");
    if (d != L"builtin" && d != L"none") {
        c.warnings.push_back(L"states default 认不出来：" + node.AttrOr(L"default", L"") +
                             L"，按 builtin 走");
        c.statesDefault.Assign(true);
    }
    for (const XmlElem* from : node.Kids(L"from")) {
        const std::wstring fs = from->AttrOr(L"state", L"");
        State base = State::Idle;
        StateRule proto;
        if (fs == L"any" || fs.empty()) {
            proto.anyFrom = true;
        } else if (!StateFromName(fs, base)) {
            c.warnings.push_back(L"states 里的状态名认不出来：" + fs);
            continue;
        } else {
            proto.from = base;
        }
        for (const XmlElem* to : from->Kids(L"to")) {
            StateRule r = proto;
            const std::wstring ev = to->AttrOr(L"event", L"");
            bool found = false;
            for (int i = 0; i < (int)GameEvent::Count; ++i) {
                if (_wcsicmp(EventName((GameEvent)i), ev.c_str()) == 0) {
                    r.ev = (GameEvent)i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                c.warnings.push_back(L"转移表里的 event 认不出来：" + ev);
                continue;
            }
            const std::wstring ts = to->AttrOr(L"state", L"");
            // state="return" 是"回到进这一态之前"，不是某个固定态。一次性动画
            // 全指望它：追人追到一半挨一下，回来还得是追人
            if (_wcsicmp(ts.c_str(), L"return") == 0) {
                r.back = true;
            } else if (!StateFromName(ts, r.to)) {
                c.warnings.push_back(L"转移表里的目标状态认不出来：" + ts);
                continue;
            }
            if (const std::wstring* v = to->Attr(L"art")) {
                Art a;
                if (ArtFromFolder(*v, a)) r.art.Assign(a);
                else c.warnings.push_back(L"art= 不是已知的动作目录：" + *v);
            }
            if (const std::wstring* v = to->Attr(L"emote")) r.emote.Assign(ParseEmoteList(*v));
            if (double d; GetDouble(*to, L"cooldown", d)) r.cooldownSec.Assign(d);
            c.stateRules.push_back(std::move(r));
            ++c.fields;
        }
    }
}

// 一份 character.xml 读进 c。已经灌好的底（比如 monster.xml）留在 c 上不动
void ApplyCharacterXml(const XmlElem& root, CharacterConfig& c) {
    if (root.name != L"character") {
        c.warnings.push_back(L"根元素该是 <character>，现在是 <" + root.name + L">");
        return;
    }
    int id = 0;
    if (GetInt(root, L"id", id)) { c.id = id; ++c.fields; }
    c.name = root.AttrOr(L"name", c.name);

    if (const XmlElem* b = root.Kid(L"body")) {
        if (double d; GetDouble(*b, L"bodylen", d)) { c.bodyLen.Assign(d); ++c.fields; }
    }
    if (const XmlElem* s = root.Kid(L"speed")) {
        for (const auto& kv : {std::pair{L"walk", &c.walkPx}, {L"run", &c.runPx},
                               {L"armedRun", &c.armedRunPx},
                               {L"walkFrame", &c.walkFrameRate},
                               {L"runFrame", &c.runFrameRate}}) {
            if (double d; GetDouble(*s, kv.first, d)) { kv.second->Assign(d); ++c.fields; }
        }
    }
    if (const XmlElem* m = root.Kid(L"mouse")) {
        const std::wstring mode = m->AttrOr(L"mode", L"");
        if (!mode.empty()) {
            std::wstring t = mode;
            std::transform(t.begin(), t.end(), t.begin(), ::towlower);
            if (t == L"flee")        c.mouseMode.Assign(0);
            else if (t == L"follow") c.mouseMode.Assign(1);
            else if (t == L"off")    c.mouseMode.Assign(2);
            else c.warnings.push_back(L"mouse mode 认不出来：" + mode + L"，按 flee 走");
            ++c.fields;
        }
        for (const auto& kv : {std::pair{L"nearLen", &c.nearLen}, {L"farLen", &c.farLen},
                               {L"keepLen", &c.keepLen}, {L"touchCool", &c.touchCoolSec}}) {
            if (double d; GetDouble(*m, kv.first, d)) { kv.second->Assign(d); ++c.fields; }
        }
    }
    if (const XmlElem* r = root.Kid(L"rest")) {
        bool allow = true;
        if (GetBool(*r, L"allow", allow)) { c.allowRest.Assign(allow); ++c.fields; }
        for (const auto& kv : {std::pair{L"after", &c.restAfterSec},
                               {L"farSec", &c.restFarSec},
                               {L"duration", &c.restDurationSec}}) {
            if (double d; GetDouble(*r, kv.first, d)) { kv.second->Assign(d); ++c.fields; }
        }
    }
    if (const XmlElem* st = root.Kid(L"steer")) {
        if (double d; GetDouble(*st, L"wander", d)) { c.wanderSec.Assign(d); ++c.fields; }
    }
    if (const XmlElem* a = root.Kid(L"armed")) {
        bool v = false;
        if (GetBool(*a, L"default", v)) { c.armedDefault.Assign(v); ++c.fields; }
    }
    if (const XmlElem* e = root.Kid(L"emotes")) {
        if (const std::wstring* v = e->Attr(L"random")) {
            c.emotePool = ParseEmoteList(*v);
            c.emotePoolSet = true;
            ++c.fields;
        }
        if (double d; GetDouble(*e, L"gap", d)) { c.emoteGapSec.Assign(d); ++c.fields; }
        if (double d; GetDouble(*e, L"showSec", d)) { c.emoteShowSec.Assign(d); ++c.fields; }
    }
    if (const XmlElem* ee = root.Kid(L"emoteEvents")) {
        std::wstring d = ee->AttrOr(L"default", L"all");
        std::transform(d.begin(), d.end(), d.begin(), ::towlower);
        c.eventDefault.Assign(d != L"none");
        ++c.fields;
        for (const XmlElem* ev : ee->Kids(L"event")) {
            const std::wstring name = ev->AttrOr(L"name", L"");
            const std::wstring list = ev->AttrOr(L"emotes", L"");
            if (name.empty()) continue;
            c.eventEmote.emplace_back(name, ParseEmoteList(list));
            ++c.fields;
        }
    }
    if (const XmlElem* en = root.Kid(L"enemy")) {
        if (en->Has(L"hp")) {
            const auto& text = *en->Attr(L"hp");
            wchar_t* end = nullptr;
            const double hp = wcstod(text.c_str(), &end);
            while (end && (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n')) ++end;
            if (end != text.c_str() && end && !*end && std::isfinite(hp) && hp >= 1 && hp <= 1000000 && std::floor(hp) == hp) {
                c.hp.Assign((int)hp); ++c.fields;
            } else c.warnings.push_back(L"enemy hp 必须是 1 到 1000000 的整数，保留已有值或阵营默认值");
        }
        for (const auto& kv : {std::pair{L"idleSec", &c.idleSec},
                               {L"disarmSec", &c.disarmSec}}) {
            if (double d; GetDouble(*en, kv.first, d)) { kv.second->Assign(d); ++c.fields; }
        }
    }
    if (const XmlElem* a = root.Kid(L"actions")) {
        ReadActions(*a, c);
    }
    if (const XmlElem* en = root.Kid(L"encounters")) {
        ReadEncounters(*en, c);
    }
    if (const XmlElem* r = root.Kid(L"react")) {
        ReadReactions(*r, c);
    }
    if (const XmlElem* s = root.Kid(L"states")) {
        ReadStates(*s, c);
    }
}

// 敌人那一摊的底色。这里只放"没有这套图"和"没有脸"两件事，
// 速度、仇恨距离这些都写在 monster.xml 里 —— 那才是人会去调的东西
CharacterConfig SeedMonster() {
    CharacterConfig c;
    c.id = kMonsterDefaultId;
    c.faction = Faction::Monster;
    c.idleSec.Assign(IDLE_SEC);
    c.disarmSec.Assign(DISARM_SEC);
    c.fields = 0;
    return c;
}

// 我方那一摊的底色：全部走常量，一个字段都不算"被写过"
CharacterConfig SeedAlly() {
    CharacterConfig c;
    c.faction = Faction::Ally;
    c.fields = 0;
    return c;
}

// 读 EnemyRoot() 底下那份公共底。没有就交回一份纯默认
CharacterConfig LoadMonsterBase(const std::wstring& dir) {
    CharacterConfig c = SeedMonster();
    if (ConfigDisabled()) return c;
    const std::wstring root = EnemyRootFor(DirectoryFaction(dir));
    if (root.empty()) return c;
    std::error_code ec;
    const fs::path p = fs::path(root) / L"monster.xml";
    if (!fs::exists(p, ec)) return c;
    XmlElem x;
    std::wstring err;
    if (!LoadXmlFile(p.wstring(), x, err)) {
        c.warnings.push_back(L"monster.xml 读不了：" + err + L"，全按内置默认走");
        return c;
    }
    ApplyCharacterXml(x, c);
    c.fields = 0;   // 公共底不算"这个人被写过"，基线那句话还指着它
    return c;
}

std::map<std::wstring, CharacterConfig>& Cache() {
    static std::map<std::wstring, CharacterConfig> m;
    return m;
}

bool g_configDisabled = false;
// 缓存代次。只有"整张缓存被丢掉"才加一 —— std::map 的引用在插入时不会失效，
// 所以插新条目不用惊动它
uint64_t g_configGen = 0;

CharacterConfig LoadOne(const std::wstring& dir, int idHint) {
    const bool enemy = IsEnemyDir(dir);
    CharacterConfig c = enemy ? LoadMonsterBase(dir) : SeedAlly();
    // 兜底号：xml 里没写 id= 才用它。敌人那份底色已经带了 201，这里不覆盖。
    //
    // 号数从 0 开始发（托盘菜单按它排），所以这里不能再拿 idHint > 0 当
    // "有人给过号"的判据 —— 那样第一个人的 0 会被当成没给过，掉回种子里的 0。
    // 两边都是 0 看着没事，但种子以后一改就会错开，判据得说明白
    if (!enemy) c.id = idHint;

    if (!g_configDisabled) {
        std::error_code ec;
        const fs::path p = fs::path(dir) / L"character.xml";
        if (fs::exists(p, ec)) {
            XmlElem x;
            std::wstring err;
            if (LoadXmlFile(p.wstring(), x, err)) {
                ApplyCharacterXml(x, c);
            } else {
                // 一个人读崩了不许把整张桌子带下来：她走全默认，只留一句
                c.warnings.push_back(L"character.xml 读不了：" + err + L"，这个人按默认走");
            }
        }
    }

    // Runtime allegiance follows the resource root, never an incidental XML id.
    c.faction = DirectoryFaction(dir);
    // Enemy HP may be supplied by shared or per-skin XML; chr stays immortal at 10 HP.
    if (!enemy) c.hp = Opt<int>{};
    if (enemy) c.id = idHint;
    if (c.emotePool.empty() && !c.emotePoolSet && !enemy) {
        for (int i = 1; i <= EMOTE_LAST_ID; ++i) c.emotePool.push_back(i);
    }
    if (enemy) { c.emotePool.clear(); c.eventDefault.Assign(false); }
    return c;
}

}  // namespace

const FactionBand* FactionBands() { return kBands; }

Faction FactionOf(int id) {
    for (const FactionBand& b : kBands) {
        if (id >= b.lo && id <= b.hi) return b.f;
    }
    return Faction::Unset;
}

bool IsHostile(int id) {
    const Faction f = FactionOf(id);
    return f == Faction::EnemyChar || f == Faction::Monster;
}

bool HasHp(int id) { return FactionOf(id) != Faction::Unset; }

const wchar_t* StateName(State s) {
    switch (s) {
    case State::Walk:       return L"walk";
    case State::Run:        return L"run";
    case State::Idle:       return L"idle";
    case State::Chase:      return L"chase";
    case State::Attack:     return L"attack";
    case State::BeAttacked: return L"beAttacked";
    case State::Stun:       return L"stun";
    case State::Rest:       return L"rest";
    case State::Dead:       return L"dead";
    case State::Victory:    return L"victory";
    }
    return L"?";
}

// 大小写不敏感：XML 本身是敏感的，但配置文件是人写的，"Chase" 和 "chase"
// 该是同一个东西
bool StateFromName(const std::wstring& s, State& out) {
    for (int i = 0; i < STATE_COUNT; ++i) {
        if (_wcsicmp(StateName((State)i), s.c_str()) == 0) {
            out = (State)i;
            return true;
        }
    }
    return false;
}

const wchar_t* EventName(GameEvent e) {
    switch (e) {
    case GameEvent::None:           return L"none";
    case GameEvent::BumpWalk:       return L"BumpWalk";
    case GameEvent::BumpRun:        return L"BumpRun";
    case GameEvent::MouseNear:      return L"MouseNear";
    case GameEvent::MouseFar:       return L"MouseFar";
    case GameEvent::Touched:        return L"Touched";
    case GameEvent::PeerBump:       return L"PeerBump";
    case GameEvent::Armed:          return L"Armed";
    case GameEvent::EncounterFar:   return L"EncounterFar";
    case GameEvent::EncounterNear:  return L"EncounterNear";
    case GameEvent::InRange:        return L"InRange";
    case GameEvent::Lost:           return L"Lost";
    case GameEvent::AttackedBy:     return L"AttackedBy";
    case GameEvent::AttackHit:      return L"AttackHit";
    case GameEvent::HpZero:         return L"HpZero";
    case GameEvent::RestBegin:      return L"RestBegin";
    case GameEvent::RestEnd:        return L"RestEnd";
    case GameEvent::StunBegin:      return L"Stun";
    case GameEvent::TimerExpired:   return L"TimerExpired";
    default:                        return L"?";
    }
}

void SetConfigDisabled(bool off) {
    g_configDisabled = off;
    Cache().clear();   // 换开关之后老缓存全是另一套值，必须整个丢掉
    ++g_configGen;
}

uint64_t ConfigGeneration() { return g_configGen; }

bool ConfigDisabled() { return g_configDisabled; }

const CharacterConfig& ConfigFor(const std::wstring& dir, int idHint) {
    auto& m = Cache();
    auto it = m.find(dir);
    if (it != m.end()) return it->second;
    auto res = m.emplace(dir, LoadOne(dir, idHint));
    return res.first->second;
}

namespace {

bool RuleHits(const RuleKind& kind, const std::wstring& rname, int lo, int hi,
              Faction rf, const std::wstring& peerName, int peerId) {
    switch (kind) {
    case RuleKind::ByName:
        return !peerName.empty() && _wcsicmp(rname.c_str(), peerName.c_str()) == 0;
    case RuleKind::ById:
        return peerId == lo;
    case RuleKind::ByIdRange:
        return peerId >= lo && peerId <= hi;
    case RuleKind::ByFaction:
        return FactionOf(peerId) == rf;
    case RuleKind::ByWild:
        return true;
    }
    return false;
}

}  // namespace

// 查找顺序是 **名字 → 号数 → 区间 → 阵营 → 通配**：名字是文件夹名，
// 换一批人物也不会认错人；号数留着当"类"用（怪物都算 201）。
// 三处都落不到才算 Ignore。
const Encounter* MatchEncounter(const CharacterConfig& c,
                                const std::wstring& peerName, int peerId) {
    static const Encounter kIgnore{};
    for (const EncRule& r : c.rules) {
        if (r.kind != RuleKind::ByName) continue;
        if (RuleHits(r.kind, r.name, r.lo, r.hi, r.faction, peerName, peerId)) return &r.act;
    }
    for (const EncRule& r : c.rules) {
        if (r.kind != RuleKind::ById) continue;
        if (RuleHits(r.kind, r.name, r.lo, r.hi, r.faction, peerName, peerId)) return &r.act;
    }
    for (const EncRule& r : c.rules) {
        if (r.kind != RuleKind::ByIdRange) continue;
        if (RuleHits(r.kind, r.name, r.lo, r.hi, r.faction, peerName, peerId)) return &r.act;
    }
    for (const EncRule& r : c.rules) {
        if (r.kind != RuleKind::ByFaction) continue;
        if (RuleHits(r.kind, r.name, r.lo, r.hi, r.faction, peerName, peerId)) return &r.act;
    }
    for (const EncRule& r : c.rules) {
        if (r.kind != RuleKind::ByWild) continue;
        return &r.act;
    }
    return &kIgnore;
}

const Reaction* MatchReaction(const CharacterConfig& c,
                              const std::wstring& attackerName, int attackerId) {
    for (const ReactRule& r : c.reactRules) {
        if (r.kind != RuleKind::ByName) continue;
        if (RuleHits(r.kind, r.name, r.lo, r.hi, r.faction, attackerName, attackerId))
            return &r.act;
    }
    for (const ReactRule& r : c.reactRules) {
        if (r.kind == RuleKind::ByName || r.kind == RuleKind::ByWild) continue;
        if (RuleHits(r.kind, r.name, r.lo, r.hi, r.faction, attackerName, attackerId))
            return &r.act;
    }
    for (const ReactRule& r : c.reactRules) {
        if (r.kind != RuleKind::ByWild) continue;
        return &r.act;
    }
    return nullptr;   // Nothing = "按默认的挨一下走"，不是"这里该安静"
}

bool ActionOn(const CharacterConfig& c, Art a) {
    for (Art off : c.artOff) {
        if (off == a) return false;
    }
    return true;
}

// 只替"关掉的那一种"找替身，而且替身自己也得是开着的 —— 否则会出现
// "Run 关了、顶它的 Walk 也关了"，最后拿着一套根本没图的条带
Art ArtKind(const CharacterConfig& c, Art a) {
    if (ActionOn(c, a)) return a;
    const Art s = SubstituteFor(a);
    if (s != a && ActionOn(c, s)) return s;
    return a;
}


namespace {
// 事件名在 xml 里是人写的，大小写不许成为一道坎
const std::vector<int>* FindEventEmote(const CharacterConfig& c, const wchar_t* name) {
    for (const auto& kv : c.eventEmote) {
        if (_wcsicmp(kv.first.c_str(), name) == 0) return &kv.second;
    }
    return nullptr;
}
}  // namespace

// nullptr 和空表是两回事：前者"没写过"，后者"这里就该安静"
const std::vector<int>* EmotePoolFor(const CharacterConfig& c, GameEvent ev) {
    return FindEventEmote(c, EventName(ev));
}

bool MentionsEvent(const CharacterConfig& c, GameEvent ev) {
    return EmotePoolFor(c, ev) != nullptr;
}

// 内置那张表，照 RunnerCharacter.vb 的 EventEmote。两张脸各一半是原版的写法：
// 撞墙是懵，被人挤到了是另一种，别拿同一张脸糊过去
int BuiltinEmote(GameEvent ev, unsigned coin) {
    const int r = (coin & 1u) ? 1 : 0;
    switch (ev) {
    case GameEvent::BumpWalk:  return r ? (int)EmotionId::Exclaim : (int)EmotionId::Question;
    case GameEvent::BumpRun:   return r ? (int)EmotionId::Confused : (int)EmotionId::Dizzy;
    case GameEvent::MouseNear: return r ? (int)EmotionId::Speechless : (int)EmotionId::Angry;
    case GameEvent::Touched:   return r ? (int)EmotionId::Deadpan : (int)EmotionId::Rage;
    case GameEvent::Armed:     return r ? (int)EmotionId::Rage : (int)EmotionId::Angry;
    case GameEvent::PeerBump:  return r ? (int)EmotionId::Confused : (int)EmotionId::Dizzy;
    default:                   return 0;
    }
}

// 内置那张表。
//
// 挑得很有讲究：凡是今天已经会发的事件（BumpWalk / Touched / PeerBump /
// MouseNear…），这里要么不管，要么就指到它今天本来就去的地方 —— 一个字都不许
// 改今天的脾气。剩下那些触发点（RestBegin / HpZero / StunBegin / AttackedBy /
// InRange / Lost）现在压根没人发，列进来只是把门立好，等第 8 刀去推。
//
// EncounterNear/Far 看着像例外，其实不是：UpdateEncounter 只在规则真的说了
// chase/attack/combat 才发这两个事件，Ignore 和 Separate 它自己就跳过了。
// 所以 --no-config 那一路一个都不发，这张表等于不存在
const std::vector<StateRule>& BuiltinStates() {
    static const std::vector<StateRule> kTable = [] {
        std::vector<StateRule> t;
        auto add = [&](State from, GameEvent ev, State to, bool back = false) {
            StateRule r;
            r.from = from;
            r.ev = ev;
            r.to = to;
            r.back = back;
            t.push_back(r);
        };
        auto addWild = [&](GameEvent ev, State to, bool back = false) {
            StateRule r;
            r.anyFrom = true;
            r.ev = ev;
            r.to = to;
            r.back = back;
            t.push_back(r);
        };

        // 鼠标那两档**不在这里**：UpdateState 里那套迟滞还带着 _runTravel 这个
        // 条件，表里再写一遍就成了两处说法。表只管今天管不了的那些态
        //
        // 照面。发不发由遭遇表定，这张表只管"发了之后去哪"
        addWild(GameEvent::EncounterNear, State::Chase);
        addWild(GameEvent::EncounterFar, State::Chase);
        // 追上了
        add(State::Chase, GameEvent::InRange, State::Attack);
        add(State::Attack, GameEvent::TimerExpired, State::Chase);
        // 跟丢了
        add(State::Chase, GameEvent::Lost, State::Walk);

        // 一次性动画：进态那一行不许带 back —— back 的意思是"回进态之前那个"，
        // 放在进态行上就等于哪儿也没去（to 直接算成 _returnTo）。它只该出现在
        // 出态那一行
        addWild(GameEvent::AttackedBy, State::BeAttacked);
        addWild(GameEvent::StunBegin, State::Stun);
        add(State::BeAttacked, GameEvent::TimerExpired, State::Idle, true);
        add(State::Stun, GameEvent::TimerExpired, State::Idle, true);

        // 歇会儿
        addWild(GameEvent::RestBegin, State::Rest);
        add(State::Rest, GameEvent::RestEnd, State::Walk);
        add(State::Rest, GameEvent::MouseNear, State::Run);

        // 倒下和赢。谁都没发这两个事件，立在这儿是为了第 8 刀接上时有地方落
        addWild(GameEvent::HpZero, State::Dead);
        addWild(GameEvent::AttackHit, State::Victory);
        add(State::Victory, GameEvent::TimerExpired, State::Idle, true);
        return t;
    }();
    return kTable;
}

// 查找顺序：xml 的具体行 → xml 的通流行 → 内置表。
// default="none" 到第三层就停：没写过的转移就不许发生，别拿内置表替他做主
const StateRule* MatchState(const CharacterConfig& c, State from, GameEvent ev) {
    const StateRule* wild = nullptr;
    for (const StateRule& r : c.stateRules) {
        if (r.ev != ev) continue;
        if (!r.anyFrom && r.from != from) continue;
        if (!r.anyFrom) return &r;
        if (!wild) wild = &r;
    }
    if (wild) return wild;
    if (!c.statesDefault.valueOr(true)) return nullptr;

    const std::vector<StateRule>& b = BuiltinStates();
    const StateRule* bwild = nullptr;
    for (const StateRule& r : b) {
        if (r.ev != ev) continue;
        if (!r.anyFrom && r.from != from) continue;
        if (!r.anyFrom) return &r;
        if (!bwild) bwild = &r;
    }
    return bwild;
}

EncounterBand BandFor(const CharacterConfig& c, double peerLen) {
    if (peerLen <= 0.0) return EncounterBand::None;
    const double nearLen = c.encNearLen.valueOr(ENC_NEAR_LEN);
    const double farLen  = c.encFarLen.valueOr(ENC_FAR_LEN);
    if (peerLen <= nearLen) return EncounterBand::Near;
    if (peerLen <= farLen)  return EncounterBand::Far;
    return EncounterBand::None;
}

namespace {

void AppendField(std::wstring& s, const wchar_t* key, const std::wstring& v,
                 const wchar_t* layer) {
    wchar_t buf[512];
    swprintf(buf, 512, L"  %-16s = %-24s [%s]\n", key, v.c_str(), layer);
    s += buf;
}

template <class T>
void AppendOpt(std::wstring& s, const wchar_t* key, const Opt<T>& o,
               double dflt, const wchar_t* layer) {
    // 永远把**生效值**打出来，方括号里那一层才说它是谁给的。
    //
    // 以前没写过就只印一句"(没写过)"，看着诚实，其实把 --dumpconfig 最该回答
    // 的那个问题问没了："这一项现在到底是几"。基线回归也指着这个 —— 拿生效值
    // 和 config.h 里的常量逐字段比，才叫证明"没有文件 = 今天的她"
    wchar_t v[64];
    swprintf(v, 64, L"%.4g", o.set ? (double)o.v : dflt);
    AppendField(s, key, v, o.set ? L"xml" : layer);
}

std::wstring KindText(EncounterKind k) {
    switch (k) {
    case EncounterKind::Ignore:   return L"ignore";
    case EncounterKind::Separate: return L"separate";
    case EncounterKind::Combat:   return L"combat";
    case EncounterKind::Chase:    return L"chase";
    case EncounterKind::Attack:   return L"attack";
    case EncounterKind::Flee:     return L"flee";
    }
    return L"?";
}

std::wstring RuleKeyText(const EncRule& r) {
    switch (r.kind) {
    case RuleKind::ByName:    return L"name=" + r.name;
    case RuleKind::ById:      return L"id=" + std::to_wstring(r.lo);
    case RuleKind::ByIdRange: return L"idRange=" + std::to_wstring(r.lo) + L".." +
                                     std::to_wstring(r.hi);
    case RuleKind::ByFaction: return L"faction=" + std::wstring(
                                 r.faction == Faction::Ally ? L"ally" :
                                 r.faction == Faction::EnemyChar ? L"enemy" : L"monster");
    case RuleKind::ByWild:    return L"*";
    }
    return L"?";
}

}  // namespace

std::wstring DumpConfig(const CharacterConfig& c, const std::wstring& dir) {
    std::wstring s;
    wchar_t buf[512];
    swprintf(buf, 512, L"---- character config ----\n目录: %s\n", dir.c_str());
    s += buf;
    swprintf(buf, 512, L"  id=%d  阵营=%s  认下来 %d 项%s\n", c.id,
             c.faction == Faction::Ally ? L"ally" :
             c.faction == Faction::EnemyChar ? L"enemy1" :
             c.faction == Faction::Monster ? L"enemy2" : L"unset",
             c.fields, g_configDisabled ? L"  (--no-config：xml 全不读)" : L"");
    s += buf;

    AppendOpt(s, L"walkPx", c.walkPx, WALK_PX, L"常量");
    AppendOpt(s, L"runPx", c.runPx, RUN_PX, L"常量");
    AppendOpt(s, L"armedRunPx", c.armedRunPx, ARMED_RUN_PX, L"常量");
    AppendOpt(s, L"bodyLen", c.bodyLen, SPRITE_BODY_HEIGHT, L"常量");
    AppendOpt(s, L"nearLen", c.nearLen, RUN_START_LEN, L"常量");
    AppendOpt(s, L"farLen", c.farLen, RUN_END_LEN, L"常量");
    AppendOpt(s, L"keepLen", c.keepLen, KEEP_LEN, L"常量");
    AppendOpt(s, L"mouseMode", c.mouseMode, 0, L"常量 (0=flee, 1=follow, 2=off)");
    AppendOpt(s, L"touchCoolSec", c.touchCoolSec, TOUCH_COOLDOWN_SEC, L"常量");
    AppendOpt(s, L"allowRest", c.allowRest, true, L"常量");
    AppendOpt(s, L"restAfterSec", c.restAfterSec, REST_AFTER_SEC, L"常量");
    AppendOpt(s, L"restFarSec", c.restFarSec, REST_FAR_SEC, L"常量");
    AppendOpt(s, L"restDurationSec", c.restDurationSec, REST_DURATION_SEC, L"常量");
    AppendOpt(s, L"hp", c.hp, StartingHp(c.faction), L"阵营默认");
    AppendOpt(s, L"encNearLen", c.encNearLen, ENC_NEAR_LEN, L"常量");
    AppendOpt(s, L"encFarLen", c.encFarLen, ENC_FAR_LEN, L"常量");
    AppendOpt(s, L"attackLen", c.attackLen, ATTACK_LEN, L"常量");
    AppendOpt(s, L"attackCoolSec", c.attackCoolSec, ATTACK_COOL_SEC, L"常量");
    AppendOpt(s, L"wanderSec", c.wanderSec, WANDER_SEC, L"常量");
    AppendOpt(s, L"emoteGapSec", c.emoteGapSec, EMOTE_GAP_SEC, L"常量");
    AppendOpt(s, L"emoteShowSec", c.emoteShowSec, EMOTE_SHOW_SEC, L"常量");
    AppendOpt(s, L"idleSec", c.idleSec, IDLE_SEC, L"常量");
    AppendOpt(s, L"disarmSec", c.disarmSec, DISARM_SEC, L"常量");

    {
        std::wstring pool;
        for (size_t i = 0; i < c.emotePool.size(); ++i) {
            if (i) pool += L",";
            pool += std::to_wstring(c.emotePool[i]);
        }
        // 没从文件里读过就是"全开默认池"，那是内置行为，不该挂 [xml]
        AppendField(s, L"emotePool", pool, c.emotePoolSet ? L"xml" : L"常量");
    }

    swprintf(buf, 512, L"  %-16s = %zu 条规则\n", L"encounters", c.rules.size());
    s += buf;
    for (const EncRule& r : c.rules) {
        // far/near 是 windows.h 的空宏，字段早就改名成 farKind/nearKind 了
        swprintf(buf, 512, L"      %-24s far=%-8s near=%-8s%s\n", RuleKeyText(r).c_str(),
                 KindText(r.act.farKind).c_str(), KindText(r.act.nearKind).c_str(),
                 r.act.armed ? L" +武器" : L"");
        s += buf;
    }
    swprintf(buf, 512, L"  %-16s = %zu 条\n", L"react", c.reactRules.size());
    s += buf;
    swprintf(buf, 512, L"  %-16s = %zu 条  default=%s\n", L"states", c.stateRules.size(),
             c.statesDefault.valueOr(true) ? L"builtin" : L"none");
    s += buf;

    if (!c.warnings.empty()) {
        s += L"  ---- 读这份文件时的问题 ----\n";
        for (const std::wstring& w : c.warnings) s += L"    ! " + w + L"\n";
    }
    return s;
}

}  // namespace fd
