#pragma once
#include "../src/enemy.h"
#include <memory>

void CombatRegression(const wchar_t* skin) {
    using namespace fd;
    struct Actor {
        LayeredWindow window;
        Runner runner;
        Actor(const wchar_t* skin, Faction faction, Vec2 pos) {
            Check(window.Create(256, 256, true, false), "battle window");
            window.Show(false);
            Runner::Options opt;
            opt.skinDir = skin; opt.skinName = FactionName(faction);
            opt.faction = faction; opt.worldW = 12000; opt.worldH = 6000;
            opt.wander = true;
            Check(runner.Create(&window, opt), "battle runner");
            runner.SetPos(pos);
        }
    };
    Actor chr(skin, Faction::Ally, {2000, 2000});
    Actor e1(skin, Faction::Enemy1, {2010, 2000});
    Actor e2(skin, Faction::Enemy2, {2200, 2000});
    Actor friend2(skin, Faction::Ally, {2020, 2000});
    std::vector<Runner*> peers{&chr.runner, &e1.runner, &e2.runner, &friend2.runner};
    Runner::SetPeers(&peers);
    Check(chr.runner.Hp() == 10 && e1.runner.Hp() == 10 && e2.runner.Hp() == 5, "three faction HP");
    Check(chr.runner.SelectTarget() == &e2.runner, "chr prefers Enemy2 over closer Enemy1");
    Check(e1.runner.SelectTarget() == &e2.runner, "Enemy1 prefers Enemy2 over closer chr");
    Check(e2.runner.SelectTarget()->MyFaction() == Faction::Ally, "Enemy2 prefers chr");
    chr.runner.Hit(&friend2.runner);
    Check(chr.runner.Hp() == 10, "same camp cannot damage");

    for (int i = 0; i < 4; ++i) e2.runner.Hit(&e1.runner);
    Check(e2.runner.Hp() == 1, "hits during hit animation continue reducing HP");
    e2.runner.Hit(&chr.runner);
    Check(e2.runner.Downed() && !e2.runner.Targetable(), "fifth hit kills Enemy2 and excludes target");
    Check(chr.runner.SelectTarget() == &e1.runner, "retarget immediately after preferred enemy dies");
    e2.runner.Tick(2.1, Vec2{});
    Check(!e2.runner.Gone(), "dead enemy retained during fade");
    e2.runner.Tick(0.95, Vec2{});
    Check(e2.runner.Gone(), "enemy removed after three seconds");
    for (int i = 0; i < 10; ++i) e1.runner.Hit(&chr.runner);
    Check(e1.runner.Downed(), "Enemy1 takes ten effective hits");

    // A live hostile downs chr, then must ignore it until recovery.
    Actor attacker(skin, Faction::Enemy2, {2100, 2000});
    peers = {&chr.runner, &attacker.runner};
    for (int i = 0; i < 10; ++i) chr.runner.Hit(&attacker.runner);
    Check(chr.runner.Downed() && attacker.runner.SelectTarget() == nullptr, "downed chr cannot be selected");
    chr.runner.Hit(&attacker.runner);
    Check(chr.runner.Hp() == 0, "downed chr ignores extra damage");
    chr.runner.Tick(4.9, Vec2{});
    Check(chr.runner.Downed(), "chr stays down for five seconds");
    chr.runner.SetTaskbar(true, 2500, 500, 11000);
    Check(chr.runner.Downed(), "mode switching preserves down state");
    chr.runner.Tick(0.11, Vec2{});
    Check(chr.runner.Hp() == 10 && !chr.runner.Downed(), "chr revives at full HP");
    Check(Near(chr.runner.Pos().y, 2500), "revival preserves taskbar footline");

    attacker.runner.SetTaskbar(true, 2500, 500, 11000);
    attacker.runner.EnterFromEdge(0);
    Check(attacker.runner.Entering() && !attacker.runner.Targetable(), "entering enemy excluded");
    const int hp = attacker.runner.Hp();
    attacker.runner.Hit(&chr.runner);
    Check(attacker.runner.Hp() == hp, "entry protected from damage");
    for (int i = 0; i < 200 && attacker.runner.Entering(); ++i) attacker.runner.Tick(0.1, Vec2{});
    Check(!attacker.runner.Entering() && Near(attacker.runner.Pos().y, 2500), "taskbar entry completes horizontally");
    chr.runner.SetPos({1500, 2500}); attacker.runner.SetPos({1550, 2500});
    const auto before = chr.runner.Attacks();
    for (int i = 0; i < 200; ++i) {
        chr.runner.Tick(0.05, Vec2{}); attacker.runner.Tick(0.05, Vec2{});
        Check(Near(chr.runner.Pos().y, 2500) && Near(attacker.runner.Pos().y, 2500), "battle and knockback stay on taskbar");
        if (attacker.runner.Downed()) break;
    }
    Check(chr.runner.Attacks() > before, "taskbar performs automatic attacks");
    const int currentHp = chr.runner.Hp();
    chr.runner.SetTaskbar(false, 0, 0, 0);
    Check(chr.runner.Hp() == currentHp, "leaving taskbar preserves HP");
    Runner::SetPeers(nullptr);

    EnemySpawner spawn(42);
    std::vector<SkinInfo> skins(3);
    for (int i = 0; i < 3; ++i) { skins[i].name = std::to_wstring(i); skins[i].dir = skins[i].name; skins[i].faction = i == 0 ? Faction::Enemy1 : Faction::Enemy2; }
    spawn.Enable(true);
    Check(!spawn.Tick(9.9) && spawn.Tick(0.11), "automatic default interval ten seconds");
    for (int i = 0; i < 3; ++i) {
        const int index = spawn.Pick(skins, {}, Faction::Unset, true);
        Check(index >= 0, "auto picks an unused skin"); spawn.Used(skins[index]);
    }
    Check(spawn.Pick(skins, {}, Faction::Unset, true) == -1 && !spawn.Enabled(), "auto pool exhaustion stops schedule");
    Check(spawn.Pick(skins, {}, Faction::Enemy1, false) == 0, "manual can reuse retired enemy");
    spawn.Enable(true); spawn.Clear();
    Check(spawn.Enabled() && !spawn.Tick(9), "clear resets clock and retains auto switch");
    spawn.SetInterval(1);
    Check(spawn.Tick(1), "interval change applies");
    Check(spawn.Pick(skins, {L"0", L"1", L"2"}) == -1, "no duplicate live skin");
    const auto installed = ScanEnemyIndex();
    int first = 0, second = 0, skipped = 0;
    for (const auto& entry : installed) {
        std::wstring error;
        if (!ValidateEnemySkin(entry, error)) {
            std::fwprintf(stderr, L"Skipped incomplete enemy skin: %ls\n", entry.name.c_str());
            ++skipped;
            continue; // Runtime also excludes incomplete skins, matching VB.
        }
        if (entry.faction == Faction::Enemy1) ++first; else ++second;
    }
    std::printf("Installed enemy skins validated: Enemy1=%d, Enemy2=%d, incomplete=%d\n", first, second, skipped);
    Check(installed.empty() || (first > 0 && second > 0), "both installed camps have usable animation sets");
    std::puts("Three-camp HP, priority, knockdown/revival, taskbar combat and spawner regressions passed.");
}
