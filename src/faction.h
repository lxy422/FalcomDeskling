#pragma once
namespace fd {
enum class Faction : int {
    Wild = -1, Unset = 0, Ally = 1, Enemy1 = 2, Enemy2 = 3,
    EnemyChar = Enemy1, Monster = Enemy2 // legacy XML names
};
inline bool EnemyFaction(Faction f) { return f == Faction::Enemy1 || f == Faction::Enemy2; }
inline bool Opposed(Faction a, Faction b) {
    return a != b && (a == Faction::Ally || EnemyFaction(a)) && (b == Faction::Ally || EnemyFaction(b));
}
inline int TargetPriority(Faction self, Faction other) {
    if (!Opposed(self, other)) return 99;
    return other == (self == Faction::Enemy2 ? Faction::Ally : Faction::Enemy2) ? 0 : 1;
}
inline int StartingHp(Faction f) { return f == Faction::Enemy2 ? 5 : 10; }
inline int DefaultEnemyRepeat(Faction f) { return f == Faction::Enemy2 ? 4 : 1; }
inline const wchar_t* FactionName(Faction f) {
    return f == Faction::Ally ? L"chr" : f == Faction::Enemy1 ? L"Enemy1" : f == Faction::Enemy2 ? L"Enemy2" : L"unset";
}
}
